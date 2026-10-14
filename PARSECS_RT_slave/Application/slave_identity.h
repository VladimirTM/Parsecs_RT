#ifndef SLAVE_IDENTITY_H
#define SLAVE_IDENTITY_H

#include "PARSECS_Data.h"

/*
 * Which physical slave this image is. Rebuild and flash once per board.
 *   0  comm board,     I2C 0x08, GetResponse value 42
 *   1  mobility board, I2C 0x09, GetResponse value 43
 * Override from the compiler with -DPARSECS_SLAVE_INDEX=1.
 */
#ifndef PARSECS_SLAVE_INDEX
#define PARSECS_SLAVE_INDEX 0
#endif

#if PARSECS_SLAVE_INDEX == 0
#define PARSECS_SLAVE_I2C_ADDR_7BIT  PARSECS_I2C_ADDR_COMM_7BIT
#define PARSECS_SLAVE_BOARD          CORE_TX_WIT_COMM_BOARD
#define PARSECS_SLAVE_VALUE          42
#define PARSECS_SLAVE_NAME           "comm"
#elif PARSECS_SLAVE_INDEX == 1
#define PARSECS_SLAVE_I2C_ADDR_7BIT  PARSECS_I2C_ADDR_MOBILITY_7BIT
#define PARSECS_SLAVE_BOARD          CORE_TX_WIT_MOBILITY_BOARD
#define PARSECS_SLAVE_VALUE          43
#define PARSECS_SLAVE_NAME           "mobility"
#else
#error PARSECS_SLAVE_INDEX must be 0 (comm, 0x08) or 1 (mobility, 0x09)
#endif

#endif
