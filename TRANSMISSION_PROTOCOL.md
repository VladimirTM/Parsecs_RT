# I2C transmission protocol

How the two boards exchange data with the PARSECS stack on I2C DMA. Layer 1
lives in `PARSECS/PARSECS_Layer1.c`. Layer 2/3 and the Low Level API are the
SPI-stack sources in `PARSECS/`. Layer 4 (WIT PDU) and Layer 7 (APP Get/Set/Call)
live in `PARSECS_Protocol.c`. Layer 6 (BER) lives in the `berlib/` submodule
(`src/berc`). Application code is `PARSECS_RT_master/Application/demo_task.c`
and the slave equivalent (the files are the same; `-DSPI_MASTER` picks the
motherboard sequencer vs the comm-board object).

See [DIAGRAMS.md](DIAGRAMS.md) for the wiring pinout and L1 byte-round diagram,
and [README.md](README.md) for the project overview.

## Job order

Each 1 ms tick of `myTestTask`:

    PARSECS_TRANSMIT_LAYER3
    PARSECS_TRANSMIT_LAYER2
    PARSECS_LAYER1          # one I2C byte each way, if the bus is idle
    PARSECS_RECEIVE_LAYER2
    PARSECS_RECEIVE_LAYER3
    PARSECS_Protocol_Interface_Task   # L4/L6/L7; calls RECEIVE_APP / TRANSMIT_APP
    MyDemoTask                        # USER_Send / USER_Receive only

I2C is half-duplex, so the RX byte from this L1 call may only be in the ring
on the next tick. Layer 2 is a byte state machine, so that one-job delay is
fine as long as order is preserved. High Level must run after L3 so a completed
DATA frame is visible to `PARSECS_RECEIVE_APP` in the same tick.

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

ACK/NACK frames have `LEN = 0` (no DATA bytes). DATA frames carry a WIT PDU
segment queued by High Level through `PARSECS_TRANSMIT_APP`.

## Layer 3

- `PARSECS_TRANSMIT_APP` sets `FG_HL_FTTR`. L3 TX turns that into a DATA frame
  (`FG_LL_FTT`) unless an ACK is pending (ACK wins).
- L3 RX: a good DATA frame sets `FG_ACK` and `FG_HL_NDF_*`; ACK/NACK update the
  last ACKed/NACKed sequence numbers. `PARSECS_RECEIVE_APP` returns the DATA
  payload. The High Level job is the only caller; the demo must not steal it.

## Layer 4 / 6 / 7

High Level sits on L3 DATA payloads:

- L4 splits/assembles a WIT FRAME (`PARSECS_WIT_FRAME_LENGTH` 1024) into WIT
  PDUs that fit in an L3 DATA field.
- L6 BER encodes the APP header (source, destination, operation, control,
  TypeID) plus the user OperationData.
- L7 APP operations are Get/Set/Call request and response.

`USER_Send` / `USER_Receive` take a board address that names the local High
Level descriptor, not an I2C address:

| Node | `USER_*` board address | APP source → destination |
|---|---|---|
| Master | `CORE_TX_WIT_COMM_BOARD` (6) | motherboard (0) → comm (6) |
| Slave | `CORE_TX_WIT_MOTHERBOARD` (0) | comm (6) → motherboard (0) |

Both descriptors live at `CORE_TX_Wit_Boards[0]` (`MAX_BOARD_COUNT` is 1).
Lookup is by the `boardAddress` field so the WIT enum is not used as an array
index.

## DMA

Transfers are driven by `HAL_I2C_*_Seq_*_DMA`. `hdma_i2c1_rx` (DMA1 Stream0,
Channel 1) and `hdma_i2c1_tx` (DMA1 Stream6, Channel 1) are configured in
`HAL_I2C_MspInit` (`i2c.c`). Completion callbacks are the same HAL names used
for `_IT` transfers.

L2 runs in task context and L1 callbacks run in ISR context, so every ring
push/pop goes through `PARSECS_Port.h` (`__disable_irq` / restore).

## Test payload

`PARSECS_Protocol_Interface_Task_Init()` on the master calls `PARSECS_Add_Slave`
with no-op select/deselect (I2C addressing replaces chip-select). The demo
then runs 12 High Level round-trips, retrying `USER_Send` on
`PARSECS_PROTOCOL_ERROR_BUSY`. The master is the motherboard bringing a comm
board into service; the slave holds live state (`FirmwareVersion`, `TxPower`,
`LinkReady`).

Request `OperationControl` is `0xFF` (encoded as BER Null). After L7 decode,
Null becomes `0` (`PARSECS_APP_Success`) — the slave does not require received
control `0xFF`. Error replies still carry valid BER OperationData (Null).
`BERIntegerEncode` is `int8_t`, so demo values stay in `-128…127`.

Demo TypeIDs (application catalog, not part of the stack):

| TypeID | Name | Get | Set | Call |
|---|---|---|---|---|
| `0x01` | FirmwareVersion | integer, default `1` | WriteDenied | OperationUnsupported |
| `0x02` | TxPower | integer, default `10` | `1…20`; else ParameterSyntaxError | OperationUnsupported |
| `0x03` | LinkReady | `0/1`, Call-owned | WriteDenied | OperationUnsupported |
| `0x20` | StartLink | ReadDenied | WriteDenied | Null → LinkReady=1 |
| `0x21` | StopLink | ReadDenied | WriteDenied | Null → LinkReady=0 |
| `0x22` | Ping | ReadDenied | WriteDenied | integer nonce echoed |
| `0x7F` | Unknown | ParameterMethodUndefined | ParameterMethodUndefined | ParameterMethodUndefined |

Sequence:

1. Get FirmwareVersion → `1` Success
2. Get TxPower → `10` Success
3. Set TxPower `15` → Success
4. Get TxPower → `15` Success
5. Set FirmwareVersion `99` → WriteDenied
6. Set TxPower `99` → ParameterSyntaxError
7. Get Unknown `0x7F` → ParameterMethodUndefined
8. Call StartLink → Success
9. Get LinkReady → `1` Success
10. Call Ping `42` → `42` Success
11. Call StopLink → Success
12. Get LinkReady → `0` Success

Expected USB CDC (abbreviated):

```
[Master] queued GetRequest FirmwareVersion
[Slave]  rx GetRequest FirmwareVersion
[Slave]  queued GetResponse FirmwareVersion=1 Success
[Master] PASS GetResponse FirmwareVersion=1
...
[Master] queued CallRequest Ping=42
[Slave]  queued CallResponse Ping=42 Success
[Master] PASS CallResponse Ping=42
...
[Master] DEMO RESULT 12/12 PASS
```

Green `LD4` toggles on each PASS / answered request. Red `LD5` is solid if the
master sequencer fails.

## Saleae validation

Saleae does not show Get/Set/Call as I2C messages. Layer 1 is a one-byte pump
(`I2C_FIRST_AND_LAST_FRAME` to address `0x08` at 400 kHz). Each round is a
1-byte write (M→S) then a 1-byte read (S→M), with `0x00` dummy when a TX ring
is empty. APP transactions appear only after you reassemble L2 frames from
those bytes. See [DIAGRAMS.md](DIAGRAMS.md) for a one-exchange view.

Capture setup:

- Probes: SCL = PB6, SDA = PB7, GND common with both boards
- Sample rate: 8 MHz or higher (I2C is 400 kHz)
- Analyzer: I2C, 7-bit address `0x08` (Saleae may also show 8-bit `0x10` write
  / `0x11` read)
- Window: start capture, reset both boards, record about 5–10 s

Logic 2 will list hundreds of 1-byte transactions. Export I2C decoded bytes
and split by direction: write data = master → slave, read data = slave →
master. Ignore runs of dummy `0x00`. Search each direction for SOF `0x7E`.

Frame on the wire:

    7E | LEN | SEQ | TYPE | DATA[LEN] | CRC16_hi | CRC16_lo

CRC covers LEN+SEQ+TYPE+DATA only (not SOF). Types: DATA `0x8C`, ACK `0x7C`,
NACK `0x7B`. ACK/NACK have `LEN = 0`.

Each High Level `USER_Send` that fits in one WIT PDU becomes one DATA frame,
then the peer sends one ACK with the same SEQ. Demo payloads are small, so
there is no multi-PDU (WIT starts `00 01 01 <len> …`). Inside DATA:

    WIT: flags=0  total=1  current=1  pdulen  |  BER APP (src, dst, op, control, TypeID, OperationData)

L7 markers after the 4-byte WIT header:

- Addresses: request `src=0 dst=6`, response `src=6 dst=0`
- OperationType: GetReq `1`, GetResp `2`, SetReq `3`, SetResp `4`, CallReq `5`,
  CallResp `6`
- TypeID as in the catalog above
- Request control is BER Null; response control is Success `0`, WriteDenied
  `4`, ParameterMethodUndefined `5`, ParameterSyntaxError `7`

Do not expect ASCII `PING` / `GetRequest` on SDA.

Checklist for the 12-step demo: 24 DATA + 24 ACK and zero NACK (`0x7B`).
Each request is M→S DATA then S→M ACK (same SEQ); each response is S→M DATA
then M→S ACK. SEQ on DATA increments per direction; ACK SEQ equals the DATA
it acknowledges.

Pass criteria:

- No `7E 00 xx 7B` NACK frames
- Every DATA followed by ACK on the opposite direction before the next DATA
  from that sender (High Level is one-in-flight)
- 12 M→S DATA and 12 S→M DATA
- CDC `DEMO RESULT 12/12 PASS` lines up with that count

USB CDC is the L7 oracle; Saleae is the L1/L2 oracle. If CDC PASSes but
Saleae shows NACK or a missing ACK, the stack dropped or retried below APP.
Dummy `0x00` L1 bytes keep the link pumping when a TX ring is empty, matching
PARSECS Layer 1 idle behaviour.

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
| `MAX_BOARD_COUNT` | 1 | one High Level peer on I2C |
| `PARSECS_WIT_FRAME_LENGTH` | 1024 | L4 assembled WIT FRAME |
| Demo TypeIDs | `0x01`–`0x03`, `0x20`–`0x22`, `0x7F` | Get/Set/Call catalog in Test payload |
| Demo Ping nonce | `42` | BER integer in Call Ping |
