//! \file	PARSECS_HighLevelTask.h
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
#ifndef __PARSECS_HIGHLEVELTASK_H
#define __PARSECS_HIGHLEVELTASK_H

void PARSECS_Protocol_Interface_Task(void);

#endif


//! @}

//! @}
