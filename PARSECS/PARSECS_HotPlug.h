//! \file	PARSECS_HotPlug.h
//!
//! \brief	PARSECS slave hot-plug (master only)
//!
//! Decides which slaves the master polls. A slave that stops answering leaves the round-robin, so
//! the Low Level ticks go to the slaves that are still there; a slave that starts answering on a
//! known address is registered, its link restarted on both ends, and handed to the High Level.
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup PARSECS-Low-Level-Substack
//! @{
#ifndef __PARSECS_HOTPLUG_H
#define __PARSECS_HOTPLUG_H

#include "PARSECS_Data.h"

#ifdef SPI_MASTER
void PARSECS_HotPlug_Init(void);
void PARSECS_HotPlug_Task(void);
SLAVE *PARSECS_HotPlug_Probe_Target(void);
uint8_t PARSECS_HotPlug_Active_Count(void);
#endif

#endif

//! @}

//! @}
