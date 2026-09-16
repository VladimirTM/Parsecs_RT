//! \file	PARSECS_Protocol_Interface.c
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
#include "PARSECS_Protocol_Interface.h"
#include "PARSECS_Includes.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ber.h>

#include "PARSECS_LowLevelAPI.h"
#include "PARSECS_Protocol.h"

#ifdef SPI_MASTER
static void i2c_cs_noop(void)
{
}
#endif

static PARSECS_PROTOCOL_BOARD_DESCRIPTOR *PARSECS_BoardByAddress(PARSECS_APP_BOARD_ADDRESS boardAddress)
{
	uint8_t i = 0;
	for (i = 0; i < MAX_BOARD_COUNT; i++)
	{
		if ((CORE_TX_Wit_Boards[i].boardAvailable == true) && (CORE_TX_Wit_Boards[i].boardAddress == boardAddress))
		{
			return &CORE_TX_Wit_Boards[i];
		}
	}
	return NULL;
}

#ifdef SPI_PROTOCOL_DEBUG
#ifdef SPI_MASTER
void SPI_Print_Raw_Packet(int8_t slave_id, uint8_t sequence_number, uint8_t size)
{	
	LOG_SERIAL ("SLAVE %1d seq: %3d size: %3d", slave_id, sequence_number, size);	
}
#else
void SPI_Print_Raw_Packet(uint8_t sequence_number, uint8_t size)
{	
	LOG_SERIAL ("MASTER seq: %3d size: %3d", sequence_number, size);		
}
#endif
#endif

//! Initialization function of the PARSECS High Level Substack with the WIT specific boards
//!
//! \return None
//! \private
void PARSECS_Protocol_Interface_Task_Init()
{
	PARSECS_PROTOCOL_BOARD_DESCRIPTOR *board = &CORE_TX_Wit_Boards[0];

#ifdef SPI_MASTER
	int8_t slave_id = PARSECS_Add_Slave(i2c_cs_noop, i2c_cs_noop);
	if (slave_id < 0)
	{
		return;
	}
	board->boardSlaveID = slave_id;
	board->boardAddress = CORE_TX_WIT_COMM_BOARD;
#else
	board->boardSlaveID = 0;
	board->boardAddress = CORE_TX_WIT_MOTHERBOARD;
#endif
	PARSECS_ProtocolDescriptorInit(&board->boardProtocolDescriptor);
	PARSECS_ProtocolCommunicationInterfaceInit(&board->boardProtocolInterface);
	board->boardAvailable = true;
}
//! Function used to implement the PARSECS High Level Task at board level. This function should be called for each configured board. This function is called by the general task and should not be called by the user
//!
//! \param wit_board The PARSECS_PROTOCOL_BOARD_DESCRIPTOR structure for which contains the image of the protocol
//! \return None
//! \private
void PARSECS_Protocol_Interface_Task_Slave(PARSECS_PROTOCOL_BOARD_DESCRIPTOR *wit_board)
{
	uint8_t rx_data_length = 0;
	uint8_t tx_data_length = 0;
	uint8_t rx_seq_no;
	uint8_t tx_seq_no;

	int8_t rx_status = 0;
	int8_t tx_status = 0;
	
	// check if a packet has been received
	rx_status = PARSECS_RECEIVE_APP(wit_board->boardSlaveID, (uint8_t*)wit_board->boardProtocolInterface.receive_raw_data_buffer, &rx_data_length, &rx_seq_no);
			
	// while here... also get current ACKed and NACKed sequence numbers
	wit_board->boardProtocolInterface.last_ACKed_packet_sequence_number = PARSECS_Get_last_ACKed_packet_sequence_number(wit_board->boardSlaveID);
	wit_board->boardProtocolInterface.last_NACKed_packet_sequence_number = PARSECS_Get_last_NACKed_packet_sequence_number(wit_board->boardSlaveID);
	
	if (rx_status == 0)
	{
		// new data packet has been received				
		wit_board->boardProtocolInterface.receive_seq_number = rx_seq_no;			// get packet received sequence number
		wit_board->boardProtocolInterface.receive_data_size = rx_data_length;		// get the data length of the received data 
		
		// while here... also get current ACKed and NACKed sequence numbers	
		wit_board->boardProtocolInterface.last_ACKed_packet_sequence_number = PARSECS_Get_last_ACKed_packet_sequence_number(wit_board->boardSlaveID);
		wit_board->boardProtocolInterface.last_NACKed_packet_sequence_number = PARSECS_Get_last_NACKed_packet_sequence_number(wit_board->boardSlaveID);				
		
		wit_board->boardProtocolInterface.command = PARSECS_PROTOCOL_COMMAND_IO_OPERATION; // set flag that it is a receive operation necessary and signaling that data may also be transmitted if necessary
	
		#ifdef SPI_PROTOCOL_DEBUG
		#ifdef SPI_PROTOCOL_LL_DEBUG
		LOG_SERIAL ("SPI <- ");
		#ifdef SPI_MASTER
		SPI_Print_Raw_Packet(wit_board->boardSlaveID, rx_seq_no, rx_data_length);				
		#else
		SPI_Print_Raw_Packet(rx_seq_no, rx_data_length);
		#endif
		LOG_SERIAL ("\n");
		#endif
		#endif
		PARSECS_Protocol_Handler(&(wit_board->boardProtocolDescriptor), &(wit_board->boardProtocolInterface));
	}
	else
	{
		// no new data has been received
		wit_board->boardProtocolInterface.receive_seq_number = 0;
		wit_board->boardProtocolInterface.receive_data_size = -1;
		wit_board->boardProtocolInterface.command = PARSECS_PROTOCOL_COMMAND_TRANSMIT_OPERATION_ONLY; //.... signaling that data may be transmitted if necessary
		PARSECS_Protocol_Handler(&(wit_board->boardProtocolDescriptor), &(wit_board->boardProtocolInterface));
		
	}
	
	if (wit_board->boardProtocolInterface.transmit_data_size >= 0)
	{
		// upper level needs to transmit data
		tx_data_length = wit_board->boardProtocolInterface.transmit_data_size; // setting transmit data siz
		tx_status = PARSECS_TRANSMIT_APP(wit_board->boardSlaveID, wit_board->boardProtocolInterface.transmit_raw_data_buffer, tx_data_length, &tx_seq_no ); // call lower level transmit method
		wit_board->boardProtocolInterface.command = PARSECS_PROTOCOL_COMMAND_TRANSMIT_STATUS_UPDATE; // lower level transmit call has retruned, signaling upper levels to update the status of the transmission

		// while here... also get current ACKed and NACKed sequence numbers	
		wit_board->boardProtocolInterface.last_ACKed_packet_sequence_number = PARSECS_Get_last_ACKed_packet_sequence_number(wit_board->boardSlaveID);
		wit_board->boardProtocolInterface.last_NACKed_packet_sequence_number = PARSECS_Get_last_NACKed_packet_sequence_number(wit_board->boardSlaveID);				
		wit_board->boardProtocolInterface.transmit_seq_number = tx_seq_no;
		//updating the transmission status variable for the upper levels
		if (tx_status == 0) 
		{			
			#ifdef SPI_PROTOCOL_DEBUG
			#ifdef SPI_PROTOCOL_LL_DEBUG
			LOG_SERIAL ("SPI -> ");
			#ifdef SPI_MASTER
			SPI_Print_Raw_Packet(wit_board->boardSlaveID, tx_seq_no, tx_data_length);				
			#else
			SPI_Print_Raw_Packet(tx_seq_no, tx_data_length);
			#endif
			LOG_SERIAL ("\n");
			#endif
			#endif
			wit_board->boardProtocolInterface.transmit_data_size = -1;
//			commboard_protocol_interface.protocol_stack_transmit_confirmation = true;			
		}
		else
		{
			//LOG_SERIAL ("SPI :: Transmission status error - %d \n", tx_status);
//			commboard_protocol_interface.protocol_stack_transmit_confirmation = false;
		}
		PARSECS_Protocol_Handler(&(wit_board->boardProtocolDescriptor), &(wit_board->boardProtocolInterface)); // calling the upper levels to update the transmission status
	}
}
//! This fucntion represents the actual PARSECS High Level Substack user API. This function should be called by the user in order to send data (PARSECS_WIT_FRAME) at the application layer
//!
//! \param boardAddress The board address of the board the data should be sent to
//! \param SourceAddress The source address of the board that sends the data
//! \param DestinationAddress The destionation address of the board the data should be sent to. This parameter must be equal to boardAddress
//! \param OperationType The OperationType field of the PARSECS_APP_API frame that should be sent
//! \param OperationControl The OperationControl field of the PARSECS_APP_API frame that should be sent
//! \param TypeID The TypeID field of the PARSECS_APP_API frame that should be sent
//! \param GenericDataAsBER The raw ber data of the OperationData field of the PARSECS_APP_API frame that should be sent
//! \param GenericDataLength The length of the raw ber data buffer designated as GenericDataAsBER
//! \param[out] resultedPacketSize The size of the resulted packet after encoding is returned to the user via this parameter
//! \return PARSECS_PROTOCOL_OK in the case the API call was successful. The indented data will be transmitted
//! \return PARSECS_PROTOCOL_ERROR_BOARD_NOT_AVAILABLE in the case the specified boardAddress value represents an unavailable or unconfigured board
//! \return PARSECS_PROTOCOL_ERROR_BUSY in the case the upper layers are busy and a new transmission cannout be handled. A retry is necessary at a later time
//! \private
int8_t PARSECS_Protocol_USER_Send(PARSECS_APP_BOARD_ADDRESS boardAddress, uint8_t SourceAddress, uint8_t DestinationAddress, PARSECS_APP_OperationType OperationType, uint8_t OperationControl, uint8_t TypeID, const uint8_t *GenericDataAsBER, uint32_t GenericDataLength, uint32_t *resultedPacketSize)
{
	int32_t result = 0;
	PARSECS_PROTOCOL_BOARD_DESCRIPTOR *board = PARSECS_BoardByAddress(boardAddress);

	if (board == NULL)
	{
		return PARSECS_PROTOCOL_ERROR_BOARD_NOT_AVAILABLE;
	}
	if (board->boardProtocolDescriptor.Transmission.resourceFree == true)
	{

		#ifdef SPI_PROTOCOL_DEBUG
		#ifdef SPI_PROTOCOL_USER_DEBUG

		LOG_SERIAL ("SPI USER -> ");
		LOG_SERIAL ("%02X - %02X ", SourceAddress, DestinationAddress);
		SPI_WIT_PrintAppOperationType(OperationType);
		LOG_SERIAL (" -");
		if (OperationControl != 0xFF)
		{
			SPI_WIT_PrintAppResponse((PARSECS_APP_OperationResponse)OperationControl);
		}
		LOG_SERIAL ("- Type: %02X Data: ", TypeID);
		
		#ifdef SPI_PROTOCOL_USER_FULL_BER_DEBUG
		uint32_t dummyIndex = 0;
		BERPrintEncodedBufferAsBer(GenericDataAsBER, &dummyIndex ,GenericDataLength);
		#endif
		LOG_SERIAL ("\n");
		#endif
		#endif
		uint32_t length = 0;
		result = PARSECS_Protocol_PresentationApplication_Layer_Transmit_Handler(board->boardProtocolDescriptor.Transmission.WITData, &length, SourceAddress, DestinationAddress, OperationType, OperationControl, TypeID,  GenericDataAsBER, GenericDataLength);
		if (result != PARSECS_PROTOCOL_OK)
		{
			#ifdef SPI_PROTOCOL_DEBUG
			#ifdef SPI_PROTOCOL_ERROR_DEBUG
			LOG_SERIAL ("SPI PRESENTATION :: ");
			SPI_WIT_PrintError((PARSECS_PROTOCOL_STATUS)result);
			#endif		
			#endif
			return result;
		}
		board->boardProtocolDescriptor.Transmission.WITDataLength = length;
		board->boardProtocolDescriptor.Transmission.resourceFree = false;
		*resultedPacketSize = length;
		return PARSECS_PROTOCOL_OK;
	}
	return PARSECS_PROTOCOL_ERROR_BUSY;
}

//! This fucntion represents the actual PARSECS High Level Substack user API. This function should be called by the user in order to check for received data at the application layer
//!
//! \param boardAddress The board address from which a reception should be checked
//! \param[out] SourceAddress The source address from which a reception should be checked
//! \param[out] DestinationAddress The destination address of the board for which the frame was indended. This parameter should be the value of the address of the board calling this API
//! \param[out] OperationType The OperationType field of the received PARSECS_APP_API will be written into this parameter
//! \param[out] OperationControl The OperationControl field of the received PARSECS_APP_API will be written into this parameter
//! \param[out] TypeID The TypeID field of the received PARSECS_APP_API will be written into this parameter
//! \param[out] GenericDataAsBER The he raw ber data of the OperationData field of the PARSECS_APP_API frame that was received will be written into this parameter
//! \param[out] GenericDataLength The length of the data written into GenericDataAsBER
//! \return PARSECS_PROTOCOL_OK The operation was succesfull and a PARSECS_APP_API was saved into the parameters of this function
//! \return PARSECS_PROTOCOL_ERROR_NO_DATA This is not an actual error but a response that there is no available received PARSECS_APP_API frame to be returned
//! \return
//! \private
int8_t PARSECS_Protocol_USER_Receive(PARSECS_APP_BOARD_ADDRESS boardAddress, uint8_t *SourceAddress, uint8_t *DestinationAddress, PARSECS_APP_OperationType *OperationType, uint8_t *OperationControl, uint8_t *TypeID, uint8_t *GenericDataAsBER, uint32_t *GenericDataLength)
{
	int32_t result = 0;
	PARSECS_PROTOCOL_BOARD_DESCRIPTOR *board = PARSECS_BoardByAddress(boardAddress);

	if (board == NULL)
	{
		return PARSECS_PROTOCOL_ERROR_BOARD_NOT_AVAILABLE;
	}

	if (board->boardProtocolDescriptor.Reception.resourceFree == false)
	{
		uint32_t length = 0;
		result = PARSECS_Protocol_PresentationApplication_Layer_Receive_Handler(board->boardProtocolDescriptor.Reception.WITData, board->boardProtocolDescriptor.Reception.WITDataLength, SourceAddress, DestinationAddress, OperationType, OperationControl, TypeID, GenericDataAsBER, &length);
		if (result != PARSECS_PROTOCOL_OK)
		{
			#ifdef SPI_PROTOCOL_DEBUG
			#ifdef SPI_PROTOCOL_ERROR_DEBUG
			LOG_SERIAL ("SPI PRES :: ");
			SPI_WIT_PrintError((PARSECS_PROTOCOL_STATUS)result);
			#endif
			#endif
			board->boardProtocolDescriptor.Reception.resourceFree = true;
			return result;
		}
		#ifdef SPI_PROTOCOL_DEBUG
		#ifdef SPI_PROTOCOL_USER_DEBUG

		LOG_SERIAL ("SPI USER <- ");
		LOG_SERIAL ("%02X - %02X ", *SourceAddress, *DestinationAddress);
		SPI_WIT_PrintAppOperationType(*OperationType);
		LOG_SERIAL (" -");
		if (*OperationControl != 0xFF)
		{			
			SPI_WIT_PrintAppResponse((PARSECS_APP_OperationResponse)(*OperationControl));
		}
		LOG_SERIAL ("- Type: %02X Data: ", *TypeID);		

		#ifdef SPI_PROTOCOL_USER_FULL_BER_DEBUG
		uint32_t dummyIndex = 0;
		BERPrintEncodedBufferAsBer(GenericDataAsBER, &dummyIndex ,length);
		#endif
		LOG_SERIAL ("\n");
		#endif
		#endif
		*GenericDataLength = length;
		board->boardProtocolDescriptor.Reception.resourceFree = true;
		return PARSECS_PROTOCOL_OK;
	}
	return PARSECS_PROTOCOL_ERROR_NO_DATA;
}

//! @}

//! @}
