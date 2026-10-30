//! \file	PARSECS_Debug.h
//!
//! \brief	PARSECS Debug definitions
//!
//! Contains defines for enabling and disabling the DEBUG printing in the PARSECS Stack.
//! \addtogroup PARSECS_RT
//! @{
#ifndef _PARSECS_Debug
#define _PARSECS_Debug

/*!
 * \def I2C_PROTOCOL_DEBUG
 * This define enable de debug mode of the PARSECS Stack. This implies the activation of all the printing to stdout.
 * If this define is commented then no printing is done on stdout from the code of the PARSECS stack
 */
/* Output goes to USB CDC through LOG_SERIAL (log.c). LOG_SERIAL only queues
 * the text and logTask sends it, so the prints do not stall I2C. */
#define I2C_PROTOCOL_DEBUG

/*!
 * \def PARSECS_SLAVE_I2C_DIAG
 * Slave builds only: counts the I2C error/recovery paths of Layer 1, logs the
 * counters once per second when they change, and pulses PE7 on each event
 * (see CHANGELOG, feature/slave-i2c-diagnostics). Comment out to remove it.
 */
#define PARSECS_SLAVE_I2C_DIAG

#ifdef I2C_PROTOCOL_DEBUG

#define I2C_PROTOCOL_LL_DEBUG
#define I2C_PROTOCOL_INFO_DEBUG
#define I2C_PROTOCOL_WIT_DEBUG
#define I2C_PROTOCOL_USER_DEBUG
#define I2C_PROTOCOL_ERROR_DEBUG


#ifdef I2C_PROTOCOL_USER_DEBUG
//#define I2C_PROTOCOL_USER_FULL_BER_DEBUG
#endif


#endif

#endif

//! @}
