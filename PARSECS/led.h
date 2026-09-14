#ifndef PARSECS_LED_H
#define PARSECS_LED_H

/* STM32 Discovery LEDs are driven from Layer 1 / demo_task, not the stack. */
#define LED_1_On()  do { } while (0)
#define LED_1_Off() do { } while (0)

#endif
