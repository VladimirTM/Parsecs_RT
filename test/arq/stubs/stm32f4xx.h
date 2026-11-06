/* Host stand-in for the CMSIS interrupt intrinsics used by PARSECS_Port.h. */
#ifndef STM32F4XX_HOST_STUB_H
#define STM32F4XX_HOST_STUB_H

#include <stdint.h>

static inline uint32_t __get_PRIMASK(void) { return 0U; }
static inline void __disable_irq(void) { }
static inline void __set_PRIMASK(uint32_t primask) { (void)primask; }

#endif
