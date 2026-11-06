/*
 * PARSECS High Level demo: GetRequest / GetResponse via USER_Send / USER_Receive.
 * Layer 1 I2C DMA, L2/L3, and L4/L6/L7 run in PARSECS_LowLevelTask and
 * PARSECS_Protocol_Interface_Task. This file must not call TRANSMIT_APP / RECEIVE_APP.
 *
 * Master sends one GetRequest to every board that is present (hot-plug: comm
 * 0x08 = 42, mobility 0x09 = 43, pm 0x0A = 44, board2..board5 0x0B..0x0E =
 * 45..48), lets ACKs drain, holds SCL and SDA low, then repeats. Boards that
 * are plugged in or pulled out are picked up on the next pass, and every
 * hot-plug state change is logged. The slave image is selected with
 * PARSECS_SLAVE_INDEX (see slave_identity.h).
 *
 * On the master, PE7 (TIMING_Pin) is high from the moment a GetRequest is
 * queued until its GetResponse arrives or the request times out.
 */

#include "demo_task.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "log.h"
#include "PARSECS_Protocol_Interface.h"
#include "PARSECS_LowLevelAPI.h"
#include <ber.h>

#ifdef SPI_MASTER
#include "cmsis_os.h"
#include "PARSECS_Layer1.h"
#include "PARSECS_HotPlug.h"
#else
#include "slave_identity.h"
#endif

#define LED_HEARTBEAT_Pin        LD4_Pin
#define DEMO_TYPE_ID             0x01U
#define DEMO_RESPONSE_TIMEOUT_MS 2000U
#define DEMO_SETTLE_MS           100U
#define DEMO_BUS_HOLD_MS         500U

#ifdef SPI_MASTER
#define ROLE_TAG            "[Master]"
#else
#define ROLE_TAG            "[Slave] "
#endif

static uint8_t ber_tx[8];
static uint8_t ber_rx[32];

#ifdef SPI_MASTER
typedef enum {
	DEMO_NEXT = 0,
	DEMO_WAIT,
	DEMO_SETTLE,
	DEMO_HOLD,
} demo_phase_t;

typedef struct {
	PARSECS_APP_BOARD_ADDRESS board;
	const char *name;
	int value;
} demo_board_t;

/* Same order as known_boards in PARSECS_Protocol_Interface.c. */
static const demo_board_t demo_boards[] = {
	{ CORE_TX_WIT_COMM_BOARD,     "comm",     42 },
	{ CORE_TX_WIT_MOBILITY_BOARD, "mobility", 43 },
	{ CORE_TX_WIT_PM_BOARD,       "pm",       44 },
	{ CORE_TX_WIT_BOARD_NA2,      "board2",   45 },
	{ CORE_TX_WIT_BOARD_NA3,      "board3",   46 },
	{ CORE_TX_WIT_BOARD_NA4,      "board4",   47 },
	{ CORE_TX_WIT_BOARD_NA5,      "board5",   48 },
};
#define DEMO_BOARD_COUNT  (sizeof(demo_boards) / sizeof(demo_boards[0]))

static demo_phase_t phase = DEMO_NEXT;
static uint16_t phase_ticks = 0U;
static uint32_t request_tick = 0U;
/* Index into demo_boards of the board being asked, or of the next one to try. */
static uint8_t demo_idx = 0U;
/* At least one request went out in this pass, so the pass ends with settle + hold. */
static bool pass_sent = false;
static int8_t last_state[DEMO_BOARD_COUNT];
#else
static bool request_seen = false;
static uint32_t link_epoch = 0U;
#endif

/* Goes through the same LOG_SERIAL ring as the stack logs, so demo lines and
 * stack logs stay in order and never wait on USB. */
static void usb_print(const char *msg)
{
	LOG_SERIAL("%s", msg);
}

/* Appended to an existing log line: a second CDC_Transmit_FS in the same
 * tick would just come back USBD_BUSY. */
static void format_link_stats(char *out, size_t size, PARSECS_APP_BOARD_ADDRESS board)
{
	uint32_t retx = 0U;
	uint32_t dropped = 0U;
	uint8_t i;

	out[0] = '\0';
	for (i = 0U; i < MAX_BOARD_COUNT; i++)
	{
		const PARSECS_PROTOCOL_BOARD_DESCRIPTOR *desc = &CORE_TX_Wit_Boards[i];
		if ((desc->boardAvailable == true) && (desc->boardAddress == board) && (desc->boardSlaveID >= 0) &&
		    (PARSECS_Get_ARQ_Stats((uint8_t)desc->boardSlaveID, &retx, &dropped) == 0))
		{
			snprintf(out, size, " link=%s retx=%lu dropped=%lu",
			         (PARSECS_Get_Link_State((uint8_t)desc->boardSlaveID) == PARSECS_LINK_UP) ? "UP" : "DOWN",
			         (unsigned long)retx, (unsigned long)dropped);
			return;
		}
	}
}

void DemoTask_Init(void)
{
#ifdef SPI_MASTER
	GPIO_InitTypeDef gpio = {0};

	__HAL_RCC_GPIOE_CLK_ENABLE();
	HAL_GPIO_WritePin(TIMING_GPIO_Port, TIMING_Pin, GPIO_PIN_RESET);
	gpio.Pin = TIMING_Pin;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(TIMING_GPIO_Port, &gpio);

	phase = DEMO_NEXT;
	phase_ticks = 0U;
	demo_idx = 0U;
	pass_sent = false;
	{
		uint8_t i;
		for (i = 0U; i < DEMO_BOARD_COUNT; i++)
		{
			last_state[i] = PARSECS_SLAVE_ABSENT;
		}
	}
#else
	request_seen = false;
	link_epoch = PARSECS_Get_Link_Epoch(0U);
#endif
}

#ifdef SPI_MASTER
static bool send_get(PARSECS_APP_BOARD_ADDRESS board, const char *name)
{
	char line[96];
	uint32_t packet_size = 0U;
	uint32_t ber_len = 0U;
	int8_t status;

	if (BERNullDataEncode(ber_tx, &ber_len) == false)
	{
		return false;
	}
	status = PARSECS_Protocol_USER_Send(board,
	                                   CORE_TX_WIT_MOTHERBOARD,
	                                   (uint8_t)board,
	                                   PARSECS_APP_GetRequest,
	                                   0xFF,
	                                   (uint8_t)DEMO_TYPE_ID,
	                                   ber_tx,
	                                   ber_len,
	                                   &packet_size);
	if (status != PARSECS_PROTOCOL_OK)
	{
		return false;
	}
	HAL_GPIO_WritePin(TIMING_GPIO_Port, TIMING_Pin, GPIO_PIN_SET);
	request_tick = HAL_GetTick();
	snprintf(line, sizeof(line), "%s %s queued GetRequest TypeID=0x%02X\n",
	         ROLE_TAG, name, (unsigned)DEMO_TYPE_ID);
	usb_print(line);
	return true;
}

static const char *state_name(int8_t state)
{
	switch (state)
	{
	case PARSECS_SLAVE_ABSENT:  return "ABSENT";
	case PARSECS_SLAVE_LINKING: return "LINKING";
	case PARSECS_SLAVE_ACTIVE:  return "ACTIVE";
	default:                    return "?";
	}
}

/* One line per hot-plug state change of any known board. */
static void log_hotplug_changes(void)
{
	char line[96];
	uint8_t i;

	for (i = 0U; i < DEMO_BOARD_COUNT; i++)
	{
		int8_t state = PARSECS_Protocol_Board_State(demo_boards[i].board);
		if ((state >= 0) && (state != last_state[i]))
		{
			snprintf(line, sizeof(line), "%s hotplug %s %s -> %s polled=%u\n",
			         ROLE_TAG, demo_boards[i].name, state_name(last_state[i]), state_name(state),
			         (unsigned)PARSECS_HotPlug_Active_Count());
			usb_print(line);
			last_state[i] = state;
		}
	}
}

static void wait_response(PARSECS_APP_BOARD_ADDRESS board, const char *name,
                          int expected, demo_phase_t next)
{
	char line[144];
	char stats[48];
	uint32_t ber_len = 0U;
	int8_t status;
	uint8_t src = 0U;
	uint8_t dst = 0U;
	uint8_t ctrl = 0U;
	uint8_t type = 0U;
	PARSECS_APP_OperationType op = PARSECS_APP_GetRequest;

	phase_ticks++;
	status = PARSECS_Protocol_USER_Receive(board,
	                                      &src,
	                                      &dst,
	                                      &op,
	                                      &ctrl,
	                                      &type,
	                                      ber_rx,
	                                      sizeof(ber_rx),
	                                      &ber_len);
	if ((status == PARSECS_PROTOCOL_OK) &&
	    (op == PARSECS_APP_GetResponse) &&
	    (src == (uint8_t)board) &&
	    (dst == CORE_TX_WIT_MOTHERBOARD) &&
	    (type == DEMO_TYPE_ID))
	{
		int8_t value = 0;
		uint32_t idx = 0U;
		uint32_t rtt;
		bool pass;

		HAL_GPIO_WritePin(TIMING_GPIO_Port, TIMING_Pin, GPIO_PIN_RESET);
		rtt = HAL_GetTick() - request_tick;
		format_link_stats(stats, sizeof(stats), board);
		if (BERIntegerDecode(&value, ber_rx, &idx, ber_len) == true)
		{
			pass = (value == (int8_t)expected);
			snprintf(line, sizeof(line),
			         "%s %s rx GetResponse TypeID=0x%02X value=%d rtt=%lums %s%s\n",
			         ROLE_TAG, name, (unsigned)type, (int)value,
			         (unsigned long)rtt, pass ? "PASS" : "FAIL", stats);
		}
		else
		{
			snprintf(line, sizeof(line),
			         "%s %s rx GetResponse TypeID=0x%02X rtt=%lums BER decode FAIL%s\n",
			         ROLE_TAG, name, (unsigned)type, (unsigned long)rtt, stats);
		}
		usb_print(line);
		HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
		phase = next;
		phase_ticks = 0U;
		return;
	}

	if (!PARSECS_Protocol_Board_Available(board))
	{
		/* Pulled out while we waited: stop waiting now instead of after the timeout. */
		HAL_GPIO_WritePin(TIMING_GPIO_Port, TIMING_Pin, GPIO_PIN_RESET);
		snprintf(line, sizeof(line), "%s %s removed while waiting, request dropped\n",
		         ROLE_TAG, name);
		usb_print(line);
		phase = next;
		phase_ticks = 0U;
		return;
	}

	if (phase_ticks >= DEMO_RESPONSE_TIMEOUT_MS)
	{
		HAL_GPIO_WritePin(TIMING_GPIO_Port, TIMING_Pin, GPIO_PIN_RESET);
		format_link_stats(stats, sizeof(stats), board);
		snprintf(line, sizeof(line), "%s %s GetResponse timeout FAIL%s\n",
		         ROLE_TAG, name, stats);
		usb_print(line);
		phase = next;
		phase_ticks = 0U;
	}
}
#endif

void MyDemoTask(void)
{
#ifdef SPI_MASTER
	log_hotplug_changes();

	switch (phase)
	{
	case DEMO_NEXT:
		/* Next present board of this pass. A board that is not there is
		 * skipped, so the pass only takes as long as the boards that answer. */
		while ((demo_idx < DEMO_BOARD_COUNT) &&
		       !PARSECS_Protocol_Board_Available(demo_boards[demo_idx].board))
		{
			demo_idx++;
		}
		if (demo_idx >= DEMO_BOARD_COUNT)
		{
			demo_idx = 0U;
			if (pass_sent)
			{
				pass_sent = false;
				phase = DEMO_SETTLE;
				phase_ticks = 0U;
			}
			/* No board present: keep the bus free so the probes can find one. */
			break;
		}
		if (send_get(demo_boards[demo_idx].board, demo_boards[demo_idx].name))
		{
			pass_sent = true;
			phase = DEMO_WAIT;
			phase_ticks = 0U;
		}
		break;
	case DEMO_WAIT:
		wait_response(demo_boards[demo_idx].board, demo_boards[demo_idx].name,
		              demo_boards[demo_idx].value, DEMO_NEXT);
		if (phase == DEMO_NEXT)
		{
			demo_idx++;
		}
		break;
	case DEMO_SETTLE:
		/* Layer 1 keeps running so the ACK for the last response can leave. */
		if (++phase_ticks >= DEMO_SETTLE_MS)
		{
			phase = DEMO_HOLD;
			phase_ticks = 0U;
		}
		break;
	case DEMO_HOLD:
		if (PARSECS_Layer1_HoldLinesLow())
		{
			usb_print("[Master] holding SCL and SDA low\n");
			osDelay(DEMO_BUS_HOLD_MS);
			PARSECS_Layer1_ReleaseLines();
			phase = DEMO_NEXT;
			phase_ticks = 0U;
		}
		break;
	default:
		phase = DEMO_NEXT;
		break;
	}
#else
	char line[144];
	char stats[48];
	uint32_t packet_size = 0U;
	uint32_t ber_len = 0U;
	int8_t status;
	uint8_t src = 0U;
	uint8_t dst = 0U;
	uint8_t ctrl = 0U;
	uint8_t type = 0U;
	PARSECS_APP_OperationType op = PARSECS_APP_GetRequest;

	if (PARSECS_Get_Link_Epoch(0U) != link_epoch)
	{
		/* The master registered this board again: a request we were about
		 * to answer belongs to the old link. */
		link_epoch = PARSECS_Get_Link_Epoch(0U);
		request_seen = false;
		snprintf(line, sizeof(line), "%s%s link reset by master\n", ROLE_TAG, PARSECS_SLAVE_NAME);
		usb_print(line);
	}

	if (request_seen == false)
	{
		status = PARSECS_Protocol_USER_Receive(CORE_TX_WIT_MOTHERBOARD,
		                                      &src,
		                                      &dst,
		                                      &op,
		                                      &ctrl,
		                                      &type,
		                                      ber_rx,
		                                      sizeof(ber_rx),
		                                      &ber_len);
		if (status != PARSECS_PROTOCOL_OK)
		{
			return;
		}
		if ((op == PARSECS_APP_GetRequest) &&
		    (src == CORE_TX_WIT_MOTHERBOARD) &&
		    (dst == (uint8_t)PARSECS_SLAVE_BOARD) &&
		    (type == DEMO_TYPE_ID))
		{
			request_seen = true;
			snprintf(line, sizeof(line), "%s%s rx GetRequest TypeID=0x%02X\n",
			         ROLE_TAG, PARSECS_SLAVE_NAME, (unsigned)type);
			usb_print(line);
			HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
		}
		else
		{
			return;
		}
	}

	ber_len = 0U;
	if (BERIntegerEncode((int8_t)PARSECS_SLAVE_VALUE, ber_tx, &ber_len) == false)
	{
		return;
	}
	status = PARSECS_Protocol_USER_Send(CORE_TX_WIT_MOTHERBOARD,
	                                   (uint8_t)PARSECS_SLAVE_BOARD,
	                                   CORE_TX_WIT_MOTHERBOARD,
	                                   PARSECS_APP_GetResponse,
	                                   PARSECS_APP_Success,
	                                   (uint8_t)DEMO_TYPE_ID,
	                                   ber_tx,
	                                   ber_len,
	                                   &packet_size);
	if (status == PARSECS_PROTOCOL_OK)
	{
		format_link_stats(stats, sizeof(stats), CORE_TX_WIT_MOTHERBOARD);
		snprintf(line, sizeof(line),
		         "%s%s queued GetResponse TypeID=0x%02X value=%d%s\n",
		         ROLE_TAG, PARSECS_SLAVE_NAME, (unsigned)DEMO_TYPE_ID,
		         PARSECS_SLAVE_VALUE, stats);
		usb_print(line);
		request_seen = false;
	}
#endif
}
