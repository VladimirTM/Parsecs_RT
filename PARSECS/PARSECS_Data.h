//! \file	PARSECS_Data.h
//!
//! \brief	PARSECS Data Structures definitions
//!
//! Contains the definitions of the data structures and variables used by PARSECS_RT Stack borth PARSECS Low Level Substack and PARSECS High Level Substack
//! Layer 1 is STM32 I2C DMA in this port; L2/L3 remain platform-independent.
//! Only the implementation for Layer 1 differs from Master Node to Slave Node, being node and platform dependent
//! The rest of the layer implementations are platform and node independent
//! \addtogroup PARSECS_RT
//! @{
/*! \page PARSECS_RT Flag Definition
<table>
<caption id="parsecs_flag_definition">PARSECS_RT Flag Definition</caption>
<tr><th>BIT<th>31<th>30<th>29<th>28<th>...<th>25<th>...<th>18<th>17<th>...<th>15<th>14<th>13<th>12<th>11<th>10<th>9
<tr><td>FLAG NAME<td>FG_LL_FRIP_1<td>FG_LL_FRIP_2<td>FG_LL_FUSE_1<td>FG_LL_FUSE_2<td>...<td>FG_HL_FTTR<td>...<td>FG_LL_FTT<td>FG_LL_FTIP<td>...<td>FG_ACK<td>FG_CMD<td>FG_NACK<td>FG_NFE_1<td>FG_NFE_2<td>FG_HL_NDF_1<td>FG_HL_NDF_2
<tr><td>FLAG MASK<td>0x80000000<td>0x40000000<td>0x20000000<td>0x10000000<td>...<td>0x02000000<td>...<td>0x00040000<td>0x00020000<td>...<td>0x00008000<td>0x00004000<td>0x00002000<td>0x00001000<td>0x00000800<td>0x00000400<td>0x00000200
</table>

**/

#ifndef __PARSECS_DATA
#define __PARSECS_DATA

#include "PARSECS_Includes.h"

#include "PARSECS_Mode.h"
#include "PARSECS_Debug.h"

/*!
 * \def START_OF_FRAME
 * Value of the start of frame byte
 */
/*!
 * \def JUNKEY
 * Value JUNKEY byte - byte used as dummy transmission when only receiving (in order to generate clock over SPI without actually sending data)
 */
/*!
 * \def ACK
 * Value acknowledge type byte - type of frame ACK
 */
/*!
 * \def NACK
 * Value not-acknowledge typ[e byte - type of frame NACK
 */
/*!
 * \def CMD
 * Value command type byte - type of frame CMD
 */
/*!
 * \def TYPE_DATA
 * Value data type byte - type of frame DATA
 */
#define START_OF_FRAME 		0x7E
#define JUNKEY 				0xFF
#define ACK 				0x7C
#define NACK 				0x7B
#define PARSECS_CMD			0x9B
#define TYPE_DATA			0x8C


#define FG_LL_FRIP_1			0x80000000 //Frame reception in progress in buffer 1 - currenly implemented using slave[i].spi_packet_rx_1.frame_in_progress
#define FG_LL_FRIP_2			0x40000000 //Frame reception in progress in buffer 2 - currenly implemented using slave[i].spi_packet_rx_1.frame_in_progress
#define FG_LL_FUSE_1			0x20000000 //Frame in use in buffer 1 - currently implemented using slave[i].spi_packet_rx_1.packet_ok
#define FG_LL_FUSE_2			0x10000000 //Frame in use in buffer 2 - currently implemented using slave[i].spi_packet_rx_1.packet_ok
#define FG_HL_FTTR	 			0x02000000 //High Level Frame to Transmit - comunicatie high level stack transmit cu layer 3/2 transmit
#define FG_LL_FTT 				0x00040000 //Low Level FRAME  TO TRANSMIT
#define	FG_LL_FTIP				0x00020000 //Low Level FRAME TRANSMIT IN PROGRESS
#define FG_ACK					0x00008000 //ACK
#define FG_CMD					0x00004000 //COMMAND
#define FG_NACK					0x00002000 //NACK
#define	FG_NFE_1				0x00001000 //NEW FRAME END 1
#define	FG_NFE_2				0x00000800 //NEW FRAME END 2
#define FG_HL_NDF_1				0x00000400 //HL NEW DATA FRAME RECEIVED
#define FG_HL_NDF_2				0x00000200 //HL NEW DATA FRAME RECEIVED

/*!
 * \def SLAVE_COUNT
 * Defines the number of maximum Low Level slaves supported. Master keeps the
 * original table size; this I2C port only calls PARSECS_Add_Slave once.
 */
/*!
 * \def MAX_BOARD_COUNT
 * High Level board table size. I2C has one peer, so this is 1 on both nodes.
 * USER_Send / USER_Receive look up the descriptor by APP board address, they
 * do not use the WIT enum as an array index (CORE_TX_WIT_COMM_BOARD is 6).
 */
#ifdef SPI_MASTER
#define SLAVE_COUNT 				8
#else
#define SLAVE_COUNT					1
#endif
#define MAX_BOARD_COUNT				1

/*!
 * \def SPI_DATA_LENGTH
 * Defines the maximum size of the raw SPI data buffer for an SPI_BASE_FRAME without any encapsulation. Defines the maximum payload of an SPI_BASE_FRAME
 */
#define SPI_DATA_LENGTH				254

/*!
 * \def DATA_BUFFER_SIZE
 * Same as \ref SPI_DATA_LENGTH
 */
/*!
 * \def DATA_ENCODING_SIZE
 * Defines the size of the encapsulation added by an SPI_BASE_FRAME
 */
/*!
 * \def RAW_RING_BUFFER_SIZE
 * Defines the size of the ring buffers
 */

#define DATA_BUFFER_SIZE			SPI_DATA_LENGTH
#define DATA_ENCODING_SIZE			6
#define RAW_RING_BUFFER_SIZE		16

#if (SPI_DATA_LENGTH < 4)
#error SPI_DATA_LENGTH must be greater than 4
#endif

#if (DATA_BUFFER_SIZE > 254)
#error SPI_DATA_LENGTH must not exceeed 254
#endif

/*! \page PARSECS_RT_Buffer_size_restrictions
 * Restrictions that apply on the size of the buffers
-# \f$ SPI\_DATA\_LENGTH >= 4 \f$
-# \f$ SPI\_DATA\_LENGTH <= 254 \f$
*
*
*If any of these restrictons are not met, a compiler error is generated through code
**/

//! \typedef SPI_SLAVE_FUNCTION
//! A function pointer that defines the select and deselect functions for the slave.
//! The select function will select the slave by making line for SSEL logic 0.
//! The deselect function will select the slave by making line for SSEL logic 1.
typedef void (*SPI_SLAVE_FUNCTION)(void);


//! \struct SPI_BASE_FRAME
//! Structure that defines the SPI_BASE_FRAME
//! \private
typedef struct
{
	uint8_t packet_type; 						/**<Packet Type (TYPE). May be \ref ACK, \ref NACK, \ref CMD, \ref TYPE_DATA*/
	uint8_t app_buffer[DATA_BUFFER_SIZE];  		/**<Byte buffer containing the Data Field (DATA FIELD)*/
	uint8_t app_buffer_length;					/**<The current length of the data field (LEN)*/
	uint8_t packet_length;						/**<The current length of the data field (LEN) - used when assembling the packet*/
	volatile uint8_t packet_ok; 				/**<Validates the current packet - 1 packet is valid - 0 packet is not valid*/
	uint8_t seq_no;								/**<Contains the current sequence number of the packet (SEQ)*/
	CRC16 crc16_object;							/**<Contains the current CRC of the packet. Used also to calculate the CRC incrementally */
	volatile bool frame_in_progress;			/**<States that the packet is currently in progress of being assembled but not yet valid*/
}SPI_BASE_FRAME;

//! \struct SLAVE
//! Structure that defines the internal behaviour and states of the protocol for one slave. The whole protocol keeps its states and values for each slave separately through this structure
//! \private
typedef struct 
{
	SPI_SLAVE_FUNCTION select_function; 							/**<Pointer to a function that selects the slave through SSEL*/
	SPI_SLAVE_FUNCTION deselect_function;							/**<Pointer to a function that deselects the slave through SSEL*/
	SPI_BASE_FRAME spi_packet_tx;									/**<SPI_BASE_FRAME for transmission - the layer 2 TX buffer*/
	SPI_BASE_FRAME spi_packet_rx_1;									/**<SPI_BASE_FRAME 1 for reception - the layer 2 RX 1 buffer*/
	SPI_BASE_FRAME spi_packet_rx_2;									/**<SPI_BASE_FRAME 2 for reception - the layer 2 RX 2 buffer*/
	tRingBufObject l1_buffer_rx;									/**<Layer 1 ring buffer for rx*/
	tRingBufObject l1_buffer_tx;									/**<Layer 1 ring buffer for tx*/
	uint8_t current_seq_no;											/**<Current sequence number*/
	volatile uint8_t ub_L2_RecState;								/**<Layer 2 reception state machine current state*/
	volatile uint8_t ub_L2_TransState;  							/**<Layer 2 transmission state machine current state*/
	uint16_t layer2_transmit_packet_index; 							/**<Layer 2 transmission data index*/
	uint8_t uw_L2_ByteCount0;										/**<Layer 2 reception data index*/
	bool slave_enabled;												/**<States if the slave structure is enabled, configured and valid*/
	uint8_t buff_rx1[RAW_RING_BUFFER_SIZE];							/**<Layer 1 memory zone for ring buffer for rx*/
	uint8_t buff_tx1[RAW_RING_BUFFER_SIZE];							/**<Layer 1 memory zone for ring buffer for tx*/
	volatile uint32_t STATUS_FLAG ;									/**<Holds the status flags*/
	volatile uint8_t last_received_packet_sequence_number;			/**<Holds the SEQ for the last received packet*/
	volatile uint8_t last_transmitted_data_packet_sequence_number;	/**<Holds the SEQ for the last transmitted data packet*/
	volatile uint8_t last_ACKed_packet_sequence_number;				/**<Holds the SEQ for the last acknowledged data packet*/
	volatile uint8_t last_NACKed_packet_sequence_number;			/**<Holds the SEQ for the last acknowledged data packet*/
	uint16_t uw_crc16; 												/**<Holds current assembled values of the CRC*/
}SLAVE;

/*!
 * \def PARSECS_WIT_PDU_SIZE
 * The size of the whole PARSECS_WIT_PDU - should be equal to \ref SPI_DATA_LENGTH thus it is extracted from this field
 */
/*!
 * \def PARSECS_WIT_PDU_DATA_SIZE
 * The size of the payload of the PARSECS_WIT_PDU. It should be the size of the whole PARSECS_WIT_PDU minus the encapsulation (4 bytes)
 */
/*!
 * \def PARSECS_WIT_FRAME_LENGTH
 * The size of the payload of the PARSECS_WIT_FRAME
 */

#define PARSECS_WIT_PDU_SIZE		SPI_DATA_LENGTH
#define PARSECS_WIT_PDU_DATA_SIZE	(PARSECS_WIT_PDU_SIZE - 4)
#define PARSECS_WIT_FRAME_LENGTH	1024


//! \enum PARSECS_PROTOCOL_STATUS
//! \typedef PARSECS_PROTOCOL_STATUS
//! Describes the possible return values of the private or public function of the PARSECS_RT implementation
typedef enum
{
	PARSECS_PROTOCOL_OK = 0,															/**<Error code for a successfull operation*/
	PARSECS_PROTOCOL_ERROR_PDU_DECODE_NULL_POINTER = -1,								/**<Function was called with NULL pointers*/
	PARSECS_PROTOCOL_ERROR_PDU_DECODE_PDU_TOO_SMALL = -2,								/**<Data buffer is too small to hold a correct SPI_WIT_PDU*/
	PARSECS_PROTOCOL_ERROR_PDU_DECODE_INCORRECT_TOTAL_PDU_PACKETS = -3,					/**<Incorrect value (<1) of the decoded TotalPDUPackets in an SPI_WIT_PDU*/
	PARSECS_PROTOCOL_ERROR_PDU_DECODE_INCORRECT_CURRENT_PDU_PACKET_NUMBER = -4,			/**<Incorrect value of the decoded CurrentPDUPacket in an SPI_WIT_PDU*/
	PARSECS_PROTOCOL_ERROR_PDU_DECODE_INSUFFICIENT_DATA_SIZE = -5,						/**<Insufficient data in buffer to decode the whole SPI_WIT_PDU - not enough bytes*/
	PARSECS_PROTOCOL_ERROR_RECEIVED_SEQUENCING_ERROR = -6,								/**<Sequence error bit was set in WIT PDU TYPE FORMAT of the received SPI_WIT_PDU*/
	PARSECS_PROTOCOL_ERROR_DETECTED_SEQUENCING_ERROR = -7,								/**<Sequence error bit was set in WIT PDU TYPE FORMAT of the received SPI_WIT_PDU*/
	PARSECS_PROTOCOL_ERROR_RECEIVED_SEGMENTATION_PARAMETERS = -8,						/**<Received SPI_WIT_PDU with incorrect segmentation parameters*/
	PARSECS_PROTOCOL_ERROR_SEGMENTATION_SIZE_EXCEEDED = -9,								/**<In a segmented transaction, the resulted assembled PDU exceeds the value of \f$ F_{MAX} \f$ implemented as \ref PARSECS_WIT_FRAME_LENGTH*/
	PARSECS_PROTOCOL_ERROR_BUSY = -10,													/**<Protocol upper layer busy */
	PARSECS_PROTOCOL_ERROR_NO_DATA = -11,												/**<Protocol upper layer no data for reception */
	PARSECS_PROTOCOL_ERROR_BER_DECODE_ERROR = -12,										/**<Presentation layer error - General error while decoded BER data format - Protocol upper layer BER decode error*/
	PARSECS_PROTOCOL_ERROR_INCORRECT_BER_FORMAT = -13,									/**<Presentation layer error - BER decode was ok but the format was incorrect or unexpected */
	PARSECS_PROTOCOL_ERROR_INCORRECT_BER_LENGTH = -14,									/**<Protocol upper layer incorrect BER length */
	PARSECS_PROTOCOL_ERROR_SIZE_EXCEEDED = -15,											/**<Protocol upper layer size exceeded. Decoding would exceed \ref PARSECS_WIT_FRAME_LENGTH*/
	PARSECS_PROTOCOL_ERROR_BER_ADDITION_JUNK_FOUND = -16,								/**<Protocol upper layer additional junk data found after BER decode - data found outside presentation layer encoding*/
	PARSECS_PROTOCOL_ERROR_BOARD_NOT_AVAILABLE = -17,									/**<Protocol upper layer board not available - API was called by user for an unavailable or unconfigured board*/
	PARSECS_PROTOCOL_ERROR_INCORRECT_APP_PARAMETERS = -18,								/**<Protocol upper layer incorrect app parameters - Application layer parameters are incorrect */
	PARSECS_PROTOCOL_ERROR_UNKNOWN_ERROR = -128,										/**<Unknown or undocumented protocol error */
}PARSECS_PROTOCOL_STATUS;


//! \struct PARSECS_WIT_PDU
//! Structure that defines the PARSECS_WIT_PDU
//! \private
typedef struct
{
	bool MultiPacketPdu;									/**<The fied MultiPacketPdu from WIT PDU TYPE FORMAT - true if a multi packet pdu receive/transmit is in process*/
	bool LastPduInMultipacket;								/**<The fied LastPduInMultipacket from WIT PDU TYPE FORMAT - true if the current \ref PARSECS_WIT_PDU is the last segment*/
	bool SequenceError;										/**<The fied SequenceError from WIT PDU TYPE FORMAT - true if a sequence number error was detected*/
	bool SizeExceeded;										/**<The fied SizeExceeded from WIT PDU TYPE FORMAT - true if the assembly process reported a size exceed, the size is greater than \f$ F_{MAX} \f$ implemented as \ref PARSECS_WIT_FRAME_LENGTH*/
	bool Error;												/**<The fied Error from WIT PDU TYPE FORMAT - true if an undocumented, generic error was reported*/
	uint8_t TotalPDUPackets;								/**<the TotalPDUPackets - the total pdu segments in a multi-packet (multi-segment) transmission */
	uint8_t CurentPDUPacket;								/**<the CurentPDUPacket - the current pdu segment in a multi-packet (multi-segment) transmission */
	uint8_t PDUDataLength;									/**<the PDUDataLength - the length of the  data in the PDUData field*/
	uint8_t PDUData[PARSECS_WIT_PDU_DATA_SIZE];				/**<Hold the actual data (the payload) of the current  PARSECS_WIT_PDU*/
}PARSECS_WIT_PDU;

//! \struct PARSECS_STATE_DATA
//! Structure that defines the PARSECS_STATE_DATA
//! it contains the current state variables and data of a transmission or reception as detailed in the PARSECS_RT documentation. It is applied for transmission and/or reception separately.
//! \private
typedef struct
{
	volatile bool resourceFree;								/**<Signifies if this current resource is free to be used (true) or busy*/
	bool isInMultiPacketState;								/**<The current RX/TX process is currently in a multi packet transaction state*/
	bool LastPduInMultipacket;								/**<The current RX/TX process signaled that, in a multi packet transaction, the last PDU was received/transmitted*/
	uint8_t TotalPDUPackets;								/**<In the current RX/TX process, in a multi packet transaction, this field holds the number of the total pdu packets involed. If not in a multi packet transaction this field should be 1*/
	uint8_t CurentPDUPacket;								/**<In the current RX/TX process, in a multi packet transaction, this field holds the number of the current pdu packet involed. If not in a multi packet transaction this field should be 1*/
	uint16_t WITDataLength;									/**<Contains the total length of the WITData - the length of a PARSECS WIT FRAME */
	uint16_t CurrentDataIndex;								/**<Contains the current index where the next segment will saves/retrieved from/to  WITData*/
	uint8_t LastSequenceNumber;								/**<An integer representing the last sequence number of the transmitted/received PARSECS WIT PDU taken from the lower levels of the stack through the SPI Base Frame*/
	uint8_t WITData[PARSECS_WIT_FRAME_LENGTH];				/**<An instance of a PARSECS WIT FRAME */
}PARSECS_STATE_DATA;


//! \struct PARSECS_PROTOCOL_Descriptor_Type
//! Structure that defines the PARSECS_PROTOCOL_Descriptor_Type. Each instance of this structure defines one slave module when using the whole stack (Low Level + High Level).
//! The protocol uses such a structure as an object and keeps an image of the whole communication process for each slave board
//! \private
typedef struct
{
	PARSECS_WIT_PDU ReceivePDU;								/**<Hold a received \ref PARSECS_WIT_PDU*/
	PARSECS_WIT_PDU TransmitPDU;							/**<Hold a \ref PARSECS_WIT_PDU to be transmitted*/
	PARSECS_STATE_DATA Reception;							/**<Hold the image for the whole reception process for a slave (state + assembled data)*/
	PARSECS_STATE_DATA Transmission;						/**<Hold the image for the whole transmission process for a slave (state + assembled data)*/
	bool ErrorTransmitRequired;								/**<A flag used by the reception flow to inform the transmission flow that an error packet needs to be transmitted */
	PARSECS_PROTOCOL_STATUS ErrorCode;						/**<In case of \ref ErrorTransmitRequired is true, this field contains the error code */
}PARSECS_PROTOCOL_Descriptor_Type;


//! \enum PARSECS_PROTOCOL_COMMAND
//! \typedef PARSECS_PROTOCOL_STATUS
//! Internal enumeration holding the possible commands that the PARSECS High Level Task transmits to itself from one job execution to another
//! \private
typedef enum
{
	PARSECS_PROTOCOL_COMMAND_IDLE,							/**<No command from the previous job execution was received */
	PARSECS_PROTOCOL_COMMAND_IO_OPERATION,					/**<The previos job execution requested both a reception and transmission operation */
	PARSECS_PROTOCOL_COMMAND_RECEIVE_OPERATION_ONLY,		/**<The previos job execution requested a reception operation */
	PARSECS_PROTOCOL_COMMAND_TRANSMIT_OPERATION_ONLY,		/**<The previos job execution requested a transmission operation */
	PARSECS_PROTOCOL_COMMAND_TRANSMIT_STATUS_UPDATE,		/**<The previos job execution requested a transmission status update operating */
}PARSECS_PROTOCOL_COMMAND;

//! \struct PARSECS_Protocol_Communication_Interface
//! Structure that is used by the PARSECS High Level Protocol Task to transfer information between job executions.
//! \private
typedef struct
{
	PARSECS_PROTOCOL_COMMAND command;									/**<Holds the command that needs to be transmitted from one job to another implemented as \ref PARSECS_PROTOCOL_COMMAND*/

	uint8_t receive_raw_data_buffer[PARSECS_WIT_PDU_SIZE];				/**<Holds the data of the previously received \ref PARSECS_WIT_PDU */
	int16_t receive_data_size;											/**<Holds the size of the data of the previously received \ref PARSECS_WIT_PDU */
	uint8_t receive_seq_number;											/**<Holds the sequence number SPI Base Frame that contained the previously received \ref PARSECS_WIT_PDU */

	uint8_t transmit_raw_data_buffer[PARSECS_WIT_PDU_SIZE];				/**<Holds the data of the next \ref PARSECS_WIT_PDU that needs to be transmitted*/
	int16_t transmit_data_size;											/**<Holds the size of data of the next \ref PARSECS_WIT_PDU that needs to be transmitted*/
	uint8_t transmit_seq_number;										/**<Holds the sequence number of the SPI_Base_Frame of the next \ref PARSECS_WIT_PDU that was transmitted*/

	uint8_t last_ACKed_packet_sequence_number;							/**<Holds the last acknowledged sequence number */
	uint8_t last_NACKed_packet_sequence_number;							/**<Holds the last not-acknowledged sequence number */

}PARSECS_Protocol_Communication_Interface;


//! \enum PARSECS_APP_OperationType
//! \typedef PARSECS_APP_OperationType
//! Defines the Operation Type field of the PARSERCS_APP_API sequence that is encapsulated by the PARSECS_WIT_FRAME. Practically it defines the supported operations that may be performed by using the PARSECS Protocol Stack
typedef enum
{
	PARSECS_APP_GetRequest = 1,						/**<Requesting the value of a parameter*/
	PARSECS_APP_GetResponse = 2,					/**<Response when a parameter value is requested */
	PARSECS_APP_SetRequest = 3,						/**<Requesting the change io the value of a parameter */
	PARSECS_APP_SetResponse = 4,					/**<Response when a parameter value change is requested */
	PARSECS_APP_CallRequest = 5,					/**<Request for calling a remote action method */
	PARSECS_APP_CallResponse = 6,					/**<Response when a remote method is being called */
}PARSECS_APP_OperationType;

//! \enum PARSECS_APP_OperationResponse
//! \typedef PARSECS_APP_OperationResponse
//! Defines the Operation Type response when a request has been made using the request types defined in \ref PARSECS_APP_OperationType.
//! These values are usualyy carried by a response type as defined in \ref PARSECS_APP_OperationType.
typedef enum
{
	PARSECS_APP_Success,							/**<Operation was successful */
	PARSECS_APP_HardwareFault,						/**<Operation failed because of a hardware malfunction or imcorrectly configured or used hardware */
	PARSECS_APP_TemporaryFailure,					/**<Operation failed temporary. May be retried later */
	PARSECS_APP_ReadDenied,							/**<Parameter may not be read */
	PARSECS_APP_WriteDenied,						/**<Parameter may not be written or changed. Paramter is read-only */
	PARSECS_APP_ParameterMethodUndefined,			/**<Request of an operation for a parameter or method that is not defined or supported */
	PARSECS_APP_OperationTimeout,					/**<Operation failed with a timeout */
	PARSECS_APP_ParameterSyntaxError,				/**<The operation was not called properly. The structure of the perameter(s) is not correct */
	PARSECS_APP_OperationUnsupported,				/**<The operation is not supported */
	PARSECS_APP_AddressMismatch,					/**<The board or slave address is not available */
	PARSECS_APP_OtherReason = 254,					/**<Defines a generic error */
	PARSECS_APP_UnknownValue = 255,					/**<Defines an unknown or undocumented error */
}PARSECS_APP_OperationResponse;


//! \enum PARSECS_APP_BOARD_ADDRESS
//! \typedef PARSECS_APP_BOARD_ADDRESS
//! Defines the addresses of the boards (modules) inside a WIT according to the CORE-TX structure
typedef enum
{
	CORE_TX_WIT_MOTHERBOARD = 0,					/**<The generic address of the motherboard. Sused only by a slave board in order to address the motherboard */
	CORE_TX_WIT_PM_BOARD = 1,						/**<The address of the power management board. Used only by the motherboard */
	CORE_TX_WIT_BOARD_NA2 = 2,						/**<Address not yet defined. Reserved. */
	CORE_TX_WIT_BOARD_NA3 = 3,						/**<Address not yet defined. Reserved. */
	CORE_TX_WIT_BOARD_NA4 = 4,						/**<Address not yet defined. Reserved. */
	CORE_TX_WIT_BOARD_NA5 = 5,						/**<Address not yet defined. Reserved. */
	CORE_TX_WIT_COMM_BOARD = 6,						/**<The address of the communication board. Used only by the motherboard */
	CORE_TX_WIT_MOBILITY_BOARD = 7,					/**<The address of the mobility board. Used only by the motherboard */
}PARSECS_APP_BOARD_ADDRESS;

//! \struct PARSECS_PROTOCOL_BOARD_DESCRIPTOR
//! Structure that is used by the PARSECS High Level Protocol Task. Each instance of this structure defines one slave board of the WIT when using the whole stack (Low Level + High Level).
//! This is the main data structure that the protocol operates on.
//! \private
typedef struct
{
	PARSECS_PROTOCOL_Descriptor_Type boardProtocolDescriptor;			/**<Contains the protocol descriptor which keeps a private protocol image for each board */
	PARSECS_Protocol_Communication_Interface boardProtocolInterface;	/**<Contains the communication interface used by one job execution of the protocol to communicate with the next job execution */
	int8_t boardSlaveID;												/**<Contains the slave id relative to the PARSECS Low Level Substack obtained by calling \ref PARSECS_Add_Slave*/
	PARSECS_APP_BOARD_ADDRESS boardAddress;								/**<Contains the board address of the WIT according to \ref PARSECS_APP_BOARD_ADDRESS*/
	bool boardAvailable;												/**<Validity field. Validates the current instance of this structure */
}PARSECS_PROTOCOL_BOARD_DESCRIPTOR;


extern SLAVE slaves[SLAVE_COUNT];

#ifdef SPI_MASTER
extern uint8_t number_of_active_slaves;
extern int8_t slave_id_comm_board;
#endif

extern PARSECS_PROTOCOL_BOARD_DESCRIPTOR CORE_TX_Wit_Boards[MAX_BOARD_COUNT];

void PARSECS_Data_Init(void);
void PARSECS_InitRingBuffers(SLAVE *slave);

#endif


//! @}
