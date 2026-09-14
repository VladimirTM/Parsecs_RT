//! \file	PARSECS_Layer3.c
//!
//! \brief	PARSECS Layer 3
//!
//! Contains the implementation of the PARSECS Layer 3
//! The implementation for PARSECS Layer 3 is platform and node independent
//! \addtogroup PARSECS_RT
//! @{
//! \addtogroup PARSECS-Low-Level-Substack
//! @{
//! \brief PARSECS Low Level Substack
//!
//! Contains the implementation of the PARSECS Low Level Substack containing Layer 1, Layer 2 and Layer 3
#ifndef __PARSECS_LAYER3_H
#define __PARSECS_LAYER3_H

#include "PARSECS_Includes.h"
#include "PARSECS_Data.h"

void PARSECS_RECEIVE_LAYER3(SLAVE *slave);
void PARSECS_TRANSMIT_LAYER3(SLAVE *slave);

#endif

//! @}

//! @}
