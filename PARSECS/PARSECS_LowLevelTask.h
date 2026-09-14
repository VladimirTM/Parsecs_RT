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
//! \brief PARSECS Low Level Substack
//!
//! Contains the implementation of the PARSECS Low Level Substack containing Layer 1, Layer 2 and Layer 3
#ifndef __PARSECS_LOW_LEVEL_TASK
#define __PARSECS_LOW_LEVEL_TASK


void PARSECS_LowLevelTask(void);
void PARSECS_LowLevelTaskInit(void);


#endif

//! @}

//! @}
