//! \file	PARSECS_Protocol.c
//!
//! \brief	PARSECS High Level Substack Main implementation files
//!
//! Contains the implementation of the PARSECS High Level Substack
//! The layer implementations is platform and node independent
//! \defgroup PARSECS_RT PARSECS_RT
//! @{
//! \addtogroup PARSECS-High-Level-Substack
//! @{
//! \brief PARSECS High Level Substack
//! Contains the implementation of the PARSECS High Level Substack containing Layer 4, Layer 6 and Layer 7

#include "PARSECS_Protocol.h"


#include <string.h>
#include "log.h"
#include <stdint.h>
#include <stdbool.h>
#include "ber.h"
#include "PARSECS_Includes.h"

#define btoa(x) ((x)?"T":"F")


//! Initialize a \ref PARSECS_PROTOCOL_Descriptor_Type structure
//!
//! This function is used to initialize a \ref PARSECS_PROTOCOL_Descriptor_Type. It is used internally by the PARSECS Stack and is not called directly by the user
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The implementation is node and platform independent.
//! \param[out] protocolDescriptor A pointer to a PARSECS_PROTOCOL_Descriptor_Type that should be initialized
//! \return None
//! \private
void PARSECS_ProtocolDescriptorInit(PARSECS_PROTOCOL_Descriptor_Type *protocolDescriptor)
{
	memset(protocolDescriptor, 0x00, sizeof(PARSECS_PROTOCOL_Descriptor_Type));
	protocolDescriptor->Reception.resourceFree = true;
	protocolDescriptor->Transmission.resourceFree = true;
}


//! Initialize a \ref PARSECS_Protocol_Communication_Interface structure
//!
//! This function is used to initialize a \ref PARSECS_Protocol_Communication_Interface. It is used internally by the PARSECS Stack and is not called directly by the user
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The implementation is node and platform independent.
//! \param[out] protocolCommunicationInterface A pointer to a PARSECS_Protocol_Communication_Interface that should be initialized
//! \return None
//! \private
void PARSECS_ProtocolCommunicationInterfaceInit(PARSECS_Protocol_Communication_Interface *protocolCommunicationInterface)
{
	memset(protocolCommunicationInterface, 0x00, sizeof(PARSECS_Protocol_Communication_Interface));
	protocolCommunicationInterface->transmit_data_size = -1;
	protocolCommunicationInterface->receive_data_size = -1;

	protocolCommunicationInterface->transmit_seq_number = 0;
	protocolCommunicationInterface->transmit_data_size = -1;
	protocolCommunicationInterface->receive_data_size = -1;
}

//! Function used to print a PARSECS_WIT_PDU to standard output. Used mainly for debug purposes.
//!
//! \param pdu A pointer to a PARSECS_WIT_PDU structure that will be printed to standard output
//! \return None
//! \private
void SPI_WIT_PDU_PrintPacket(PARSECS_WIT_PDU *pdu)
{
	LOG_SERIAL("%s %s %s %s Total: %3d Current: %3d PDUSize: %3d", btoa(pdu->SizeExceeded), btoa(pdu->SequenceError),  btoa(pdu->LastPduInMultipacket), btoa(pdu->MultiPacketPdu), pdu->TotalPDUPackets, pdu->CurentPDUPacket, pdu->PDUDataLength);
}

//! Function used to print a \ref PARSECS_PROTOCOL_STATUS to standard output. Used mainly for debug purposes.
//!
//! \param error The enum value of the \ref PARSECS_PROTOCOL_STATUS structure that will be printed to standard output
//! \return None
//! \private
void SPI_WIT_PrintError(PARSECS_PROTOCOL_STATUS error)
{
	switch (error)
	{        
		case PARSECS_PROTOCOL_OK:
		{
			LOG_SERIAL ("Protocol Operating Succeeded\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_PDU_DECODE_NULL_POINTER:
		{
			LOG_SERIAL ("Packet decode failed: NULL pointer found\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_PDU_DECODE_PDU_TOO_SMALL:
		{
			LOG_SERIAL ("Packet decode failed: PDU too small\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_PDU_DECODE_INCORRECT_TOTAL_PDU_PACKETS:
		{
			LOG_SERIAL ("Packet decode failed: incorrect numbr of total pdu packets\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_PDU_DECODE_INCORRECT_CURRENT_PDU_PACKET_NUMBER:
		{
			LOG_SERIAL ("Packet decode failed: incorrect numbr of current pdu packet\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_PDU_DECODE_INSUFFICIENT_DATA_SIZE:
		{
			LOG_SERIAL ("Packet decode failed: insufficient data size\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_RECEIVED_SEQUENCING_ERROR:
		{
			LOG_SERIAL ("Protocol sequencing error received\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_DETECTED_SEQUENCING_ERROR:
		{
			LOG_SERIAL ("Protocol sequencing error detected\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS:
		{
			LOG_SERIAL ("Incorrect segmentation parameters received\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_SEGMENTATION_SIZE_EXCEEDED:
		{
			LOG_SERIAL ("Segmentation size exceeded\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_BUSY:
		{
			LOG_SERIAL ("Protocol upper layer busy\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_NO_DATA:
		{
			LOG_SERIAL ("Protocol upper layer no data for reception\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_BER_DECODE_ERROR:
		{
			LOG_SERIAL ("Protocol upper layer BER decode error\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT:
		{
			LOG_SERIAL ("Protocol upper layer incorrect BER format\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_INCORRECT_BER_LENGTH:
		{
			LOG_SERIAL ("Protocol upper layer incorrect BER length\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_SIZE_EXCEEDED:
		{
			LOG_SERIAL ("Protocol upper layer size exceeded\n");
			break;
		}		
		case PARSECS_PROTOCOL_ERROR_BER_ADDITION_JUNK_FOUND:
		{
			LOG_SERIAL ("Protocol upper layer additional junk data found after BER decode\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_BOARD_NOT_AVAILABLE:
		{
			LOG_SERIAL ("Protocol upper layer board not available\n");
			break;
		}
		case PARSECS_PROTOCOL_ERROR_INCORRECT_APP_PARAMETERS:
		{
			LOG_SERIAL ("Protocol upper layer incorrect app parameters");
			break;
		}
		default:
		{
			LOG_SERIAL ("Unknown protocol error\n");
		}
	}
}

//! Function to state if a byte has a value valid to be a \ref PARSECS_APP_OperationType enum
//!
//! \param operationType The byte to be checked
//! \return true if the byte is a valid value of PARSECS_APP_OperationType
//! \return false if the byte is not a valid value of PARSECS_APP_OperationType
//! private
bool SPI_WIT_ValidateAppOperationType(uint8_t operationType)
{
	switch (operationType)
	{
		case PARSECS_APP_GetRequest:
		case PARSECS_APP_GetResponse:
		case PARSECS_APP_SetRequest:
		case PARSECS_APP_SetResponse:
		case PARSECS_APP_CallRequest:
		case PARSECS_APP_CallResponse:
		{
			return true;
		}
	}
	return false;
}

//! Function to state if a byte has a value valid to be a \ref PARSECS_APP_OperationResponse enum
//!
//! \param response The byte to be checked
//! \return true if the byte is a valid value of PARSECS_APP_OperationResponse
//! \return false if the byte is not a valid value of PARSECS_APP_OperationResponse
//! private
bool SPI_WIT_ValidateAppResponse(uint8_t response)
{
	switch (response)
	{
		case PARSECS_APP_Success:
		case PARSECS_APP_HardwareFault:
		case PARSECS_APP_TemporaryFailure:
		case PARSECS_APP_ReadDenied:
		case PARSECS_APP_WriteDenied:
		case PARSECS_APP_ParameterMethodUndefined:
		case PARSECS_APP_OtherReason:			
		case PARSECS_APP_OperationTimeout:
		case PARSECS_APP_ParameterSyntaxError:
		case PARSECS_APP_OperationUnsupported:
		case PARSECS_APP_AddressMismatch:
		case PARSECS_APP_UnknownValue:			
		{
			return true;
		}
	}
	return false;
}

//! Function used to print a PARSECS_APP_OperationType to standard output. Used mainly for debug purposes.
//!
//! \param operationType The value opf the PARSECS_APP_OperationType structure that will be printed to standard output
//! \return None
//! \private
void SPI_WIT_PrintAppOperationType(PARSECS_APP_OperationType operationType)
{
	switch (operationType)
	{
		case PARSECS_APP_GetRequest:
		{
			LOG_SERIAL ("Get Request");
			break;
		}
		case PARSECS_APP_GetResponse:
		{
			LOG_SERIAL ("Get Response");
			break;
		}
		case PARSECS_APP_SetRequest:
		{
			LOG_SERIAL ("Set Request");
			break;
		}
		case PARSECS_APP_SetResponse:
		{
			LOG_SERIAL ("Set Response");
			break;
		}
		case PARSECS_APP_CallRequest:
		{
			LOG_SERIAL ("Call Request");
			break;
		}
		case PARSECS_APP_CallResponse:
		{
			LOG_SERIAL ("Call Reponse");
			break;
		}
		default:
		{
			LOG_SERIAL ("Unknown Operation Type");
			break;
		}
	}
}

//! Function used to print a PARSECS_APP_OperationResponse to standard output. Used mainly for debug purposes.
//!
//! \param response The value opf the PARSECS_APP_OperationResponse structure that will be printed to standard output
//! \return None
//! \private
void SPI_WIT_PrintAppResponse(PARSECS_APP_OperationResponse response)
{
	switch (response)
	{
		case PARSECS_APP_Success:
		{
			LOG_SERIAL ("Success");
			break;
		}
		case PARSECS_APP_HardwareFault:
		{
			LOG_SERIAL ("Hardware fault");
			break;
		}
		case PARSECS_APP_TemporaryFailure:
		{
			LOG_SERIAL ("Temporary failure");
			break;
		}
		case PARSECS_APP_ReadDenied:
		{
			LOG_SERIAL ("Read Denied");
			break;
		}
		case PARSECS_APP_WriteDenied:
		{
			LOG_SERIAL ("Write Denied");
			break;
		}
		case PARSECS_APP_ParameterMethodUndefined:
		{
			LOG_SERIAL ("Parameter Method Undefined");
			break;
		}
		case PARSECS_APP_OperationTimeout:
		{
			LOG_SERIAL ("Operation Timeout");
			break;
		}			
		case PARSECS_APP_ParameterSyntaxError:
		{
			LOG_SERIAL ("Parameter Syntax Error");
			break;
		}			
		case PARSECS_APP_OperationUnsupported:
		{
			LOG_SERIAL ("Operation Unsupported");
			break;
		}			
		case PARSECS_APP_AddressMismatch:
		{
			LOG_SERIAL ("Address Mismatch");
			break;
		}			
		case PARSECS_APP_OtherReason:
		{
			LOG_SERIAL ("Other Reason");
			break;
		}
		default:
		{
			LOG_SERIAL ("Unknown response");
			break;
		}
	}
}

//! Function used to decode a raw byte buffer into a PARSECS_WIT_PDU
//!
//! \param[out] pdu A pointer to the memory zone of The PARSECS_WIT_PDU that will be written with the decoded data
//! \param data_buffer The raw byte buffer which will be decoded into the PARSECS_WIT_PDU
//! \param data_length The length of the data in the data_buffer parameter
//! \return PARSECS_PROTOCOL_OK in case the decoding process was succesfull
//! \return PARSECS_PROTOCOL_ERROR_PDU_DECODE_NULL_POINTER in the case the given parameters are NULL pointers
//! \return PARSECS_PROTOCOL_ERROR_PDU_DECODE_PDU_TOO_SMALL in case the buffer is too small
//! \return PARSECS_PROTOCOL_ERROR_PDU_DECODE_INCORRECT_TOTAL_PDU_PACKETS in the case when an incorrect TotalPDUPackets field was decoded (when this value is < 0)
//! \return PARSECS_PROTOCOL_ERROR_PDU_DECODE_INCORRECT_CURRENT_PDU_PACKET_NUMBER in the case when an incorrect CurrentPDUPacket field was decoded (when this value is < 0 or when  CurrentPDUPacket > TotalPDUPackets)
//! \return PARSECS_PROTOCOL_ERROR_PDU_DECODE_INSUFFICIENT_DATA_SIZE the data_length is smaller than the necessary value decoded from WITDataLength
//! \private
int8_t SPI_WIT_PDU_Decode(PARSECS_WIT_PDU *pdu, uint8_t *data_buffer, uint16_t data_length)
{
	uint16_t i = 0;
	uint16_t j = 0;
	if ( (data_buffer == NULL) || (pdu == NULL) )
	{
		return PARSECS_PROTOCOL_ERROR_PDU_DECODE_NULL_POINTER;
	}
	if (data_length < 4)
	{
		return PARSECS_PROTOCOL_ERROR_PDU_DECODE_PDU_TOO_SMALL;
	}
	memset(pdu, 0x00, sizeof(PARSECS_WIT_PDU));
	// decode field: WIT PDU Type Format (byte 0)
	pdu->MultiPacketPdu = (( data_buffer[i] & (1 << 0)) != 0) ? true : false;
	pdu->LastPduInMultipacket = (( data_buffer[i] & (1 << 1)) != 0) ? true : false;
	pdu->SequenceError  = (( data_buffer[i] & (1 << 2)) != 0) ? true : false;
	pdu->SizeExceeded = (( data_buffer[i] & (1 << 3)) != 0) ? true : false;
	pdu->Error = (( data_buffer[i] & (1 << 7)) != 0) ? true : false;
	i++;
	
	// decode field: total PDU packets (byte 1)
	pdu->TotalPDUPackets = data_buffer[i++];
	if (pdu->TotalPDUPackets < 1)
	{
		return PARSECS_PROTOCOL_ERROR_PDU_DECODE_INCORRECT_TOTAL_PDU_PACKETS;
	}
	
	// decode field: current PDU packet (byte 2)
	pdu->CurentPDUPacket = data_buffer[i++];
	if ((pdu->CurentPDUPacket < 1) || (pdu->CurentPDUPacket > pdu->TotalPDUPackets))
	{
		return PARSECS_PROTOCOL_ERROR_PDU_DECODE_INCORRECT_CURRENT_PDU_PACKET_NUMBER;
	}
	
	// decode field: PDU data length (byte 3)
	pdu->PDUDataLength = data_buffer[i++];
	if (data_length < (i + pdu->PDUDataLength))
	{
		return PARSECS_PROTOCOL_ERROR_PDU_DECODE_INSUFFICIENT_DATA_SIZE;
	}
	
	// decode filed: PDU Data
	for (j = 0; j < pdu->PDUDataLength; j++, i++)
	{
		pdu->PDUData[j] = data_buffer[i];
	}
	
	return PARSECS_PROTOCOL_OK;
}

//! Function used to encode a PARSECS_WIT_PDU structure into a aw byte buffer
//!
//! \param pdu A pointer to the memory zone of The PARSECS_WIT_PDU that will be encoded into the raw byte buffer
//! \param[out] data_buffer The raw byte buffer which will hold the encoded PARSECS_WIT_PDU
//! \param[out] data_length The length of the data that was written into parameter data_buffer after the encoding process
//! \return PARSECS_PROTOCOL_OK in case the decoding process was succesfull
//! \return PARSECS_PROTOCOL_ERROR_PDU_DECODE_NULL_POINTER in the case the given parameters are NULL pointers
//! \private
int8_t SPI_WIT_PDU_Encode(PARSECS_WIT_PDU *pdu, uint8_t *data_buffer, uint16_t *data_length)
{
	uint32_t index = 0;
	uint8_t temp = 0;
	uint16_t i = 0;
	if ( (data_buffer == NULL) || (pdu == NULL) )
	{
		return -1;
	}
	
	// encode field: WIT PDU Type Format (byte 0)	
	temp |= (pdu->MultiPacketPdu) ? (1 << 0) : 0;
	temp |= (pdu->LastPduInMultipacket) ? (1 << 1) : 0;
	temp |= (pdu->SequenceError) ? (1 << 2) : 0;
	temp |= (pdu->SizeExceeded) ? (1 << 3) : 0;
	temp |= (pdu->Error) ? (1 << 7) : 0;
	data_buffer[index++] = temp;
	
	// encode field: total PDU packets (byte 1)
	data_buffer[index++] = pdu->TotalPDUPackets;
	
	// encode field: current PDU packet (byte 2)
	data_buffer[index++] = pdu->CurentPDUPacket;
	
	// encode field: PDU data length (byte 3)
	data_buffer[index++] = pdu->PDUDataLength;
	
	// encode filed: PDU data;
	for (i = 0; i < pdu->PDUDataLength; i++, index++)
	{
		data_buffer[index] = pdu->PDUData[i];
	}
	
	*data_length = index;
	return PARSECS_PROTOCOL_OK;
}

//! This function checks if there is an error signaled in a ReceivePDU of type \ref PARSECS_WIT_PDU and return the error as a \ref PARSECS_PROTOCOL_STATUS
//!
//! \param protocolDescriptor A pointer to the PARSECS_PROTOCOL_Descriptor_Type structure of the protocol
//! \param communicationInterface A pointer ot the PARSECS_Protocol_Communication_Interface of the protocol
//! \return PARSECS_PROTOCOL_OK no error was found
//! \return PARSECS_PROTOCOL_ERROR_RECEIVED_SEQUENCING_ERROR a sequencing error was found. The flag SequenceError in the ReceivePDU was set
//! \return PARSECS_PROTOCOL_ERROR_RECEIVED_SEQUENCING_ERROR a size exceed error was found. The flag SizeExceeded in the ReceivePDU was set
//! \return PARSECS_PROTOCOL_ERROR_UNKNOWN_ERROR a generic error was found. The flag Error in the ReceivePDU was set
//! \private
int8_t PARSECS_Protocol_Receive_Error_Handler(PARSECS_PROTOCOL_Descriptor_Type* protocolDescriptor, PARSECS_Protocol_Communication_Interface *communicationInterface)
{
	if (protocolDescriptor->ReceivePDU.SequenceError)
	{
		return PARSECS_PROTOCOL_ERROR_RECEIVED_SEQUENCING_ERROR;
	}
	if (protocolDescriptor->ReceivePDU.SizeExceeded)
	{
		return PARSECS_PROTOCOL_ERROR_DETECTED_SEQUENCING_ERROR;
	}
	if (protocolDescriptor->ReceivePDU.Error)
	{
		if (protocolDescriptor->ReceivePDU.PDUDataLength == 1)
		{
			return protocolDescriptor->ReceivePDU.PDUData[0];
		}
		else
		{
			return PARSECS_PROTOCOL_ERROR_UNKNOWN_ERROR;
		}
	}
	return PARSECS_PROTOCOL_OK;
}

//! This function is used internally to implement the main reception flow of the PARSECS High Level Substack. It is called by the PARSECS High Level Stack implementation.
//! This function is implemented according to the PARSECS Documenation
//!
//! \param protocolDescriptor A pointer to the PARSECS_PROTOCOL_Descriptor_Type structure of the protocol
//! \param communicationInterface A pointer ot the PARSECS_Protocol_Communication_Interface of the protocol
//! \return PARSECS_PROTOCOL_OK no error was found
//! \return A value of type PARSECS_PROTOCOL_STATUS that signals the error state
//! \private
int8_t PARSECS_Protocol_Reception_Handler(PARSECS_PROTOCOL_Descriptor_Type* protocolDescriptor, PARSECS_Protocol_Communication_Interface *communicationInterface)
{
	int8_t status = 0;	
	status = SPI_WIT_PDU_Decode(&protocolDescriptor->ReceivePDU, communicationInterface->receive_raw_data_buffer, communicationInterface->receive_data_size);	
	#ifdef SPI_PROTOCOL_DEBUG
	#ifdef SPI_PROTOCOL_WIT_DEBUG
	LOG_SERIAL ("SPI_WIT <- ");	
	SPI_WIT_PDU_PrintPacket(&protocolDescriptor->ReceivePDU);
	LOG_SERIAL ("\n");
	#endif
	#endif
	if (status != PARSECS_PROTOCOL_OK)
	{
		#ifdef SPI_PROTOCOL_DEBUG
		#ifdef SPI_PROTOCOL_ERROR_DEBUG
		LOG_SERIAL ("SPI :: ");
		SPI_WIT_PrintError((PARSECS_PROTOCOL_STATUS)status);
		#endif
		#endif
		return status;
	}
	// checking possible received errors
	status = PARSECS_Protocol_Receive_Error_Handler(protocolDescriptor, communicationInterface);
	if (status != PARSECS_PROTOCOL_OK)
	{
		protocolDescriptor->Reception.isInMultiPacketState = false;
		protocolDescriptor->Reception.CurrentDataIndex = 0;
		protocolDescriptor->Reception.WITDataLength = 0;	
		protocolDescriptor->Reception.LastPduInMultipacket = false;
		protocolDescriptor->Reception.resourceFree = true;
		#ifdef SPI_PROTOCOL_DEBUG
		#ifdef SPI_PROTOCOL_ERROR_DEBUG
		LOG_SERIAL ("SPI :: ");
		SPI_WIT_PrintError((PARSECS_PROTOCOL_STATUS)status);
		#endif		
		#endif
		return status;
	}
	if (protocolDescriptor->Reception.isInMultiPacketState == true)
	{		
		// currently in multipacket reception
		// check for inconsistancies
		if ((protocolDescriptor->ReceivePDU.LastPduInMultipacket == true) && (protocolDescriptor->ReceivePDU.CurentPDUPacket != protocolDescriptor->ReceivePDU.TotalPDUPackets))
		{
			// incorrect parameters, reset multipacket state and data
			protocolDescriptor->Reception.isInMultiPacketState = false;
			protocolDescriptor->Reception.CurrentDataIndex = 0;
			protocolDescriptor->Reception.WITDataLength = 0;
			#ifdef SPI_PROTOCOL_DEBUG
			#ifdef SPI_PROTOCOL_ERROR_DEBUG
			LOG_SERIAL ("SPI :: ");
			SPI_WIT_PrintError(PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS);
			#endif			
			#endif
			return PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS;
		}
		if ((protocolDescriptor->Reception.CurrentDataIndex + protocolDescriptor->ReceivePDU.PDUDataLength) > PARSECS_WIT_FRAME_LENGTH)
		{
			protocolDescriptor->Reception.isInMultiPacketState = false;
			protocolDescriptor->Reception.CurrentDataIndex = 0;
			protocolDescriptor->Reception.WITDataLength = 0;
			// !!!!!!! SIGNAL HIGHER LAYERS TO TRANSMIT A SIZE_EXCEEDED PACKET !!!!!!!!!!!!!! TBD		
			#ifdef SPI_PROTOCOL_DEBUG
			#ifdef SPI_PROTOCOL_ERROR_DEBUG
			LOG_SERIAL ("SPI :: ");
			SPI_WIT_PrintError(PARSECS_PROTOCOL_ERROR_SEGMENTATION_SIZE_EXCEEDED);
			#endif				
			#endif
			return PARSECS_PROTOCOL_ERROR_SEGMENTATION_SIZE_EXCEEDED;			
		}
		memcpy(&protocolDescriptor->Reception.WITData[protocolDescriptor->Reception.CurrentDataIndex], protocolDescriptor->ReceivePDU.PDUData, protocolDescriptor->ReceivePDU.PDUDataLength);
		protocolDescriptor->Reception.CurrentDataIndex += protocolDescriptor->ReceivePDU.PDUDataLength;	
		protocolDescriptor->Reception.WITDataLength = protocolDescriptor->Reception.CurrentDataIndex;
		if (protocolDescriptor->ReceivePDU.LastPduInMultipacket == true)
		{
			protocolDescriptor->Reception.isInMultiPacketState = false;
			protocolDescriptor->Reception.CurrentDataIndex = 0;
			
			uint32_t index2 = 0;


			if (BERCheck(protocolDescriptor->Reception.WITData, &index2, protocolDescriptor->Reception.WITDataLength) == false)
			{			
				#ifdef SPI_PROTOCOL_DEBUG
				#ifdef SPI_PROTOCOL_ERROR_DEBUG
				LOG_SERIAL ("SPI :: ");
				SPI_WIT_PrintError(PARSECS_PROTOCOL_ERROR_BER_DECODE_ERROR);
				#endif		
				#endif
				return PARSECS_PROTOCOL_ERROR_BER_DECODE_ERROR;
			}
			if (index2 != protocolDescriptor->Reception.WITDataLength)
			{
				#ifdef SPI_PROTOCOL_DEBUG
				#ifdef SPI_PROTOCOL_ERROR_DEBUG
				LOG_SERIAL ("SPI :: ");
				SPI_WIT_PrintError(PARSECS_PROTOCOL_ERROR_BER_ADDITION_JUNK_FOUND);
				#endif						
				#endif
				return PARSECS_PROTOCOL_ERROR_BER_ADDITION_JUNK_FOUND;
			}			
			protocolDescriptor->Reception.resourceFree = false;
					
			return PARSECS_PROTOCOL_OK;
		}
	}
	else
	{
		
		// corrently not in multipacket reception
		protocolDescriptor->Reception.CurrentDataIndex = 0;
		// check if single packet reception or entering multi packet reception state
		if (protocolDescriptor->ReceivePDU.MultiPacketPdu == false)
		{
			// single packet reception
			uint32_t index = 0;
			if (protocolDescriptor->ReceivePDU.TotalPDUPackets != 1)
			{
				return PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS;
			}
			memcpy(protocolDescriptor->Reception.WITData, protocolDescriptor->ReceivePDU.PDUData, protocolDescriptor->ReceivePDU.PDUDataLength);
			protocolDescriptor->Reception.WITDataLength = protocolDescriptor->ReceivePDU.PDUDataLength;
			if (BERCheck(protocolDescriptor->Reception.WITData, &index, protocolDescriptor->Reception.WITDataLength) == false)
			{				
				return PARSECS_PROTOCOL_ERROR_BER_DECODE_ERROR;
			}
			if (index != protocolDescriptor->Reception.WITDataLength)
			{
				#ifdef SPI_PROTOCOL_DEBUG
				#ifdef SPI_PROTOCOL_ERROR_DEBUG
				LOG_SERIAL ("SPI :: ");
				SPI_WIT_PrintError(PARSECS_PROTOCOL_ERROR_BER_ADDITION_JUNK_FOUND);
				#endif						
				#endif
				return PARSECS_PROTOCOL_ERROR_BER_ADDITION_JUNK_FOUND;
			}				
			protocolDescriptor->Reception.resourceFree = false;

			return PARSECS_PROTOCOL_OK;
		}
		else
		{
			// beginning multipacket reception
			if (protocolDescriptor->ReceivePDU.TotalPDUPackets == 1)
			{
				#ifdef SPI_PROTOCOL_DEBUG
				#ifdef SPI_PROTOCOL_ERROR_DEBUG
				LOG_SERIAL ("SPI :: ");
				SPI_WIT_PrintError(PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS);
				#endif					
				#endif
				return PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS;
			}			
			if (protocolDescriptor->ReceivePDU.LastPduInMultipacket == true)
			{
				#ifdef SPI_PROTOCOL_DEBUG
				#ifdef SPI_PROTOCOL_ERROR_DEBUG
				LOG_SERIAL ("SPI :: ");
				SPI_WIT_PrintError(PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS);
				#endif					
				#endif
				return PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS;
			}
			if ((protocolDescriptor->ReceivePDU.CurentPDUPacket + 1) > protocolDescriptor->ReceivePDU.TotalPDUPackets)
			{
				#ifdef SPI_PROTOCOL_DEBUG
				#ifdef SPI_PROTOCOL_ERROR_DEBUG 
				LOG_SERIAL ("SPI :: ");
				SPI_WIT_PrintError(PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS);
				#endif
				#endif
				return PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS;				
			}
			protocolDescriptor->Reception.isInMultiPacketState = true;
			protocolDescriptor->Reception.CurrentDataIndex = 0;
			protocolDescriptor->Reception.TotalPDUPackets = protocolDescriptor->ReceivePDU.TotalPDUPackets;
			protocolDescriptor->Reception.CurentPDUPacket = protocolDescriptor->ReceivePDU.CurentPDUPacket;
			memcpy(protocolDescriptor->Reception.WITData, protocolDescriptor->ReceivePDU.PDUData, protocolDescriptor->ReceivePDU.PDUDataLength);
			protocolDescriptor->Reception.CurrentDataIndex += protocolDescriptor->ReceivePDU.PDUDataLength;			
		}		
	}
	return PARSECS_PROTOCOL_OK;
}

//! This function is used internally to implement the main transmission flow of the PARSECS High Level Substack. It is called by the PARSECS High Level Stack implementation.
//! This function is implemented according to the PARSECS Documenation
//!
//! \param protocolDescriptor A pointer to the PARSECS_PROTOCOL_Descriptor_Type structure of the protocol
//! \param communicationInterface A pointer ot the PARSECS_Protocol_Communication_Interface of the protocol
//! \return PARSECS_PROTOCOL_OK no error was found
//! \return A value of type PARSECS_PROTOCOL_STATUS that signals the error state
//! \private
int8_t PARSECS_Protocol_Transmission_Handler(PARSECS_PROTOCOL_Descriptor_Type* protocolDescriptor, PARSECS_Protocol_Communication_Interface *communicationInterface)
{
	// check if transmission is possible from lower levers
	if (communicationInterface->transmit_data_size < 0)
	{
		// check if resource is busy -> if data needs to be transmitted
		if (protocolDescriptor->Transmission.resourceFree == false)
		{		
			// check if already in segmentation transmission mode
			if (protocolDescriptor->Transmission.isInMultiPacketState)
			{
				// I am already in multi packet transmission mode. Need to compute and send the next segment
				// is this the last segment
				if ((protocolDescriptor->Transmission.CurentPDUPacket) == protocolDescriptor->Transmission.TotalPDUPackets)
				{
					uint32_t size_to_transmit = 0;
					// this is the last segment
					protocolDescriptor->TransmitPDU.LastPduInMultipacket = true;
					protocolDescriptor->Transmission.isInMultiPacketState = false;
					protocolDescriptor->TransmitPDU.MultiPacketPdu = true;
					size_to_transmit = protocolDescriptor->Transmission.WITDataLength - ((protocolDescriptor->Transmission.CurentPDUPacket - 1) * PARSECS_WIT_PDU_DATA_SIZE);
					memcpy(protocolDescriptor->TransmitPDU.PDUData, &protocolDescriptor->Transmission.WITData[protocolDescriptor->Transmission.CurrentDataIndex], size_to_transmit);				
					protocolDescriptor->TransmitPDU.PDUDataLength = protocolDescriptor->Transmission.WITDataLength - ((protocolDescriptor->Transmission.CurentPDUPacket - 1) * PARSECS_WIT_PDU_DATA_SIZE);
					//protocolDescriptor->Transmission.CurentPDUPacket++;
					protocolDescriptor->TransmitPDU.SequenceError = false;
					protocolDescriptor->TransmitPDU.SizeExceeded = false;
					protocolDescriptor->TransmitPDU.TotalPDUPackets = protocolDescriptor->Transmission.TotalPDUPackets;
					protocolDescriptor->TransmitPDU.CurentPDUPacket = protocolDescriptor->Transmission.CurentPDUPacket;
					protocolDescriptor->Transmission.resourceFree = true;
					#ifdef SPI_PROTOCOL_DEBUG
					#ifdef SPI_PROTOCOL_WIT_DEBUG
					LOG_SERIAL ("SPI_WIT -> ");	
					SPI_WIT_PDU_PrintPacket(&protocolDescriptor->TransmitPDU);
					LOG_SERIAL ("\n");
					#endif
					#endif		
				}
				else
				{
					// this is not the last segment
					protocolDescriptor->TransmitPDU.LastPduInMultipacket = false;				
					protocolDescriptor->TransmitPDU.MultiPacketPdu = true;				
					memcpy(protocolDescriptor->TransmitPDU.PDUData, &protocolDescriptor->Transmission.WITData[protocolDescriptor->Transmission.CurrentDataIndex], PARSECS_WIT_PDU_DATA_SIZE);				
					protocolDescriptor->Transmission.CurrentDataIndex += PARSECS_WIT_PDU_DATA_SIZE;				
					protocolDescriptor->TransmitPDU.CurentPDUPacket = protocolDescriptor->Transmission.CurentPDUPacket;				
					protocolDescriptor->TransmitPDU.SequenceError = false;
					protocolDescriptor->TransmitPDU.SizeExceeded = false;
					protocolDescriptor->TransmitPDU.TotalPDUPackets = protocolDescriptor->Transmission.TotalPDUPackets;
					protocolDescriptor->TransmitPDU.PDUDataLength = PARSECS_WIT_PDU_DATA_SIZE;
					protocolDescriptor->Transmission.CurentPDUPacket++;
					#ifdef SPI_PROTOCOL_DEBUG
					#ifdef SPI_PROTOCOL_WIT_DEBUG
					LOG_SERIAL ("SPI_WIT -> ");	
					SPI_WIT_PDU_PrintPacket(&protocolDescriptor->TransmitPDU);
					LOG_SERIAL ("\n");
					#endif
					#endif			
				}
			}
			else
			{
				// I am not in multi packet transmission mode -> need to start a new clean transmisssion
				// check if data segmentation is needed
				if (protocolDescriptor->Transmission.WITDataLength > PARSECS_WIT_PDU_DATA_SIZE)
				{
					//data segmentation is needed
					protocolDescriptor->Transmission.isInMultiPacketState = true;
					protocolDescriptor->Transmission.TotalPDUPackets = ((protocolDescriptor->Transmission.WITDataLength) / PARSECS_WIT_PDU_DATA_SIZE) + (((protocolDescriptor->Transmission.WITDataLength % PARSECS_WIT_PDU_DATA_SIZE) == 0) ? 0 : 1);
					protocolDescriptor->Transmission.CurentPDUPacket = 1;
					protocolDescriptor->Transmission.CurrentDataIndex = 0;
					
					
					protocolDescriptor->TransmitPDU.MultiPacketPdu = true;
					protocolDescriptor->TransmitPDU.LastPduInMultipacket = false;
					protocolDescriptor->TransmitPDU.CurentPDUPacket = 1;
					protocolDescriptor->TransmitPDU.SequenceError = false;
					protocolDescriptor->TransmitPDU.SizeExceeded = false;
					
					
					protocolDescriptor->TransmitPDU.TotalPDUPackets = protocolDescriptor->Transmission.TotalPDUPackets;
					memcpy(protocolDescriptor->TransmitPDU.PDUData, &protocolDescriptor->Transmission.WITData[protocolDescriptor->Transmission.CurrentDataIndex], PARSECS_WIT_PDU_DATA_SIZE);
					protocolDescriptor->Transmission.CurrentDataIndex += PARSECS_WIT_PDU_DATA_SIZE;
					protocolDescriptor->Transmission.CurentPDUPacket++;
					protocolDescriptor->TransmitPDU.PDUDataLength = PARSECS_WIT_PDU_DATA_SIZE;	
					#ifdef SPI_PROTOCOL_DEBUG
					#ifdef SPI_PROTOCOL_WIT_DEBUG
					LOG_SERIAL ("SPI_WIT -> ");	
					SPI_WIT_PDU_PrintPacket(&protocolDescriptor->TransmitPDU);
					LOG_SERIAL ("\n");
					#endif				
					#endif
				}
				else
				{	
					//data segmentation is not needed				
					protocolDescriptor->TransmitPDU.MultiPacketPdu = false;
					protocolDescriptor->TransmitPDU.SizeExceeded = false;
					protocolDescriptor->TransmitPDU.LastPduInMultipacket = false;
					protocolDescriptor->TransmitPDU.TotalPDUPackets = 1;
					protocolDescriptor->TransmitPDU.CurentPDUPacket = 1;				
					protocolDescriptor->Transmission.resourceFree = true;
					memcpy(protocolDescriptor->TransmitPDU.PDUData, protocolDescriptor->Transmission.WITData, protocolDescriptor->Transmission.WITDataLength);
					protocolDescriptor->TransmitPDU.PDUDataLength = protocolDescriptor->Transmission.WITDataLength;
					#ifdef SPI_PROTOCOL_DEBUG
					#ifdef SPI_PROTOCOL_WIT_DEBUG
					LOG_SERIAL ("SPI_WIT -> ");	
					SPI_WIT_PDU_PrintPacket(&protocolDescriptor->TransmitPDU);
					LOG_SERIAL ("\n");
					#endif												
					#endif					
				}			
			}	
			SPI_WIT_PDU_Encode(&protocolDescriptor->TransmitPDU, communicationInterface->transmit_raw_data_buffer, (uint16_t*)&communicationInterface->transmit_data_size);
		}
	}	
	return PARSECS_PROTOCOL_OK;
}

//! This function is used internally to implement the main flow of the PARSECS High Level Substack. It is called by the PARSECS High Level Stack implementation.
//! This function is implemented according to the PARSECS Documenation
//!
//! \param protocolDescriptor A pointer to the PARSECS_PROTOCOL_Descriptor_Type structure of the protocol
//! \param communicationInterface A pointer to the PARSECS_Protocol_Communication_Interface of the protocol
//! \return PARSECS_PROTOCOL_OK no error was found
//! \return A value of type PARSECS_PROTOCOL_STATUS that signals the error state
//! \private
int8_t PARSECS_Protocol_Handler(PARSECS_PROTOCOL_Descriptor_Type* protocolDescriptor, PARSECS_Protocol_Communication_Interface *communicationInterface)
{	
	// first update status
	int8_t return_status = 0;
	// check why this method has been called by the lower level
	switch (communicationInterface->command)
	{
		case PARSECS_PROTOCOL_COMMAND_IO_OPERATION:
		{
			// new packet was received			

			
			if (communicationInterface->receive_data_size > 0)
			{
				// new packet was really received
				#ifdef SPI_PROTOCOL_DEBUG
				#ifdef SPI_PROTOCOL_INFO_DEBUG
				LOG_SERIAL ("SPI :: seq  TX - %d, RX - %d, ACK - %d, NACK - %d\n", communicationInterface->transmit_seq_number, communicationInterface->receive_seq_number, communicationInterface->last_ACKed_packet_sequence_number, communicationInterface->last_NACKed_packet_sequence_number); 	
				#endif				
				#endif
				PARSECS_Protocol_Reception_Handler(protocolDescriptor, communicationInterface);				
			}
			// transmission is also possible if needed
			PARSECS_Protocol_Transmission_Handler(protocolDescriptor, communicationInterface);
			break;
		}
		case PARSECS_PROTOCOL_COMMAND_TRANSMIT_OPERATION_ONLY:
		{
			// transmission is possible if needed
			#ifdef SPI_PROTOCOL_DEBUG
			#ifdef SPI_PROTOCOL_INFO_DEBUG
			if ((communicationInterface->transmit_data_size < 0) && (protocolDescriptor->Transmission.resourceFree == false))
			{				
				LOG_SERIAL ("SPI :: seq  TX - %d, RX - %d, ACK - %d, NACK - %d\n", communicationInterface->transmit_seq_number, communicationInterface->receive_seq_number, communicationInterface->last_ACKed_packet_sequence_number, communicationInterface->last_NACKed_packet_sequence_number); 								
			}
			#endif
			#endif				
			PARSECS_Protocol_Transmission_Handler(protocolDescriptor, communicationInterface);
			break;
		}			
		default:
		{
			//nothing to do. Why was I called ?
			break;
		}
	}
	
	return return_status;
}

//! This function is used internally check and extract the data encoding (as BER) according to the Presentation Layer presented in the PARSECS Documentation
//! This function is implemented according to the PARSECS Documenation. This function actually decodes a PARSECS_WIT_FRAME and a PARSECS_APP_API
//!
//! \param ber_data_buffer A pointer to the byte buffer containing the encoded data as BER that should be decoded as a PARSECS_WIT_FRAME and a PARSECS_APP_API
//! \param ber_data_buffer_length The size of the data_buffer
//! \param[out] SourceAddress The decoded SourceAddress field of the PARSECS_WIT_FRAME will be written into this parameter
//! \param[out] DestinationAddress The decoded DestinationAddress field of the PARSECS_WIT_FRAME will be written into this parameter
//! \param[out] OperationType The decoded OperationType field of the PARSECS_APP_API will be written into this parameter
//! \param[out] OperationControl The decoded OperationControl field of the PARSECS_APP_API will be written into this parameter
//! \param[out] TypeID The decoded TypeID field of the PARSECS_APP_API will be written into this parameter
//! \param[out] ber_app_data_buffer The raw OperationData field encoded as BER will be written into this memory zone in order to be decoded further by the application layer
//! \param[out] ber_app_data_buffer_length The size of the data that was written into ber_app_data_buffer
//! \return PARSECS_PROTOCOL_OK no error was found
//! \return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT if a BER encoding error was found
//! \return PARSECS_PROTOCOL_ERROR_INCORRECT_APP_PARAMETERS if the BER encoding was successful but the decoded data does not match the format of the PARSECS_WIT_FRAME and PARSECS_APP_API
//! \private
int8_t PARSECS_Protocol_PresentationApplication_Layer_Receive_Handler(uint8_t* ber_data_buffer, uint32_t ber_data_buffer_length, uint8_t *SourceAddress, uint8_t *DestinationAddress, PARSECS_APP_OperationType *OperationType, uint8_t *OperationControl, uint8_t *TypeID, uint8_t *ber_app_data_buffer, uint32_t *ber_app_data_buffer_length)
{
	uint32_t index = 0;
	uint16_t structureLength = 0;
	uint8_t tempDecode = 0;
//	#ifdef SPI_PROTOCOL_DEBUG
//	uint32_t index2 = 0;
//	LOG_SERIAL ("SPI_BER <- ");	
//	BERPrintEncodedBufferAsBer(ber_data_buffer, &index2, ber_data_buffer_length);
//	LOG_SERIAL ("\n");	
//	#endif	
	if (ber_data_buffer_length < 7)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}	
	if (BERStructureDecode(&structureLength, ber_data_buffer, &index, ber_data_buffer_length) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}
	if (structureLength != 3)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}
	if (BERUnsignedDecode(SourceAddress, ber_data_buffer, &index, ber_data_buffer_length) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}
	if (BERUnsignedDecode(DestinationAddress, ber_data_buffer, &index, ber_data_buffer_length) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}
	if (BERStructureDecode(&structureLength, ber_data_buffer, &index, ber_data_buffer_length) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}
	if (structureLength != 4)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}
	if (BEREnumDecode(&tempDecode, ber_data_buffer, &index, ber_data_buffer_length) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}
	if (SPI_WIT_ValidateAppOperationType(tempDecode) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_APP_PARAMETERS;
	}
	*OperationType = (PARSECS_APP_OperationType)tempDecode;
	if (BEREnumDecode(&tempDecode, ber_data_buffer, &index, ber_data_buffer_length) == false)
	{
		if (BERNullDataDecode(NULL, ber_data_buffer, &index, ber_data_buffer_length) == false)
		{
			return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
		}
		else
		{
			tempDecode = 0;
		}
	}
	if (SPI_WIT_ValidateAppResponse(tempDecode) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_APP_PARAMETERS;
	}
	*OperationControl = tempDecode;
	if (BEREnumDecode(&tempDecode, ber_data_buffer, &index, ber_data_buffer_length) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}
	*TypeID = tempDecode;
	memcpy(ber_app_data_buffer, &ber_data_buffer[index], ber_data_buffer_length - index);
	*ber_app_data_buffer_length = ber_data_buffer_length - index;
	
	return PARSECS_PROTOCOL_OK;
}

//! This function is used internally to encode as BER aa PARSECS_WIT_FRAME and a PARSECS_APP_API according to the Presentation Layer presented in the PARSECS Documentation
//! \param[out] encoded_ber_data_buffer A buffer memory zone where the encoded data will be written
//! \param[out] encoded_ber_data_buffer_length The length of the data that was written into encoded_ber_data_buffer
//! \param SourceAddress The SourceAddress field of the PARSECS_WIT_FRAME to be encoded
//! \param DestinationAddress The DestinationAddress field of the PARSECS_WIT_FRAME to be encoded
//! \param OperationType The OperationType field of the PARSECS_APP_API to be encoded
//! \param OperationControl The OperationControl field of the PARSECS_WIT_FRAME to be encoded
//! \param TypeID The TypeID field of the PARSECS_WIT_FRAME to be encoded
//! \param ber_app_data_buffer The raw data buffer of the OperationData field encoded as BER
//! \param ber_app_data_buffer_length The length of the raw data buffer of the OperationData field encoded as BER
//! \private
int8_t PARSECS_Protocol_PresentationApplication_Layer_Transmit_Handler(uint8_t* encoded_ber_data_buffer, uint32_t *encoded_ber_data_buffer_length, uint8_t SourceAddress, uint8_t DestinationAddress, PARSECS_APP_OperationType OperationType, uint8_t OperationControl, uint8_t TypeID, const uint8_t *ber_app_data_buffer, uint32_t ber_app_data_buffer_length)
{
	uint32_t index = 0;
	uint32_t index2 = 0;

	if (BERCheck(ber_app_data_buffer, &index2, ber_app_data_buffer_length) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT;
	}
	if (index2 != ber_app_data_buffer_length)
	{
		return PARSECS_PROTOCOL_ERROR_BER_ADDITION_JUNK_FOUND;
	}
	if ((ber_app_data_buffer_length + index) > PARSECS_WIT_FRAME_LENGTH)
	{
		return PARSECS_PROTOCOL_ERROR_SIZE_EXCEEDED;
	}
	if (SPI_WIT_ValidateAppOperationType(OperationType) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_APP_PARAMETERS;
	}
	if (SPI_WIT_ValidateAppResponse(OperationControl) == false)
	{
		return PARSECS_PROTOCOL_ERROR_INCORRECT_APP_PARAMETERS;
	}
	// encode presentation layer
	BERStructureEncode(3, encoded_ber_data_buffer, &index);
	BERUnsignedEncode(SourceAddress, encoded_ber_data_buffer, &index);
	BERUnsignedEncode(DestinationAddress, encoded_ber_data_buffer, &index);

	BERStructureEncode(4, encoded_ber_data_buffer, &index);
	BEREnumEncode(OperationType, encoded_ber_data_buffer, &index);
	if (OperationControl == 0xFF)
	{
		BERNullDataEncode(encoded_ber_data_buffer, &index);
	}
	else
	{
		BEREnumEncode(OperationControl, encoded_ber_data_buffer, &index);
	}
	BEREnumEncode(TypeID, encoded_ber_data_buffer, &index);

	memcpy(&encoded_ber_data_buffer[index], ber_app_data_buffer, ber_app_data_buffer_length);
	*encoded_ber_data_buffer_length = ber_app_data_buffer_length + index;
	index = 0;
//	#ifdef SPI_PROTOCOL_DEBUG
//	LOG_SERIAL ("SPI_BER -> ");
//	BERPrintEncodedBufferAsBer(encoded_ber_data_buffer, &index, *encoded_ber_data_buffer_length);
//	LOG_SERIAL ("\n");
//	#endif
	
	return PARSECS_PROTOCOL_OK;
}


//! @}

//! @}
