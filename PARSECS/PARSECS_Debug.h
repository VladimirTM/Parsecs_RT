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
/* Output goes to USB CDC through LOG_SERIAL (log.c). The prints run inside
 * myTestTask, so comment this out for clean round-trip timing on PE7. */
#define I2C_PROTOCOL_DEBUG

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
