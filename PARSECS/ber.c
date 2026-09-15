//*****************************************************************************
//! \file	ber.c
//!
//! \brief	BER (Basic Encoding Rules) C Library
//! 
//! Contains the implemention of the encoders and decoders for the data types defined by the BER subset of ASN.1 standard
//*****************************************************************************

//*****************************************************************************
//! \headerfile ber.h "ber.h"
//*****************************************************************************

#include "ber.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#ifdef BER_ENABLE_PRINT
#include <stdio.h>
#endif

//*****************************************************************************
//
//! \addtogroup BER
//! @{
//! \brief	BER (Basic Encoding Rules) C Library
//!
//! Contains the implemention of the encoders and decoders for the data types defined by the BER subset of ASN.1 standard
//
//*****************************************************************************

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

//*****************************************************************************
//! Decode the length of a BER type according to ASN.1 standard
//! 
//! \param[in] buffer - Reference to the memory zone containing the data buffer on which to operate
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index from which to begin the extraction of the length. 
//! Upon return, the function writes the new index to this parameter (the index at which the decoding ended)
//! \return The decoded length
//*****************************************************************************
uint32_t BERDecodeLength(const uint8_t *buffer, uint32_t *buffer_index)
{
	uint16_t index = *buffer_index;
	uint32_t length = 0;
	uint8_t i = 0;
	uint8_t temp = buffer[index++];
	if (temp <= 0x80)
	{
		length = temp;
	}
	else
	{
		for (i = 0; i < (temp - 0x80); i++)
		{
			length = (length << 8) | buffer[index];
			index++;
		}		
	}
	*buffer_index = index;
	return length;
}

//*****************************************************************************
//! Encode the length of a BER type according to ASN.1 standard
//! 
//! \param[in] length - The \b length value to be encoded
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the length. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BEREncodeLength(uint32_t length, uint8_t *dest, uint32_t *dest_index) 
{
	uint8_t bytenr = 0;
	uint32_t index = *dest_index;
	int8_t i = 0;
	if ((dest == NULL) || (dest_index == NULL))
	{
		return false;
	}
	// find the number of bytes needed to encode the length
	if (length <= 0x80) // < 2^8
	{
		bytenr = 1;
	}
	else
	{
		if (length < 65536) // < 2^16
		{
			bytenr = 2;
		}
		else
		{
			if (length < 16777216) // < 2^24
			{
				bytenr = 3;
			}
			else
			{
				bytenr = 4; 
			}
		}
	}
	if (bytenr == 1)
	{
		// only one byte needed for length... encode it as it is
		dest[index++] = (length & 0xFF);
	}
	else
	{
		// more bytes needed for length... encoding the length of the length
		dest[index++] = (0x80 | (bytenr & 0xF));		
		for (i = bytenr - 1; i >= 0; i--)
		{
			dest[index++] = ((length >> (i * 8)) & 0xFF);
		}
	}
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Encode a BERBoolean type according to ASN.1 standard
//! 
//! \param[in] value - The value to be encoded
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERBooleanEncode(bool value, uint8_t *dest, uint32_t *dest_index)
{
	uint16_t index = *dest_index;
	dest[index++] = BERBoolean;
	dest[index++] = value;
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Decode a BERBoolean type according to ASN.1 standard
//! 
//! \param[out] value - The resulted value after the decode process
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERBooleanDecode(bool *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	if (buffer_size < 2)
	{
		return false;
	}
	if (buffer[index] == BERBoolean)
	{
		index++;
		*value = buffer[index++];
		*buffer_index = index;
		return true;
	}
	return false;
}
//*****************************************************************************
//! Decode a BERStructure type according to ASN.1 standard. 
//! This function only decodes the size of the structure and advances the index in the raw data buffer to the next (first) BER element in the structure
//! 
//! \param[out] structureSize - The size of the decoded structure
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERStructureDecode(uint16_t *structureSize, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	if (buffer_size < 2)
	{
		return false;
	}
	if (buffer[index] == BERStructure)
	{
		index++;
		*structureSize = BERDecodeLength(buffer, &index);
		*buffer_index = index;
		return true;
	}
	return false;
}

bool BERStructureEncode(uint16_t structureSize, uint8_t *dest, uint32_t *dest_index)
{
	uint32_t index = *dest_index;
	dest[index++] = BERStructure;
	BEREncodeLength(structureSize, dest, &index);
	*dest_index = index;
	return true;
}

bool BERArrayEncode(uint16_t arraySize, uint8_t *dest, uint32_t *dest_index)
{
	uint32_t index = *dest_index;
	dest[index++] = BERArray;
	BEREncodeLength(arraySize, dest, &index);
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Decode a BERArray type according to ASN.1 standard. 
//! This function only decodes the size of the array and advances the index in the raw data buffer to the next (first) BER element in the array
//! 
//! \param[out] arraySize - The size of the decoded structure
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERArrayDecode(uint16_t *arraySize, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	if (buffer_size < 2)
	{
		return false;
	}
	if (buffer[index] == BERArray)
	{
		index++;
		*arraySize = BERDecodeLength(buffer, &index);
		*buffer_index = index;
		return true;
	}
	return false;
}


//*****************************************************************************
//! Decode a BERDoubleLongUnsigned type according to ASN.1 standard
//! 
//! \param[out] value - The resulted value after the decode process
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERDoubleLongUnsignedDecode(uint32_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	uint32_t result = 0;
	if (buffer_size < 5)
	{
		return false;
	}
	if (buffer[index] == BERDoubleLongUnsigned)
	{
		index++;
		result |= buffer[index++] << 24;
		result |= buffer[index++] << 16;
		result |= buffer[index++] <<  8;
		result |= buffer[index++];
		*value = result;
		*buffer_index = index;
		return true;
	}
	return false;
}

//*****************************************************************************
//! Encode a BERDoubleLongUnsigned type according to ASN.1 standard
//! 
//! \param[in] value - The value to be encoded
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERDoubleLongUnsignedEncode(uint32_t value, uint8_t *dest, uint32_t *dest_index)
{
	uint16_t index = *dest_index;
	int8_t i = 0;
	dest[index++] = BERDoubleLongUnsigned;
	for (i = 3; i >= 0; i--)
	{
		dest[index++] = ((value >> (i * 8)) & 0xFF);
	}
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Decode a BEREnum type according to ASN.1 standard
//! 
//! \param[out] value - The resulted value after the decode process
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BEREnumDecode(uint8_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	if (buffer_size < 2)
	{
		return false;
	}
	if (buffer[index] == BEREnum)
	{
		index++;
		*value = buffer[index++];
		*buffer_index = index;
		return true;
	}
	return false;
}

//*****************************************************************************
//! Encode a BEREnum type according to ASN.1 standard
//! 
//! \param[in] value - The value to be encoded
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BEREnumEncode(uint8_t value, uint8_t *dest, uint32_t *dest_index)
{
	uint16_t index = *dest_index;
	dest[index++] = BEREnum;
	dest[index++] = value;
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Encode a BERInteger type according to ASN.1 standard
//! 
//! \param[in] value - The value to be encoded
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERIntegerEncode(int8_t value, uint8_t *dest, uint32_t *dest_index)
{
	uint16_t index = *dest_index;
	dest[index++] = BERInteger;
	dest[index++] = value;
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Decode a BERInteger type according to ASN.1 standard
//! 
//! \param[out] value - The resulted value after the decode process
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERIntegerDecode(int8_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	int8_t result = 0;
	if (buffer_size < 2)
	{
		return false;
	}
	if (buffer[index] == BERInteger)
	{
		index++;
		result = buffer[index++];
		*value = result;
		*buffer_index = index;
		return true;
	}
	return false;
}

//*****************************************************************************
//! Encode a BERLong type according to ASN.1 standard
//! 
//! \param[in] value - The value to be encoded
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERLongEncode(int16_t value, uint8_t *dest, uint32_t *dest_index)
{
	uint16_t index = *dest_index;
	int8_t i = 0;
	dest[index++] = BERLong;
	for (i = 1; i >= 0; i--)
	{
		dest[index++] = ((value >> (i * 8)) & 0xFF);
	}
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Decode a BERLong type according to ASN.1 standard
//! 
//! \param[out] value - The resulted value after the decode process
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERLongDecode(int16_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	int16_t result = 0;
	if (buffer_size < 3)
	{
		return false;
	}
	if (buffer[index] == BERLong)
	{
		index++;
		result = buffer[index++] << 8;
		result |= buffer[index++];
		*value = result;
		*buffer_index = index;
		return true;
	}
	return false;
}


//*****************************************************************************
//! Encode a BERLong64Unsigned type according to ASN.1 standard
//! 
//! \param[in] value - The value to be encoded
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERLong64UnsignedEncode(uint64_t value,  uint8_t *dest, uint32_t *dest_index)
{
	uint16_t index = *dest_index;
	int8_t i = 0;
	dest[index++] = BERLong64Unsigned;
	for (i = 7; i >= 0; i--)
	{
		dest[index++] = ((value >> (i * 8)) & 0xFF);
	}	
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Decode a BERLong type according to ASN.1 standard
//! 
//! \param[out] value - The resulted value after the decode process
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERLong64UnsignedDecode(uint64_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	uint64_t result = 0;
	if (buffer_size < 9)
	{
		return false;
	}
	if (buffer[index] == BERLong64Unsigned)
	{
		index++;
		result = (uint64_t)(buffer[index++]) << 56;
		result |= (uint64_t)(buffer[index++]) << 48;
		result |= (uint64_t)(buffer[index++]) << 40;
		result |= (uint64_t)(buffer[index++]) << 32;
		result |= (uint64_t)(buffer[index++]) << 24;
		result |= (uint64_t)(buffer[index++]) << 16;
		result |= (uint64_t)(buffer[index++]) << 8;
		result |= (uint64_t)(buffer[index++]);
		*value = result;
		*buffer_index = index;
		return true;
	}
	return false;
}



//*****************************************************************************
//! Decode a BERLongUnsigned type according to ASN.1 standard
//! 
//! \param[out] value - The resulted value after the decode process
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERLongUnsignedDecode(uint16_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	uint16_t result = 0;
	if (buffer_size < 2)
	{
		return false;
	}
	if (buffer[index] == BERLongUnsigned)
	{
		index++;
		result = buffer[index++] << 8;
		result |= buffer[index++];		
		*value = result;
		*buffer_index = index;
		return true;
	}
	return false;
}

//*****************************************************************************
//! Encode a BERLongUnsigned type according to ASN.1 standard
//! 
//! \param[in] value - The value to be encoded
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERLongUnsignedEncode(uint16_t value, uint8_t *dest, uint32_t *dest_index)
{
	uint16_t index = *dest_index;
	int8_t i = 0;
	dest[index++] = BERLongUnsigned;
	for (i = 1; i >= 0; i--)
	{
		dest[index++] = ((value >> (i * 8)) & 0xFF);
	}	
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Encode a BERNull type according to ASN.1 standard
//! 
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERNullDataEncode(uint8_t *dest, uint32_t *dest_index)
{
	dest[*dest_index] = BERNullData;
	*dest_index = (*dest_index) + 1;
	return true;
}

//*****************************************************************************
//! Decode a BERNullData type according to ASN.1 standard
//!
//! \param[out] value - This parameter has no meaning thus it is a NULL data type. A valid, invalid or null pointer can be assigned
//! \param[in] buffer - Reference to the memory zone where the raw encoded data
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value.
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERNullDataDecode(uint16_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint16_t index = *buffer_index;
	if (buffer_size < 1)
	{
		return false;
	}
	if (buffer[index] == BERNullData)
	{
		index++;
		*buffer_index = index;
		return true;
	}
	return true;
}

//*****************************************************************************
//! Decode a BEROctetString type according to ASN.1 standard
//! 
//! \param[out] value - The resulted value after the decode process
//! \param[out] value_size - The resulted size of the decoded value
//! \param[in] max_value_size - The maximum size of the value to be decoded (maximum size of the \b value memory zone)
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BEROctetStringDecode(uint8_t *value, uint32_t *value_size, uint32_t max_value_size, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	uint32_t size;
	uint32_t count = 0;
	if (buffer_size < 2)
	{
		return false;
	}
	if (buffer[index] == BEROctetString)
	{
		index++;
		size = BERDecodeLength(buffer, &index);
		count = MIN(size, max_value_size);
		memcpy(value, &buffer[index], count);
		index += size;
		*value_size = count;
		*buffer_index = index;
		return true;
	}
	
	return false;
}

//*****************************************************************************
//! Encode a BEROctetString type according to ASN.1 standard
//! 
//! \param value - The value to be encoded
//! \param value_size - The size of the value to be encoded
//! \param dest - Reference to the memory zone where the encoded buffer should be stored
//! \param dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BEROctetStringEncode(uint8_t *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index)
{
	uint32_t index = *dest_index;
	uint16_t i = 0;
	dest[index++] = BEROctetString;
	BEREncodeLength(value_size, dest, &index);
	for (i = 0; i < value_size; i++)
	{
		dest[index++] = value[i];
	}	
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Encode a BERUnsigned type according to ASN.1 standard
//! 
//! \param[in] value - The value to be encoded
//! \param[out] dest - Reference to the memory zone where the encoded buffer should be stored
//! \param[in,out] dest_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the encoding of the value. 
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERUnsignedEncode(uint8_t value, uint8_t *dest, uint32_t *dest_index)
{
	uint16_t index = *dest_index;
	dest[index++] = BERUnsigned;
	dest[index++] = value;
	*dest_index = index;
	return true;
}

//*****************************************************************************
//! Decode a BERUnsigned type according to ASN.1 standard
//! 
//! \param[out] value - The resulted value after the decode process
//! \param[in] buffer - Reference to the memory zone where the raw encoded data 
//! \param[in,out] buffer_index - Input/Output parameter. It specified the index in the \b dest buffer from which to begin the decoding of the value. 
//! \param[in] buffer_size - The size of the buffer containg the raw encoded data
//! Upon return, the function writes the new index to this parameter (the index at which the encoding ended)
//! \return \b true - The encoding was successfull
//! \return \b false - The encoding was not successfull
//*****************************************************************************
bool BERUnsignedDecode(uint8_t *value, const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	uint8_t result = 0;
	if (buffer_size < 2)
	{
		return false;
	}
	if (buffer[index] == BERUnsigned)
	{
		index++;
		result |= buffer[index++];		
		*value = result;
		*buffer_index = index;
		return true;
	}
	return false;
}

//uint8_t BEREncodeFlagList(uint8_t *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index)
//{
//	// compute the number of bytes needed to encode the bit list
//	
//	uint8_t mask = 0x80;
//	uint8_t bitcount = 0;
//	uint8_t tempByte = 0;
//	uint32_t i = 0;
//	uint32_t index = *dest_index;
//	
//	for (i = 0; i < value_size; i++)
//	{
//		if (value[i] != 0) // == true
//		{
//			tempByte |= mask;
//		}
//		mask = mask >> 1;
//		bitcount++;
//		if ((bitcount == 8) || (i == value_size - 1))
//		{
//			dest[index++] = tempByte;
//			mask = 0x80;
//			tempByte = 0;
//			bitcount = 0;
//		}
//	}
//	*dest_index = index;
//	return BER_OK;
//}

//uint8_t BEREncodeFlagListBigNumber(BIG_NUMBER *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index)
//{
//	// compute the number of bytes needed to encode the bit list
//	
//	uint8_t mask = 0x80;
//	uint8_t bitcount = 0;
//	uint8_t tempByte = 0;
//	uint32_t i = 0;
//	uint32_t index = *dest_index;
//	uint8_t temp = 0;
//	for (i = 0; i < value_size; i++)
//	{
//		temp = GetBitFromBigNumber(value, i);
//		if (temp != 0) // == true
//		{
//			tempByte |= mask;
//		}
//		mask = mask >> 1;
//		bitcount++;
//		if ((bitcount == 8) || (i == value_size - 1))
//		{
//			dest[index++] = tempByte;
//			mask = 0x80;
//			tempByte = 0;
//			bitcount = 0;
//		}
//	}
//	*dest_index = index;
//	return BER_OK;
//}
//
//
//uint8_t BERBitStringEncode(uint8_t *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index)
//{
//	uint32_t index = 0;
//	dest[index++] = BERBitString;
//	BEREncodeLength(value_size, dest, &index);
//	BEREncodeFlagList(value, value_size, dest, &index); 
//	*dest_index = index;
//	return BER_OK;
//}
//
//uint8_t BERBitStringEncodeBigNumber(BIG_NUMBER *value, uint32_t value_size, uint8_t *dest, uint32_t *dest_index)
//{
//	uint32_t index = 0;
//	dest[index++] = BERBitString;
//	BEREncodeLength(value_size, dest, &index);
//	BEREncodeFlagListBigNumber(value, value_size, dest, &index); 
//	*dest_index = index;
//	return BER_OK;
//}
//

bool BERCheck(const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	static uint8_t work_buffer[32];
	switch (buffer[index])
	{
		case BERNullData:
		{
			index++;
			break;
		}

		case BERBoolean:
		{
			bool value;
			if (BERBooleanDecode(&value, buffer, &index, buffer_size) == false)
			{
				return false;
			}
			break;
		}

		case BERDoubleLongUnsigned:
		{
			uint32_t value;
			if (BERDoubleLongUnsignedDecode(&value, buffer, &index, buffer_size) == false)
			{
				return false;
			}
			break;
		}

		case BEROctetString:
		{
			uint32_t value_size;
			if (BEROctetStringDecode(work_buffer, &value_size, 32, buffer, &index, buffer_size) == false)
			{
				return false;
			}
			break;
		}
			
		case BERInteger:
		{
			int8_t value;
			if (BERIntegerDecode(&value, buffer, &index, buffer_size) == false)
			{
				return false;
			}			
			break;
		}
		case BERLong:
		{
			int16_t value;
			if (BERLongDecode(&value, buffer, &index, buffer_size) == false)
			{
				return false;
			}		
			break;
		}

		case BERUnsigned:
		{
			uint8_t value;
			if (BERUnsignedDecode(&value, buffer, &index, buffer_size) == false)
			{
				return false;
			}
			break;
		}

		case BERLongUnsigned:
		{
			uint16_t value;
			if (BERLongUnsignedDecode(&value, buffer, &index, buffer_size) == false)
			{
				return false;
			}
			break;
		}

		case BERLong64Unsigned:
		{
			uint64_t value;
			if (BERLong64UnsignedDecode(&value, buffer, &index, buffer_size) == false)
			{
				return false;
			}
			break;
		}
		case BEREnum:
		{
			uint8_t value;
			if (BEREnumDecode(&value, buffer, &index, buffer_size) == false)
			{
				return false;
			}
			break;
		}
		case BERStructure:
		{
			uint16_t structureSize = 0;
			uint16_t i = 0;
			if (BERStructureDecode(&structureSize, buffer, &index, buffer_size) == true)
			{				
				for (i = 0; i < structureSize; i++)
				{
					if (BERCheck(buffer, &index, buffer_size) == false)
					{
						return false;
					}
				}				
			}
			else
			{
				return false;
			}
			break;
		}
		case BERArray:
		{
			uint16_t structureSize = 0;
			uint16_t i = 0;
			if (BERArrayDecode(&structureSize, buffer, &index, buffer_size) == true)
			{
				for (i = 0; i < structureSize; i++)
				{
					if (BERCheck(buffer, &index, buffer_size) == false)
					{
						return false;
					}
				}				
			}
			else
			{
				return false;
			}
			break;
		}		
		default:
		{
			return false;
			break;
		}
	}	
	*buffer_index = index;
	return true;
}

#ifdef BER_ENABLE_PRINT

bool BERPrintEncodedBufferAsBer(const uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_size)
{
	uint32_t index = *buffer_index;
	static uint8_t work_buffer[32];
	switch (buffer[index])
	{
		case BERNullData:
		{
			index++;
			printf("BERNullData ");
			break;
		}

		case BERBoolean:
		{
			bool value;
			if (BERBooleanDecode(&value, buffer, &index, buffer_size) == true)
			{
				printf("BERBoolean: %s", (value == true) ? "true" : "false");
			}
			else
			{
				return false;
			}
			break;
		}

		case BERDoubleLongUnsigned:
		{
			uint32_t value;
			if (BERDoubleLongUnsignedDecode(&value, buffer, &index, buffer_size) == true)
			{
				printf("BERDoubleLongUnsigned: %u", value);
			}
			else
			{
				return false;
			}
			break;
		}

		case BEROctetString:
		{
			uint32_t value_size;
			uint8_t i = 0;
			if (BEROctetStringDecode(work_buffer, &value_size, 32, buffer, &index, buffer_size) == true)
			{
				printf("BEROctetString: ");
				for (i = 0; i < value_size; i++)
				{
					printf ("%c", work_buffer[i]);
				}
			}
			else
			{
				return false;
			}
			break;
		}
			
		case BERInteger:
		{
			int8_t value;
			if (BERIntegerDecode(&value, buffer, &index, buffer_size) == true)
			{
				printf("BERInteger: %d", value);
			}
			else
			{
				return false;
			}			
			break;
		}
		case BERLong:
		{
			int16_t value;
			if (BERLongDecode(&value, buffer, &index, buffer_size) == true)
			{
				printf("BERInteger: %d", value);
			}
			else
			{
				return false;
			}			
			break;
		}

		case BERUnsigned:
		{
			uint8_t value;
			if (BERUnsignedDecode(&value, buffer, &index, buffer_size) == true)
			{
				printf("BERUnsigned: %d", value);
			}
			else
			{
				return false;
			}					
			break;
		}

		case BERLongUnsigned:
		{
			uint16_t value;
			if (BERLongUnsignedDecode(&value, buffer, &index, buffer_size) == true)
			{
				printf("BERLongUnsigned: %d", value);
			}
			else
			{
				return false;
			}					
			break;
		}

		case BERLong64Unsigned:
		{
			uint64_t value;
			if (BERLong64UnsignedDecode(&value, buffer, &index, buffer_size) == true)
			{
				printf("BERLong64Unsigned: %lu", value);
			}
			else
			{
				return false;
			}							
			break;
		}
		case BEREnum:
		{
			uint8_t value;
			if (BEREnumDecode(&value, buffer, &index, buffer_size) == true)
			{
				printf("BEREnum: %d", value);
			}
			else
			{
				return false;
			}				
			break;
		}
		case BERStructure:
		{
			uint16_t structureSize = 0;
			uint16_t i = 0;
			if (BERStructureDecode(&structureSize, buffer, &index, buffer_size) == true)
			{
				printf("BERStructure: %d {", structureSize);
				for (i = 0; i < structureSize; i++)
				{
					if (BERPrintEncodedBufferAsBer(buffer, &index, buffer_size) == false)
					{
						return false;
					}
					if (i < (structureSize - 1))
					{
						printf (", ");
					}
				}				
				printf("}");
			}
			else
			{
				return false;
			}
			break;
		}
		case BERArray:
		{
			uint16_t structureSize = 0;
			uint16_t i = 0;
			if (BERArrayDecode(&structureSize, buffer, &index, buffer_size) == true)
			{
				printf("BERStructure: %d {", structureSize);
				for (i = 0; i < structureSize; i++)
				{
					if (BERPrintEncodedBufferAsBer(buffer, &index, buffer_size) == false)
					{
						return false;
					}
					if (i < (structureSize - 1))
					{
						printf (", ");
					}
				}				
				printf("}");
			}
			else
			{
				return false;
			}
			break;
		}		
		default:
		{
			return false;
			break;
		}
	}	
	*buffer_index = index;
	return true;
}

#endif

//*****************************************************************************
//
// Close the Doxygen group.
//! @}
//
//*****************************************************************************


