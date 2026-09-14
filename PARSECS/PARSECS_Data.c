//! \file	PARSECS_Data.c
//!
//! \brief	PARSECS Data Structures definitions
//!
//! Contains the definitions of the data structures and variables used by PARSECS_RT Stack borth PARSECS Low Level Substack and PARSECS High Level Substack
//! The Master node in this implementation is an STM32 I2C master; the slave is an STM32 I2C slave.
//! Only the implementation for Layer 1 differs from Master Node to Slave Node, being node and platform dependent
//! The rest of the layer implementations are platform and node independent
//! \addtogroup PARSECS_RT
//! @{
#include "PARSECS_Data.h"
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>
#include <ringbuf.h>
#include "crc16.h"
#include <stdlib.h>
#include <string.h>

/*! \var slaves
Contains the array of slave modules used by the PARSECS Low Level Substack to operate on. This array is not handled directly by the user.
\private
*/
SLAVE slaves[SLAVE_COUNT];
#ifdef SPI_MASTER
/*! \var number_of_active_slaves
A private variable containing the current number of configured slaves in the PARSECS stack. This variable is updated when the users calls the function \ref PARSECS_Add_Slave
\private
*/
uint8_t number_of_active_slaves = 0;
/*! \var slave_id_comm_board
Deprecated. Unused. Will be eliminated at the next version
\private
*/
int8_t slave_id_comm_board = 0;
#endif

/*! \var CORE_TX_Wit_Boards
Contains the array of the WIT boards modules used by the PARSECS High Level Substack to operate on. This array is not handled directly by the user.
\private
*/
PARSECS_PROTOCOL_BOARD_DESCRIPTOR CORE_TX_Wit_Boards[MAX_BOARD_COUNT];


//! Function used to initialize the Ring Buffer for Layer 1 for the specified slave device
//!
//! This function is not called directly by the user
//! \param slave A pointer to a SLAVE structure that identifies the slave that this function will work on.
//! \return None
//! \private
void PARSECS_InitRingBuffers(SLAVE *slave)
{
	RingBufInit(&slave->l1_buffer_rx, slave->buff_rx1, RAW_RING_BUFFER_SIZE);//initializez buffer-ul circular buffer rx
	RingBufInit(&slave->l1_buffer_tx, slave->buff_tx1, RAW_RING_BUFFER_SIZE);//initializez buffer-ul circular buffer tx
}

//! Function used to initialize all the internal structures and variables of the PARSECS Stack
//!
//! This function is not called directly by the user
//! \return None
//! \private
void PARSECS_Data_Init()
{
	uint32_t i = 0;
	for (i = 0; i < SLAVE_COUNT; i++)
	{
		memset(&slaves[i], 0, sizeof(SLAVE));
		slaves[i].slave_enabled = 0;
		#ifndef SPI_MASTER
		PARSECS_InitRingBuffers(&slaves[i]);
		CRC16_constructor(&slaves[i].spi_packet_rx_1.crc16_object);			// instantiez crc-ul pentru receptie
		CRC16_constructor(&slaves[i].spi_packet_rx_2.crc16_object);			// instantiez crc-ul pentru receptie
		CRC16_constructor(&slaves[i].spi_packet_tx.crc16_object);			//instantiez crc-ul pentru transmisie
		slaves[i].slave_enabled = 1;
		#endif
	}
	#ifdef SPI_MASTER
	number_of_active_slaves = 0;
	for (i = 0; i < MAX_BOARD_COUNT; i++)
	{
		memset(&CORE_TX_Wit_Boards[i], 0, sizeof(PARSECS_PROTOCOL_BOARD_DESCRIPTOR));
		CORE_TX_Wit_Boards[i].boardAvailable = false;
	}
	#endif
}

//! @}
