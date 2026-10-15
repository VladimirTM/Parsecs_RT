/*
 * PARSECS High Level demo: GetRequest / GetResponse via USER_Send / USER_Receive.
 * Layer 1 I2C DMA, L2/L3, and L4/L6/L7 run in PARSECS_LowLevelTask and
 * PARSECS_Protocol_Interface_Task. This file must not call TRANSMIT_APP / RECEIVE_APP.
 *
 * Master polls comm (0x08, value 42) then mobility (0x09, value 43), lets ACKs
 * drain, holds SCL and SDA low, then repeats. The slave image is selected with
 * PARSECS_SLAVE_INDEX (see slave_identity.h).
 */

#include "demo_task.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "usbd_cdc_if.h"
#include "PARSECS_Protocol_Interface.h"
#include "PARSECS_LowLevelAPI.h"
#include <ber.h>

#ifdef SPI_MASTER
#include "cmsis_os.h"
#include "PARSECS_Layer1.h"
#else
#include "slave_identity.h"
#endif

#define LED_HEARTBEAT_Pin        LD4_Pin
#define LED_WARNING_Pin          LD3_Pin
#define DEMO_TYPE_ID             0x01U
#define DEMO_COMM_VALUE          42
#define DEMO_MOBILITY_VALUE      43
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
	DEMO_GET_COMM = 0,
	DEMO_WAIT_COMM,
	DEMO_GET_MOB,
	DEMO_WAIT_MOB,
	DEMO_SETTLE,
	DEMO_HOLD,
} demo_phase_t;

static demo_phase_t phase = DEMO_GET_COMM;
static uint16_t phase_ticks = 0U;
#else
static bool request_seen = false;
#endif

static void usb_print(const char *msg)
{
	if (CDC_Transmit_FS((uint8_t *)msg, (uint16_t)strlen(msg)) == USBD_BUSY)
	{
		HAL_GPIO_TogglePin(GPIOD, LED_WARNING_Pin);
	}
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
	phase = DEMO_GET_COMM;
	phase_ticks = 0U;
#else
	request_seen = false;
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
	snprintf(line, sizeof(line), "%s %s queued GetRequest TypeID=0x%02X\n",
	         ROLE_TAG, name, (unsigned)DEMO_TYPE_ID);
	usb_print(line);
	return true;
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
	                                      &ber_len);
	if ((status == PARSECS_PROTOCOL_OK) &&
	    (op == PARSECS_APP_GetResponse) &&
	    (src == (uint8_t)board) &&
	    (dst == CORE_TX_WIT_MOTHERBOARD) &&
	    (type == DEMO_TYPE_ID))
	{
		int8_t value = 0;
		uint32_t idx = 0U;
		bool pass;

		format_link_stats(stats, sizeof(stats), board);
		if (BERIntegerDecode(&value, ber_rx, &idx, ber_len) == true)
		{
			pass = (value == (int8_t)expected);
			snprintf(line, sizeof(line),
			         "%s %s rx GetResponse TypeID=0x%02X value=%d %s%s\n",
			         ROLE_TAG, name, (unsigned)type, (int)value,
			         pass ? "PASS" : "FAIL", stats);
		}
		else
		{
			snprintf(line, sizeof(line),
			         "%s %s rx GetResponse TypeID=0x%02X BER decode FAIL%s\n",
			         ROLE_TAG, name, (unsigned)type, stats);
		}
		usb_print(line);
		HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
		phase = next;
		phase_ticks = 0U;
		return;
	}

	if (phase_ticks >= DEMO_RESPONSE_TIMEOUT_MS)
	{
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
	switch (phase)
	{
	case DEMO_GET_COMM:
		if (send_get(CORE_TX_WIT_COMM_BOARD, "comm"))
		{
			phase = DEMO_WAIT_COMM;
			phase_ticks = 0U;
		}
		break;
	case DEMO_WAIT_COMM:
		wait_response(CORE_TX_WIT_COMM_BOARD, "comm", DEMO_COMM_VALUE, DEMO_GET_MOB);
		break;
	case DEMO_GET_MOB:
		if (send_get(CORE_TX_WIT_MOBILITY_BOARD, "mobility"))
		{
			phase = DEMO_WAIT_MOB;
			phase_ticks = 0U;
		}
		break;
	case DEMO_WAIT_MOB:
		wait_response(CORE_TX_WIT_MOBILITY_BOARD, "mobility", DEMO_MOBILITY_VALUE, DEMO_SETTLE);
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
			phase = DEMO_GET_COMM;
			phase_ticks = 0U;
		}
		break;
	default:
		phase = DEMO_GET_COMM;
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

	if (request_seen == false)
	{
		status = PARSECS_Protocol_USER_Receive(CORE_TX_WIT_MOTHERBOARD,
		                                      &src,
		                                      &dst,
		                                      &op,
		                                      &ctrl,
		                                      &type,
		                                      ber_rx,
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
