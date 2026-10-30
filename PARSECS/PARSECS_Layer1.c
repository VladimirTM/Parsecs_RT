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
/* A write plus repeated-START read takes about 60 us, so 5 ticks (~5 ms) is a
 * wide margin and keeps a hung round from adding 50 ms to the round trip. */
#define TRANSACTION_TIMEOUT_TICKS   5U

/* Test aid, off by default: define PARSECS_L1_FAULT_INJECT_EVERY (e.g. -D...=97)
 * to flip every Nth non-dummy byte this node sends, so the peer sees a CRC
 * error, answers NACK, and the ARQ retransmission shows up on the bus. */
#ifdef PARSECS_L1_FAULT_INJECT_EVERY
static uint32_t fault_inject_count = 0U;

static uint8_t fault_inject(uint8_t byte)
{
	if (++fault_inject_count >= PARSECS_L1_FAULT_INJECT_EVERY)
	{
		fault_inject_count = 0U;
		return (uint8_t)(byte ^ 0xFFU);
	}
	return byte;
}
#else
static inline uint8_t fault_inject(uint8_t byte) { return byte; }
#endif

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

/* Resets every I2C1 register from RCC. Part of the STM32F40x erratum
 * workaround for a locked BUSY flag (ES0182), on top of the SWRST that
 * HAL_I2C_Init already does. Call between HAL_I2C_DeInit and MX_I2C1_Init. */
static void i2c_peripheral_reset(void)
{
	__HAL_RCC_I2C1_FORCE_RESET();
	__HAL_RCC_I2C1_RELEASE_RESET();
}

#ifdef SPI_MASTER

/* No rounds before this tick, so slaves powered up together are listening first. */
#define L1_STARTUP_MS               100U
#define BUS_RECOVER_PULSES          9U
/* Resync when this many of the last 32 rounds failed. Failures that alternate
 * with good rounds never reach MAX_CONSEC_ERRORS, and a master left in a bad
 * state can produce them for seconds. About 5% of rounds fail normally. */
#define ROUND_WINDOW_MAX_FAILS      8U
/* Minimum time between two window resyncs. A slave that is not connected fails
 * every one of its rounds, and a resync cannot fix that. */
#define ROUND_WINDOW_HOLDOFF_MS     250U

typedef enum {
	BUS_IDLE = 0,
	BUS_SENDING,
	BUS_RECEIVING,
} bus_state_t;

static SLAVE *l1_active_slave = NULL;
static uint16_t active_i2c_address = 0U;
static volatile uint8_t tx_byte_val;
static volatile uint8_t rx_byte_val;
static volatile bus_state_t bus_state = BUS_IDLE;
static uint16_t wait_ticks = 0U;
static volatile bool hold_requested = false;
static volatile bool hold_active = false;
static uint16_t hold_wait = 0U;
/* Bit 0 is the latest round, 1 = failed. */
static volatile uint32_t round_history = 0U;
/* tx_byte_val came from the TX ring and leaves it once the slave ACKs it. */
static volatile bool tx_consume = false;
static uint32_t window_resync_tick = 0U;

static void round_done(bool failed)
{
	uint32_t key = PARSECS_IrqLock();
	round_history = (round_history << 1) | (failed ? 1U : 0U);
	PARSECS_IrqUnlock(key);
}

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
	wait_ticks = 0U;
	tx_consume = false;
	round_history = 0U;
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
	i2c_peripheral_reset();
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

	/* For a 1-byte DMA read HAL sets STOP twice: in I2C_Master_ADDR and again in
	 * I2C_DMAXferCplt. If the first STOP is already on the bus, the second stays
	 * pending and fires right after the next START, which leaves the master
	 * without its SB event until the round times out. With BUSY clear nothing
	 * is left to stop, so the bit can only be that stray request. */
	if (((hi2c1.Instance->CR1 & I2C_CR1_STOP) != 0U) &&
	    ((hi2c1.Instance->SR2 & I2C_SR2_BUSY) == 0U))
	{
		CLEAR_BIT(hi2c1.Instance->CR1, I2C_CR1_STOP);
	}

	tx_byte_val = have_tx ? fault_inject(PARSECS_RingPeekOne(&slave->l1_buffer_tx)) : DUMMY_BYTE;
	tx_consume = have_tx;
	active_i2c_address = slave->i2c_address;

	/* Counted even if the round fails, so an absent slave still runs down
	 * its ARQ timeout and ends up LINK_DOWN. */
	slave->l1_rounds++;
	bus_state  = BUS_SENDING;
	wait_ticks = 0U;
	/* FIRST_FRAME: no STOP after the write, the read follows with a repeated
	 * START. A STOP and a new START about 2 us later is where the master and
	 * the slaves lost frame alignment. */
	if (HAL_I2C_Master_Seq_Transmit_DMA(&hi2c1, active_i2c_address, (uint8_t *)&tx_byte_val, 1U,
	                                     I2C_FIRST_FRAME) == HAL_OK)
	{
		led_activity();
	}
	else
	{
		bus_state = BUS_IDLE;
		tx_consume = false;
		error_inc();
		round_done(true);
	}
}

/* The slave ACKed the byte, so it leaves the TX ring. On a NACK or bus error
 * it stays and is sent again next round. */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	if (tx_consume && (l1_active_slave != NULL))
	{
		PARSECS_RingAdvanceRead(&l1_active_slave->l1_buffer_tx, 1U);
	}
	tx_consume = false;

	bus_state = BUS_RECEIVING;
	if (HAL_I2C_Master_Seq_Receive_DMA(hi2c, active_i2c_address, (uint8_t *)&rx_byte_val, 1U,
	                                    I2C_LAST_FRAME) != HAL_OK)
	{
		/* No STOP was sent after the write, so release the bus here. */
		SET_BIT(hi2c->Instance->CR1, I2C_CR1_STOP);
		bus_state = BUS_IDLE;
		error_inc();
		round_done(true);
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
	round_done(false);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	error_inc();
	round_done(true);
	last_error = hi2c->ErrorCode;
	tx_consume = false;
	bus_state = BUS_IDLE;
}

void PARSECS_Layer1_Init(void)
{
	bus_state = BUS_IDLE;
	l1_active_slave = NULL;
	active_i2c_address = 0U;
	wait_ticks = 0U;
	hold_requested = false;
	hold_active = false;
	hold_wait = 0U;
	round_history = 0U;
	tx_consume = false;
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
	else if (((uint32_t)__builtin_popcount(round_history) >= ROUND_WINDOW_MAX_FAILS) &&
	         ((HAL_GetTick() - window_resync_tick) >= ROUND_WINDOW_HOLDOFF_MS))
	{
		window_resync_tick = HAL_GetTick();
		i2c_resync();
		led_fault_on();
		error_reset();
		LOG_SERIAL("[Master] L1 resync (window) err=0x%02lX\n", (unsigned long)last_error);
	}
}

#else /* slave */

typedef enum { PHASE_RECEIVE, PHASE_TRANSMIT } phase_t;

/* Ticks slave_stuck() may stay true before a resync.
 * A real 1-byte transfer takes about 50 us at 400 kHz. */
#define SLAVE_STUCK_TICKS   5U
#define SLAVE_SCL_PIN       GPIO_PIN_6
#define SLAVE_SDA_PIN       GPIO_PIN_7

static volatile bool listening = false;
static volatile bool resync_requested = false;
static uint16_t stuck_ticks = 0U;
static volatile phase_t phase = PHASE_RECEIVE;
static volatile uint8_t rx_byte_val;
static volatile uint8_t tx_byte_val;
/* tx_byte_val came from the TX ring and leaves it once the master has clocked
 * it out and NACKed it, which ends the read. On a bus error it stays. */
static volatile bool tx_pending = false;

#ifdef PARSECS_SLAVE_I2C_DIAG
/* Diagnostics for the episodes where the slave ACKs the write but NACKs its
 * read address after the repeated START. Each event below bumps a counter
 * (printed by PARSECS_Layer1_DiagReport) and puts n short pulses on PE7, so it
 * can be lined up with SCL/SDA on the logic analyser. PE7 is also held high
 * for the whole of i2c_resync(). */
#define DIAG_PORT   GPIOE
#define DIAG_PIN    GPIO_PIN_7

typedef enum
{
	DIAG_ERROR_CB    = 1U,	/* HAL_I2C_ErrorCallback */
	DIAG_LISTEN_FAIL = 2U,	/* HAL_I2C_EnableListen_IT did not return HAL_OK */
	DIAG_RX_ARM_FAIL = 3U,	/* Seq_Receive_DMA refused in AddrCallback (master write) */
	DIAG_TX_ARM_FAIL = 4U,	/* Seq_Transmit_DMA refused in AddrCallback (master read) */
	DIAG_RX_LOST     = 5U,	/* write addressed, but SlaveRxCpltCallback never came */
	DIAG_ACK_OFF     = 6U,	/* CR1.ACK or CR1.PE is 0 while the slave should be listening */
} diag_event_t;

typedef struct
{
	uint32_t err_cb;
	uint32_t err_berr;
	uint32_t err_arlo;
	uint32_t err_af;
	uint32_t err_ovr;
	uint32_t err_dma;
	uint32_t err_other;
	uint32_t listen_fail;
	uint32_t rx_arm_fail;
	uint32_t tx_arm_fail;
	uint32_t rx_lost;
	uint32_t ack_off_rx;	/* seen in SlaveRxCpltCallback, just before the repeated START */
	uint32_t ack_off_tick;	/* seen by the Layer 1 tick */
	uint32_t resync_error;
	uint32_t resync_stuck;
	uint32_t resync_errors;
	uint32_t stuck_flag;	/* ADDR or BTF left set */
	uint32_t stuck_state;	/* HAL handle left in BUSY_RX_LISTEN / BUSY_TX_LISTEN */
	uint32_t stuck_scl;		/* SCL held low with SDA high */
	uint32_t stuck_busy;	/* BUSY set with both lines high */
	uint32_t last_state;	/* hi2c1.State at the last error / failed call */
} slave_diag_t;

typedef enum { STUCK_NONE, STUCK_FLAG, STUCK_STATE, STUCK_SCL, STUCK_BUSY } stuck_why_t;

static volatile slave_diag_t diag;
static slave_diag_t diag_reported;
static volatile bool rx_wait = false;	/* write addressed, byte not delivered yet */
static bool ack_off_seen = false;
static stuck_why_t stuck_why = STUCK_NONE;

/* ~150 ns high, ~150 ns low at 168 MHz: short enough not to change the race
 * being measured, long enough for a 100 MS/s capture. */
static inline void diag_wait(void)
{
	for (volatile uint8_t d = 0U; d < 4U; d++)
	{
	}
}

static void diag_pulse(diag_event_t n)
{
	uint32_t i;
	for (i = 0U; i < (uint32_t)n; i++)
	{
		DIAG_PORT->BSRR = DIAG_PIN;
		diag_wait();
		DIAG_PORT->BSRR = (uint32_t)DIAG_PIN << 16U;
		diag_wait();
	}
}

static inline void diag_level(bool high)
{
	DIAG_PORT->BSRR = high ? DIAG_PIN : ((uint32_t)DIAG_PIN << 16U);
}

static void diag_init(void)
{
	GPIO_InitTypeDef gpio = {0};
	__HAL_RCC_GPIOE_CLK_ENABLE();
	gpio.Pin = DIAG_PIN;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
	HAL_GPIO_Init(DIAG_PORT, &gpio);
	diag_level(false);
}

static inline bool diag_ack_on(void)
{
	return (hi2c1.Instance->CR1 & (I2C_CR1_ACK | I2C_CR1_PE)) == (I2C_CR1_ACK | I2C_CR1_PE);
}

static void diag_error(uint32_t code)
{
	diag.err_cb++;
	if ((code & HAL_I2C_ERROR_BERR) != 0U) diag.err_berr++;
	if ((code & HAL_I2C_ERROR_ARLO) != 0U) diag.err_arlo++;
	if ((code & HAL_I2C_ERROR_AF) != 0U)   diag.err_af++;
	if ((code & HAL_I2C_ERROR_OVR) != 0U)  diag.err_ovr++;
	if ((code & HAL_I2C_ERROR_DMA) != 0U)  diag.err_dma++;
	if ((code & ~(HAL_I2C_ERROR_BERR | HAL_I2C_ERROR_ARLO | HAL_I2C_ERROR_AF |
	              HAL_I2C_ERROR_OVR | HAL_I2C_ERROR_DMA)) != 0U) diag.err_other++;
	diag.last_state = (uint32_t)hi2c1.State;
	diag_pulse(DIAG_ERROR_CB);
}

static void diag_listen_result(bool ok)
{
	if (!ok)
	{
		diag.listen_fail++;
		diag.last_state = (uint32_t)hi2c1.State;
		diag_pulse(DIAG_LISTEN_FAIL);
	}
}

/* A new address match while the previous write byte was never delivered. */
static void diag_check_rx_lost(void)
{
	if (rx_wait)
	{
		rx_wait = false;
		diag.rx_lost++;
		diag_pulse(DIAG_RX_LOST);
	}
}
#else
static inline void diag_init(void) {}
static inline void diag_level(bool high) { (void)high; }
static inline void diag_error(uint32_t code) { (void)code; }
static inline void diag_listen_result(bool ok) { (void)ok; }
static inline void diag_check_rx_lost(void) {}
#endif

static void start_listen(void)
{
	phase = PHASE_RECEIVE;
	listening = (HAL_I2C_EnableListen_IT(&hi2c1) == HAL_OK);
	diag_listen_result(listening);
}

/* The registers and pin levels are captured before the reset so the log shows
 * the state the slave was stuck in. */
static void i2c_resync(const char *reason)
{
	uint32_t sr1;
	uint32_t sr2;
	uint32_t state;
	uint32_t err;
	unsigned int scl;
	unsigned int sda;

	HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
	HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
	HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_DisableIRQ(DMA1_Stream6_IRQn);
	diag_level(true);
#ifdef PARSECS_SLAVE_I2C_DIAG
	if (rx_wait)
	{
		rx_wait = false;
		diag.rx_lost++;
	}
#endif

	sr1 = hi2c1.Instance->SR1;
	sr2 = hi2c1.Instance->SR2;
	state = (uint32_t)hi2c1.State;
	err = last_error;
	scl = (HAL_GPIO_ReadPin(GPIOB, SLAVE_SCL_PIN) == GPIO_PIN_SET) ? 1U : 0U;
	sda = (HAL_GPIO_ReadPin(GPIOB, SLAVE_SDA_PIN) == GPIO_PIN_SET) ? 1U : 0U;

	if (hi2c1.hdmarx != NULL) HAL_DMA_Abort(hi2c1.hdmarx);
	if (hi2c1.hdmatx != NULL) HAL_DMA_Abort(hi2c1.hdmatx);

	HAL_I2C_DeInit(&hi2c1);
	i2c_peripheral_reset();
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
	diag_level(false);

	LOG_SERIAL("[Slave]  L1 resync (%s) err=0x%02lX sr1=0x%04lX sr2=0x%04lX st=0x%02lX scl=%u sda=%u\n",
	           reason, (unsigned long)err, (unsigned long)sr1, (unsigned long)sr2,
	           (unsigned long)state, scl, sda);
}

/* Any one of these held for SLAVE_STUCK_TICKS means the slave will not recover
 * on its own:
 * - ADDR set, or HAL in BUSY_*_LISTEN: after an error in listen mode HAL keeps
 *   State at LISTEN, so EnableListen_IT returns HAL_BUSY while the peripheral
 *   still ACKs its address and holds SCL low with nobody clearing ADDR.
 * - BTF set: the master ACKed the last byte instead of NACKing it, so the slave
 *   stretches SCL waiting for a DR write that the finished DMA never makes.
 * - SCL low with SDA high: any other stretch that does not end. Both lines low
 *   is the master holding the bus on purpose, so it does not count.
 * - BUSY with both lines high: STM32F40x erratum (ES0182), a glitch on the
 *   analog filter locks BUSY although the bus is free.
 * SR2 is only read when SR1 showed ADDR clear: reading SR1 with ADDR set and
 * then SR2 clears ADDR in hardware before the ISR has handled it. */
static bool slave_stuck(void)
{
	uint32_t sr1 = hi2c1.Instance->SR1;
	bool scl;
	bool sda;

	if ((sr1 & (I2C_SR1_ADDR | I2C_SR1_BTF)) != 0U)
	{
#ifdef PARSECS_SLAVE_I2C_DIAG
		stuck_why = STUCK_FLAG;
#endif
		return true;
	}
	if ((hi2c1.State == HAL_I2C_STATE_BUSY_RX_LISTEN) ||
	    (hi2c1.State == HAL_I2C_STATE_BUSY_TX_LISTEN))
	{
#ifdef PARSECS_SLAVE_I2C_DIAG
		stuck_why = STUCK_STATE;
#endif
		return true;
	}

	scl = (HAL_GPIO_ReadPin(GPIOB, SLAVE_SCL_PIN) == GPIO_PIN_SET);
	sda = (HAL_GPIO_ReadPin(GPIOB, SLAVE_SDA_PIN) == GPIO_PIN_SET);
	if (!scl && sda)
	{
#ifdef PARSECS_SLAVE_I2C_DIAG
		stuck_why = STUCK_SCL;
#endif
		return true;
	}
#ifdef PARSECS_SLAVE_I2C_DIAG
	stuck_why = STUCK_BUSY;
#endif
	return scl && sda && ((hi2c1.Instance->SR2 & I2C_SR2_BUSY) != 0U);
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
		diag_check_rx_lost();
		if (HAL_I2C_Slave_Seq_Receive_DMA(hi2c, (uint8_t *)&rx_byte_val, 1U,
		                                   I2C_FIRST_AND_LAST_FRAME) != HAL_OK)
		{
			error_inc();
			listening = false;
#ifdef PARSECS_SLAVE_I2C_DIAG
			diag.rx_arm_fail++;
			diag.last_state = (uint32_t)hi2c->State;
			diag_pulse(DIAG_RX_ARM_FAIL);
#endif
		}
#ifdef PARSECS_SLAVE_I2C_DIAG
		else
		{
			rx_wait = true;
		}
#endif
	}
	else
	{
		phase = PHASE_TRANSMIT;
		diag_check_rx_lost();
		bool have_tx = !PARSECS_RingEmpty(&slave->l1_buffer_tx);
		tx_byte_val = have_tx ? fault_inject(PARSECS_RingPeekOne(&slave->l1_buffer_tx)) : DUMMY_BYTE;
		tx_pending = have_tx;
		if (HAL_I2C_Slave_Seq_Transmit_DMA(hi2c, (uint8_t *)&tx_byte_val, 1U,
		                                    I2C_FIRST_AND_LAST_FRAME) != HAL_OK)
		{
			tx_pending = false;
			error_inc();
			listening = false;
#ifdef PARSECS_SLAVE_I2C_DIAG
			diag.tx_arm_fail++;
			diag.last_state = (uint32_t)hi2c->State;
			diag_pulse(DIAG_TX_ARM_FAIL);
#endif
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
#ifdef PARSECS_SLAVE_I2C_DIAG
	rx_wait = false;
	/* The repeated START with the read address follows ~5 us later; it is
	 * only ACKed if CR1.ACK and CR1.PE are still set here. */
	if (!diag_ack_on())
	{
		diag.ack_off_rx++;
		diag.last_state = (uint32_t)hi2c->State;
		diag_pulse(DIAG_ACK_OFF);
	}
#endif
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;
}

/* Called by the master's NACK after a read (I2C_Slave_AF) and again by the
 * STOP that follows, so tx_pending is cleared on the first call. */
void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	if (phase == PHASE_TRANSMIT)
	{
		if (tx_pending)
		{
			PARSECS_RingAdvanceRead(&slaves[0].l1_buffer_tx, 1U);
			tx_pending = false;
		}
		/* Single 32-bit store; the task side only reads it, which is atomic on M4. */
		slaves[0].l1_rounds++;
		led_activity();
		led_heartbeat();
		led_fault_off();
		error_reset();
	}

	listening = (HAL_I2C_EnableListen_IT(hi2c) == HAL_OK);
	diag_listen_result(listening);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	if (hi2c->Instance != I2C1) return;

	error_inc();
	last_error = hi2c->ErrorCode;
	diag_error(hi2c->ErrorCode);
	tx_pending = false;
	listening = false;
	resync_requested = true;
}

void PARSECS_Layer1_Init(void)
{
	diag_init();
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

#ifdef PARSECS_SLAVE_I2C_DIAG
	/* ISRs run to completion before this task code, so a cleared ACK seen here
	 * with listening still true was not put back by anyone. */
	if (listening && !resync_requested && !diag_ack_on())
	{
		if (!ack_off_seen)
		{
			ack_off_seen = true;
			diag.ack_off_tick++;
			diag.last_state = (uint32_t)hi2c1.State;
			diag_pulse(DIAG_ACK_OFF);
		}
	}
	else
	{
		ack_off_seen = false;
	}
#endif

	if (resync_requested)
	{
#ifdef PARSECS_SLAVE_I2C_DIAG
		diag.resync_error++;
#endif
		i2c_resync("error");
	}

	if (slave_stuck())
	{
		if (++stuck_ticks > SLAVE_STUCK_TICKS)
		{
#ifdef PARSECS_SLAVE_I2C_DIAG
			diag.resync_stuck++;
			switch (stuck_why)
			{
				case STUCK_FLAG:  diag.stuck_flag++;  break;
				case STUCK_STATE: diag.stuck_state++; break;
				case STUCK_SCL:   diag.stuck_scl++;   break;
				case STUCK_BUSY:  diag.stuck_busy++;  break;
				default: break;
			}
#endif
			i2c_resync("stuck");
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
#ifdef PARSECS_SLAVE_I2C_DIAG
		diag.resync_errors++;
#endif
		i2c_resync("errors");
	}
}

void PARSECS_Layer1_DiagReport(void)
{
#ifdef PARSECS_SLAVE_I2C_DIAG
	slave_diag_t now;
	uint32_t key = PARSECS_IrqLock();
	memcpy(&now, (const void *)&diag, sizeof(now));
	PARSECS_IrqUnlock(key);

	if (memcmp(&now, &diag_reported, sizeof(now)) == 0)
	{
		return;
	}
	diag_reported = now;
	LOG_SERIAL("[Slave]  diag t=%lu rounds=%lu err=%lu(B%lu A%lu F%lu O%lu D%lu X%lu) listenfail=%lu armfail=%lu/%lu rxlost=%lu ackoff=%lu/%lu\n",
	           (unsigned long)HAL_GetTick(), (unsigned long)slaves[0].l1_rounds,
	           (unsigned long)now.err_cb, (unsigned long)now.err_berr, (unsigned long)now.err_arlo,
	           (unsigned long)now.err_af, (unsigned long)now.err_ovr, (unsigned long)now.err_dma,
	           (unsigned long)now.err_other, (unsigned long)now.listen_fail,
	           (unsigned long)now.rx_arm_fail, (unsigned long)now.tx_arm_fail,
	           (unsigned long)now.rx_lost, (unsigned long)now.ack_off_rx, (unsigned long)now.ack_off_tick);
	LOG_SERIAL("[Slave]  diag resync=%lu/%lu/%lu stuck=%lu/%lu/%lu/%lu state=0x%02lX\n",
	           (unsigned long)now.resync_error, (unsigned long)now.resync_stuck,
	           (unsigned long)now.resync_errors, (unsigned long)now.stuck_flag,
	           (unsigned long)now.stuck_state, (unsigned long)now.stuck_scl,
	           (unsigned long)now.stuck_busy, (unsigned long)now.last_state);
#endif
}

#endif

//! @}

//! @}
