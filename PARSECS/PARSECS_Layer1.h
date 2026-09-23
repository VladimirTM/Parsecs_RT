//! \file	PARSECS_Layer1.h
//!
//! \brief	PARSECS Layer 1
//!
//! Contains the implementation of the PARSECS Layer 1 for STM32 I2C DMA
//! (master write-then-read, or slave listen mode).
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup PARSECS-Low-Level-Substack
//! @{
#ifndef __PARSECS_LAYER1_H
#define __PARSECS_LAYER1_H

#include "PARSECS_Data.h"
#include "PARSECS_Includes.h"

void PARSECS_Layer1_Init(void);
void PARSECS_LAYER1(SLAVE *slave);

#ifdef SPI_MASTER
bool PARSECS_Layer1_HoldLinesLow(void);
void PARSECS_Layer1_ReleaseLines(void);
#endif

#endif

//! @}

//! @}
