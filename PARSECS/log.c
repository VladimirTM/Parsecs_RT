/*
 * LOG_SERIAL over USB CDC, ported from coretx_commboard/commboard_freertos_stm32/log.
 * USB is initialised by StartDefaultTask (MX_USB_DEVICE_Init), not here.
 */

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "main.h"
#include "usbd_cdc_if.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

#ifdef LOG_ENABLED
static char log_buf[LOG_BUFFER_SIZE];

/* CDC_Transmit_FS keeps a pointer to log_buf, so it must not be rewritten
 * while the previous transfer is still in flight. */
static bool log_wait_tx_idle(void)
{
	uint32_t start = HAL_GetTick();
	USBD_CDC_HandleTypeDef *hcdc;

	for (;;)
	{
		hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
		if (hcdc == NULL)
		{
			return false;
		}
		if (hcdc->TxState == 0U)
		{
			return true;
		}
		if ((HAL_GetTick() - start) >= LOG_TX_TIMEOUT_MS)
		{
			return false;
		}
	}
}
#endif

void LOG_SERIAL(const char *format, ...)
{
#ifdef LOG_ENABLED
	va_list ap;
	int len;

	if (log_wait_tx_idle() == false)
	{
		HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
		return;
	}
	va_start(ap, format);
	len = vsnprintf(log_buf, sizeof(log_buf), format, ap);
	va_end(ap);
	if (len <= 0)
	{
		return;
	}
	if ((size_t)len > (sizeof(log_buf) - 1U))
	{
		len = (int)(sizeof(log_buf) - 1U);
	}
	if (CDC_Transmit_FS((uint8_t *)log_buf, (uint16_t)len) != USBD_OK)
	{
		HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
	}
#else
	(void)format;
#endif
}
