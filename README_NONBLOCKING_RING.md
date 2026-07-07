# Non-blocking I2C demo (ring buffer)

Two STM32F407G-DISC1 boards talk over I2C1. The master sends `"hello N"`, the
slave upper-cases it and sends `"HELLO N"` back in the same round. Everything is
non-blocking and byte-by-byte: the transfers run under the I2C1 interrupt via the
HAL sequential API, so the FreeRTOS tasks never block inside HAL.

## Wiring

- PB6 = SCL, PB7 = SDA between the two boards, common GND
- 4.7k pull-ups on SDA/SCL to 3.3V
- I2C1 at 400 kHz, 7-bit addressing, slave address `0x08`
- Each board prints status over USB CDC to a PC terminal

## How it works

Master `MyDemoTask()`, once per tick:

- queues a new `"hello N"` into the TX ring (rate-limited)
- if the bus is idle, kicks off a transfer (first byte only; the ISR streams the rest)
- prints the echo once the ISR flags it
- re-inits I2C if a round stalls or errors pile up

The slave stays in interrupt listen mode. `AddrCallback` arms the first byte, the
per-byte Rx/Tx complete callbacks stream the rest, the request is upper-cased into
the reply buffer on the last received byte, and `ListenCpltCallback` closes the
round and re-arms listen. Completed rounds go into a log ring the task drains to USB.

The payload is a fixed 16 bytes each way, NUL-padded, so neither side needs a
length byte. `MSG_LEN` must match on both boards.

## NVIC note

CubeMX generated the projects without the I2C1 EV/ER interrupts enabled (the first
demo only used the blocking API). `HAL_I2C_MspInit` now enables them at priority 5,
and `stm32f4xx_it.c` forwards `I2C1_EV/ER_IRQHandler` to the HAL. If you regenerate
from the `.ioc`, re-enable the I2C1 interrupts first or these edits get wiped.

## LEDs (GPIOD, same on both boards)

- LD4 green  - heartbeat, toggles on each healthy round
- LD6 blue   - activity (master: round on the wire; slave: USB log line)
- LD3 orange - warning: USB busy / ring full / echo mismatch (retried)
- LD5 red    - fault: solid while re-syncing, clears on the next good round
