# I2C transmission protocol

How the two boards exchange data in the `PARSECS_LAYER1` ring-buffer demo. Code
is in `PARSECS_RT_master/Application/demo_task.c` and the slave equivalent.
This is a port of `coretx-motherboard/rpi_motherboard/SPI/PARSECS_Layer1.c`
(master) and `coretx_commboard/commboard_freertos_stm32/SPI/PARSECS_Layer1.c`
(slave) from SPI onto I2C, using the same `tRingBufObject` ring buffer
(`ringbuffer/`) as the reference stack.

See [DIAGRAMS.md](DIAGRAMS.md) for the wiring pinout and byte-framing diagrams
referenced below, and [README.md](README.md) for the project overview.

## Wire format

Each round is two single-byte DMA transfers to/from slave `0x08` on I2C1
(400 kHz, 7-bit):

    write (M->S):  1 byte   - popped from the master's tx_ring, or 0x00 if empty
    read  (M<-S):  1 byte   - popped from the slave's tx_ring, or 0x00 if empty

There is no length byte and no fixed frame: Layer1 is a raw byte pump, not a
framed protocol. Each round both sides use `I2C_FIRST_AND_LAST_FRAME`, so a
"byte" is a complete standalone I2C transaction (`START` + address + data +
`STOP`/NACK), not part of a longer multi-byte sequence.

## One round

1. Master task (`start_round`) pops one byte from its `tx_ring` (or uses the
   dummy `0x00` filler if it is empty), unless `tx_ring` is empty *and*
   `rx_ring` is already full - in that case there is nothing useful to do and
   the round is skipped.
2. Master writes that byte with `HAL_I2C_Master_Seq_Transmit_DMA`.
3. Slave `AddrCallback` (write direction) arms a one-byte
   `HAL_I2C_Slave_Seq_Receive_DMA`; on completion (`SlaveRxCplt`) the byte is
   pushed into the slave's `rx_ring` if there is room.
4. The write's STOP hits the slave's `ListenCplt` (phase `PHASE_RECEIVE`,
   so nothing else happens yet) which re-arms listen.
5. From `MasterTxCplt` the master turns the bus around and starts the read
   with `HAL_I2C_Master_Seq_Receive_DMA`.
6. Slave `AddrCallback` (read direction) pops one byte from its own
   `tx_ring` (or `0x00` if empty) and arms `HAL_I2C_Slave_Seq_Transmit_DMA`.
7. On `MasterRxCplt` the master pushes the received byte into its `rx_ring`
   if there is room, then sets `byte_ready`.
8. The read's terminating NACK hits the slave's `ListenCplt` (phase
   `PHASE_TRANSMIT`) -> round complete, sets `round_ready`.
9. Master task prints `tx='x' rx='y' PASS/FAIL` (against the known peer
   payload) and clears `byte_ready`; slave task does the same against
   `round_ready`, comparing the same `rx_byte_val`/`tx_byte_val` the ISRs
   captured.

Steps 2-8 run under the I2C1/DMA1 interrupts and typically finish within one
task tick (`osDelay(1)`), so the master's next round starts on the following
tick.

## DMA

Transfers are driven by `HAL_I2C_*_Seq_*_DMA` instead of the plain `_IT`
variants. `hdma_i2c1_rx` (DMA1 Stream0, Channel 1) and `hdma_i2c1_tx` (DMA1
Stream6, Channel 1) are configured and linked to `hi2c1` in `HAL_I2C_MspInit`
(`i2c.c`); `MX_DMA_Init` (`dma.c`) enables the DMA1 clock and the two stream
NVIC lines once at boot. The completion callbacks (`MasterTxCplt`,
`MasterRxCplt`, `SlaveRxCplt`, `SlaveTxCplt`, `AddrCallback`, `ListenCplt`,
`ErrorCallback`) are the same ones HAL uses for `_IT` transfers, so the
ring-buffer pop/push logic above does not care whether DMA or IT moved the
byte.

## Test payload

`DemoTask_Init()` seeds the master's `tx_ring` with `a,b,c,d` and the slave's
`tx_ring` with `A,B,C,D` before the scheduler starts. After 4 rounds both
sides have fully swapped payloads: the master's `rx_ring` holds `A,B,C,D` and
the slave's `rx_ring` holds `a,b,c,d`. Later rounds (both `tx_ring`s now
empty) just exchange dummy `0x00` bytes forever, matching `PARSECS_LAYER1`'s
always-on idle-pump behaviour; the demo stops printing PASS/FAIL lines once
all 4 bytes have been checked.

## Error handling

- Master watchdog: if a round holds the bus longer than
  `TRANSACTION_TIMEOUT_TICKS`, re-init I2C (and re-link the DMA handles) and
  move on - Layer1 does not retry a specific byte, only the peripheral's
  health is tracked.
- Both sides: after `MAX_CONSEC_ERRORS`, `HAL_I2C_DeInit` + `MX_I2C1_Init`
  (which tears down and rebuilds `hdma_i2c1_rx`/`hdma_i2c1_tx` via
  `HAL_I2C_MspDeInit`/`MspInit`).
- A byte popped from `tx_ring` is not put back on error (same as the SPI
  reference); a byte dropped because the destination `rx_ring` was full is
  also not retried. Both sides always re-arm/retry the *next* round, so a bad
  byte does not stall the link.

## Constants

| Constant | Default | Notes |
|---|---|---|
| `L1_RING_SIZE` | 16 | ring buffer capacity, matches the reference `RAW_RING_BUFFER_SIZE` |
| `DUMMY_BYTE` | 0x00 | filler byte when `tx_ring` is empty |
| `SLAVE_ADDRESS` / `OwnAddress1` | 0x08 | |
| `TRANSACTION_TIMEOUT_TICKS` | 50 | watchdog |
| `MAX_CONSEC_ERRORS` | 10 | errors before a resync |
