//! \file	PARSECS_LowLevelAPI.c
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
#include "PARSECS_LowLevelAPI.h"
#include "PARSECS_Includes.h"

//! The PARSECS Low Level Substack transmit (TX) API
//!
//! This function is used by the user to transmit a data buffer over the SPI bus using the PARSECS Low Level Substack
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The implementation is node and platform independent.
//! \param slave_id The slave_id of the slave device that the data should be transmitted to. In case of the Master node, this parameter is obtained by calling
//! \ref PARSECS_Add_Slave. In case of calling this function in a slave node, this parameter should be 0
//! \param data The buffer that should be transmitted
//! \param data_length The length of the buffer
//! \param[out] sent_sequence_number This is an output parameter where this function will write the sequence number of the packet that was sent
//! \return  0 In case of a successfully scheduled transmission
//! \return -1 In case of a wrong slave_id value or if the stack is busy
//! \return -2 In case the data_length is zero
//! \return -3 In case of the data_length is higher than supported as defined by DATA_BUFFER_SIZE
int8_t PARSECS_TRANSMIT_APP(uint8_t slave_id ,uint8_t *data, uint8_t data_length, uint8_t *sent_sequence_number)
{   
	// apelata de aplicatie
	// daca nu am trimis inca ACK pentru pachetul receptionat inainte nu las transmisie... returnez busy
	if (slave_id > SLAVE_COUNT)
	{
		return -1;
	}
	if (data_length == 0)//lungimea e 0?
	{
		return -2;
	}
	if (data_length > DATA_BUFFER_SIZE)//lungimea care vreau sa o transmit  e mai mare decat lungimea maxima?
	{
		return -3;
	}

	if (((slaves[slave_id].STATUS_FLAG & FG_LL_FTIP) == 0) && ((slaves[slave_id].STATUS_FLAG & FG_LL_FTT) == 0))//nu am frame in progres sau transmit
	{
		memcpy(slaves[slave_id].spi_packet_tx.app_buffer, data , data_length);//copiez in buffer data ce vreau sa transmit
		slaves[slave_id].spi_packet_tx.app_buffer_length = data_length;//pun lungime +1  pt  ca am lungime + tipul de pachet
		slaves[slave_id].STATUS_FLAG |= FG_HL_FTTR;// setez flag intre hl si layer 3 transmit
		if (sent_sequence_number != NULL)
		{
			*sent_sequence_number = slaves[slave_id].current_seq_no;
		}
		return 0;
	}	
	else 
	{
		return -1;
	}
}

//! Checks whether the PARSECS Low Level Substack is ready to transmit new data
//!
//! This function is used by the user interrogate if the PARSECS Low Level Substack is ready to transmit new data
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The implementation is node and platform independent.
//! \param slave_id The slave_id of the slave device that the data should be transmitted to. In case of the Master node, this parameter is obtained by calling
//! \ref PARSECS_Add_Slave. In case of calling this function in a slave node, this parameter should be 0
//! \return  0 The PARSECS Low Level Substack is ready to trasnmit new data
//! \return -1 The PARSECS Low Level Substack is currently busy and unable to trasnmit new data
int8_t PARSECS_CHECK_APP_TX_READY(uint8_t slave_id)
{
	if (((slaves[slave_id].STATUS_FLAG & FG_LL_FTIP) == 0) && ((slaves[slave_id].STATUS_FLAG & FG_LL_FTT) == 0))//nu am frame in progres sau transmit
	{
		return 0;
	}	
	else 
	{
		return -1;
	}
}

//! The PARSECS Low Level Substack receive (RX) API
//!
//! This function is used by the user to retrieve an already received data buffer over the SPI bus using the PARSECS Low Level Substack
//! The implementation is according to the documentation of the PARSECS Protocol Stack
//! The implementation is node and platform independent.
//! \param slave_id The slave_id of the slave device that the data should be received from. In case of the Master node, this parameter is obtained by calling
//! PARSECS_Add_Slave. In case of calling this function in a slave node, this parameter should be 0
//! \param[out] data The memory zone where the data will be written
//! \param[out] data_length The length of the buffer. This parameter will be written with the length of the data written in parameter data
//! \param[out] sequence_number This is an output parameter where this function will write the sequence number of the packet that contained the received data
//! \return  0 In case of a successfully scheduled transmission
//! \return -1 In case of a wrong slave_id value or if the stack is busy
//! \return -2 In case the data_length is zero
//! \return -3 In case of the data_length is higher than supported as defined by DATA_BUFFER_SIZE
int8_t PARSECS_RECEIVE_APP(uint8_t slave_id, uint8_t *data, uint8_t *data_length, uint8_t *sequence_number)
{
	if (slave_id > SLAVE_COUNT)
	{
		return -1;
	}
	if (data == NULL)
	{
		return -2;
	}

	if (((slaves[slave_id].STATUS_FLAG & FG_HL_NDF_1) != 0) && ((slaves[slave_id].STATUS_FLAG & FG_HL_NDF_2) != 0))   //am new frame receive?
	{
		if ((slaves[slave_id].spi_packet_rx_1.packet_ok == 1) && (slaves[slave_id].spi_packet_rx_2.packet_ok == 1))
		{
			// caut pe cel cu SEQ mai mic
			if (slaves[slave_id].spi_packet_rx_1.seq_no < slaves[slave_id].spi_packet_rx_2.seq_no)
			{
				// il dau pe 1
				slaves[slave_id].STATUS_FLAG &= ~FG_HL_NDF_1; // resetez flagul de new frame receive
				
				if (slaves[slave_id].spi_packet_rx_1.packet_ok == 0)// nu am un pachet bun??
				{  
					return -1;
				}		
				memcpy(data,slaves[slave_id].spi_packet_rx_1.app_buffer , slaves[slave_id].spi_packet_rx_1.app_buffer_length);//copiez din buffer data	 
				if (data_length != NULL)
				{
					*data_length = slaves[slave_id].spi_packet_rx_1.app_buffer_length;// copiez lungimea din buffer
				}
				if (sequence_number != NULL)
				{
					*sequence_number = slaves[slave_id].spi_packet_rx_1.seq_no;
				}
				slaves[slave_id].spi_packet_rx_1.packet_ok = 0;// resetez  starea pachetul => pachetul nu este bun
				return 0; // returnez ca am primit un pachet bun				
			}
			else
			{
				// il dau pe 2
				slaves[slave_id].STATUS_FLAG &= ~FG_HL_NDF_2; // resetez flagul de new frame receive
				
				if (slaves[slave_id].spi_packet_rx_2.packet_ok == 0)// nu am un pachet bun??
				{  
					return -1;
				}		
				memcpy(data,slaves[slave_id].spi_packet_rx_2.app_buffer , slaves[slave_id].spi_packet_rx_2.app_buffer_length);//copiez din buffer data	 
				if (data_length != NULL)
				{
					*data_length = slaves[slave_id].spi_packet_rx_2.app_buffer_length;// copiez lungimea din buffer
				}
				if (sequence_number != NULL)
				{
					*sequence_number = slaves[slave_id].spi_packet_rx_2.seq_no;
				}
				slaves[slave_id].spi_packet_rx_2.packet_ok = 0;// resetez  starea pachetul => pachetul nu este bun
				return 0; // returnez ca am primit un pachet bun				
			}
		}
	}
	
	if (( slaves[slave_id].STATUS_FLAG & FG_HL_NDF_1) != 0)//am new frame receive?
	{	
		slaves[slave_id].STATUS_FLAG &= ~FG_HL_NDF_1; // resetez flagul de new frame receive
		
		if (slaves[slave_id].spi_packet_rx_1.packet_ok == 0)// nu am un pachet bun??
		{  
			return -1;
		}		
		memcpy(data, slaves[slave_id].spi_packet_rx_1.app_buffer, slaves[slave_id].spi_packet_rx_1.app_buffer_length);//copiez din buffer data	 
		if (data_length != NULL)
		{
			*data_length = slaves[slave_id].spi_packet_rx_1.app_buffer_length;// copiez lungimea din buffer
		}
		if (sequence_number != NULL)
		{
			*sequence_number = slaves[slave_id].spi_packet_rx_1.seq_no;
		}
		slaves[slave_id].spi_packet_rx_1.packet_ok = 0;// resetez  starea pachetul => pachetul nu este bun
		return 0; // returnez ca am primit un pachet bun
	}

	if (( slaves[slave_id].STATUS_FLAG & FG_HL_NDF_2) != 0)//am new frame receive?
	{	
		slaves[slave_id].STATUS_FLAG &= ~FG_HL_NDF_2; // resetez flagul de new frame receive
		
		if (slaves[slave_id].spi_packet_rx_2.packet_ok == 0)// nu am un pachet bun??
		{  
			return -1;
		}		
		memcpy(data, slaves[slave_id].spi_packet_rx_2.app_buffer , slaves[slave_id].spi_packet_rx_2.app_buffer_length);//copiez din buffer data	 
		if (data_length != NULL)
		{
			*data_length = slaves[slave_id].spi_packet_rx_2.app_buffer_length;// copiez lungimea din buffer
		}
		if (sequence_number != NULL)
		{
			*sequence_number = slaves[slave_id].spi_packet_rx_2.seq_no;
		}
		slaves[slave_id].spi_packet_rx_2.packet_ok = 0;// resetez  starea pachetul => pachetul nu este bun
		return 0; // returnez ca am primit un pachet bun
	}

	
	return -2; // nu am primit nimic
}
#ifdef SPI_MASTER
//! The PARSECS Low Level Substack Add Slave API
//!
//! This function is only available for the Master node and it is called by the user to add a new slave to the PARSECS Low Leve Substack.
//! The function will require function pointers to functions which toggle the slave select pin. This function will return an integer number representing a unique identifier
//! of the slave in the PARSERCS Low Level Substack. This id will be then used to call other functions of the PARSECS Low Level Substack
//! \param SelectFunctionPointer A pointer to a function of type SPI_SLAVE_FUNCTION that selects the slave (toggles the SSEL to low). The function must be void with no parameters
//! \param DeselectFunctionPointer A pointer to a function of type SPI_SLAVE_FUNCTION that deselects the slave (toggles the SSEL to high). The function must be void with no parameters
//! \return -1 on error
//! \return >= 0  representing the slave unique identifier
int8_t PARSECS_Add_Slave(SPI_SLAVE_FUNCTION SelectFunctionPointer, SPI_SLAVE_FUNCTION DeselectFunctionPointer)
{
	uint8_t i = 0;
	for (i = 0; i < SLAVE_COUNT; i++)
	{
		if (slaves[i].slave_enabled == 0)
		{			
			slaves[i].select_function = SelectFunctionPointer; //selectez slave-ul
			slaves[i].deselect_function = DeselectFunctionPointer; //deselectez slave-ul
			slaves[i].i2c_address = (uint16_t)(PARSECS_I2C_ADDR_COMM_7BIT << 1);
			slaves[i].slave_enabled = 1;							//pun slave-ul pe enable
			PARSECS_InitRingBuffers(&slaves[i]);								//initializez buffer-ul circular pentru fiecare slave
			CRC16_constructor(&slaves[i].spi_packet_rx_1.crc16_object);			// instantiez crc-ul pentru receptie
			CRC16_constructor(&slaves[i].spi_packet_rx_2.crc16_object);			// instantiez crc-ul pentru receptie
			CRC16_constructor(&slaves[i].spi_packet_tx.crc16_object);			//instantiez crc-ul pentru transmisie
			slaves[i].current_seq_no = 0;
			slaves[i].ub_L2_RecState = 0;
			slaves[i].uw_L2_ByteCount0 = 0;
			slaves[i].STATUS_FLAG = 0;
			slaves[i].last_received_packet_sequence_number = 0;
			slaves[i].last_transmitted_data_packet_sequence_number = 0;
			slaves[i].last_ACKed_packet_sequence_number = 0;
			slaves[i].last_NACKed_packet_sequence_number = 0;
			slaves[i].ub_L2_TransState = 0;  // state pt layer 2 transmit
			slaves[i].layer2_transmit_packet_index = 0; 
			number_of_active_slaves++;
			return i;
		}
	}
	return -1;
}

//! Store the 7-bit I2C address Layer 1 uses for this slave.
//! \param slave_id Identifier returned by \ref PARSECS_Add_Slave
//! \param addr_7bit 7-bit address (not shifted)
//! \return 0 on success, -1 if slave_id is not an enabled slave
int8_t PARSECS_Set_Slave_I2C_Address(int8_t slave_id, uint8_t addr_7bit)
{
	if ((slave_id < 0) || ((uint8_t)slave_id >= SLAVE_COUNT))
	{
		return -1;
	}
	if (slaves[slave_id].slave_enabled == 0)
	{
		return -1;
	}
	slaves[slave_id].i2c_address = (uint16_t)((uint16_t)addr_7bit << 1);
	return 0;
}
#endif

//! Get the value of the maximum supported length of the data buffer for transmission
//!
//! The actual value is configured by the DATA_BUFFER_SIZE macro
//!
//! \return the value of the maximum supported length of the data buffer for transmission
uint8_t PARSECS_GetApp_tx_buffer_max_length()
{
	return DATA_BUFFER_SIZE;
}

//! Get the value of the maximum supported length of the data buffer for reception
//!
//! The actual value is configured by the DATA_BUFFER_SIZE macro
//!
//! \return the value of the maximum supported length of the data buffer for reception
uint8_t PARSECS_GetApp_rx_buffer_max_length()
{
	return DATA_BUFFER_SIZE;
}

//! Get the sequence number of the last acknowledged packet
//! \param slave_id The unique slave identifier obtained by calling \ref PARSECS_Add_Slave
//! \return -1 in case an out of range slave_id is given
//! \return -2 in case the slave_id parameter points to an inexistant or uninitialized slave
//! \return >= 0 the actual value of the sequence number of the last acknowledged packet
int16_t PARSECS_Get_last_ACKed_packet_sequence_number(uint8_t slave_id)
{
	if (slave_id > SLAVE_COUNT)
	{
		return -1;
	}
	#ifdef SPI_MASTER
	if (slaves[slave_id].slave_enabled == false)
	{
		return -2;
	}
	#endif
	return slaves[slave_id].last_ACKed_packet_sequence_number;
}

//! Get the sequence number of the last not-acknowledged packet
//! \param slave_id The unique slave identifier obtained by calling \ref PARSECS_Add_Slave
//! \return -1 in case an out of range slave_id is given
//! \return -2 in case the slave_id parameter points to an inexistant or uninitialized slave
//! \return >= 0 the actual value of the sequence number of the last not-acknowledged packet
int16_t PARSECS_Get_last_NACKed_packet_sequence_number(uint8_t slave_id)
{
	if (slave_id > SLAVE_COUNT)
	{
		return -1;
	}
	#ifdef SPI_MASTER
	if (slaves[slave_id].slave_enabled == false)
	{
		return -2;
	}
	#endif
	return slaves[slave_id].last_NACKed_packet_sequence_number;
}

//! @}
//! @}
