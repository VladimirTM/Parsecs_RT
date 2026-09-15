# Parsecs_RT

Two STM32F407G-DISC1 boards communicating over I2C using FreeRTOS. The shared
[`PARSECS/`](PARSECS/) tree is the PARSECS stack ported from
`coretx-motherboard/rpi_motherboard/SPI` (master) and
`coretx_commboard/commboard_freertos_stm32/SPI` (slave). Layer 1 is the only
node-specific piece: it keeps the I2C DMA byte pump. Layers 2–3 frame
`SPI_BASE_FRAME` and ACK/NACK. Layers 4, 6 and 7 are the WIT PDU, BER, and
APP Get/Set/Call path (`PARSECS_Protocol_USER_Send` /
`PARSECS_Protocol_USER_Receive`). The demo is one GetRequest / GetResponse
exchange (TypeID `0x01`, BER integer `42`).

Built with STM32CubeIDE / CubeMX. Two separate projects live under
`PARSECS_RT_master/` and `PARSECS_RT_slave/`. They share `PARSECS/` and
`ringbuffer/`; the master is built with `-DSPI_MASTER`.

---

## Wiring

PB6/PB7 (SCL/SDA) tied together between the two boards with a common GND, I2C1 at 400 kHz, 7-bit addressing, slave address `0x08`. See [DIAGRAMS.md](DIAGRAMS.md) for the full pinout diagram.

---

## Demos

### PARSECS High Level over I2C DMA (`main`)

The current branch. `PARSECS_LowLevelTask` runs L3 TX, L2 TX, L1, L2 RX, L3 RX
each tick. `PARSECS_Protocol_Interface_Task` then runs L4/L6/L7 on the one I2C
peer. Layer 1 still moves **one byte each way per I2C round** with
`HAL_I2C_*_Seq_*_DMA` (DMA1 Stream0/Stream6).

`PARSECS_Protocol_Interface_Task_Init` registers that peer (`PARSECS_Add_Slave`
no-op chip-select on the master). `MyDemoTask` uses only the High Level
USER API — it does not call `PARSECS_TRANSMIT_APP` / `PARSECS_RECEIVE_APP`.

See [DIAGRAMS.md](DIAGRAMS.md) for the wire-level L1 round and
[TRANSMISSION_PROTOCOL.md](TRANSMISSION_PROTOCOL.md) for the L2 frame layout,
High Level addressing, job order, test payload, and error handling.

LED feedback (GPIOD, same mapping on both boards):

| LED | Color | Meaning |
|-----|-------|---------|
| LD4 | Green | Heartbeat — L1 round complete, or APP Get seen/answered |
| LD6 | Blue | Activity — byte on the wire |
| LD3 | Orange | Warning — USB busy or a ring buffer was full (byte dropped) |
| LD5 | Red | Fault — solid during re-sync |

---

## Project layout

```
PARSECS/              Shared stack (L1 I2C DMA, L2, L3, L4 WIT, L6 BER, L7 APP)
ringbuffer/           Ring buffer library (git submodule, shared by both projects)
PARSECS_RT_master/    STM32CubeIDE project — I2C master (-DSPI_MASTER)
PARSECS_RT_slave/     STM32CubeIDE project — I2C slave
  Application/
    demo_task.c/h     USER_Send / USER_Receive GetRequest demo
  Core/               HAL-generated startup, GPIO, DMA, I2C, IRQ handlers
  Middlewares/        FreeRTOS, USB CDC
CHANGELOG.md          Per-branch change notes
DIAGRAMS.md           Sole source for ASCII diagrams: wiring, byte framing, boot sequence
TRANSMISSION_PROTOCOL.md  Sole source for the detailed protocol walkthrough, error handling, constants
```

Each doc above owns a single topic — if you're updating the protocol or wiring, change it in exactly one of these files rather than duplicating it here in the README.

---

## Building

Open `PARSECS_RT_master/` and `PARSECS_RT_slave/` as separate projects in STM32CubeIDE and build each for the `Debug` configuration. Flash one board with the master firmware and the other with the slave.

> **Note:** the `.ioc` files fully own the I2C1/DMA/NVIC configuration (DMA requests, `I2C1_EV`/`I2C1_ER`/`DMA1_Stream0`/`DMA1_Stream6` interrupts, and — on the slave — the `OwnAddress1` value), and every line CubeMX would auto-generate has been moved out of the `USER CODE` markers in `i2c.c`/`stm32f4xx_it.c` to match. Regenerating code from the `.ioc` (e.g. after tweaking an I2C setting) is safe: only the application logic inside `USER CODE` sections and `Application/demo_task.c` needs to survive, and that's exactly what's preserved. The `ringbuffer/` and `PARSECS/` include paths (linked resources + `../../ringbuffer` / `../../PARSECS`) live in the IDE project settings and aren't touched by code generation, but it's worth a quick glance after a regen regardless. The master must keep `-DSPI_MASTER`.

---

## Requirements

- 2× STM32F407G-DISC1
- STM32CubeIDE 1.13+
- A USB CDC terminal on each board (e.g. `minicom`, PuTTY, or the IDE console)
