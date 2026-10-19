/*
 * PARSECS High Level segmentation test via USER_Send / USER_Receive.
 * Layer 1 I2C DMA, L2/L3, and L4/L6/L7 run in PARSECS_LowLevelTask and
 * PARSECS_Protocol_Interface_Task. This file must not call TRANSMIT_APP / RECEIVE_APP.
 *
 * Master sends a CallRequest with method DEMO_SEG_METHOD_ID and the
 * EncodeTestData payload (about 1000 bytes of BER, several WIT segments) to
 * comm (0x08), then mobility (0x09). Each slave checks the payload and echoes
 * it in a CallResponse. The master compares the echo, lets ACKs drain, holds
 * SCL and SDA low, then repeats. The slave image is selected with
 * PARSECS_SLAVE_INDEX (see slave_identity.h).
 *
 * On the master, PE7 (TIMING_Pin) is high from the moment a CallRequest is
 * queued until its CallResponse arrives or the request times out.
 */

#include "demo_task.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "log.h"
#include "PARSECS_Protocol_Interface.h"
#include <ber.h>

#ifdef SPI_MASTER
#include "cmsis_os.h"
#include "PARSECS_Layer1.h"
#else
#include "slave_identity.h"
#endif

#define LED_HEARTBEAT_Pin        LD4_Pin
/* Same method ID as COMMBOARD_METHOD_TEST_SPI_COMMUNICATION in coretx. */
#define DEMO_SEG_METHOD_ID       0xFEU
#define DEMO_SEG_REPEAT          15U
/* About 1300 bytes each way at one byte per slave every ~2 ms, plus logs. */
#define DEMO_RESPONSE_TIMEOUT_MS 15000U
#define DEMO_SETTLE_MS           100U
#define DEMO_BUS_HOLD_MS         500U

#ifdef SPI_MASTER
#define ROLE_TAG            "[Master]"
#else
#define ROLE_TAG            "[Slave] "
#endif

static uint8_t seg_expected[PARSECS_WIT_FRAME_LENGTH];
static uint32_t seg_expected_len = 0U;
static uint8_t ber_rx[PARSECS_WIT_FRAME_LENGTH];

#ifdef SPI_MASTER
typedef enum {
	DEMO_CALL_COMM = 0,
	DEMO_WAIT_COMM,
	DEMO_CALL_MOB,
	DEMO_WAIT_MOB,
	DEMO_SETTLE,
	DEMO_HOLD,
} demo_phase_t;

static demo_phase_t phase = DEMO_CALL_COMM;
static uint16_t phase_ticks = 0U;
static uint32_t request_tick = 0U;
#else
static bool request_seen = false;
static bool request_valid = false;
static uint32_t request_len = 0U;
#endif

/* Goes through the same LOG_SERIAL ring as the stack logs, so demo lines and
 * stack logs stay in order and never wait on USB. */
static void usb_print(const char *msg)
{
	LOG_SERIAL("%s", msg);
}

/* Same payload as EncodeTestData in coretx brapi_cb.c / app_spi_dispatcher.c. */
static void EncodeTestData(uint8_t *encoded_data_buffer, uint32_t *encoded_size)
{
	static const char test_string[] = "Test String from motherboard for segmentation transmission !!   ";
	uint32_t encoded_data_buffer_size = 0U;
	uint32_t i;

	BERStructureEncode(DEMO_SEG_REPEAT + 2U, encoded_data_buffer, &encoded_data_buffer_size);
	BERUnsignedEncode(DEMO_SEG_REPEAT, encoded_data_buffer, &encoded_data_buffer_size);
	for (i = 0U; i < DEMO_SEG_REPEAT; i++)
	{
		BEROctetStringEncode((uint8_t *)test_string, strlen(test_string), encoded_data_buffer, &encoded_data_buffer_size);
	}
	BERLongUnsignedEncode(0xCACA, encoded_data_buffer, &encoded_data_buffer_size);
	*encoded_size = encoded_data_buffer_size;
}

void DemoTask_Init(void)
{
	EncodeTestData(seg_expected, &seg_expected_len);
#ifdef SPI_MASTER
	GPIO_InitTypeDef gpio = {0};

	__HAL_RCC_GPIOE_CLK_ENABLE();
	HAL_GPIO_WritePin(TIMING_GPIO_Port, TIMING_Pin, GPIO_PIN_RESET);
	gpio.Pin = TIMING_Pin;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(TIMING_GPIO_Port, &gpio);

	phase = DEMO_CALL_COMM;
	phase_ticks = 0U;
#else
	request_seen = false;
	request_valid = false;
	request_len = 0U;
#endif
}

#ifdef SPI_MASTER
static bool send_call(PARSECS_APP_BOARD_ADDRESS board, const char *name)
{
	char line[96];
	uint32_t packet_size = 0U;
	int8_t status;

	status = PARSECS_Protocol_USER_Send(board,
	                                   CORE_TX_WIT_MOTHERBOARD,
	                                   (uint8_t)board,
	                                   PARSECS_APP_CallRequest,
	                                   0xFF,
	                                   (uint8_t)DEMO_SEG_METHOD_ID,
	                                   seg_expected,
	                                   seg_expected_len,
	                                   &packet_size);
	if (status != PARSECS_PROTOCOL_OK)
	{
		return false;
	}
	HAL_GPIO_WritePin(TIMING_GPIO_Port, TIMING_Pin, GPIO_PIN_SET);
	request_tick = HAL_GetTick();
	snprintf(line, sizeof(line), "%s %s queued CallRequest 0x%02X ber=%lu wit=%lu\n",
	         ROLE_TAG, name, (unsigned)DEMO_SEG_METHOD_ID,
	         (unsigned long)seg_expected_len, (unsigned long)packet_size);
	usb_print(line);
	return true;
}

static void wait_response(PARSECS_APP_BOARD_ADDRESS board, const char *name,
                          demo_phase_t next)
{
	char line[96];
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
	    (op == PARSECS_APP_CallResponse) &&
	    (src == (uint8_t)board) &&
	    (dst == CORE_TX_WIT_MOTHERBOARD) &&
	    (type == DEMO_SEG_METHOD_ID))
	{
		uint32_t rtt;
		bool pass;

		HAL_GPIO_WritePin(TIMING_GPIO_Port, TIMING_Pin, GPIO_PIN_RESET);
		rtt = HAL_GetTick() - request_tick;
		pass = (ctrl == PARSECS_APP_Success) &&
		       (ber_len == seg_expected_len) &&
		       (memcmp(ber_rx, seg_expected, seg_expected_len) == 0);
		snprintf(line, sizeof(line),
		         "%s %s rx CallResponse ctrl=%u ber=%lu rtt=%lums %s\n",
		         ROLE_TAG, name, (unsigned)ctrl, (unsigned long)ber_len,
		         (unsigned long)rtt, pass ? "PASS" : "FAIL");
		usb_print(line);
		HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
		phase = next;
		phase_ticks = 0U;
		return;
	}

	if (phase_ticks >= DEMO_RESPONSE_TIMEOUT_MS)
	{
		HAL_GPIO_WritePin(TIMING_GPIO_Port, TIMING_Pin, GPIO_PIN_RESET);
		snprintf(line, sizeof(line), "%s %s CallResponse timeout FAIL\n",
		         ROLE_TAG, name);
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
	case DEMO_CALL_COMM:
		if (send_call(CORE_TX_WIT_COMM_BOARD, "comm"))
		{
			phase = DEMO_WAIT_COMM;
			phase_ticks = 0U;
		}
		break;
	case DEMO_WAIT_COMM:
		wait_response(CORE_TX_WIT_COMM_BOARD, "comm", DEMO_CALL_MOB);
		break;
	case DEMO_CALL_MOB:
		if (send_call(CORE_TX_WIT_MOBILITY_BOARD, "mobility"))
		{
			phase = DEMO_WAIT_MOB;
			phase_ticks = 0U;
		}
		break;
	case DEMO_WAIT_MOB:
		wait_response(CORE_TX_WIT_MOBILITY_BOARD, "mobility", DEMO_SETTLE);
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
			phase = DEMO_CALL_COMM;
			phase_ticks = 0U;
		}
		break;
	default:
		phase = DEMO_CALL_COMM;
		break;
	}
#else
	static uint8_t ber_err[4];
	char line[96];
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
		                                      sizeof(ber_rx),
		                                      &ber_len);
		if (status != PARSECS_PROTOCOL_OK)
		{
			return;
		}
		if ((op != PARSECS_APP_CallRequest) ||
		    (src != CORE_TX_WIT_MOTHERBOARD) ||
		    (dst != (uint8_t)PARSECS_SLAVE_BOARD) ||
		    (type != DEMO_SEG_METHOD_ID))
		{
			return;
		}
		request_seen = true;
		request_len = ber_len;
		request_valid = (ber_len == seg_expected_len) &&
		                (memcmp(ber_rx, seg_expected, seg_expected_len) == 0);
		snprintf(line, sizeof(line), "%s%s rx CallRequest 0x%02X ber=%lu %s\n",
		         ROLE_TAG, PARSECS_SLAVE_NAME, (unsigned)type,
		         (unsigned long)ber_len, request_valid ? "match" : "MISMATCH");
		usb_print(line);
		HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
	}

	/* ber_rx is not overwritten while request_seen is true, so it is the echo. */
	if (request_valid)
	{
		status = PARSECS_Protocol_USER_Send(CORE_TX_WIT_MOTHERBOARD,
		                                   (uint8_t)PARSECS_SLAVE_BOARD,
		                                   CORE_TX_WIT_MOTHERBOARD,
		                                   PARSECS_APP_CallResponse,
		                                   PARSECS_APP_Success,
		                                   (uint8_t)DEMO_SEG_METHOD_ID,
		                                   ber_rx,
		                                   request_len,
		                                   &packet_size);
	}
	else
	{
		ber_len = 0U;
		if (BERNullDataEncode(ber_err, &ber_len) == false)
		{
			return;
		}
		status = PARSECS_Protocol_USER_Send(CORE_TX_WIT_MOTHERBOARD,
		                                   (uint8_t)PARSECS_SLAVE_BOARD,
		                                   CORE_TX_WIT_MOTHERBOARD,
		                                   PARSECS_APP_CallResponse,
		                                   PARSECS_APP_OtherReason,
		                                   (uint8_t)DEMO_SEG_METHOD_ID,
		                                   ber_err,
		                                   ber_len,
		                                   &packet_size);
	}
	if (status == PARSECS_PROTOCOL_OK)
	{
		snprintf(line, sizeof(line), "%s%s queued CallResponse %s wit=%lu\n",
		         ROLE_TAG, PARSECS_SLAVE_NAME,
		         request_valid ? "Success" : "OtherReason",
		         (unsigned long)packet_size);
		usb_print(line);
		request_seen = false;
	}
#endif
}
