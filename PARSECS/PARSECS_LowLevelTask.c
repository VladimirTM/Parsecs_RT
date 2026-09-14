//! \file	PARSECS_LowLevelTask.c
//!
//! \brief	PARSECS Low Level Task definition
//!
//! Contains the implementation of the main entry point for the PARSECS Low Level Task as well as the initialization function of the task
//!
//! The functions in this file are to be called by the operating system with a strict scheduling as defined in the PARSECS main documentation.
//! The \ref PARSECS_LowLevelTaskInit should be called at the init phase
//! and the \ref PARSECS_LowLevelTask function should be called periodically as a job
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup PARSECS-Low-Level-Substack
//! @{
#include "PARSECS_LowLevelTask.h"
#include "PARSECS_Includes.h"
#include "PARSECS_Layer1.h"
#include "PARSECS_Layer2.h"
#include "PARSECS_Layer3.h"


//! Initialization function for the PARSECS Low Level Substack. This function should be called by the operating system before any execution of the job represented by \ref PARSECS_LowLevelTask
//! \return None
void PARSECS_LowLevelTaskInit(void)
{
	PARSECS_Layer1_Init();
	PARSECS_Data_Init();
}

//! The entry point of the PARSECS Low Level Substack. This function implements the actual job of the task.
//! This function should be called as a job, periodically, by the operating system with a strict time scheduling as defined by the PARSECS documentation.
//! Before calling this function, the \ref PARSECS_LowLevelTaskInit function must be called once before the execution of the PARSECS Low Level Substack task
//! \return None
void PARSECS_LowLevelTask(void)
{
#ifdef SPI_MASTER
	static uint8_t i = 0;
	uint8_t j = 0;
	uint8_t slave_processed_during_modx_execution = 0;
	if (number_of_active_slaves > 0)
	{
		while (!slave_processed_during_modx_execution)
		{
			if (slaves[i].slave_enabled == 1)
			{
				PARSECS_TRANSMIT_LAYER3(&slaves[i]);
				PARSECS_TRANSMIT_LAYER2(&slaves[i]);
				PARSECS_LAYER1(&slaves[i]);
				PARSECS_RECEIVE_LAYER2(&slaves[i]);
				PARSECS_RECEIVE_LAYER3(&slaves[i]);
				slave_processed_during_modx_execution = 1;
			}

			i = (i + 1) % SLAVE_COUNT;
			j++;
			if (j > (SLAVE_COUNT + 1))
			{
				break;
			}
		}
	}
#else
	PARSECS_TRANSMIT_LAYER3(slaves);
	PARSECS_TRANSMIT_LAYER2(slaves);
	PARSECS_LAYER1(slaves);
	PARSECS_RECEIVE_LAYER2(slaves);
	PARSECS_RECEIVE_LAYER3(slaves);
#endif
}

//! @}

//! @}
