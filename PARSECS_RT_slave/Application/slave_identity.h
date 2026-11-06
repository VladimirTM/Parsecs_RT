#ifndef SLAVE_IDENTITY_H
#define SLAVE_IDENTITY_H

#include "PARSECS_Data.h"

/*
 * Which physical slave this image is. Rebuild and flash once per board.
 *   0  comm board,     I2C 0x08, GetResponse value 42
 *   1  mobility board, I2C 0x09, GetResponse value 43
 *   2  pm board,       I2C 0x0A, GetResponse value 44
 *   3  board2,         I2C 0x0B, GetResponse value 45
 *   4  board3,         I2C 0x0C, GetResponse value 46
 *   5  board4,         I2C 0x0D, GetResponse value 47
 *   6  board5,         I2C 0x0E, GetResponse value 48
 * The master looks for all seven (hot-plug), so any of them can be plugged in
 * while it runs. Override from the compiler with -DPARSECS_SLAVE_INDEX=2.
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
#elif PARSECS_SLAVE_INDEX == 2
#define PARSECS_SLAVE_I2C_ADDR_7BIT  PARSECS_I2C_ADDR_PM_7BIT
#define PARSECS_SLAVE_BOARD          CORE_TX_WIT_PM_BOARD
#define PARSECS_SLAVE_VALUE          44
#define PARSECS_SLAVE_NAME           "pm"
#elif PARSECS_SLAVE_INDEX == 3
#define PARSECS_SLAVE_I2C_ADDR_7BIT  PARSECS_I2C_ADDR_BOARD2_7BIT
#define PARSECS_SLAVE_BOARD          CORE_TX_WIT_BOARD_NA2
#define PARSECS_SLAVE_VALUE          45
#define PARSECS_SLAVE_NAME           "board2"
#elif PARSECS_SLAVE_INDEX == 4
#define PARSECS_SLAVE_I2C_ADDR_7BIT  PARSECS_I2C_ADDR_BOARD3_7BIT
#define PARSECS_SLAVE_BOARD          CORE_TX_WIT_BOARD_NA3
#define PARSECS_SLAVE_VALUE          46
#define PARSECS_SLAVE_NAME           "board3"
#elif PARSECS_SLAVE_INDEX == 5
#define PARSECS_SLAVE_I2C_ADDR_7BIT  PARSECS_I2C_ADDR_BOARD4_7BIT
#define PARSECS_SLAVE_BOARD          CORE_TX_WIT_BOARD_NA4
#define PARSECS_SLAVE_VALUE          47
#define PARSECS_SLAVE_NAME           "board4"
#elif PARSECS_SLAVE_INDEX == 6
#define PARSECS_SLAVE_I2C_ADDR_7BIT  PARSECS_I2C_ADDR_BOARD5_7BIT
#define PARSECS_SLAVE_BOARD          CORE_TX_WIT_BOARD_NA5
#define PARSECS_SLAVE_VALUE          48
#define PARSECS_SLAVE_NAME           "board5"
#else
#error PARSECS_SLAVE_INDEX must be 0..6 (I2C 0x08..0x0E)
#endif

#endif
