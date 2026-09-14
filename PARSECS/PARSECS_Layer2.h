//! \file	PARSECS_Layer2.h
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
#ifndef __PARSECS_LAYER2_H
#define __PARSECS_LAYER2_H

#include "PARSECS_Includes.h"
#include "PARSECS_Data.h"

void PARSECS_TRANSMIT_LAYER2(SLAVE *slave);
void PARSECS_RECEIVE_LAYER2(SLAVE *slave);

#endif

//! @}

//! @}
