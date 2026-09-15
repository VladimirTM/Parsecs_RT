//! \file	PARSECS_Protocol_Interface.h
//!
//! \brief	PARSECS High Level Substack Main implementation files for the public user interface
//!
//! Contains the implementation of the PARSECS High Level Substack interface that may be called by the user in certain conditions
//! The layer implementations is platform and node independent
//! \defgroup PARSECS_RT PARSECS_RT
//! @{
//! \addtogroup PARSECS-High-Level-Substack
//! @{
//! \brief PARSECS High Level Substack
//! Contains the implementation of the PARSECS High Level Substack interface that may be called by the user in certain conditions
#ifndef __PARSECS_PROTOCOL_INTERFACE_H
#define __PARSECS_PROTOCOL_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

#include "PARSECS_Protocol.h"

int8_t PARSECS_Protocol_USER_Receive(PARSECS_APP_BOARD_ADDRESS boardAddress, uint8_t *SourceAddress, uint8_t *DestinationAddress, PARSECS_APP_OperationType *OperationType, uint8_t *OperationControl, uint8_t *TypeID, uint8_t *GenericDataAsBER, uint32_t *GenericDataLength);
int8_t PARSECS_Protocol_USER_Send(PARSECS_APP_BOARD_ADDRESS boardAddress, uint8_t SourceAddress, uint8_t DestinationAddress, PARSECS_APP_OperationType OperationType, uint8_t OperationControl, uint8_t TypeID, const uint8_t *GenericDataAsBER, uint32_t GenericDataLength, uint32_t *resultedPacketSize);

void PARSECS_Protocol_Interface_Task_Init(void);

void PARSECS_Protocol_Interface_Task_Slave(PARSECS_PROTOCOL_BOARD_DESCRIPTOR *wit_board);

#endif

//! @}

//! @}
