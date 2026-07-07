/*
 * Slave side of the I2C demo. Runs in interrupt LISTEN mode and echoes back an
 * uppercased copy of each fixed-size message, one byte per Seq call. Completed
 * rounds are pushed into a log ring that the task drains to USB CDC. Persistent
 * bus errors trigger a full peripheral re-init.
 */

#include "demo_task.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "main.h"
#include "i2c.h"
#include "usbd_cdc_if.h"

#define MSG_LEN             16U             /* must match the master */
#define LOG_SLOTS           8U              /* power of two */
#define MAX_CONSEC_ERRORS   10U

#define LED_HEARTBEAT_Pin   LD4_Pin     /* green  */
#define LED_ACTIVITY_Pin    LD6_Pin     /* blue   */
#define LED_WARNING_Pin     LD3_Pin     /* orange */
#define LED_FAULT_Pin       LD5_Pin     /* red    */

static inline void led_heartbeat(void) { HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin); }
static inline void led_activity(void)  { HAL_GPIO_TogglePin(GPIOD, LED_ACTIVITY_Pin); }
static inline void led_warn(void)      { HAL_GPIO_TogglePin(GPIOD, LED_WARNING_Pin); }
static inline void led_fault_on(void)  { HAL_GPIO_WritePin(GPIOD, LED_FAULT_Pin, GPIO_PIN_SET); }
static inline void led_fault_off(void) { HAL_GPIO_WritePin(GPIOD, LED_FAULT_Pin, GPIO_PIN_RESET); }

typedef struct {
	uint8_t rx[MSG_LEN];
	uint8_t tx[MSG_LEN];
} log_entry_t;

typedef enum { PHASE_RECEIVE, PHASE_TRANSMIT } phase_t;

static uint8_t      rx_buf[MSG_LEN];
static uint8_t      tx_buf[MSG_LEN];

static log_entry_t          log_ring[LOG_SLOTS];
static volatile uint8_t     log_head = 0U;     /* ISR writes; task reads */
static volatile uint8_t     log_tail = 0U;     /* task writes; ISR reads */

static volatile uint16_t    error_count = 0U;
static volatile bool        listening   = false;
static volatile phase_t     phase       = PHASE_RECEIVE;

static volatile uint16_t    rx_byte = 0U;
static volatile uint16_t    tx_byte = 0U;

/* error_count is shared with the ISR, so keep the read-modify-write atomic. */
static void error_inc(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	error_count++;
	__set_PRIMASK(primask);
}

static void error_reset(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	error_count = 0U;
	__set_PRIMASK(primask);
}

static void usb_print(const char *msg)
{
	if (CDC_Transmit_FS((uint8_t *)msg, (uint16_t)strlen(msg)) == USBD_BUSY)
	{
		led_warn();
	}
}

static void to_uppercase(const uint8_t *src, uint8_t *dst)
{
	for (uint16_t i = 0U; i < MSG_LEN; i++)
	{
		uint8_t c = src[i];
		dst[i] = (c >= 'a' && c <= 'z') ? (uint8_t)(c - ('a' - 'A')) : c;
	}
}

/* Flag the reply's last byte LAST so the master's terminating NACK is routed
 * to ListenCplt as a normal end-of-read; the request ends on the master STOP. */
static uint32_t slave_rx_frame(uint16_t i)
{
	return (i == 0U) ? I2C_FIRST_FRAME : I2C_NEXT_FRAME;
}

static uint32_t slave_tx_frame(uint16_t i)
{
	if (i == 0U)             return I2C_FIRST_FRAME;
	if (i == (MSG_LEN - 1U)) return I2C_LAST_FRAME;
	return I2C_NEXT_FRAME;
}

static void start_listen(void)
{
	phase = PHASE_RECEIVE;
	listening = (HAL_I2C_EnableListen_IT(&hi2c1) == HAL_OK);
}

static void i2c_resync(void)
{
	/* Mask the I2C IRQs first so a late callback can't race the teardown. */
	HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
	HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
	HAL_I2C_DeInit(&hi2c1);
	HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
	HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
	led_fault_on();             /* latch the fault while IRQs are still off */
	MX_I2C1_Init();
	listening = false;
	error_reset();
	phase = PHASE_RECEIVE;
}

static void round_complete(void)
{
	led_heartbeat();
	led_fault_off();

	uint8_t next = (uint8_t)((log_head + 1U) & (LOG_SLOTS - 1U));
	if (next != log_tail)
	{
		memcpy(log_ring[log_head].rx, rx_buf, MSG_LEN);
		memcpy(log_ring[log_head].tx, tx_buf, MSG_LEN);
		log_head = next;
	}
	else
	{
		led_warn();             /* USB too slow to drain: entry dropped */
	}

	error_reset();
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection,
                          uint16_t AddrMatchCode)
{
	if (hi2c->Instance != I2C1) return;
	(void)AddrMatchCode;

	/* On F4, TransferDirection == TRANSMIT means the master is writing (we
	 * receive); RECEIVE means the master is reading (we transmit). Arm only
	 * the first byte; the Cplt callbacks stream the rest. */
	if (TransferDirection == I2C_DIRECTION_TRANSMIT)
	{
		phase   = PHASE_RECEIVE;
		rx_byte = 0U;
		memset(rx_buf, 0, sizeof(rx_buf));
		if (HAL_I2C_Slave_Seq_Receive_IT(hi2c, &rx_buf[0], 1U,
		                                 slave_rx_frame(0U)) != HAL_OK)
		{
			error_inc();
			listening = false;
		}
	}
	else
	{
		phase   = PHASE_TRANSMIT;
		tx_byte = 0U;
		if (HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &tx_buf[0], 1U,
		                                  slave_tx_frame(0U)) != HAL_OK)
		{
			error_inc();
			listening = false;
		}
	}
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	rx_byte++;
	if (rx_byte < MSG_LEN)
	{
		if (HAL_I2C_Slave_Seq_Receive_IT(hi2c, &rx_buf[rx_byte], 1U,
		                                 slave_rx_frame(rx_byte)) != HAL_OK)
		{
			error_inc();
			listening = false;
		}
		return;
	}

	to_uppercase(rx_buf, tx_buf);   /* reply ready before the read phase */
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	tx_byte++;
	if (tx_byte < MSG_LEN)
	{
		if (HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &tx_buf[tx_byte], 1U,
		                                  slave_tx_frame(tx_byte)) != HAL_OK)
		{
			error_inc();
			listening = false;
		}
	}
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	/* A STOP (or the read's terminating NACK) ended the transaction; if it
	 * closed the read phase the reply is fully out, so the round is done. */
	if (phase == PHASE_TRANSMIT && tx_byte >= MSG_LEN)
	{
		round_complete();
	}

	listening = (HAL_I2C_EnableListen_IT(hi2c) == HAL_OK);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	/* The normal NACK is handled via LAST_FRAME -> ListenCplt, so anything
	 * here is a real bus error. Drop listen; the task re-arms it. */
	error_inc();
	listening = false;
}

void MyDemoTask(void)
{
	if (!listening)
	{
		start_listen();
	}

	if (log_tail != log_head)
	{
		const log_entry_t *e = &log_ring[log_tail];
		char line[80];
		uint8_t rx_safe[MSG_LEN];
		uint8_t tx_safe[MSG_LEN];
		memcpy(rx_safe, e->rx, MSG_LEN); rx_safe[MSG_LEN - 1U] = '\0';
		memcpy(tx_safe, e->tx, MSG_LEN); tx_safe[MSG_LEN - 1U] = '\0';
		snprintf(line, sizeof(line),
		         "[Slave]  rx='%s' tx='%s'\n",
		         (const char *)rx_safe, (const char *)tx_safe);
		usb_print(line);
		led_activity();
		log_tail = (uint8_t)((log_tail + 1U) & (LOG_SLOTS - 1U));
	}

	if (error_count >= MAX_CONSEC_ERRORS)
	{
		i2c_resync();
		usb_print("[Slave]  I2C resync\n");
	}
}
