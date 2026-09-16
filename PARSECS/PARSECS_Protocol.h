//! \file	PARSECS_Protocol.h
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
#ifndef __PARSECS_PROTOCOL_H
#define __PARSECS_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#include "PARSECS_Data.h"




int8_t PARSECS_Protocol_Handler(PARSECS_PROTOCOL_Descriptor_Type* protocolDescriptor, PARSECS_Protocol_Communication_Interface *communicationInterface);
void PARSECS_ProtocolDescriptorInit(PARSECS_PROTOCOL_Descriptor_Type *protocolDescriptor);
void PARSECS_ProtocolCommunicationInterfaceInit(PARSECS_Protocol_Communication_Interface *protocolCommunicationInterface);
int8_t PARSECS_Protocol_PresentationApplication_Layer_Transmit_Handler(uint8_t* encoded_ber_data_buffer, uint32_t *encoded_ber_data_buffer_length, uint8_t SourceAddress, uint8_t DestinationAddress, PARSECS_APP_OperationType OperatinType, uint8_t OperationControl, uint8_t TypeID, const uint8_t *ber_app_data_buffer, uint32_t ber_app_data_buffer_length);
int8_t PARSECS_Protocol_PresentationApplication_Layer_Receive_Handler(uint8_t* ber_data_buffer, uint32_t ber_data_buffer_length, uint8_t *SourceAddress, uint8_t *DestinationAddress, PARSECS_APP_OperationType *OperationType, uint8_t *OperationControl, uint8_t *TypeID, uint8_t *ber_app_data_buffer, uint32_t *ber_app_data_buffer_length);

void SPI_WIT_PrintError(PARSECS_PROTOCOL_STATUS error);
void SPI_WIT_PDU_PrintPacket(PARSECS_WIT_PDU *pdu);
void SPI_WIT_PrintError(PARSECS_PROTOCOL_STATUS error);
void SPI_WIT_PrintAppOperationType(PARSECS_APP_OperationType operationType);
void SPI_WIT_PrintAppResponse(PARSECS_APP_OperationResponse response);

#endif


//! @}

//! @}
