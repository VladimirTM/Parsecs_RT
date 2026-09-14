# I2C transmission protocol

How the two boards exchange data with the PARSECS Low Level Substack (L1–L3)
on I2C DMA. Layer 1 lives in `PARSECS/PARSECS_Layer1.c`. Layer 2/3 and the
Low Level API are the SPI-stack sources in `PARSECS/`. Application code is
`PARSECS_RT_master/Application/demo_task.c` and the slave equivalent (the
files are the same; `-DSPI_MASTER` picks the payload and `PARSECS_Add_Slave`).

See [DIAGRAMS.md](DIAGRAMS.md) for the wiring pinout and L1 byte-round diagram,
and [README.md](README.md) for the project overview.

## Job order

Each 1 ms tick of `myTestTask`:

    PARSECS_TRANSMIT_LAYER3
    PARSECS_TRANSMIT_LAYER2
    PARSECS_LAYER1          # one I2C byte each way, if the bus is idle
    PARSECS_RECEIVE_LAYER2
    PARSECS_RECEIVE_LAYER3
    MyDemoTask              # TRANSMIT_APP / RECEIVE_APP

I2C is half-duplex, so the RX byte from this L1 call may only be in the ring
on the next tick. Layer 2 is a byte state machine, so that one-job delay is
fine as long as order is preserved.

## Layer 1 wire format

Each round is two single-byte DMA transfers to/from slave `0x08` on I2C1
(400 kHz, 7-bit):

    write (M->S):  1 byte   - popped from the master's l1_buffer_tx, or 0x00 if empty
    read  (M<-S):  1 byte   - popped from the slave's l1_buffer_tx, or 0x00 if empty

There is no length byte at L1: Layer 1 is a raw byte pump. Each round both
sides use `I2C_FIRST_AND_LAST_FRAME`.

Master `PARSECS_LAYER1` starts a round only when the bus is idle. Slave L1 is
ISR-driven (listen + `AddrCallback`); the job only re-arms listen and resyncs
on errors.

## Layer 2 frame (`SPI_BASE_FRAME`)

Bytes Layer 1 moves are assembled by Layer 2:

    SOF (0x7E) | LEN | SEQ | TYPE | DATA[LEN] | CRC16_hi | CRC16_lo

| Field | Value |
|---|---|
| SOF | `0x7E` `START_OF_FRAME` |
| TYPE ACK | `0x7C` |
| TYPE NACK | `0x7B` |
| TYPE DATA | `0x8C` |
| TYPE CMD | `0x9B` `PARSECS_CMD` |
| CRC | incremental CRC16 (`crc16.c`), same as the SPI stack |

ACK/NACK frames have `LEN = 0` (no DATA bytes). DATA frames carry the buffer
passed to `PARSECS_TRANSMIT_APP`.

## Layer 3

- `PARSECS_TRANSMIT_APP` sets `FG_HL_FTTR`. L3 TX turns that into a DATA frame
  (`FG_LL_FTT`) unless an ACK is pending (ACK wins).
- L3 RX: a good DATA frame sets `FG_ACK` and `FG_HL_NDF_*`; ACK/NACK update the
  last ACKed/NACKed sequence numbers. `PARSECS_RECEIVE_APP` returns the DATA
  payload.

## DMA

Transfers are driven by `HAL_I2C_*_Seq_*_DMA`. `hdma_i2c1_rx` (DMA1 Stream0,
Channel 1) and `hdma_i2c1_tx` (DMA1 Stream6, Channel 1) are configured in
`HAL_I2C_MspInit` (`i2c.c`). Completion callbacks are the same HAL names used
for `_IT` transfers.

L2 runs in task context and L1 callbacks run in ISR context, so every ring
push/pop goes through `PARSECS_Port.h` (`__disable_irq` / restore).

## Test payload

`DemoTask_Init()` on the master calls `PARSECS_Add_Slave` with no-op
select/deselect (I2C addressing replaces chip-select). Both sides then queue
one Low Level payload when the stack is ready:

- Master: `PING` (4 bytes)
- Slave: `PONG` (4 bytes)

Each side prints a USB CDC line when `PARSECS_RECEIVE_APP` returns the peer's
DATA frame (after L2 CRC and L3 ACK). Dummy `0x00` L1 bytes keep the link
pumping when a TX ring is empty, matching PARSECS Layer 1 idle behaviour.

## Error handling

- Master watchdog: if a round holds the bus longer than
  `TRANSACTION_TIMEOUT_TICKS`, re-init I2C (and re-link the DMA handles).
  Layer 1 does not retry a specific byte.
- Both sides: after `MAX_CONSEC_ERRORS`, `HAL_I2C_DeInit` + `MX_I2C1_Init`.
- A byte popped from `l1_buffer_tx` is not put back on error; a byte dropped
  because `l1_buffer_rx` was full is also not retried. L2 CRC failure raises
  `FG_NACK` and the peer transmits a NACK frame.

## Constants

| Constant | Default | Notes |
|---|---|---|
| `RAW_RING_BUFFER_SIZE` | 16 | L1 ring capacity |
| `DUMMY_BYTE` | 0x00 | filler when `l1_buffer_tx` is empty |
| `SLAVE_ADDRESS` / `OwnAddress1` | 0x08 | |
| `TRANSACTION_TIMEOUT_TICKS` | 50 | master L1 watchdog |
| `MAX_CONSEC_ERRORS` | 10 | errors before a resync |
| `SPI_DATA_LENGTH` | 254 | max L2 DATA payload |
