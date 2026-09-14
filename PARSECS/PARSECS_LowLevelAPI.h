//! \file	PARSECS_LowLevelAPI.h
//!
//! \brief	PARSECS Low Level Task API
//!
//! Contains the implementation of the public API for the PARSECS Low Level Substack.
//! In case one needs to use only the PARSECS Low level Substack then he should use this API.
//! This API is also called by the PARSECS High Level Substack
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup PARSECS-Low-Level-Substack
//! @{
//! \brief PARSECS Low Level Substack
//!
//! Contains the implementation of the PARSECS Low Level Substack containing Layer 1, Layer 2 and Layer 3
#ifndef __PARSECS_LOW_LEVEL_API
#define __PARSECS_LOW_LEVEL_API

#include "PARSECS_Data.h"

int8_t PARSECS_TRANSMIT_APP(uint8_t slave_id ,uint8_t *data, uint8_t data_length, uint8_t *sent_sequence_number);//layer aplicatie transmisie
int8_t PARSECS_RECEIVE_APP(uint8_t slave_id, uint8_t *data, uint8_t *data_length, uint8_t *sequence_number);//layer aplicatie receptie
int8_t PARSECS_CHECK_APP_TX_READY(uint8_t slave_id);

#ifdef SPI_MASTER
int8_t PARSECS_Add_Slave(SPI_SLAVE_FUNCTION SelectFunctionPointer, SPI_SLAVE_FUNCTION DeselectFunctionPointer);//adaug slave
#endif

uint8_t PARSECS_GetApp_tx_buffer_max_length(void);
uint8_t PARSECS_GetApp_rx_buffer_max_length(void);

int16_t PARSECS_Get_last_ACKed_packet_sequence_number(uint8_t slave_id);
int16_t PARSECS_Get_last_NACKed_packet_sequence_number(uint8_t slave_id);


#endif

//! @}

//! @}
