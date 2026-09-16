//! \file	PARSECS_HighLevelTask.c
//!
//! \brief	PARSECS High Level Substack Main Task implementation
//!
//! Contains the implementation of the main entry point for the PARSECS High Level Task
//! The \ref PARSECS_Protocol_Interface_Task function in this file should only be called by the operating system with a strict scheduling as defined in the PARSECS main documentation.
//! \defgroup PARSECS_RT PARSECS_RT
//! @{
//! \addtogroup PARSECS-High-Level-Substack
//! @{
//! \brief PARSECS High Level Substack
//! Contains the implementation of the PARSECS High Level Substack containing Layer 4, Layer 6 and Layer 7
#include "PARSECS_HighLevelTask.h"
#include "PARSECS_Includes.h"
#include "PARSECS_Data.h"
#include "PARSECS_Protocol_Interface.h"

//! The entry point of the PARSECS High Level Substack. This function implements the actual job of the task.
//! This function should be called as a job, periodically, by the operating system with a strict time scheduling as defined by the PARSECS documentation.
//! Before calling this function, the \ref PARSECS_Protocol_Interface_Task_Init function must be called once before the execution of the PARSECS High Level Substack task
//! \return None
void PARSECS_Protocol_Interface_Task(void)
{
	uint8_t i = 0;
	for (i = 0; i < MAX_BOARD_COUNT; i++)
	{
		if (CORE_TX_Wit_Boards[i].boardAvailable == true)
		{
			PARSECS_Protocol_Interface_Task_Slave(&CORE_TX_Wit_Boards[i]);
		}
	}
}

//! @}

//! @}
