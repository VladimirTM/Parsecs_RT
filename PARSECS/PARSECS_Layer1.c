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
#include "log.h"

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
/* HAL ErrorCode of the most recent I2C error, printed with the resync logs. */
static volatile uint32_t last_error = 0U;

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

#ifdef SPI_MASTER

/* No rounds before this tick, so slaves powered up together are listening first. */
#define L1_STARTUP_MS               100U
#define BUS_RECOVER_PULSES          9U

typedef enum {
	BUS_IDLE = 0,
	BUS_SENDING,
	BUS_RECEIVING,
} bus_state_t;

static SLAVE *l1_active_slave = NULL;
static uint16_t active_i2c_address = 0U;
static volatile uint8_t tx_byte_val;
/* tx_byte_val came from the TX ring and is consumed once the slave ACKs it. */
static volatile bool tx_pending = false;
static volatile uint8_t rx_byte_val;
static volatile bus_state_t bus_state = BUS_IDLE;
static uint16_t wait_ticks = 0U;
static volatile bool hold_requested = false;
static volatile bool hold_active = false;
static uint16_t hold_wait = 0U;

static void i2c_quiesce(void)
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

	bus_state = BUS_IDLE;
	l1_active_slave = NULL;
	tx_pending = false;
	wait_ticks = 0U;
}

static void i2c_restore(void)
{
	HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

	MX_I2C1_Init();
	bus_state = BUS_IDLE;
}

/* Open-drain, driven to state. ODR is written before the mode switch so the
 * pin does not glitch the other way when it becomes an output. */
static void bus_pin_output_od(uint16_t pin, GPIO_PinState state)
{
	GPIO_InitTypeDef gpio = {0};

	__HAL_RCC_GPIOB_CLK_ENABLE();
	HAL_GPIO_WritePin(GPIOB, pin, state);
	gpio.Pin = pin;
	gpio.Mode = GPIO_MODE_OUTPUT_OD;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(GPIOB, &gpio);
	HAL_GPIO_WritePin(GPIOB, pin, state);
}

/* Roughly 5 us, so the recovery clock stays well under 100 kHz. */
static void bus_delay(void)
{
	for (volatile uint32_t i = 0U; i < (SystemCoreClock / 1000000U); i++)
	{
	}
}

/* Clocks out a slave stuck in the middle of a byte, then sends a STOP.
 * Also clears the F4 erratum where a glitch leaves the BUSY flag locked. */
static void bus_recover(void)
{
	uint8_t pulse;

	bus_pin_output_od(GPIO_PIN_6, GPIO_PIN_SET);
	bus_pin_output_od(GPIO_PIN_7, GPIO_PIN_SET);
	bus_delay();

	for (pulse = 0U; pulse < BUS_RECOVER_PULSES; pulse++)
	{
		if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET)
		{
			break;
		}
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
		bus_delay();
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
		bus_delay();
	}

	/* SDA rising while SCL is high is the STOP. */
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
	bus_delay();
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
	bus_delay();
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
	bus_delay();
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
	bus_delay();
}

static void i2c_resync(void)
{
	i2c_quiesce();
	bus_recover();
	i2c_restore();
}

static void drive_lines_low(void)
{
	/* SCL first. SDA falling while SCL is high would be a START. */
	bus_pin_output_od(GPIO_PIN_6, GPIO_PIN_RESET);
	bus_pin_output_od(GPIO_PIN_7, GPIO_PIN_RESET);
	hold_active = true;
	hold_wait = 0U;
}

static void start_round(SLAVE *slave)
{
	bool have_tx = !PARSECS_RingEmpty(&slave->l1_buffer_tx);
	if (!have_tx && PARSECS_RingFull(&slave->l1_buffer_rx))
	{
		return;
	}

	tx_byte_val = have_tx ? PARSECS_RingPeekOne(&slave->l1_buffer_tx) : DUMMY_BYTE;
	active_i2c_address = slave->i2c_address;

	bus_state  = BUS_SENDING;
	wait_ticks = 0U;
	tx_pending = have_tx;
	if (HAL_I2C_Master_Seq_Transmit_DMA(&hi2c1, active_i2c_address, (uint8_t *)&tx_byte_val, 1U,
	                                     I2C_FIRST_AND_LAST_FRAME) == HAL_OK)
	{
		led_activity();
	}
	else
	{
		tx_pending = false;
		bus_state = BUS_IDLE;
		error_inc();
	}
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	if (tx_pending && (l1_active_slave != NULL))
	{
		PARSECS_RingAdvanceRead(&l1_active_slave->l1_buffer_tx, 1U);
	}
	tx_pending = false;

	bus_state = BUS_RECEIVING;
	if (HAL_I2C_Master_Seq_Receive_DMA(hi2c, active_i2c_address, (uint8_t *)&rx_byte_val, 1U,
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
	last_error = hi2c->ErrorCode;
	bus_state = BUS_IDLE;
}

void PARSECS_Layer1_Init(void)
{
	bus_state = BUS_IDLE;
	l1_active_slave = NULL;
	active_i2c_address = 0U;
	tx_pending = false;
	wait_ticks = 0U;
	hold_requested = false;
	hold_active = false;
	hold_wait = 0U;
	error_reset();
}

bool PARSECS_Layer1_HoldLinesLow(void)
{
	if (hold_active)
	{
		return true;
	}

	/* Stop new rounds immediately. An in-flight byte is allowed to finish. */
	hold_requested = true;
	if (bus_state != BUS_IDLE)
	{
		if (++hold_wait <= TRANSACTION_TIMEOUT_TICKS)
		{
			return false;
		}
	}

	i2c_quiesce();
	drive_lines_low();
	error_reset();
	return true;
}

void PARSECS_Layer1_ReleaseLines(void)
{
	if (!hold_active)
	{
		return;
	}

	/* SDA first, while SCL is still low, so the rising edge is not a STOP. */
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
	hold_requested = false;
	hold_active = false;
	hold_wait = 0U;
	error_reset();
	led_fault_off();
	i2c_restore();
}

void PARSECS_LAYER1(SLAVE *slave)
{
	if (hold_requested || (HAL_GetTick() < L1_STARTUP_MS))
	{
		return;
	}

	if (bus_state != BUS_IDLE)
	{
		if (++wait_ticks > TRANSACTION_TIMEOUT_TICKS)
		{
			i2c_resync();
			led_fault_on();
			error_inc();
			wait_ticks = 0U;
			LOG_SERIAL("[Master] L1 resync (timeout) err=0x%02lX\n", (unsigned long)last_error);
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
		LOG_SERIAL("[Master] L1 I2C resync err=0x%02lX\n", (unsigned long)last_error);
	}
}

#else /* slave */

typedef enum { PHASE_RECEIVE, PHASE_TRANSMIT } phase_t;

/* Ticks the peripheral may sit with ADDR set or in a BUSY_*_LISTEN state.
 * A real 1-byte transfer takes about 50 us at 400 kHz. */
#define SLAVE_STUCK_TICKS   5U

static volatile bool listening = false;
static volatile bool resync_requested = false;
static uint16_t stuck_ticks = 0U;
static volatile phase_t phase = PHASE_RECEIVE;
static volatile uint8_t rx_byte_val;
static volatile uint8_t tx_byte_val;
/* tx_byte_val came from the TX ring and is consumed once the master NACKs the
 * end of the read, which is after the whole byte left the shift register. */
static volatile bool tx_pending = false;

static void start_listen(void)
{
	phase = PHASE_RECEIVE;
	tx_pending = false;
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
	resync_requested = false;
	tx_pending = false;
	stuck_ticks = 0U;
	error_reset();
	phase = PHASE_RECEIVE;
}

/* After an error in listen mode HAL keeps State at LISTEN, so EnableListen_IT
 * returns HAL_BUSY, while the peripheral can still ACK its address and hold
 * SCL low with nobody clearing ADDR. */
static bool slave_stuck(void)
{
	return (__HAL_I2C_GET_FLAG(&hi2c1, I2C_FLAG_ADDR) == SET) ||
	       (hi2c1.State == HAL_I2C_STATE_BUSY_RX_LISTEN) ||
	       (hi2c1.State == HAL_I2C_STATE_BUSY_TX_LISTEN);
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
			tx_pending = have_tx;
		}
		else
		{
			tx_pending = false;
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
		if (tx_pending)
		{
			PARSECS_RingAdvanceRead(&slaves[0].l1_buffer_tx, 1U);
		}
		tx_pending = false;
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
	last_error = hi2c->ErrorCode;
	listening = false;
	resync_requested = true;
}

void PARSECS_Layer1_Init(void)
{
	listening = false;
	resync_requested = false;
	tx_pending = false;
	stuck_ticks = 0U;
	phase = PHASE_RECEIVE;
	error_reset();
	start_listen();
}

void PARSECS_LAYER1(SLAVE *slave)
{
	(void)slave;

	if (resync_requested)
	{
		i2c_resync();
		LOG_SERIAL("[Slave]  L1 resync err=0x%02lX\n", (unsigned long)last_error);
	}

	if (slave_stuck())
	{
		if (++stuck_ticks > SLAVE_STUCK_TICKS)
		{
			i2c_resync();
			LOG_SERIAL("[Slave]  L1 resync (stuck) err=0x%02lX\n", (unsigned long)last_error);
		}
	}
	else
	{
		stuck_ticks = 0U;
	}

	if (!listening)
	{
		start_listen();
		if (!listening)
		{
			error_inc();
		}
	}

	if (error_count >= MAX_CONSEC_ERRORS)
	{
		i2c_resync();
		LOG_SERIAL("[Slave]  L1 I2C resync err=0x%02lX\n", (unsigned long)last_error);
	}
}

#endif

//! @}

//! @}
