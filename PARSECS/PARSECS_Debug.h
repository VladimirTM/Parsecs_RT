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
 * \def SPI_PROTOCOL_DEBUG
 * This define enable de debug mode of the PARSECS Stack. This implies the activation of all the printing to stdout.
 * If this define is commented then no printing is done on stdout from the code of the PARSECS stack
 */
/* Disabled on STM32: the Low Level Substack must not printf over USB/ITM. */
/* #define SPI_PROTOCOL_DEBUG */

#ifdef SPI_PROTOCOL_DEBUG

#define SPI_PROTOCOL_LL_DEBUG
#define SPI_PROTOCOL_INFO_DEBUG
#define SPI_PROTOCOL_WIT_DEBUG
#define SPI_PROTOCOL_USER_DEBUG
#define SPI_PROTOCOL_ERROR_DEBUG


#ifdef SPI_PROTOCOL_USER_DEBUG
#define SPI_PROTOCOL_USER_FULL_BER_DEBUG
#endif


#endif

#endif

//! @}
