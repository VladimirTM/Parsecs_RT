//! \file	PARSECS_Layer3.c
//!
//! \brief	PARSECS Layer 3
//!
//! Contains the implementation of the PARSECS Layer 3
//! The implementation for PARSECS Layer 3 is platform and node independent
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup PARSECS-Low-Level-Substack
//! @{
//! \brief PARSECS Low Level Substack
//!
//! Contains the implementation of the PARSECS Low Level Substack containing Layer 1, Layer 2 and Layer 3
//!
//! Layer 3 runs a stop-and-wait ARQ per slave: one DATA frame is kept in arq_buffer until the peer
//! ACKs its SEQ. A NACK or a timeout (counted in Layer 1 rounds of that slave) resends it with the
//! same SEQ, up to \ref ARQ_MAX_RETRIES times, after which the frame is dropped and the link is
//! reported \ref PARSECS_LINK_DOWN. The receiving side re-ACKs a repeated SEQ without delivering it again.
#include "PARSECS_Layer3.h"

static bool PARSECS_L2_TX_Idle(SLAVE *slave)
{
	return (slave->STATUS_FLAG & (FG_LL_FTT | FG_LL_FTIP)) == 0;
}

static bool PARSECS_RX_Frame_In_Progress(SLAVE *slave)
{
	return slave->spi_packet_rx_1.frame_in_progress || slave->spi_packet_rx_2.frame_in_progress;
}

//! Hands the stored ARQ frame to Layer 2
static void PARSECS_ARQ_Load_Frame(SLAVE *slave)
{
	memcpy(slave->spi_packet_tx.app_buffer, slave->arq_buffer, slave->arq_length);
	slave->spi_packet_tx.app_buffer_length = slave->arq_length;
	slave->spi_packet_tx.seq_no = slave->arq_seq;
	slave->spi_packet_tx.packet_type = TYPE_DATA;
	slave->STATUS_FLAG |= FG_LL_FTT;
}

//! Schedules a retransmission of the stored frame, or drops it once the retry budget is spent
//! \param backoff true for a timeout (doubles the RTO), false for a NACK
static void PARSECS_ARQ_Retransmit_Or_Drop(SLAVE *slave, bool backoff)
{
	slave->STATUS_FLAG &= ~FG_ARQ_WAIT_ACK;
	if (slave->arq_retries < ARQ_MAX_RETRIES)
	{
		slave->arq_retries++;
		if (backoff)
		{
			slave->arq_rto = (slave->arq_rto >= (ARQ_RTO_MAX_ROUNDS / 2U)) ? ARQ_RTO_MAX_ROUNDS : (uint16_t)(slave->arq_rto * 2U);
		}
		slave->STATUS_FLAG |= FG_ARQ_RETX;
	}
	else
	{
		slave->STATUS_FLAG &= ~FG_ARQ_RETX;
		slave->link_state = PARSECS_LINK_DOWN;
		slave->arq_link_down_events++;
		slave->arq_retries = 0;
		slave->arq_rto = ARQ_RTO_ROUNDS;
	}
}

//! Advances the retransmission timer by the Layer 1 rounds done since the previous call
static void PARSECS_ARQ_Check_Timeout(SLAVE *slave)
{
	uint32_t now = slave->l1_rounds;
	uint32_t delta = now - slave->arq_last_round;
	slave->arq_last_round = now;

	if ((slave->STATUS_FLAG & FG_ARQ_WAIT_ACK) == 0)
	{
		return;
	}
	// the peer can only send our ACK after the frame it is sending now, so that time is not counted
	if (PARSECS_RX_Frame_In_Progress(slave))
	{
		return;
	}
	if (delta > (uint32_t)(UINT16_MAX - slave->arq_elapsed))
	{
		slave->arq_elapsed = UINT16_MAX;
	}
	else
	{
		slave->arq_elapsed += (uint16_t)delta;
	}
	if (slave->arq_elapsed >= slave->arq_rto)
	{
		PARSECS_ARQ_Retransmit_Or_Drop(slave, true);
	}
}

//! The PARSECS Layer 3 transmit (TX) flow task implementation
//!
//! This function is used to implement the PARSECS Layer 3 transmission over the SPI bus
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The task is executed for a slave devices identified by a pointer to a SLAVE structure. If there more slave nodes present
//! then this function should be called for each slave.
//! The implementation is node and platform independent.
//! Priority: ACK, NACK, CMD, retransmission of the stored DATA frame, new DATA frame.
//! \param slave A pointer to a SLAVE structure that identifies the slave that this task will be executed for
//! \return None
//! \private
void PARSECS_TRANSMIT_LAYER3(SLAVE *slave)
{
	PARSECS_ARQ_Check_Timeout(slave);

	if ((FG_ACK & slave->STATUS_FLAG) != 0)//Flagul de ACK este setat?
	{
		if (PARSECS_L2_TX_Idle(slave))//nu am nici un frame to transmit??
		{	
			//CONSTRUIESC FRAME-UL pt ACK			
			slave->spi_packet_tx.app_buffer_length = 0; 	   // pun lungimea ACK = 1
			slave->spi_packet_tx.packet_type = ACK;    		   //tipul pachetului este ACK
			slave->STATUS_FLAG |= FG_LL_FTT;				   //setez flagul de frame  to transmit	
			slave->STATUS_FLAG &= ~FG_ACK;					   //resetez flagul de ACK
		}
	}
	if ((FG_NACK & slave->STATUS_FLAG) != 0)//Flagul de NACK este setat?
	{
		if (PARSECS_L2_TX_Idle(slave))//nu am nici un frame to transmit??
		{				
			slave->spi_packet_tx.app_buffer_length = 0;			// pun lungimea NACK = 1
			slave->spi_packet_tx.packet_type = NACK;			//tipul pachetului este NACK
			slave->STATUS_FLAG |= FG_LL_FTT;					//setez flagul de frame  to transmit
			slave->STATUS_FLAG &= ~FG_NACK;						//resetez flagul de NACK
		}
	}
	if((FG_CMD & slave->STATUS_FLAG) != 0)
	{
		slave->spi_packet_tx.packet_type = PARSECS_CMD;
		slave->STATUS_FLAG |= FG_LL_FTT;					//setez flagul de frame  to transmit
		slave->STATUS_FLAG &= ~FG_CMD;
	}
	if ((FG_ARQ_RETX & slave->STATUS_FLAG) != 0)
	{
		if (PARSECS_L2_TX_Idle(slave))
		{
			PARSECS_ARQ_Load_Frame(slave);
			slave->STATUS_FLAG &= ~FG_ARQ_RETX;
			slave->arq_retransmissions++;
		}
	}
	if (((FG_HL_FTTR & slave->STATUS_FLAG) != 0) && ((FG_ACK & slave->STATUS_FLAG) == 0) &&
	    ((slave->STATUS_FLAG & (FG_ARQ_WAIT_ACK | FG_ARQ_RETX)) == 0))//nu am de transmis ACK si am setat transmit request de la hl?
	{
		if (PARSECS_L2_TX_Idle(slave))
		{
			slave->arq_seq = slave->current_seq_no++;
			slave->arq_retries = 0;
			slave->arq_rto = ARQ_RTO_ROUNDS;
			PARSECS_ARQ_Load_Frame(slave);
			slave->STATUS_FLAG &= ~FG_HL_FTTR;				//resetez flagul dintre hl si l3 pana nu transmit ce am
		}
	}
}

//! Layer 3 handling of one CRC-checked frame from Layer 2
//! \param slave The slave the frame came from
//! \param packet The Layer 2 RX buffer holding the frame
//! \param new_data_flag FG_HL_NDF_1 or FG_HL_NDF_2, matching packet
static void PARSECS_RECEIVE_LAYER3_PROCESS(SLAVE *slave, SPI_BASE_FRAME *packet, uint32_t new_data_flag)
{
	switch (packet->packet_type)//tipul pachetului receptionat
	{
		case ACK:
		{
			slave->last_ACKed_packet_sequence_number = packet->seq_no;
			packet->packet_ok = 0;
			// a late ACK can still beat a retransmission that is only scheduled
			if (((slave->STATUS_FLAG & (FG_ARQ_WAIT_ACK | FG_ARQ_RETX)) != 0) && (packet->seq_no == slave->arq_seq))
			{
				slave->STATUS_FLAG &= ~(FG_ARQ_WAIT_ACK | FG_ARQ_RETX);
				slave->link_state = PARSECS_LINK_UP;
				slave->arq_retries = 0;
				slave->arq_rto = ARQ_RTO_ROUNDS;
			}
			break;
		}
		case NACK:
		{
			slave->last_NACKed_packet_sequence_number = packet->seq_no;
			packet->packet_ok = 0;
			// the NACK SEQ comes from a corrupted frame, so any NACK while waiting means resend
			if ((slave->STATUS_FLAG & FG_ARQ_WAIT_ACK) != 0)
			{
				PARSECS_ARQ_Retransmit_Or_Drop(slave, false);
			}
			break;
		}
		case TYPE_DATA:
		{
			slave->STATUS_FLAG |= FG_ACK; 	//setez flagul de ACK
			if (slave->rx_seq_valid && (packet->seq_no == slave->last_delivered_rx_seq))
			{
				// our previous ACK was lost: ACK again, do not deliver twice
				packet->packet_ok = 0;
			}
			else
			{
				slave->last_delivered_rx_seq = packet->seq_no;
				slave->rx_seq_valid = true;
				slave->STATUS_FLAG |= new_data_flag; 	// setez ca am primit un nou frame de date catre nivelul applicatie
			}
			break;
		}
		default:
		{
			break;
		}
	}
}

//! The PARSECS Layer 3 receive (RX) flow task implementation
//!
//! This function is used to implement the PARSECS Layer 3 reception over the SPI bus
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The task is executed for a slave devices identified by a pointer to a SLAVE structure. If there more slave nodes present
//! then this function should be called for each slave.
//! The implementation is node and platform independent.
//! \param slave A pointer to a SLAVE structure that identifies the slave that this task will be executed for
//! \return None
//! \private
void PARSECS_RECEIVE_LAYER3(SLAVE *slave)
{
	if ((slave->STATUS_FLAG & FG_NFE_1) != 0)
	{
		PARSECS_RECEIVE_LAYER3_PROCESS(slave, &slave->spi_packet_rx_1, FG_HL_NDF_1);
		slave->STATUS_FLAG &= ~FG_NFE_1;	//resetez flagul de NEW FRAME END
	}
	if ((slave->STATUS_FLAG & FG_NFE_2) != 0)
	{
		PARSECS_RECEIVE_LAYER3_PROCESS(slave, &slave->spi_packet_rx_2, FG_HL_NDF_2);
		slave->STATUS_FLAG &= ~FG_NFE_2;	//resetez flagul de NEW FRAME END
	}
}

//! @}

//! @}
