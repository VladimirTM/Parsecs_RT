# Changelog

## feature/i2c-nonblocking-ring

- Non-blocking, byte-by-byte I2C using the HAL sequential API
  (`HAL_I2C_*_Seq_*_IT`), driven by the I2C1 EV/ER interrupts.
- Master: TX ring buffer of pending strings, bus state machine, watchdog and
  error-threshold I2C re-init.
- Slave: interrupt listen mode (`AddrCallback` / `ListenCpltCallback`),
  upper-cases the request and echoes it back, log ring drained to USB CDC.
- Enabled the I2C1 EV/ER NVIC lines in `i2c.c` and added the IRQ handlers in
  `stm32f4xx_it.c`.
- 16-byte fixed payload each way, slave address `0x08`.

## feature/i2c-simple-sync

- Blocking single-letter demo; sequence number embedded in the payload so the
  master and slave counters can't drift.
- I2C re-init after 10 consecutive errors.

## Initial

- Split the project into separate master and slave STM32 projects.
- Fixed USB double-init, made the USB TX buffer static, replaced `sprintf` with
  `snprintf`, corrected the `uint32_t` format specifier, handled `USBD_BUSY`.
