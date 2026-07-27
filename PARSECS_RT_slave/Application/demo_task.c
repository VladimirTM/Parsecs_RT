/*
 * Slave side of the I2C demo (PARSECS_LAYER1 ring buffer over I2C DMA).
 * On a write from the master, the received byte lands in rx_ring; on a
 * read, one byte is popped from tx_ring (or 0x00 if empty) and shifted
 * out. Runs in interrupt listen mode; each completed round just sets a
 * ready flag for the task to verify and log over USB, mirroring the
 * master's byte_ready pattern. Persistent bus errors trigger a full
 * re-init.
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

#define L1_RING_SIZE        16U             /* matches RAW_RING_BUFFER_SIZE upstream */
#define DUMMY_BYTE          0x00U
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

typedef enum { PHASE_RECEIVE, PHASE_TRANSMIT } phase_t;

/* Both rings are only touched from ISR context (AddrCallback /
 * SlaveRxCpltCallback) once seeded, so ringbuf.c's lack of locking is fine. */
static tRingBufObject       tx_ring;
static tRingBufObject       rx_ring;
static uint8_t              tx_ring_mem[L1_RING_SIZE];
static uint8_t              rx_ring_mem[L1_RING_SIZE];

static volatile uint8_t     rx_byte_val;
static volatile uint8_t     tx_byte_val;

static volatile bool        round_ready = false;   /* ISR sets; task clears */

static volatile uint16_t    error_count = 0U;
static volatile bool        listening   = false;
static volatile phase_t     phase       = PHASE_RECEIVE;

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

/* ringbuf.h has no peek, so read at ulReadIndex directly without advancing -
 * lets a failed HAL call leave the byte in tx_ring to retry next time. */
static uint8_t ring_peek_one(const tRingBufObject *ring)
{
	return ring->pucBuf[ring->ulReadIndex];
}

static void start_listen(void)
{
	phase = PHASE_RECEIVE;
	listening = (HAL_I2C_EnableListen_IT(&hi2c1) == HAL_OK);
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
	led_fault_on();             /* latch the fault while IRQs are still off */

	/* MX_I2C1_Init() re-enables the I2C1 pair itself; DMA1 needs it here. */
	HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

	MX_I2C1_Init();
	listening = false;
	error_reset();
	phase = PHASE_RECEIVE;
}

/* Mirrors the master's byte_ready flag - defers the LED/error-reset work
 * to the task instead of doing it here in ISR context. */
static void round_complete(void)
{
	round_ready = true;
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection,
                          uint16_t AddrMatchCode)
{
	if (hi2c->Instance != I2C1) return;
	(void)AddrMatchCode;

	/* TRANSMIT = master writing to us (we receive); RECEIVE = master
	 * reading from us (we transmit). One byte per direction. */
	if (TransferDirection == I2C_DIRECTION_TRANSMIT)
	{
		phase = PHASE_RECEIVE;
		if (HAL_I2C_Slave_Seq_Receive_DMA(hi2c, (uint8_t *)&rx_byte_val, 1U,
		                                   I2C_FIRST_AND_LAST_FRAME) != HAL_OK)
		{
			error_inc();
			listening = false;
		}
	}
	else
	{
		phase = PHASE_TRANSMIT;
		/* Peek, don't pop - only consumed once HAL actually accepts it. */
		bool have_tx = !RingBufEmpty(&tx_ring);
		tx_byte_val = have_tx ? ring_peek_one(&tx_ring) : DUMMY_BYTE;
		if (HAL_I2C_Slave_Seq_Transmit_DMA(hi2c, (uint8_t *)&tx_byte_val, 1U,
		                                    I2C_FIRST_AND_LAST_FRAME) == HAL_OK)
		{
			if (have_tx)
			{
				RingBufAdvanceRead(&tx_ring, 1U);
			}
		}
		else
		{
			error_inc();
			listening = false;
		}
	}
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	if (!RingBufFull(&rx_ring))
	{
		RingBufWriteOne(&rx_ring, rx_byte_val);
	}
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;
	/* Nothing to do here; ListenCpltCallback closes the round. */
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	/* Round is only done once the transmit phase's STOP/NACK lands. */
	if (phase == PHASE_TRANSMIT)
	{
		round_complete();
	}

	listening = (HAL_I2C_EnableListen_IT(hi2c) == HAL_OK);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	/* The expected NACK is handled via ListenCplt; anything here is a real
	 * error. Drop listen - the task re-arms it. */
	error_inc();
	listening = false;
}

void DemoTask_Init(void)
{
	RingBufInit(&tx_ring, tx_ring_mem, L1_RING_SIZE);
	RingBufInit(&rx_ring, rx_ring_mem, L1_RING_SIZE);

	/* Test payload, ready to answer the master's first read. */
	static const uint8_t seed[] = { 'A', 'B', 'C', 'D' };
	RingBufWrite(&tx_ring, (uint8_t *)seed, sizeof(seed));
}

void MyDemoTask(void)
{
	/* Expected master payload, just for the PASS/FAIL log below. */
	static const uint8_t expected_from_master[] = { 'a', 'b', 'c', 'd' };
	static uint8_t        verify_index = 0U;

	if (!listening)
	{
		start_listen();
	}

	if (round_ready)
	{
		round_ready = false;

		if (verify_index < sizeof(expected_from_master))
		{
			bool pass = (rx_byte_val == expected_from_master[verify_index]);
			char line[64];
			snprintf(line, sizeof(line), "[Slave]  rx='%c' tx='%c' %s\n",
			         (char)rx_byte_val, (char)tx_byte_val, pass ? "PASS" : "FAIL");
			usb_print(line);
			verify_index++;
			if (verify_index == sizeof(expected_from_master))
			{
				usb_print("[Slave]  ring buffer pass-through test complete\n");
			}
		}
		led_activity();
		led_heartbeat();
		led_fault_off();
		error_reset();
	}

	if (error_count >= MAX_CONSEC_ERRORS)
	{
		i2c_resync();
		usb_print("[Slave]  I2C resync\n");
	}
}
