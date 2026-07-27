# Parsecs_RT

Two STM32F407G-DISC1 boards communicating over I2C using FreeRTOS. Each side ports the `PARSECS_LAYER1` ring-buffer byte pump (see `coretx-motherboard`'s SPI master and `coretx_commboard`'s SPI slave) onto I2C DMA: the master's TX ring is seeded with `a,b,c,d`, the slave's TX ring with `A,B,C,D`, and one byte moves each way per round until both rings have crossed over into the other side's RX ring. Each board logs the exchange (and a PASS/FAIL check) to a PC over USB CDC.

Built with STM32CubeIDE / CubeMX. Two separate projects live under `PARSECS_RT_master/` and `PARSECS_RT_slave/`.

---

## Wiring

PB6/PB7 (SCL/SDA) tied together between the two boards with a common GND, I2C1 at 400 kHz, 7-bit addressing, slave address `0x08`. See [DIAGRAMS.md](DIAGRAMS.md) for the full pinout diagram.

---

## Demos

### PARSECS_LAYER1 ring buffer over I2C DMA (`main`)

The current branch. Transfers are driven by the HAL sequential **DMA** API (`HAL_I2C_*_Seq_*_DMA`, DMA1 Stream0/Stream6) — the FreeRTOS tasks never block inside HAL, and the CPU never touches the byte-by-byte shifting. Both sides use the real `tRingBufObject` ring buffer from [`ringbuffer/`](ringbuffer/) (the same library the SPI reference stack uses) instead of ad hoc fixed-size buffers, exchanging one byte per side per round and recovering from bus faults automatically via a per-round watchdog and a consecutive-error threshold.

See [DIAGRAMS.md](DIAGRAMS.md) for the wire-level framing diagram and [TRANSMISSION_PROTOCOL.md](TRANSMISSION_PROTOCOL.md) for the full callback-by-callback walkthrough, test payload, and error handling.

LED feedback (GPIOD, same mapping on both boards):

| LED | Color | Meaning |
|-----|-------|---------|
| LD4 | Green | Heartbeat — toggles on each completed round |
| LD6 | Blue | Activity — round on the wire / USB log line |
| LD3 | Orange | Warning — USB busy or a ring buffer was full (byte dropped) |
| LD5 | Red | Fault — solid during re-sync |

---

## Project layout

```
ringbuffer/           Ring buffer library (git submodule, shared by both projects)
PARSECS_RT_master/    STM32CubeIDE project — I2C master
PARSECS_RT_slave/     STM32CubeIDE project — I2C slave
  Application/
    demo_task.c/h     All application logic (both sides)
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

> **Note:** the `.ioc` files fully own the I2C1/DMA/NVIC configuration (DMA requests, `I2C1_EV`/`I2C1_ER`/`DMA1_Stream0`/`DMA1_Stream6` interrupts, and — on the slave — the `OwnAddress1` value), and every line CubeMX would auto-generate has been moved out of the `USER CODE` markers in `i2c.c`/`stm32f4xx_it.c` to match. Regenerating code from the `.ioc` (e.g. after tweaking an I2C setting) is safe: only the application logic inside `USER CODE` sections and `Application/demo_task.c` needs to survive, and that's exactly what's preserved. The `ringbuffer/` include path (linked resource + `../../ringbuffer`) lives in the IDE project settings and isn't touched by code generation, but it's worth a quick glance after a regen regardless.

---

## Requirements

- 2× STM32F407G-DISC1
- STM32CubeIDE 1.13+
- A USB CDC terminal on each board (e.g. `minicom`, PuTTY, or the IDE console)
