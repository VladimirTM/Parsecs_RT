# Parsecs_RT

Two STM32F407G-DISC1 boards communicating over I2C using FreeRTOS. The master sends a message, the slave echoes it back upper-cased. Each board logs to a PC over USB CDC.

Built with STM32CubeIDE / CubeMX. Two separate projects live under `PARSECS_RT_master/` and `PARSECS_RT_slave/`.

---

## Wiring

```
Master (STM32F407 Disc)              Slave (STM32F407 Disc)
  PB6 SCL o---------+----------------o PB6 SCL
  PB7 SDA o------+--|----------------o PB7 SDA
  GND     o------|--|----------------o GND
  USB <-> PC     |  |                USB <-> PC
               [4k7][4k7] to 3.3V
```

I2C1 at 400 kHz, 7-bit addressing, slave address `0x08`.

---

## Demos

### Non-blocking ring buffer (`main`)

The current branch. Transfers are entirely interrupt-driven via the HAL sequential API (`HAL_I2C_*_Seq_*_IT`) — the FreeRTOS tasks never block inside HAL.

Each round:
1. Master queues `"hello N"` into a TX ring and kicks off byte 0.
2. The I2C1 ISR streams the remaining bytes on both sides.
3. Slave upper-cases the payload into a reply buffer on the last received byte.
4. Master reads back `"HELLO N"` and verifies the echo.

Fixed 16-byte payload each way, NUL-padded. Both sides recover from bus faults automatically: a per-round watchdog and a consecutive-error threshold both trigger `HAL_I2C_DeInit` + re-init.

LED feedback (GPIOD, same mapping on both boards):

| LED | Color | Meaning |
|-----|-------|---------|
| LD4 | Green | Heartbeat — toggles on each healthy round |
| LD6 | Blue | Activity — round on the wire / USB log line |
| LD3 | Orange | Warning — USB busy, ring full, echo mismatch |
| LD5 | Red | Fault — solid during re-sync |

### Simple blocking demo (`feature/i2c-simple-sync`)

Minimal proof of concept. Master sends one byte over blocking I2C, slave echoes it. Sequence number embedded in the payload keeps both USB counters in sync across retries. See `README_SIMPLE_SYNC.md`.

---

## Project layout

```
PARSECS_RT_master/   STM32CubeIDE project — I2C master
PARSECS_RT_slave/    STM32CubeIDE project — I2C slave
  Application/
    demo_task.c/h    All application logic (both sides)
  Core/              HAL-generated startup, GPIO, I2C, IRQ handlers
  Middlewares/       FreeRTOS, USB CDC
CHANGELOG.md         Per-branch change notes
DIAGRAMS.md          Wiring diagram and protocol state machine
TRANSMISSION_PROTOCOL.md  Wire format and byte-by-byte framing details
```

---

## Building

Open `PARSECS_RT_master/` and `PARSECS_RT_slave/` as separate projects in STM32CubeIDE and build each for the `Debug` configuration. Flash one board with the master firmware and the other with the slave.

> **Note:** if you regenerate code from the `.ioc` file, CubeMX will disable the I2C1 EV/ER interrupts. Re-enable them in `HAL_I2C_MspInit` (`i2c.c`) and restore the IRQ handlers in `stm32f4xx_it.c` before building.

---

## Requirements

- 2× STM32F407G-DISC1
- STM32CubeIDE 1.13+
- A USB CDC terminal on each board (e.g. `minicom`, PuTTY, or the IDE console)
