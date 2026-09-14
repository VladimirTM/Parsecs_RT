//! \file	crc16.c
//!
//! \brief	CRC16 Calculation Object
//!
//! This file contains the definitions for the software module responsible for calculating the CRC16. It provides ways to calculate the CRC either by buffer or
//! incremental by adding each byte separately
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup CRC16Object
//! @{
//! \brief CRC16 Calculation Object
#ifndef __CRC16_H
#define __CRC16_H

#include <stdint.h>

/*!
 * \def PPPINITFCS16
 * Initial FCS value
 */
/*!
 * \def PPPGOODFCS16
 * Good final FCS value - Not used, should be eliminated
 */

#define PPPINITFCS16 0xffff
#define PPPGOODFCS16 0xf0b8

/*!
 * \def CRC_OK
 * Definition of an OK return value
 */
/*!
 * \def CRC_NOT_OK
 * Definition of a NOT OK return value
 */
#define CRC_OK		1
#define CRC_NOT_OK	0

//! \struct CRC16
//! Structure that defines CRC16 object
typedef struct
{
	uint16_t currentCRC; /**<The current calculated value of the CRC16*/
}CRC16;

uint8_t CRC16_constructor(CRC16 *me);
void CRC16_reset_crc(CRC16 *me);
void CRC16_AddToCRC(CRC16 *me, uint8_t byte);
uint16_t CRC16_GetCRC(CRC16 *me);
uint16_t CRC16_CalculateCRCBuffer(CRC16 *me, uint8_t *buffer, uint16_t len);

#endif
//! @}
//! @}
