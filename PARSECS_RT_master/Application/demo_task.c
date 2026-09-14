/*
 * PARSECS Low Level demo: queue a framed payload via PARSECS_TRANSMIT_APP
 * and print whatever PARSECS_RECEIVE_APP delivers over USB CDC.
 * Layer 1 I2C DMA, Layer 2 framing and Layer 3 ACK run in PARSECS_LowLevelTask.
 */

#include "demo_task.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "usbd_cdc_if.h"
#include "PARSECS_LowLevelAPI.h"
#include "PARSECS_Mode.h"

#define LED_HEARTBEAT_Pin   LD4_Pin
#define LED_WARNING_Pin     LD3_Pin

#ifdef SPI_MASTER
#define ROLE_TAG            "[Master]"
static const uint8_t k_tx_payload[] = { 'P', 'I', 'N', 'G' };
#else
#define ROLE_TAG            "[Slave] "
static const uint8_t k_tx_payload[] = { 'P', 'O', 'N', 'G' };
#endif

static int8_t peer_id = 0;
static bool tx_queued = false;
static uint8_t rx_buf[DATA_BUFFER_SIZE];

#ifdef SPI_MASTER
static void i2c_cs_noop(void)
{
}
#endif

static void usb_print(const char *msg)
{
	if (CDC_Transmit_FS((uint8_t *)msg, (uint16_t)strlen(msg)) == USBD_BUSY)
	{
		HAL_GPIO_TogglePin(GPIOD, LED_WARNING_Pin);
	}
}

void DemoTask_Init(void)
{
#ifdef SPI_MASTER
	peer_id = PARSECS_Add_Slave(i2c_cs_noop, i2c_cs_noop);
#else
	peer_id = 0;
#endif
	tx_queued = false;
}

void MyDemoTask(void)
{
	uint8_t id = (peer_id >= 0) ? (uint8_t)peer_id : 0U;
	uint8_t seq = 0U;
	uint8_t rx_len = 0U;
	char line[80];

	if (!tx_queued && (PARSECS_CHECK_APP_TX_READY(id) == 0))
	{
		if (PARSECS_TRANSMIT_APP(id, (uint8_t *)k_tx_payload, (uint8_t)sizeof(k_tx_payload), &seq) == 0)
		{
			tx_queued = true;
			snprintf(line, sizeof(line), "%s queued '%c%c%c%c' seq=%u\n",
			         ROLE_TAG,
			         (char)k_tx_payload[0], (char)k_tx_payload[1],
			         (char)k_tx_payload[2], (char)k_tx_payload[3],
			         (unsigned)seq);
			usb_print(line);
		}
	}

	if (PARSECS_RECEIVE_APP(id, rx_buf, &rx_len, &seq) == 0)
	{
		char payload[9];
		uint8_t n = (rx_len < 8U) ? rx_len : 8U;
		uint8_t i;
		for (i = 0; i < n; i++)
		{
			payload[i] = ((rx_buf[i] >= 32U) && (rx_buf[i] < 127U)) ? (char)rx_buf[i] : '.';
		}
		payload[n] = '\0';
		snprintf(line, sizeof(line), "%s rx len=%u seq=%u '%s'\n",
		         ROLE_TAG, (unsigned)rx_len, (unsigned)seq, payload);
		usb_print(line);
		HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
	}
}
