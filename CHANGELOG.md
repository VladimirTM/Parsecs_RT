# Changelog

## feature/debug-logs-roundtrip-timing

- Turned on the High Level layer logs that were already in the stack, ported
  from `coretx_commboard` / `coretx-motherboard`: L3 raw `I2C ->/<-`, L4 WIT
  `I2C_WIT ->/<-` and protocol errors, and L7 `I2C USER ->/<-`. The debug
  flags and log tags are renamed from `SPI_*` to `I2C_*`.
  `I2C_PROTOCOL_DEBUG` is defined in `PARSECS_Debug.h`, and each sub-flag can be
  switched on its own. `I2C_PROTOCOL_USER_FULL_BER_DEBUG` stays off.
- `LOG_SERIAL` is now real (`PARSECS/log.c`, based on the coretx `log.c`). It
  writes to USB CDC and does not initialise USB a second time.
- `LOG_SERIAL` does not block. It formats the message into an 8 KB RAM ring
  (`LOG_RING_SIZE`) and returns. A separate `logTask` (`LOG_TaskStart`,
  created in both `freertos.c`) sends the ring to CDC in chunks of up to
  `LOG_CHUNK_SIZE` bytes whenever the previous transfer has finished. Before
  this, each log line could wait up to 10 ms for USB inside `myTestTask`, which
  showed up on I2C as a ~70 ms pause at the start of every transaction. If the
  ring is full (for example the COM port is not open), the message is dropped
  and LD3 toggles. The demo's `usb_print` and the Layer 1 resync messages also
  go through `LOG_SERIAL`, so nothing else writes to the CDC endpoint.
- Master round-trip probe on PE7 (`TIMING_Pin`): it goes high when a
  GetRequest is queued and low when the GetResponse arrives or the request
  times out. The PASS/FAIL line also prints `rtt=<ms>` from `HAL_GetTick`.
  With `I2C_PROTOCOL_DEBUG` on, the pulse only includes the time to format the
  log lines, not the USB transfers.
- Layer 1 recovers from a stuck bus without a manual reset. At boot a stray
  START/STOP could make a slave hit a bus error. HAL then kept the handle in
  `LISTEN`, so `HAL_I2C_EnableListen_IT` returned `HAL_BUSY` forever while the
  peripheral still ACKed its address and held SCL low.
  - Slave: `HAL_I2C_ErrorCallback` requests an immediate `i2c_resync()`. A
    failed re-listen counts as an error. A watchdog resyncs when ADDR stays set,
    or the handle stays in `BUSY_RX_LISTEN` / `BUSY_TX_LISTEN`, for more than
    `SLAVE_STUCK_TICKS` (5 ms).
  - Master: `i2c_resync()` runs `bus_recover()` between DeInit and Init. It
    sends up to 9 SCL pulses until SDA is released, then a STOP. Layer 1 also
    waits `L1_STARTUP_MS` (100 ms) after reset before the first round.
  - Resync logs now print the HAL `ErrorCode`, e.g.
    `[Slave]  L1 resync err=0x01` (0x01 BERR, 0x02 ARLO, 0x04 AF, 0x08 OVR).
- `PARSECS_Protocol_USER_Receive` takes a new `GenericDataCapacity` argument
  (the size of `GenericDataAsBER`), right after the buffer. Before, the
  OperationData of a received frame was copied into the caller's buffer
  without a length check, so a frame larger than the demo's 32-byte `ber_rx`
  overwrote the memory after it. If the data does not fit, the call now
  returns `PARSECS_PROTOCOL_ERROR_SIZE_EXCEEDED`, drops the frame and leaves the
  buffer untouched. The demo passes `sizeof(ber_rx)`.
- `PARSECS_Protocol_USER_Send` now counts the 13-14 byte presentation header
  in the 1024-byte frame limit (`PARSECS_WIT_FRAME_LENGTH`). Before, the check
  ran before the header was encoded, so payloads of 1011-1024 bytes were
  accepted and wrote up to 14 bytes past `Transmission.WITData`. They now
  return `PARSECS_PROTOCOL_ERROR_SIZE_EXCEEDED`.
- A stack overflow now stops the board. `configCHECK_FOR_STACK_OVERFLOW` was
  already 1, but `vApplicationStackOverflowHook` in both `freertos.c` was
  empty, so the kernel carried on with corrupted memory. The hook now disables
  interrupts and flashes LD3-LD6 fast forever. `pcTaskName` names the task in
  the debugger.
- New `heartbeatTask` (1 KB stack, both boards) logs `heartbeat <n>` once per
  second. At the 5th beat it logs the stack high-water marks once, in free
  words: `stack free words: myTestTask=.. logTask=.. heartbeat=..`. By then
  `myTestTask` has run several round trips, so with `I2C_PROTOCOL_DEBUG` on
  this is the worst case.
- `LOG_SERIAL` can be called from any task. It suspends the scheduler while
  it formats into the shared `fmt_buf` and copies to the ring. Interrupts keep
  running. It must still not be called from an ISR.
- The slave recovers from a clock stretch that never ends. After a few
  minutes of traffic a slave could hold SCL low forever. The master then saw
  only AF (0x04) and resynced in a loop, while the slave watchdog never fired,
  so only a board reset helped. `slave_stuck()` now also resyncs after
  `SLAVE_STUCK_TICKS` (5 ms) of:
  - `BTF` set: the master ACKed the last byte, so the slave waits for a `DR`
    write that the finished DMA never makes.
  - SCL low with SDA high. Both lines low is the master's deliberate bus hold
    and is ignored.
  - `BUSY` set with both lines high, the STM32F40x locked-BUSY erratum
    (ES0182).
- Each Layer 1 resync on master and slave now also resets I2C1 from RCC
  (`__HAL_RCC_I2C1_FORCE_RESET` / `RELEASE_RESET`) between `HAL_I2C_DeInit` and
  `MX_I2C1_Init`, as part of that erratum workaround.
- Slave resync logs are one line with the state captured before the reset:
  `[Slave]  L1 resync (<error|stuck|errors>) err=.. sr1=.. sr2=.. st=.. scl=.. sda=..`.
  SR1: ADDR 0x0002, BTF 0x0004, TXE 0x0080, AF 0x0400. SR2: BUSY 0x0002,
  TRA 0x0004.
- Fewer and shorter ~1.9 s round trips, still 1 byte per round. In a 300 s
  logic-analyzer capture, about 10% of transactions were misframed (decoded
  shifted by one bit, no START seen), mostly the read that started ~2 us after
  the write's STOP. Twice the master stayed in that state for ~1.9 s, until
  the 50 ms round timeout resynced it, because failures alternating with good
  rounds never reached `MAX_CONSEC_ERRORS`.
  - The master's write and read are one transaction with a repeated START
    (`I2C_FIRST_FRAME` then `I2C_LAST_FRAME`) instead of STOP + START. The
    STM32F40x repeated-START erratum only affects Standard mode at 88-100 kHz.
  - The master resyncs when `ROUND_WINDOW_MAX_FAILS` (8) of the last 32 rounds
    failed, at most once per `ROUND_WINDOW_HOLDOFF_MS` (250 ms). Log:
    `[Master] L1 resync (window) err=..`.
  - A TX byte leaves the ring only after it was transferred: on the master
    when the slave ACKs it (`MasterTxCpltCallback`), on the slave when the
    master's NACK ends the read (`ListenCpltCallback`). After a NACK or bus
    error the same byte is sent again, so a resync no longer drops a byte from
    the middle of a Layer 2 frame.
  - Layer 2 RX drops a frame in progress, and requests a NACK, after
    `L2_RX_IDLE_CALLS_MAX` (20) calls without a new byte, so a frame cut off
    by a resync or a bus hold does not swallow the start of the next one.
- With the changes above, a 220 s capture had no ~1.9 s events: the active
  part of a demo cycle was 330 ms median and 388 ms max. The remaining spread
  came from ~0.4 hung rounds per second, each a START followed 0.4 us later by
  a STOP and then 50 ms of silence until the round timeout. For a 1-byte DMA
  read HAL sets STOP in `I2C_Master_ADDR` and again in `I2C_DMAXferCplt`; when
  the first STOP has already gone out, the second stays pending in `CR1` and
  fires right after the next START. `start_round` now clears a pending STOP
  while `BUSY` is clear, and `TRANSACTION_TIMEOUT_TICKS` is 5 (~5 ms) instead
  of 50 for any other hung round.

### Layer 3 retransmission (ported from `feature/l3-retransmission`)

- Layer 3 stop-and-wait ARQ per slave. The DATA frame is kept in
  `SLAVE.arq_buffer` until an ACK with its SEQ arrives. A NACK resends it at
  once. A timeout (`ARQ_RTO_ROUNDS`, 70 Layer 1 rounds of that slave) resends
  it with doubling backoff, capped at `ARQ_RTO_MAX_ROUNDS`.
  After `ARQ_MAX_RETRIES` (3) the frame is dropped and the slave reports
  `PARSECS_LINK_DOWN` until a later frame is ACKed.
- The timer counts `SLAVE.l1_rounds` (new; incremented in Layer 1) and pauses
  while a peer frame is being received.
- Receiver re-ACKs a repeated SEQ without delivering it twice.
- Layer 1 counts `l1_rounds` on top of the lossless TX bytes: the master per
  attempted round (also failed ones), the slave per completed read.
- Layer 2 takes the DATA SEQ from Layer 3, starts the ARQ timer when a DATA
  frame has been sent, and records the received SEQ only for DATA frames that
  pass the CRC.
- Fixed: sending an ACK/NACK cleared `FG_HL_FTTR` and overwrote the queued DATA
  frame, so High Level data could be lost. `PARSECS_TRANSMIT_APP` checked the
  slave id with `>` instead of `>=`.
- Low Level API: `PARSECS_Get_Link_State`, `PARSECS_Get_ARQ_Stats`,
  `PARSECS_Reset_Link` (hook for a future hot-swap). `PARSECS_CHECK_APP_TX_READY`
  stays false while a frame waits for its ACK.
- Demo USB lines end with `link=UP|DOWN retx=N dropped=N`.
- `PARSECS_L1_FAULT_INJECT_EVERY` (off by default) corrupts every Nth Layer 1
  byte for hardware tests.
- Host tests in `test/arq` (`make -C test/arq run`).

## feature/dual-slave-get-loop

- Second I2C peer: master `MAX_BOARD_COUNT` is 2. Comm stays `0x08` /
  `CORE_TX_WIT_COMM_BOARD` (GetResponse `42`); mobility is `0x09` /
  `CORE_TX_WIT_MOBILITY_BOARD` (GetResponse `43`). `SLAVE.i2c_address` and
  `PARSECS_Set_Slave_I2C_Address` replace the hardcoded Layer 1 address.
- Slave firmware is one project, two images. `PARSECS_SLAVE_INDEX` in
  `slave_identity.h` (0 default, or `-DPARSECS_SLAVE_INDEX=1`) picks the board,
  the answer value, and `OwnAddress1`. The `.ioc` address stays `0x08`; slave
  `i2c.c` applies the override after `MX_I2C1_Init`.
- Demo loops GetRequest/GetResponse for comm then mobility. A slave that does
  not answer within 2 s is logged FAIL and the cycle continues. After both
  replies, Layer 1 keeps running for 100 ms so the last ACK can leave, then
  the master deinits I2C1 and drives SCL low, then SDA low, for 500 ms.
  Release is SDA first, then SCL, then `MX_I2C1_Init`.

## i2c-parsecs-highlevel

- Ported the PARSECS High Level Substack (Layer 4 WIT PDU, Layer 6 BER, Layer 7
  APP Get/Set/Call) into the shared `PARSECS/` tree: `PARSECS_Protocol`,
  `PARSECS_Protocol_Interface`, and `PARSECS_HighLevelTask`. Layer 6 uses the
  `berlib/` git submodule (`src/berc`, same URL as `coretx_commboard`) instead of
  a copied `ber.c`. Raspberry Pi GPIO chip-select (`PARSECS_WIT_BOARDS`) is not
  used; I2C still addresses slave `0x08`.
- One I2C peer: `MAX_BOARD_COUNT` is 1 on both nodes. The master descriptor is
  `CORE_TX_WIT_COMM_BOARD` and the slave descriptor is `CORE_TX_WIT_MOTHERBOARD`,
  stored at `CORE_TX_Wit_Boards[0]`. `USER_Send` / `USER_Receive` look up that
  slot by APP address instead of using the WIT enum as an array index
  (`CORE_TX_WIT_COMM_BOARD` is 6).
- `PARSECS_Protocol_Interface_Task_Init` calls `PARSECS_Add_Slave` (noop CS) on
  the master. `myTestTask` runs `PARSECS_LowLevelTask`, then
  `PARSECS_Protocol_Interface_Task`, then `MyDemoTask`.
- Demo: master sends `GetRequest` TypeID `0x01` with BER Null; slave answers
  `GetResponse` with BER integer `42`. USB CDC prints the exchange. The demo
  does not call `PARSECS_TRANSMIT_APP` / `PARSECS_RECEIVE_APP` (High Level owns
  L3). PING/PONG is gone.

## i2c-parsecs-lowlevel

- Migrated the PARSECS Low Level Substack (Layer 1, 2 and 3 plus LowLevelTask /
  LowLevelAPI / Data / crc16) into a shared `PARSECS/` tree used by both STM32
  projects. Master/slave stay identical except `-DSPI_MASTER`, Layer 1 HAL role,
  LowLevelTask slave walk, and `PARSECS_Add_Slave`.
- Layer 1 is the existing I2C DMA byte pump, now operating on each `SLAVE`'s
  `l1_buffer_tx` / `l1_buffer_rx` with IRQ-safe ring access. Chip-select
  callbacks are no-ops (I2C address `0x08` replaces SPI SSEL).
- Layer 2/3 come from the STM32 slave SPI sources (`PARSECS_CMD` instead of
  `CMD`, `RAW_RING_BUFFER_SIZE` 16). High Level (Protocol / BER) is not ported.
- `main` calls `PARSECS_LowLevelTaskInit` then `DemoTask_Init`; `myTestTask`
  runs `PARSECS_LowLevelTask` then `MyDemoTask` each tick.
- Demo queues `PING` (master) / `PONG` (slave) via `PARSECS_TRANSMIT_APP` and
  prints `PARSECS_RECEIVE_APP` over USB CDC. The old `abcd`/`ABCD` L1 seed test
  is gone.

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
