//! \file	PARSECS_Layer1.c
//!
//! \brief	PARSECS Layer 1 (STM32 I2C DMA)
//!
//! Ports the PARSECS Layer 1 ring-buffer byte pump onto I2C DMA.
//! Master drives write-then-read rounds; slave runs in listen mode.
//! Only Layer 1 is node- and platform-dependent.
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup PARSECS-Low-Level-Substack
//! @{

#include "PARSECS_Layer1.h"
#include "PARSECS_Port.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "i2c.h"
#include "usbd_cdc_if.h"

#define SLAVE_ADDRESS               (0x08 << 1)
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

static volatile uint16_t error_count = 0U;

static void error_inc(void)
{
	uint32_t key = PARSECS_IrqLock();
	error_count++;
	PARSECS_IrqUnlock(key);
}

static void error_reset(void)
{
	uint32_t key = PARSECS_IrqLock();
	error_count = 0U;
	PARSECS_IrqUnlock(key);
}

static void usb_print(const char *msg)
{
	if (CDC_Transmit_FS((uint8_t *)msg, (uint16_t)strlen(msg)) == USBD_BUSY)
	{
		led_warn();
	}
}

#ifdef SPI_MASTER

typedef enum {
	BUS_IDLE = 0,
	BUS_SENDING,
	BUS_RECEIVING,
} bus_state_t;

static SLAVE *l1_active_slave = NULL;
static volatile uint8_t tx_byte_val;
static volatile uint8_t rx_byte_val;
static volatile bus_state_t bus_state = BUS_IDLE;
static uint16_t wait_ticks = 0U;

static void i2c_resync(void)
{
	HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
	HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
	HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_DisableIRQ(DMA1_Stream6_IRQn);

	if (hi2c1.hdmarx != NULL) HAL_DMA_Abort(hi2c1.hdmarx);
	if (hi2c1.hdmatx != NULL) HAL_DMA_Abort(hi2c1.hdmatx);

	HAL_I2C_DeInit(&hi2c1);
	HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
	HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream6_IRQn);

	HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

	MX_I2C1_Init();
	bus_state = BUS_IDLE;
}

static void start_round(SLAVE *slave)
{
	bool have_tx = !PARSECS_RingEmpty(&slave->l1_buffer_tx);
	if (!have_tx && PARSECS_RingFull(&slave->l1_buffer_rx))
	{
		return;
	}

	tx_byte_val = have_tx ? PARSECS_RingPeekOne(&slave->l1_buffer_tx) : DUMMY_BYTE;

	bus_state  = BUS_SENDING;
	wait_ticks = 0U;
	if (HAL_I2C_Master_Seq_Transmit_DMA(&hi2c1, SLAVE_ADDRESS, (uint8_t *)&tx_byte_val, 1U,
	                                     I2C_FIRST_AND_LAST_FRAME) == HAL_OK)
	{
		if (have_tx)
		{
			PARSECS_RingAdvanceRead(&slave->l1_buffer_tx, 1U);
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

	if ((l1_active_slave != NULL) && !PARSECS_RingFull(&l1_active_slave->l1_buffer_rx))
	{
		PARSECS_RingWriteOne(&l1_active_slave->l1_buffer_rx, rx_byte_val);
	}
	else
	{
		led_warn();
	}
	bus_state = BUS_IDLE;
	led_heartbeat();
	led_fault_off();
	error_reset();
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	error_inc();
	bus_state = BUS_IDLE;
}

void PARSECS_Layer1_Init(void)
{
	bus_state = BUS_IDLE;
	l1_active_slave = NULL;
	wait_ticks = 0U;
	error_reset();
}

void PARSECS_LAYER1(SLAVE *slave)
{
	if (bus_state != BUS_IDLE)
	{
		if (++wait_ticks > TRANSACTION_TIMEOUT_TICKS)
		{
			i2c_resync();
			led_fault_on();
			error_inc();
			wait_ticks = 0U;
			usb_print("[Master] L1 resync (timeout)\n");
		}
	}
	else
	{
		wait_ticks = 0U;
	}

	if (bus_state == BUS_IDLE)
	{
		l1_active_slave = slave;
		start_round(slave);
	}

	if (error_count >= MAX_CONSEC_ERRORS)
	{
		i2c_resync();
		led_fault_on();
		error_reset();
		usb_print("[Master] L1 I2C resync\n");
	}
}

#else /* slave */

typedef enum { PHASE_RECEIVE, PHASE_TRANSMIT } phase_t;

static volatile bool listening = false;
static volatile phase_t phase = PHASE_RECEIVE;
static volatile uint8_t rx_byte_val;
static volatile uint8_t tx_byte_val;

static void start_listen(void)
{
	phase = PHASE_RECEIVE;
	listening = (HAL_I2C_EnableListen_IT(&hi2c1) == HAL_OK);
}

static void i2c_resync(void)
{
	HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
	HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
	HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_DisableIRQ(DMA1_Stream6_IRQn);

	if (hi2c1.hdmarx != NULL) HAL_DMA_Abort(hi2c1.hdmarx);
	if (hi2c1.hdmatx != NULL) HAL_DMA_Abort(hi2c1.hdmatx);

	HAL_I2C_DeInit(&hi2c1);
	HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
	HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream6_IRQn);
	led_fault_on();

	HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

	MX_I2C1_Init();
	listening = false;
	error_reset();
	phase = PHASE_RECEIVE;
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection,
                          uint16_t AddrMatchCode)
{
	SLAVE *slave = &slaves[0];
	if (hi2c->Instance != I2C1) return;
	(void)AddrMatchCode;

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
		bool have_tx = !PARSECS_RingEmpty(&slave->l1_buffer_tx);
		tx_byte_val = have_tx ? PARSECS_RingPeekOne(&slave->l1_buffer_tx) : DUMMY_BYTE;
		if (HAL_I2C_Slave_Seq_Transmit_DMA(hi2c, (uint8_t *)&tx_byte_val, 1U,
		                                    I2C_FIRST_AND_LAST_FRAME) == HAL_OK)
		{
			if (have_tx)
			{
				PARSECS_RingAdvanceRead(&slave->l1_buffer_tx, 1U);
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
	SLAVE *slave = &slaves[0];
	if (hi2c->Instance != I2C1) return;

	if (!PARSECS_RingFull(&slave->l1_buffer_rx))
	{
		PARSECS_RingWriteOne(&slave->l1_buffer_rx, rx_byte_val);
	}
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	if (phase == PHASE_TRANSMIT)
	{
		led_activity();
		led_heartbeat();
		led_fault_off();
		error_reset();
	}

	listening = (HAL_I2C_EnableListen_IT(hi2c) == HAL_OK);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	error_inc();
	listening = false;
}

void PARSECS_Layer1_Init(void)
{
	listening = false;
	phase = PHASE_RECEIVE;
	error_reset();
	start_listen();
}

void PARSECS_LAYER1(SLAVE *slave)
{
	(void)slave;

	if (!listening)
	{
		start_listen();
	}

	if (error_count >= MAX_CONSEC_ERRORS)
	{
		i2c_resync();
		usb_print("[Slave]  L1 I2C resync\n");
	}
}

#endif

//! @}

//! @}
