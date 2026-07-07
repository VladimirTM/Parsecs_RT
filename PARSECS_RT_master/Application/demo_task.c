/*
 * Master side of the I2C demo. Non-blocking, byte-by-byte transfers driven
 * from the I2C1 ISR: each round sends a fixed "hello N" payload and reads back
 * the slave's uppercased echo. A per-tick watchdog plus an error threshold
 * re-init the peripheral if the bus ever gets stuck.
 */

#include "demo_task.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "main.h"
#include "i2c.h"
#include "usbd_cdc_if.h"

#define SLAVE_ADDRESS               (0x08 << 1)
#define MSG_LEN                     16U          /* fixed payload, NUL-padded */
#define TX_SLOTS                    8U           /* power of two */
#define PRODUCER_PERIOD_TICKS       250U
#define MAX_CONSEC_ERRORS           10U
#define TRANSACTION_TIMEOUT_TICKS   50U

#define LED_HEARTBEAT_Pin   LD4_Pin     /* green  */
#define LED_ACTIVITY_Pin    LD6_Pin     /* blue   */
#define LED_WARNING_Pin     LD3_Pin     /* orange */
#define LED_FAULT_Pin       LD5_Pin     /* red    */

static inline void led_heartbeat(void) { HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin); }
static inline void led_activity(void)  { HAL_GPIO_TogglePin(GPIOD, LED_ACTIVITY_Pin); }
static inline void led_warn(void)      { HAL_GPIO_TogglePin(GPIOD, LED_WARNING_Pin); }
static inline void led_fault_on(void)  { HAL_GPIO_WritePin(GPIOD, LED_FAULT_Pin, GPIO_PIN_SET); }
static inline void led_fault_off(void) { HAL_GPIO_WritePin(GPIOD, LED_FAULT_Pin, GPIO_PIN_RESET); }

typedef struct { uint8_t data[MSG_LEN]; } msg_t;

typedef enum {
	BUS_IDLE = 0,
	BUS_SENDING,
	BUS_RECEIVING,
} bus_state_t;

static msg_t                tx_ring[TX_SLOTS];
static uint8_t              write_index = 0U;
static volatile uint8_t     read_index  = 0U;

static msg_t                sent_msg;           /* stays valid while HAL holds the pointer */
static msg_t                echo;

static volatile bus_state_t bus_state   = BUS_IDLE;
static volatile bool        echo_ready  = false;
static volatile uint16_t    error_count = 0U;

static volatile uint16_t    tx_byte     = 0U;
static volatile uint16_t    rx_byte     = 0U;

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

static inline uint8_t ring_count(void)
{
	return (uint8_t)((write_index - read_index) & (TX_SLOTS - 1U));
}

static inline bool ring_empty(void) { return write_index == read_index; }
static inline bool ring_full(void)  { return ring_count() == (TX_SLOTS - 1U); }

static bool ring_push(const char *s)
{
	if (ring_full()) return false;
	msg_t *slot = &tx_ring[write_index];
	memset(slot->data, 0, MSG_LEN);
	size_t n = strlen(s);
	if (n > (MSG_LEN - 1U)) n = (MSG_LEN - 1U);
	memcpy(slot->data, s, n);
	write_index = (uint8_t)((write_index + 1U) & (TX_SLOTS - 1U));
	return true;
}

static void usb_print(const char *msg)
{
	if (CDC_Transmit_FS((uint8_t *)msg, (uint16_t)strlen(msg)) == USBD_BUSY)
	{
		led_warn();
	}
}

static bool echo_matches(const uint8_t *sent, const uint8_t *got)
{
	for (uint16_t i = 0U; i < MSG_LEN; i++)
	{
		uint8_t c  = sent[i];
		uint8_t up = (c >= 'a' && c <= 'z') ? (uint8_t)(c - ('a' - 'A')) : c;
		if (up != got[i]) return false;
	}
	return true;
}

static void i2c_resync(void)
{
	/* Mask the I2C IRQs first so a late callback can't race the teardown. */
	HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
	HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
	HAL_I2C_DeInit(&hi2c1);
	HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
	HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
	MX_I2C1_Init();
	bus_state  = BUS_IDLE;
	echo_ready = false;
}

/* Master owns START/STOP: first byte opens the frame, last byte closes it. */
static uint32_t frame_opt(uint16_t i)
{
	if (i == 0U)             return I2C_FIRST_AND_NEXT_FRAME;
	if (i == (MSG_LEN - 1U)) return I2C_LAST_FRAME;
	return I2C_NEXT_FRAME;
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	tx_byte++;
	if (tx_byte < MSG_LEN)
	{
		if (HAL_I2C_Master_Seq_Transmit_IT(hi2c, SLAVE_ADDRESS,
		                                   &sent_msg.data[tx_byte], 1U,
		                                   frame_opt(tx_byte)) != HAL_OK)
		{
			bus_state = BUS_IDLE;
			error_inc();
		}
		return;
	}

	/* Request sent, turn the bus around to read the echo. */
	bus_state = BUS_RECEIVING;
	rx_byte   = 0U;
	if (HAL_I2C_Master_Seq_Receive_IT(hi2c, SLAVE_ADDRESS,
	                                  &echo.data[0], 1U, frame_opt(0U)) != HAL_OK)
	{
		bus_state = BUS_IDLE;
		error_inc();
	}
}

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	rx_byte++;
	if (rx_byte < MSG_LEN)
	{
		if (HAL_I2C_Master_Seq_Receive_IT(hi2c, SLAVE_ADDRESS,
		                                  &echo.data[rx_byte], 1U,
		                                  frame_opt(rx_byte)) != HAL_OK)
		{
			bus_state = BUS_IDLE;
			error_inc();
		}
		return;
	}

	/* Whole echo is in, consume the slot and flag the task. */
	read_index = (uint8_t)((read_index + 1U) & (TX_SLOTS - 1U));
	echo_ready = true;
	bus_state  = BUS_IDLE;
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	/* Leave the slot in the ring so the task retries it next tick. */
	error_inc();
	bus_state = BUS_IDLE;
}

void MyDemoTask(void)
{
	static uint32_t producer_tick = 0U;
	static uint16_t next_seq      = 0U;
	static uint16_t wait_ticks    = 0U;

	/* Watchdog: force a resync if a transaction never completes. */
	if (bus_state != BUS_IDLE)
	{
		if (++wait_ticks > TRANSACTION_TIMEOUT_TICKS)
		{
			i2c_resync();
			led_fault_on();
			error_inc();
			wait_ticks = 0U;
			usb_print("[Master] resync (timeout)\n");
		}
	}
	else
	{
		wait_ticks = 0U;
	}

	if (++producer_tick >= PRODUCER_PERIOD_TICKS)
	{
		producer_tick = 0U;
		char tmp[MSG_LEN];
		snprintf(tmp, sizeof(tmp), "hello %u", (unsigned)next_seq);
		if (ring_push(tmp))
		{
			next_seq++;
		}
		else
		{
			led_warn();
		}
	}

	/* Copy the slot out before starting TX so the producer can keep writing
	 * fresh slots while HAL still holds the pointer. */
	if (bus_state == BUS_IDLE && !echo_ready && !ring_empty())
	{
		memcpy(sent_msg.data, tx_ring[read_index].data, MSG_LEN);
		sent_msg.data[MSG_LEN - 1U] = '\0';
		bus_state  = BUS_SENDING;
		wait_ticks = 0U;
		tx_byte    = 0U;
		if (HAL_I2C_Master_Seq_Transmit_IT(&hi2c1, SLAVE_ADDRESS,
		                                   &sent_msg.data[0], 1U,
		                                   frame_opt(0U)) == HAL_OK)
		{
			led_activity();
			char line[64];
			snprintf(line, sizeof(line),
			         "[Master] sent  '%s'\n", (const char *)sent_msg.data);
			usb_print(line);
		}
		else
		{
			bus_state = BUS_IDLE;
			error_inc();
		}
	}

	if (echo_ready)
	{
		echo_ready = false;
		echo.data[MSG_LEN - 1U] = '\0';
		char line[64];
		if (echo_matches(sent_msg.data, echo.data))
		{
			snprintf(line, sizeof(line),
			         "[Master] echo  '%s'\n", (const char *)echo.data);
			usb_print(line);
			led_heartbeat();
			led_fault_off();
			error_reset();
		}
		else
		{
			snprintf(line, sizeof(line),
			         "[Master] desync '%s'\n", (const char *)echo.data);
			usb_print(line);
			led_warn();
			error_inc();
		}
	}

	if (error_count >= MAX_CONSEC_ERRORS)
	{
		i2c_resync();
		led_fault_on();
		error_reset();
		usb_print("[Master] I2C resync\n");
	}
}
