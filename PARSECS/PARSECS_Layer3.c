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
#include "PARSECS_Layer3.h"

//! The PARSECS Layer 3 transmit (TX) flow task implementation
//!
//! This function is used to implement the PARSECS Layer 3 transmission over the SPI bus
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The task is executed for a slave devices identified by a pointer to a SLAVE structure. If there more slave nodes present
//! then this function should be called for each slave.
//! The implementation is node and platform independent.
//! \param slave A pointer to a SLAVE structure that identifies the slave that this task will be executed for
//! \return None
//! \private
void PARSECS_TRANSMIT_LAYER3(SLAVE *slave)
{
	if ((FG_ACK & slave->STATUS_FLAG) != 0)//Flagul de ACK este setat?
	{
		if (((FG_LL_FTT & slave->STATUS_FLAG) == 0) && ((FG_LL_FTIP & slave->STATUS_FLAG) == 0))//nu am nici un frame to transmit??
		{	
			//CONSTRUIESC FRAME-UL pt ACK			
			slave->spi_packet_tx.app_buffer_length = 0; 	   // pun lungimea ACK = 1
			slave->spi_packet_tx.packet_type = ACK;    		   //tipul pachetului este ACK
			slave->STATUS_FLAG |= FG_LL_FTT;				   //setez flagul de frame  to transmit	
			slave->STATUS_FLAG &=~FG_HL_FTTR;				   //resetez flagul dintre hl si l3 pana nu transmit ce am		
			slave->STATUS_FLAG &= ~FG_ACK;					   //resetez flagul de ACK
		}
	}
	if ((FG_NACK & slave->STATUS_FLAG) != 0)//Flagul de NACK este setat?
	{
		if (((FG_LL_FTT & slave->STATUS_FLAG) == 0) && ((FG_LL_FTIP & slave->STATUS_FLAG) == 0))//nu am nici un frame to transmit??
		{				
			slave->spi_packet_tx.app_buffer_length = 0;			// pun lungimea NACK = 1
			slave->spi_packet_tx.packet_type = NACK;			//tipul pachetului este NACK
			slave->STATUS_FLAG |= FG_LL_FTT;					//setez flagul de frame  to transmit
			slave->STATUS_FLAG &=~FG_HL_FTTR;					//resetez flagul dintre hl si l3 pana nu transmit ce am
			slave->STATUS_FLAG &= ~FG_NACK;						//resetez flagul de NACK
		}
	}
	if((FG_CMD & slave->STATUS_FLAG) != 0)
	{
		slave->spi_packet_tx.packet_type = PARSECS_CMD;
		slave->STATUS_FLAG |= FG_LL_FTT;					//setez flagul de frame  to transmit
		slave->STATUS_FLAG &=~FG_HL_FTTR;					//resetez flagul dintre hl si l3 pana nu transmit ce am
		slave->STATUS_FLAG &= ~FG_CMD;
	}
	if (((FG_HL_FTTR & slave->STATUS_FLAG) != 0) && ((FG_ACK & slave->STATUS_FLAG) == 0))//nu am de transmis ACK si am setat transmit request de la hl?
	{
		if (((FG_LL_FTT & slave->STATUS_FLAG) == 0) && ((FG_LL_FTIP & slave->STATUS_FLAG) == 0))
		{
			slave->spi_packet_tx.packet_type = TYPE_DATA;//pun lungimea pachetului de tip DATA
			slave->STATUS_FLAG |= FG_LL_FTT;				//setez flagul de frame  to transmit
			slave->STATUS_FLAG &= ~FG_HL_FTTR;				//resetez flagul dintre hl si l3 pana nu transmit ce am
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
	// daca FG_NFE = 1 => am un frame nou
	// daca FG_NFE = 0 -> nu am un frame nou
//	if ((slave->STATUS_FLAG & FG_NFE) == 0)//nu am frame terminat ???
//	{
//		return; 					// ies nu am frame de transmis
//	}
	if ((slave->STATUS_FLAG & FG_NFE_1) != 0)
	{
		switch (slave->spi_packet_rx_1.packet_type)//tipul pachetului receptionat
		{
			case ACK:
			{
				slave->last_ACKed_packet_sequence_number = slave->spi_packet_rx_1.seq_no;
				slave->spi_packet_rx_1.packet_ok = 0;
				break;
			}
			case NACK:
			{
				slave->last_NACKed_packet_sequence_number = slave->spi_packet_rx_1.seq_no;
				slave->spi_packet_rx_1.packet_ok = 0;
				break;
			}

			case TYPE_DATA:
			{
				slave->STATUS_FLAG |= FG_ACK; 	//setez flagul de ACK
				slave->STATUS_FLAG |= FG_HL_NDF_1; 	// setez ca am primit un nou frame de date catre nivelul applicatie
				break;
			}
		}
		slave->STATUS_FLAG &= ~FG_NFE_1;	//resetez flagul de NEW FRAME END
	}
	if ((slave->STATUS_FLAG & FG_NFE_2) != 0)
	{
		switch (slave->spi_packet_rx_2.packet_type)//tipul pachetului receptionat
		{
			case ACK:
			{
				slave->last_ACKed_packet_sequence_number = slave->spi_packet_rx_2.seq_no;
				slave->spi_packet_rx_2.packet_ok = 0;
				break;
			}
			case NACK:
			{
				slave->last_NACKed_packet_sequence_number = slave->spi_packet_rx_2.seq_no;
				slave->spi_packet_rx_2.packet_ok = 0;
				break;
			}
			case TYPE_DATA:
			{
				slave->STATUS_FLAG |= FG_ACK; 	//setez flagul de ACK
				slave->STATUS_FLAG |= FG_HL_NDF_2; 	// setez ca am primit un nou frame de date catre nivelul applicatie
				break;
			}
		}
		slave->STATUS_FLAG &= ~FG_NFE_2;	//resetez flagul de NEW FRAME END
	}
}

//! @}

//! @}
