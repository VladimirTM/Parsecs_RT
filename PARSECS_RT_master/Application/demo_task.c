/*
 * Master side of the I2C demo (PARSECS_LAYER1 ring buffer over I2C DMA).
 * Each round: pop a byte from tx_ring (or send 0x00 if empty), write it via
 * DMA, then read one byte back from the slave's tx_ring into rx_ring.
 * Fully interrupt/DMA-driven - the FreeRTOS task never blocks inside HAL.
 * A watchdog and error threshold re-init I2C/DMA if the bus hangs.
 */

#include "demo_task.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ringbuf.h>
#include "main.h"
#include "i2c.h"
#include "usbd_cdc_if.h"

#define SLAVE_ADDRESS               (0x08 << 1)
#define L1_RING_SIZE                16U          /* matches RAW_RING_BUFFER_SIZE upstream */
#define DUMMY_BYTE                  0x00U
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

typedef enum {
	BUS_IDLE = 0,
	BUS_SENDING,
	BUS_RECEIVING,
} bus_state_t;

/* tx_ring is only popped from task context (start_round); rx_ring is only
 * pushed from ISR context (HAL_I2C_MasterRxCpltCallback). ringbuf.c has no
 * locking of its own, so that split is what keeps access safe. */
static tRingBufObject       tx_ring;
static tRingBufObject       rx_ring;
static uint8_t              tx_ring_mem[L1_RING_SIZE];
static uint8_t              rx_ring_mem[L1_RING_SIZE];

static volatile uint8_t     tx_byte_val;
static volatile uint8_t     rx_byte_val;

static volatile bus_state_t bus_state   = BUS_IDLE;
static volatile bool        byte_ready  = false;
static volatile uint16_t    error_count = 0U;
static uint16_t             wait_ticks  = 0U;

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

/* ringbuf.c doesn't disable interrupts internally, so rx_ring needs its own
 * critical section since it's shared between start_round() and the ISR. */
static bool rx_ring_full(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	bool full = RingBufFull(&rx_ring);
	__set_PRIMASK(primask);
	return full;
}

static bool rx_ring_push(uint8_t data)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	bool ok = !RingBufFull(&rx_ring);
	if (ok)
	{
		RingBufWriteOne(&rx_ring, data);
	}
	__set_PRIMASK(primask);
	return ok;
}

static void usb_print(const char *msg)
{
	if (CDC_Transmit_FS((uint8_t *)msg, (uint16_t)strlen(msg)) == USBD_BUSY)
	{
		led_warn();
	}
}

/* ringbuf.h has no peek, so read at ulReadIndex directly without advancing -
 * lets a failed HAL call leave the byte in tx_ring for the next round. */
static uint8_t ring_peek_one(const tRingBufObject *ring)
{
	return ring->pucBuf[ring->ulReadIndex];
}

static void i2c_resync(void)
{
	/* Mask I2C + DMA IRQs so a late callback can't race the teardown. */
	HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
	HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
	HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_DisableIRQ(DMA1_Stream6_IRQn);

	/* Abort first, or HAL_DMA_DeInit() below just no-ops on a busy stream. */
	if (hi2c1.hdmarx != NULL) HAL_DMA_Abort(hi2c1.hdmarx);
	if (hi2c1.hdmatx != NULL) HAL_DMA_Abort(hi2c1.hdmatx);

	HAL_I2C_DeInit(&hi2c1);
	HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
	HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream6_IRQn);

	/* MX_I2C1_Init() re-enables the I2C1 pair itself; DMA1 needs it here. */
	HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

	MX_I2C1_Init();
	bus_state  = BUS_IDLE;
	byte_ready = false;
}

/* Send whatever's queued, or a dummy byte to keep polling the slave - the
 * master has to drive the bus even with nothing to say. */
static void start_round(void)
{
	bool have_tx = !RingBufEmpty(&tx_ring);
	if (!have_tx && rx_ring_full())
	{
		return;
	}
	/* Peek, don't pop - only consumed once HAL actually accepts it below. */
	tx_byte_val = have_tx ? ring_peek_one(&tx_ring) : DUMMY_BYTE;

	bus_state  = BUS_SENDING;
	wait_ticks = 0U;
	if (HAL_I2C_Master_Seq_Transmit_DMA(&hi2c1, SLAVE_ADDRESS, (uint8_t *)&tx_byte_val, 1U,
	                                     I2C_FIRST_AND_LAST_FRAME) == HAL_OK)
	{
		if (have_tx)
		{
			RingBufAdvanceRead(&tx_ring, 1U);
		}
		led_activity();
	}
	else
	{
		bus_state = BUS_IDLE;
		error_inc();
	}
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	/* Byte sent - turn the bus around and read one byte back. */
	bus_state = BUS_RECEIVING;
	if (HAL_I2C_Master_Seq_Receive_DMA(hi2c, SLAVE_ADDRESS, (uint8_t *)&rx_byte_val, 1U,
	                                    I2C_FIRST_AND_LAST_FRAME) != HAL_OK)
	{
		bus_state = BUS_IDLE;
		error_inc();
	}
}

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	if (!rx_ring_push(rx_byte_val))
	{
		led_warn();
	}
	byte_ready = true;
	bus_state  = BUS_IDLE;
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	/* Best-effort byte pump: a failed byte isn't retried, just counted. */
	error_inc();
	bus_state = BUS_IDLE;
}

void DemoTask_Init(void)
{
	RingBufInit(&tx_ring, tx_ring_mem, L1_RING_SIZE);
	RingBufInit(&rx_ring, rx_ring_mem, L1_RING_SIZE);

	/* Test payload the round loop below drains across the link. */
	static const uint8_t seed[] = { 'a', 'b', 'c', 'd' };
	RingBufWrite(&tx_ring, (uint8_t *)seed, sizeof(seed));
}

void MyDemoTask(void)
{
	/* Expected slave payload, just for the PASS/FAIL log below. */
	static const uint8_t expected_from_slave[] = { 'A', 'B', 'C', 'D' };
	static uint8_t        verify_index = 0U;

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

	if (bus_state == BUS_IDLE && !byte_ready)
	{
		start_round();
	}

	if (byte_ready)
	{
		byte_ready = false;

		if (verify_index < sizeof(expected_from_slave))
		{
			bool pass = ((uint8_t)rx_byte_val == expected_from_slave[verify_index]);
			char line[64];
			snprintf(line, sizeof(line), "[Master] tx='%c' rx='%c' %s\n",
			         (char)tx_byte_val, (char)rx_byte_val, pass ? "PASS" : "FAIL");
			usb_print(line);
			verify_index++;
			if (verify_index == sizeof(expected_from_slave))
			{
				usb_print("[Master] ring buffer pass-through test complete\n");
			}
		}
		led_heartbeat();
		led_fault_off();
		error_reset();
	}

	if (error_count >= MAX_CONSEC_ERRORS)
	{
		i2c_resync();
		led_fault_on();
		error_reset();
		usb_print("[Master] I2C resync\n");
	}
}
