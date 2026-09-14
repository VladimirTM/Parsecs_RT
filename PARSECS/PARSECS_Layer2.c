//! \file	PARSECS_Layer2.c
//!
//! \brief	PARSECS Layer 2
//!
//! Contains the implementation of the PARSECS Layer 2
//! The implementation for PARSECS Layer 2 is platform and node independent
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup PARSECS-Low-Level-Substack
//! @{
//! \brief PARSECS Low Level Substack
//!
//! Contains the implementation of the PARSECS Low Level Substack containing Layer 1, Layer 2 and Layer 3

#include "PARSECS_Layer2.h"
#include "PARSECS_Port.h"

//! The PARSECS Layer 2 transmit (TX) flow task implementation
//!
//! This function is used to implement the PARSECS Layer 2 transmission over the SPI bus
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The task is executed for a slave devices identified by a pointer to a SLAVE structure. If there more slave nodes present
//! then this function should be called for each slave.
//! The implementation is node and platform independent.
//! \param slave A pointer to a SLAVE structure that identifies the slave that this task will be executed for
//! \return None
//! \private
void PARSECS_TRANSMIT_LAYER2(SLAVE *slave)
{	
	while( PARSECS_RingFull(&slave->l1_buffer_tx) == false ) 
	{
		switch (slave->ub_L2_TransState)
		{
			case 0: // encode start of frame
			{
				if ((slave->STATUS_FLAG & FG_LL_FTT) != 0)//AM UN FRAME DE TRANSMIS??
				{
					slave->STATUS_FLAG |= FG_LL_FTIP;		//setez flagul de frame transmit in progress
					CRC16_reset_crc(&slave->spi_packet_tx.crc16_object);//resetez crc-ul
					PARSECS_RingWriteOne(&slave->l1_buffer_tx, START_OF_FRAME);  						  //scriu in buffer-ul circular de transmisie START_OF_FRAME			
					slave->layer2_transmit_packet_index = 0;
					slave->uw_crc16 = 0;
					slave->ub_L2_TransState = 1;						
				}
				break;
			}
			case 1: // encode length
			{
				PARSECS_RingWriteOne(&slave->l1_buffer_tx,slave->spi_packet_tx.app_buffer_length);     //scriu in buffer lungimea
				CRC16_AddToCRC(&slave->spi_packet_tx.crc16_object, slave->spi_packet_tx.app_buffer_length);  // la crc adun lungimea
				slave->ub_L2_TransState = 2;
				break;				
			}
			case 2: // encode sequence number
			{
				if (slave->spi_packet_tx.packet_type == TYPE_DATA)
				{
					slave->spi_packet_tx.seq_no = slave->current_seq_no++; //nu am ACK/NACK	? atunci incrementez sequence no
					slave->last_transmitted_data_packet_sequence_number = slave->spi_packet_tx.seq_no;
				}
				else
				{
					slave->spi_packet_tx.seq_no = slave->last_received_packet_sequence_number;	//altfel iau sequence no de la pachetul de date si il pun la pachetul de ACK
					
				}
				PARSECS_RingWriteOne(&slave->l1_buffer_tx,slave->spi_packet_tx.seq_no);				  //scriu in buffer sequence no
				CRC16_AddToCRC(&slave->spi_packet_tx.crc16_object, slave->spi_packet_tx.seq_no);			  // adun sequence no la crc
				slave->ub_L2_TransState = 3;
				break;
			}
			case 3: // encode packet type
			{
				PARSECS_RingWriteOne(&slave->l1_buffer_tx, slave->spi_packet_tx.packet_type);		  //scriu in buffer tipul pachetului
				CRC16_AddToCRC(&slave->spi_packet_tx.crc16_object, slave->spi_packet_tx.packet_type); 	      //adun la crc tipul pachetului =>crc= length+pachet_type
				slave->ub_L2_TransState = 4;
				break;
			}
			case 4: // encode packet data
			{
				if (slave->layer2_transmit_packet_index < slave->spi_packet_tx.app_buffer_length)
				{
					PARSECS_RingWriteOne(&slave->l1_buffer_tx, slave->spi_packet_tx.app_buffer[slave->layer2_transmit_packet_index]);	  //scriu in buffer data
					CRC16_AddToCRC(&slave->spi_packet_tx.crc16_object, slave->spi_packet_tx.app_buffer[slave->layer2_transmit_packet_index]);	  //adun la CRC data =>crc= length+pachet_type+data
					slave->layer2_transmit_packet_index++;	
					
				}
				else
				{
					slave->ub_L2_TransState = 5;	
				}
				break;
			}
			case 5: // encode CRC16 and send MSW
			{
				slave->uw_crc16 = CRC16_GetCRC(&slave->spi_packet_tx.crc16_object);				// iau valoarea CRC-ului
				PARSECS_RingWriteOne(&slave->l1_buffer_tx, (uint8_t)(slave->uw_crc16 >> 8));		// scriu CRC-ul in buffer-ul circular
				slave->ub_L2_TransState = 6;
				break;
			}
			case 6: // encode CRC16 and send LSW
			{
				PARSECS_RingWriteOne(&slave->l1_buffer_tx, (uint8_t)(slave->uw_crc16 & 0xFF));
				slave->STATUS_FLAG &= ~FG_LL_FTIP;  //resetez flagul de frame transmit in progress
				slave->STATUS_FLAG &= ~FG_LL_FTT;   //resetez flagul de frame  to transmit			
				slave->ub_L2_TransState = 0;
				break;
			}
		}
		if ( (slave->STATUS_FLAG & FG_LL_FTIP) == 0)
		{
			break;
		}
	}
}


//! The PARSECS Layer 2 receive (RX) state machine implementation
//!
//! This function is used to implement the state machine for assembling the SPI_BASE_FRAME
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! This function is called by PARSECS_RECEIVE_LAYER2
//!
//! \param[in] slave A pointer to a SLAVE structure that identifies the slave
//! \param[out] packet A pointer to a SPI_BASE_FRAME structure containing the newly assembled SPI_BASE_FRAME
//!
//! \return 0 if an error was found
//! \return 1 if a SPI_BASE_FRAME was successfully received and save in parameter packet
//! \private
uint8_t PARSECS_RECEIVE_LAYER2_PROCESS(SLAVE *slave, SPI_BASE_FRAME *packet)
{ 
	static uint8_t crc_byte_0, crc_byte_1; // iau crc-urile receptionate
	uint8_t word1;				   //variabila in care preiau cate un word din buffer-ul circular de receptie
	uint16_t uw_CRC16;
	uint16_t concatword;
	uint8_t ret = 0;
	while (!PARSECS_RingEmpty(&slave->l1_buffer_rx))
	{
		word1 = PARSECS_RingReadOne(&slave->l1_buffer_rx); //iau word-ul din buffer-ul circular
		switch(slave->ub_L2_RecState)
		{
			case 0: // waiting for START_OF_FRAME
			{
				if (word1 == START_OF_FRAME) //este Start of frame??
				{
					packet->frame_in_progress = true; // setez flagul de frame in pending
					slave->ub_L2_RecState = 1;		 //ma duc in starea urmatoare mi-a venit START OF FRAME
					crc_byte_0 = 0x00;				 //restez  valorile de la crc-urile
					crc_byte_1 = 0x00;
					uw_CRC16 =0x00;			//restez variabila in care calculez crc-urile
					slave->uw_L2_ByteCount0 = 0;	//resetez index-ul de la date
					packet->packet_ok = 0; // pun starea pachet-ul pe 0 , inca nu am un pachet bun
					CRC16_reset_crc(&packet->crc16_object);//restez crc-ul
				}	
				break;
			}
			case 1: // Get Frame Length
			{		
				packet->packet_length = word1; 	   //iau lungimea pachet-ului
				if (packet->packet_length > DATA_BUFFER_SIZE)
				{
					packet->frame_in_progress = false;				//restez flagul de frame in progress
					slave->ub_L2_RecState = 0;					   // ma duc in starea 0	
				}
				CRC16_AddToCRC(&packet->crc16_object, word1);    // adun la crc lungimea
				if (packet->frame_in_progress == true)
				{
					slave->ub_L2_RecState = 2;					   // ma duc in starea 2
				}
				else
				{	
					packet->frame_in_progress = false;				//restez flagul de frame in progress
					slave->ub_L2_RecState = 0;					   // ma duc in starea 0
				}
				break;	
			}	
			case 2: // Get seq_no
			{		
				packet->seq_no = word1; 						   //iau lungimea pachet-ului
				CRC16_AddToCRC(&packet->crc16_object, word1);    // adun la crc lungimea
				if (packet->frame_in_progress == true)
				{
					slave->ub_L2_RecState = 3;					   // ma duc in starea 2
					slave->last_received_packet_sequence_number = packet->seq_no;
				}
				else
				{	
					packet->frame_in_progress = false;
					slave->ub_L2_RecState = 0;					   // ma duc in starea 0
				}
				break;	
			}	
			case 3: //get packet type
			{
				packet->packet_type = word1;			// iau tipul pachetului
				CRC16_AddToCRC(&packet->crc16_object, word1);		// adun la crc  tipul pachetului
				if (packet->packet_length == 0) 	// lungimea este 0??? =>inseamna ca  am ACK
				{
					slave->ub_L2_RecState = 5; 						// ma duc in starea 5
				}
				else
				{
					slave->ub_L2_RecState = 4;							  //  ma duc in starea 4 trebuie si prelucrez  datele  
					packet->app_buffer_length = packet->packet_length;// pun lungimea datelor
				}
				
				break;
			 }

			case 4:
			{
				packet->app_buffer[slave->uw_L2_ByteCount0++] = word1;  	// iau datele si le pun in buffer
				CRC16_AddToCRC(&packet->crc16_object, word1);				 			// adun la crc-ul datele
				if (slave->uw_L2_ByteCount0 == packet->app_buffer_length)  //verific  daca am terminat de scris datele
				{					
					slave->ub_L2_RecState = 5; //trec  in starea 4			
				}
				break;
			}
			
			case 5:
			{ 
				crc_byte_1 = word1;			// luam primul crc
				slave->ub_L2_RecState = 6;	// mergem in starea 5
				break;
			}
			
			case 6:
			{
				crc_byte_0 = word1; 							  // iau al doilea crc
				concatword = ((crc_byte_1 << 8) | crc_byte_0); // concatenez cele 2 crc-uri
				uw_CRC16 = CRC16_GetCRC(&packet->crc16_object); //  iau crc-ul calculat
				if (uw_CRC16 == concatword) // verific daca  sunt la fel crc-urile
				{
					ret = 1;
					packet->packet_ok = 1; //am un pachet bun
					slave->ub_L2_RecState = 0;			//resetez automatul si ma duc in starea 0
					slave->STATUS_FLAG &= ~FG_NACK;     // RESETEZ FALGUL DE NACK
				}
				else
				{
					slave->STATUS_FLAG |= FG_NACK; 		//nu s-a calaculat crc-ul bine transmit ACK
					slave->ub_L2_RecState = 0;			//ma duc in starea 0 nu am crc-ul bun
				}
				packet->frame_in_progress = false;		//restez flagul de frame in progress
				break;
			}		
		}// end of switch
	}
	return ret;
}

//! The PARSECS Layer 2 receive (RX) flow task implementation
//!
//! This function is used to implement the PARSECS Layer 2 transmission over the SPI bus
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The task is executed for a slave devices identified by a pointer to a SLAVE structure. If there more slave nodes present
//! then this function should be called for each slave
//! The implementation is node and platform independent
//! \param slave A pointer to a SLAVE structure that identifies the slave that this task will be executed for
//! \return None
//! \private
void PARSECS_RECEIVE_LAYER2(SLAVE *slave)
{
	uint8_t result = 0;
	if (slave->spi_packet_rx_1.frame_in_progress == true)
	{
		result = PARSECS_RECEIVE_LAYER2_PROCESS(slave, &slave->spi_packet_rx_1);
		if (result == 1)
		{
			slave->STATUS_FLAG |= FG_NFE_1; 				//setez frame end 1
		}
		return;
	}
	if (slave->spi_packet_rx_2.frame_in_progress == true)
	{
		result = PARSECS_RECEIVE_LAYER2_PROCESS(slave, &slave->spi_packet_rx_2);
		if (result == 1)
		{
			slave->STATUS_FLAG |= FG_NFE_2; 				//setez frame end 1
		}
		return;
	}

	if (slave->spi_packet_rx_1.packet_ok == 0)
	{
		result = PARSECS_RECEIVE_LAYER2_PROCESS(slave, &slave->spi_packet_rx_1);
		if (result == 1)
		{
			slave->STATUS_FLAG |= FG_NFE_1; 				//setez frame end 1
		}
	}
	else
	{
		if (slave->spi_packet_rx_2.packet_ok == 0)
		{
			result = PARSECS_RECEIVE_LAYER2_PROCESS(slave, &slave->spi_packet_rx_2);
			if (result == 1)
			{
				slave->STATUS_FLAG |= FG_NFE_2; 				//setez frame end 1
			}
		}
	}
}

//! @}
//! @}
