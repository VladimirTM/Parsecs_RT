/*
 * LOG_SERIAL over USB CDC, ported from coretx_commboard/commboard_freertos_stm32/log.
 * USB is initialised by StartDefaultTask (MX_USB_DEVICE_Init), not here.
 *
 * LOG_SERIAL only formats the message into a RAM ring and returns, so the
 * PARSECS stack never waits on USB. LOG_TaskStart runs as its own task and
 * drains the ring to CDC whenever the previous transfer has finished.
 */

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "main.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usbd_cdc_if.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

#ifdef LOG_ENABLED
static char fmt_buf[LOG_BUFFER_SIZE];
static uint8_t log_ring[LOG_RING_SIZE];
static volatile uint32_t ring_head = 0U;
static volatile uint32_t ring_tail = 0U;
/* CDC_Transmit_FS keeps a pointer to this buffer until the transfer ends. */
static uint8_t tx_chunk[LOG_CHUNK_SIZE];

static uint32_t ring_free(void)
{
	return (ring_tail + LOG_RING_SIZE - ring_head - 1U) % LOG_RING_SIZE;
}
#endif

/* fmt_buf is shared, so LOG_SERIAL must only be called from task context
 * by one task at a time (today all callers run in myTestTask). */
void LOG_SERIAL(const char *format, ...)
{
#ifdef LOG_ENABLED
	va_list ap;
	int len;
	uint32_t first;

	va_start(ap, format);
	len = vsnprintf(fmt_buf, sizeof(fmt_buf), format, ap);
	va_end(ap);
	if (len <= 0)
	{
		return;
	}
	if ((size_t)len > (sizeof(fmt_buf) - 1U))
	{
		len = (int)(sizeof(fmt_buf) - 1U);
	}

	taskENTER_CRITICAL();
	if (ring_free() < (uint32_t)len)
	{
		taskEXIT_CRITICAL();
		HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
		return;
	}
	first = LOG_RING_SIZE - ring_head;
	if (first > (uint32_t)len)
	{
		first = (uint32_t)len;
	}
	memcpy(&log_ring[ring_head], fmt_buf, first);
	memcpy(&log_ring[0], &fmt_buf[first], (uint32_t)len - first);
	ring_head = (ring_head + (uint32_t)len) % LOG_RING_SIZE;
	taskEXIT_CRITICAL();
#else
	(void)format;
#endif
}

void LOG_TaskStart(void *argument)
{
	(void)argument;
#ifdef LOG_ENABLED
	USBD_CDC_HandleTypeDef *hcdc;
	uint32_t head;
	uint32_t count;

	for (;;)
	{
		hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
		head = ring_head;
		if ((hcdc != NULL) && (hcdc->TxState == 0U) && (head != ring_tail))
		{
			count = (head > ring_tail) ? (head - ring_tail) : (LOG_RING_SIZE - ring_tail);
			if (count > LOG_CHUNK_SIZE)
			{
				count = LOG_CHUNK_SIZE;
			}
			memcpy(tx_chunk, &log_ring[ring_tail], count);
			if (CDC_Transmit_FS(tx_chunk, (uint16_t)count) == USBD_OK)
			{
				ring_tail = (ring_tail + count) % LOG_RING_SIZE;
			}
		}
		osDelay(1);
	}
#else
	osThreadExit();
#endif
}
