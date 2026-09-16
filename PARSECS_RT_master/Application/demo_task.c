/*
 * PARSECS High Level demo: GetRequest / GetResponse via USER_Send / USER_Receive.
 * Layer 1 I2C DMA, L2/L3, and L4/L6/L7 run in PARSECS_LowLevelTask and
 * PARSECS_Protocol_Interface_Task. This file must not call TRANSMIT_APP / RECEIVE_APP.
 */

#include "demo_task.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "usbd_cdc_if.h"
#include "PARSECS_Protocol_Interface.h"
#include <ber.h>

#define LED_HEARTBEAT_Pin   LD4_Pin
#define LED_WARNING_Pin     LD3_Pin
#define DEMO_TYPE_ID        0x01U
#define DEMO_VALUE          42

#ifdef SPI_MASTER
#define ROLE_TAG            "[Master]"
#else
#define ROLE_TAG            "[Slave] "
#endif

static bool request_sent;
static bool request_seen;
static bool response_sent;
static bool response_seen;
static uint8_t ber_tx[8];
static uint8_t ber_rx[32];

static void usb_print(const char *msg)
{
	if (CDC_Transmit_FS((uint8_t *)msg, (uint16_t)strlen(msg)) == USBD_BUSY)
	{
		HAL_GPIO_TogglePin(GPIOD, LED_WARNING_Pin);
	}
}

void DemoTask_Init(void)
{
	request_sent = false;
	request_seen = false;
	response_sent = false;
	response_seen = false;
}

void MyDemoTask(void)
{
	char line[80];
	uint32_t packet_size = 0U;
	uint32_t ber_len = 0U;
	int8_t status;
	uint8_t src = 0U;
	uint8_t dst = 0U;
	uint8_t ctrl = 0U;
	uint8_t type = 0U;
	PARSECS_APP_OperationType op = PARSECS_APP_GetRequest;

#ifdef SPI_MASTER
	if (request_sent == false)
	{
		ber_len = 0U;
		if (BERNullDataEncode(ber_tx, &ber_len) == false)
		{
			return;
		}
		status = PARSECS_Protocol_USER_Send(CORE_TX_WIT_COMM_BOARD,
		                                   CORE_TX_WIT_MOTHERBOARD,
		                                   CORE_TX_WIT_COMM_BOARD,
		                                   PARSECS_APP_GetRequest,
		                                   0xFF,
		                                   (uint8_t)DEMO_TYPE_ID,
		                                   ber_tx,
		                                   ber_len,
		                                   &packet_size);
		if (status == PARSECS_PROTOCOL_OK)
		{
			request_sent = true;
			snprintf(line, sizeof(line), "%s queued GetRequest TypeID=0x%02X\n",
			         ROLE_TAG, (unsigned)DEMO_TYPE_ID);
			usb_print(line);
		}
		return;
	}

	if (response_seen != false)
	{
		return;
	}

	status = PARSECS_Protocol_USER_Receive(CORE_TX_WIT_COMM_BOARD,
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
	if ((op == PARSECS_APP_GetResponse) &&
	    (src == CORE_TX_WIT_COMM_BOARD) &&
	    (dst == CORE_TX_WIT_MOTHERBOARD) &&
	    (type == DEMO_TYPE_ID))
	{
		int8_t value = 0;
		uint32_t idx = 0U;
		if (BERIntegerDecode(&value, ber_rx, &idx, ber_len) == true)
		{
			snprintf(line, sizeof(line),
			         "%s rx GetResponse TypeID=0x%02X value=%d\n",
			         ROLE_TAG, (unsigned)type, (int)value);
		}
		else
		{
			snprintf(line, sizeof(line),
			         "%s rx GetResponse TypeID=0x%02X (BER decode failed)\n",
			         ROLE_TAG, (unsigned)type);
		}
		usb_print(line);
		HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
		response_seen = true;
	}
#else
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
		    (dst == CORE_TX_WIT_COMM_BOARD) &&
		    (type == DEMO_TYPE_ID))
		{
			request_seen = true;
			snprintf(line, sizeof(line), "%s rx GetRequest TypeID=0x%02X\n",
			         ROLE_TAG, (unsigned)type);
			usb_print(line);
			HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
		}
		else
		{
			return;
		}
	}

	if (response_sent != false)
	{
		return;
	}

	ber_len = 0U;
	if (BERIntegerEncode((int8_t)DEMO_VALUE, ber_tx, &ber_len) == false)
	{
		return;
	}
	status = PARSECS_Protocol_USER_Send(CORE_TX_WIT_MOTHERBOARD,
	                                   CORE_TX_WIT_COMM_BOARD,
	                                   CORE_TX_WIT_MOTHERBOARD,
	                                   PARSECS_APP_GetResponse,
	                                   PARSECS_APP_Success,
	                                   (uint8_t)DEMO_TYPE_ID,
	                                   ber_tx,
	                                   ber_len,
	                                   &packet_size);
	if (status == PARSECS_PROTOCOL_OK)
	{
		response_sent = true;
		snprintf(line, sizeof(line),
		         "%s queued GetResponse TypeID=0x%02X value=%d\n",
		         ROLE_TAG, (unsigned)DEMO_TYPE_ID, DEMO_VALUE);
		usb_print(line);
	}
#endif
}
