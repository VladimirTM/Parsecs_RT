# Changelog

## feature/i2c-layer1-ringbuffer-dma

- Ported the `PARSECS_LAYER1` ring-buffer byte pump (see
  `coretx-motherboard/rpi_motherboard/SPI/PARSECS_Layer1.c` for the master and
  `coretx_commboard/commboard_freertos_stm32/SPI/PARSECS_Layer1.c` for the
  slave) from SPI onto I2C: both sides now use the real `tRingBufObject` ring
  buffer from `ringbuffer/` (the same library the SPI reference stack uses)
  instead of the old fixed 16-byte `"hello N"` / uppercase-echo buffers.
- Each round moves exactly one byte per side: pop `tx_ring` (or send the dummy
  `0x00` filler if empty), push the received byte into `rx_ring` if there is
  room. The slave's reply is its own independent `tx_ring` data, not an echo
  of the request.
- Switched the transfers from the plain HAL sequential `_IT` API to
  `HAL_I2C_*_Seq_*_DMA`. Added `dma.c`/`dma.h` (`MX_DMA_Init`) and linked
  `hdma_i2c1_rx`/`hdma_i2c1_tx` (DMA1 Stream0/Stream6, Channel 1) into the
  I2C1 MSP init/deinit on both boards; `HAL_I2C_MspDeInit` now also tears down
  the DMA handles so `i2c_resync()` rebuilds them cleanly.
- Test payload: `DemoTask_Init()` seeds the master's `tx_ring` with `a,b,c,d`
  and the slave's `tx_ring` with `A,B,C,D`; each side prints a PASS/FAIL line
  over USB CDC as the peer's bytes arrive, then a "pass-through test
  complete" line.
- Wired `ringbuffer/` (sibling submodule) into both projects: added to
  `CMakeLists.txt`/`CMakeLists_template.txt` include dirs + sources, added a
  `../../ringbuffer` compiler include path and a linked `ringbuffer` source
  folder in `.cproject`/`.project`.
- Simplified the slave's round-completion handling to mirror the master's
  `byte_ready` pattern: the `log_ring`/`LOG_SLOTS` ring buffer and the
  LED/`error_reset()` work that used to happen in `round_complete()` (ISR
  context) are gone, replaced by a single `round_ready` flag that
  `MyDemoTask()` checks and clears, doing the PASS/FAIL compare and
  LED/error-reset work the same way the master already did. Keeps both
  sides symmetric ahead of removing this Layer 1 test/verification code
  entirely.

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
