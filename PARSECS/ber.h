//*****************************************************************************
//! \file	ber.h
//!
//! \brief	BER (Basic Encoding Rules) C Library
//! 
//! Constains the implemention of the encoders and decoders for the data types defined by the BER subset of ASN.1 standard
//*****************************************************************************

#ifndef __BER_H
#define __BER_H

//*****************************************************************************
//
//! \addtogroup BER
//! @{
//
//*****************************************************************************

#include <stdint.h>
#include <stdbool.h>

/* Disabled on STM32: BER print uses printf. */
/* #define BER_ENABLE_PRINT */


//*****************************************************************************
//! \enum BERUnit
//! Defines the possible measurement units supported by the implementation
//*****************************************************************************
typedef enum
{
	BERUnit_ActivePower_W = 27,
	BERUnit_ActiveEnergy_Wh = 30,
	BERUnit_ApparentEnergy_VAh = 31,
	BERUnit_ReactiveEnergy_varh = 32,
	BERUnit_Current_A = 33,
	BERUnit_Voltage_V = 35,
	BERUnit_NoUnit = 255,
}BERUnit;



//*****************************************************************************
//! \enum BERCodes
//! Defines the BER tags
//*****************************************************************************
typedef enum
{
	BERNullData = 0,
	BERArray = 1,
	BERStructure = 2,
	BERBoolean = 3,
	BERBitString = 4,
	BERDoubleLong = 5,
	BERDoubleLongUnsigned = 6,
	BEROctetString = 9,
	BERVisibleString = 10,
	BERBCD = 13,
	BERInteger = 15,
	BERLong = 16,
	BERUnsigned = 17,
	BERLongUnsigned = 18,
	BERCompactArray = 19,
	BERLong64 = 20,
	BERLong64Unsigned = 21,
	BEREnum = 22,
	BERFloat32 = 23,
	BERFLoat64 = 24,
	BERDateTime = 25,
	BERDate = 26,
	BERTime = 27
}BERCodes;

uint32_t BERDecodeLength(const uint8_t *buffer, uint32_t *buffer_index);
bool BEREncodeLength(uint32_t length, uint8_t *dest, uint32_t *dest_index);


bool BERBooleanEncode(bool value, uint8_t *dest, uint32_t *dest_index);
bool BERBooleanDecode(bool *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

bool BERDoubleLongUnsignedEncode(uint32_t value, uint8_t *dest, uint32_t *dest_index);
bool BERDoubleLongUnsignedDecode(uint32_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

bool BEREnumEncode(uint8_t value, uint8_t *dest, uint32_t *dest_index);
bool BEREnumDecode(uint8_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

bool BERIntegerEncode(int8_t value, uint8_t *dest, uint32_t *dest_index);
bool BERIntegerDecode(int8_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

bool BERLongEncode(int16_t value, uint8_t *dest, uint32_t *dest_index);
bool BERLongDecode(int16_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

bool BERLong64UnsignedEncode(uint64_t value, uint8_t *dest, uint32_t *dest_index);
bool BERLong64UnsignedDecode(uint64_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

bool BERLongUnsignedDecode(uint16_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);
bool BERLongUnsignedEncode(uint16_t value, uint8_t *dest, uint32_t *dest_index);

bool BERNullDataEncode(uint8_t *dest, uint32_t *dest_index);
bool BERNullDataDecode(uint16_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

bool BEROctetStringEncode(uint8_t *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index);
bool BEROctetStringDecode(uint8_t *value, uint32_t *value_size, uint32_t max_value_size, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

bool BERUnsignedEncode(uint8_t value, uint8_t *dest, uint32_t *dest_index);
bool BERUnsignedDecode(uint8_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

bool BERStructureDecode(uint16_t *structureSize, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);
bool BERStructureEncode(uint16_t structureSize, uint8_t *dest, uint32_t *dest_index);

bool BERArrayDecode(uint16_t *arraySize, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);
bool BERArrayEncode(uint16_t arraySize, uint8_t *dest, uint32_t *dest_index);


//uint8_t BEREncodeFlagList(uint8_t *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index);
//uint8_t BEREncodeFlagListBigNumber(BIG_NUMBER *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index);
//uint8_t BERBitStringEncodeBigNumber(BIG_NUMBER *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index);
//uint8_t BERBitStringEncode(uint8_t *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index);
//uint8_t BERBitStringEncode2(BIG_NUMBER *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index);

bool BERCheck(const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);

#ifdef BER_ENABLE_PRINT
bool BERPrintEncodedBufferAsBer(const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size);
#endif

//*****************************************************************************
//
// Close the Doxygen group.
//! @}
//
//*****************************************************************************


#endif
