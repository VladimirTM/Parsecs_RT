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
A private variable containing the number of slaves the master currently polls (hot-plug state LINKING or ACTIVE). Updated by the hot-plug code in PARSECS_HotPlug.c
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

//! Clears the Layer 3 ARQ state of one slave: drops any frame waiting for an ACK, forgets the last
//! delivered RX SEQ and marks the link up. Diagnostic counters and l1_rounds are kept.
//!
//! This function is not called directly by the user
//! \param slave A pointer to a SLAVE structure that identifies the slave that this function will work on.
//! \return None
//! \private
void PARSECS_ARQ_Reset(SLAVE *slave)
{
	slave->STATUS_FLAG &= ~(FG_ARQ_WAIT_ACK | FG_ARQ_RETX);
	slave->arq_length = 0;
	slave->arq_seq = 0;
	slave->arq_retries = 0;
	slave->arq_rto = ARQ_RTO_ROUNDS;
	slave->arq_elapsed = 0;
	slave->arq_last_round = slave->l1_rounds;
	slave->link_state = PARSECS_LINK_UP;
	slave->last_delivered_rx_seq = 0;
	slave->rx_seq_valid = false;
	slave->arq_type = TYPE_DATA;
}

//! Restarts the link state of one peer after it asked for it with a \ref PARSECS_CMD_LINK_RESET frame.
//!
//! Called by Layer 3 RX (task context). The Layer 1 rings and the Layer 2 TX state machine are left
//! alone, because the Layer 1 ISR may be in the middle of a byte: whatever Layer 2 is still sending
//! goes out, but is marked \ref FG_ARQ_STALE_TX so it is not waited for. DATA already handed to the
//! High Level and not read yet is dropped. The CMD SEQ becomes the last delivered SEQ, so a resent
//! CMD is not taken for the peer's first DATA frame.
//! \param slave The peer that sent the CMD
//! \param cmd_seq SEQ of the CMD frame
//! \return None
//! \private
void PARSECS_Link_Reset_Local(SLAVE *slave, uint8_t cmd_seq)
{
	if ((slave->STATUS_FLAG & FG_HL_NDF_1) != 0)
	{
		slave->spi_packet_rx_1.packet_ok = 0;
	}
	if ((slave->STATUS_FLAG & FG_HL_NDF_2) != 0)
	{
		slave->spi_packet_rx_2.packet_ok = 0;
	}
	slave->STATUS_FLAG &= ~(FG_ARQ_WAIT_ACK | FG_ARQ_RETX | FG_HL_FTTR | FG_ACK | FG_NACK | FG_HL_NDF_1 | FG_HL_NDF_2);
	if ((slave->STATUS_FLAG & (FG_LL_FTT | FG_LL_FTIP)) != 0)
	{
		slave->STATUS_FLAG |= FG_ARQ_STALE_TX;
	}
	PARSECS_ARQ_Reset(slave);
	slave->last_delivered_rx_seq = cmd_seq;
	slave->rx_seq_valid = true;
	slave->link_sync_pending = false;
	slave->link_resets++;
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
		PARSECS_ARQ_Reset(&slaves[i]);
		#ifndef SPI_MASTER
		PARSECS_InitRingBuffers(&slaves[i]);
		CRC16_constructor(&slaves[i].spi_packet_rx_1.crc16_object);			// instantiez crc-ul pentru receptie
		CRC16_constructor(&slaves[i].spi_packet_rx_2.crc16_object);			// instantiez crc-ul pentru receptie
		CRC16_constructor(&slaves[i].spi_packet_tx.crc16_object);			//instantiez crc-ul pentru transmisie
		slaves[i].slave_enabled = 1;
		slaves[i].hp_state = PARSECS_SLAVE_ACTIVE;
		#else
		slaves[i].hp_state = PARSECS_SLAVE_ABSENT;
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
