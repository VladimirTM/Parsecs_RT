# I2C transmission protocol

How the two boards exchange data with the PARSECS stack on I2C DMA. Layer 1
lives in `PARSECS/PARSECS_Layer1.c`. Layer 2/3 and the Low Level API are the
SPI-stack sources in `PARSECS/`. Layer 4 (WIT PDU) and Layer 7 (APP Get/Set/Call)
live in `PARSECS_Protocol.c`. Layer 6 (BER) lives in the `berlib/` submodule
(`src/berc`). Application code is `PARSECS_RT_master/Application/demo_task.c`
and the slave equivalent (the files are the same; `-DSPI_MASTER` picks
GetRequest vs GetResponse). The slave image is selected with
`PARSECS_SLAVE_INDEX` in `PARSECS_RT_slave/Application/slave_identity.h`.

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

Each round is two single-byte DMA transfers to one slave on I2C1
(400 kHz, 7-bit). The address is `SLAVE.i2c_address` for that peer
(comm `0x08`, mobility `0x09`):

    write (M->S):  1 byte   - popped from the master's l1_buffer_tx, or 0x00 if empty
    read  (M<-S):  1 byte   - popped from the slave's l1_buffer_tx, or 0x00 if empty

There is no length byte at L1: Layer 1 is a raw byte pump. Each round both
sides use `I2C_FIRST_AND_LAST_FRAME`.

Master `PARSECS_LAYER1` starts a round only when the bus is idle and the demo
is not holding SCL/SDA low. Slave L1 is
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
segment queued by High Level through `PARSECS_TRANSMIT_APP`. A DATA frame's SEQ
comes from Layer 3. An ACK or NACK carries the SEQ of the last DATA frame that
passed its CRC check.

## Layer 3

- `PARSECS_TRANSMIT_APP` copies the payload into `SLAVE.arq_buffer` and sets
  `FG_HL_FTTR`. L3 TX turns that into a DATA frame (`FG_LL_FTT`) with a new
  SEQ, unless an ACK is pending (ACK wins) or the previous frame is still
  unacknowledged.
- TX priority each tick: ACK, NACK, retransmission, new frame. A new frame is
  DATA from `PARSECS_TRANSMIT_APP` or the link reset CMD (see
  [Hot-plug](#hot-plug)); both wait in `arq_buffer` and use the same ARQ.
  Sending an ACK or NACK does not discard a queued frame.
- L3 RX: a good DATA frame sets `FG_ACK` and, unless it is a duplicate,
  `FG_HL_NDF_*`. `PARSECS_RECEIVE_APP` returns the DATA payload. The High Level
  job is the only caller; the demo must not steal it.

## Retransmission (ARQ)

Layer 3 runs stop-and-wait ARQ per `SLAVE` (per peer on the master): one DATA
frame in flight, and the next one leaves only after the peer ACKs it.

- When Layer 2 finishes sending a DATA frame it sets `FG_ARQ_WAIT_ACK` and
  starts the timer.
- An ACK whose SEQ equals `arq_seq` completes the frame. An ACK with another
  SEQ is ignored.
- A NACK while waiting schedules a retransmission at once (`FG_ARQ_RETX`).
  The RTO stays the same.
- Timeout: once `arq_elapsed` reaches `arq_rto`, the frame is resent and the
  RTO doubles, capped at `ARQ_RTO_MAX_ROUNDS`.
- A retransmission reuses the stored bytes and the original SEQ.
- After `ARQ_MAX_RETRIES` retransmissions without an ACK, the frame is dropped.
  The slave becomes `PARSECS_LINK_DOWN` and `arq_link_down_events` increments.
  `PARSECS_CHECK_APP_TX_READY` turns true again, and the next frame acts as a
  probe. Its ACK sets the link back to `PARSECS_LINK_UP`.
- Receiver side: a DATA frame whose SEQ equals `last_delivered_rx_seq` is ACKed
  again but not delivered. This covers the case where an ACK was lost.

Time is counted in Layer 1 rounds of that slave (`SLAVE.l1_rounds`), not
milliseconds, so it does not depend on the task period or on how many peers the
master polls. The master counts every round it starts, including failed ones.
The slave counts every completed round. The timer pauses while a peer frame is
being received, because the peer can only send our ACK after that frame. So a
long DATA frame from the peer does not cause a false timeout.

The base RTO is `2 * RAW_RING_BUFFER_SIZE + DATA_ENCODING_SIZE +
ARQ_RTO_MARGIN_ROUNDS` = 70 rounds. That covers the ACK waiting behind a full
TX ring on each side, plus the ACK frame, plus a margin. With an absent peer,
the worst case before `LINK_DOWN` is 70 + 140 + 280 + 280 = 770 rounds.

Low Level API:

| Function | Returns |
|---|---|
| `PARSECS_Get_Link_State(id)` | `PARSECS_LINK_UP` / `PARSECS_LINK_DOWN`, -1 bad id, -2 slave not enabled |
| `PARSECS_Get_ARQ_Stats(id, &retx, &dropped)` | 0, or -1 / -2; either out pointer may be `NULL` |
| `PARSECS_Reset_Link(id)` | 0, or -1 / -2; clears rings, Layer 2/3 state, SEQ counters and ARQ state |

`PARSECS_Reset_Link` is the entry point for a future hot-swap: when a peer is
replaced or restarts, both ends should reset so a stale SEQ is not taken as a
duplicate. Diagnostic counters survive the reset. The demo appends
` link=UP|DOWN retx=N dropped=N` to its USB lines.

`PARSECS_L1_FAULT_INJECT_EVERY` (undefined by default) flips every Nth non-dummy
Layer 1 TX byte, to exercise NACK and retransmission on hardware, e.g.
`-DPARSECS_L1_FAULT_INJECT_EVERY=97`.

Host tests: `make -C test/arq run` (gcc, MSYS2 on Windows). The tests run Layer 2,
Layer 3 and the Low Level API of two nodes over a simulated Layer 1. They cover
clean delivery, lost DATA, CRC corruption, lost ACK, an absent peer and
recovery, SEQ wrap, a stale ACK, queued data behind an ACK, a long peer frame,
and `PARSECS_Reset_Link`.

## Hot-plug

The master finds slaves at run time and stops polling the ones that are gone.
`PARSECS_Protocol_Interface_Task_Init` gives every known I2C address a slot:

| I2C | Board | `PARSECS_SLAVE_INDEX` | Demo value |
|---|---|---|---|
| `0x08` | comm (`CORE_TX_WIT_COMM_BOARD`) | 0 | 42 |
| `0x09` | mobility | 1 | 43 |
| `0x0A` | pm (`CORE_TX_WIT_PM_BOARD`) | 2 | 44 |
| `0x0B`..`0x0E` | board2..board5 (`CORE_TX_WIT_BOARD_NA2..5`) | 3..6 | 45..48 |

Each slot is in one of three states (`PARSECS_SLAVE_STATE`,
`PARSECS/PARSECS_HotPlug.c`):

    ABSENT --(HOTPLUG_ADD_OK_ROUNDS good probes in a row)--> LINKING
    LINKING --(link reset CMD ACKed)--> ACTIVE
    LINKING --(CMD dropped by the ARQ, or HOTPLUG_REMOVE_FAIL_ROUNDS failed rounds)--> ABSENT
    ACTIVE --(HOTPLUG_REMOVE_FAIL_ROUNDS failed rounds in a row)--> ABSENT

All slots start ABSENT, so the boot order of the boards does not matter.

**Round-robin (auto-scale).** `PARSECS_LowLevelTask` serves one slot per tick
and walks only LINKING and ACTIVE slots. With N of them each one gets every Nth
tick: one slave alone gets all of them (about 1 kB/s instead of 500 B/s), and an
unplugged one costs nothing after it is taken out. All Low Level timers count
rounds or calls of that slave, not milliseconds (ARQ RTO, L2 RX idle
watchdog), so they follow the new rate on their own. A slave answers its bytes
from the ISR and drains its whole RX ring every tick, so one round per tick is
within what it can take.

**Probes.** While some slot is ABSENT, one tick in `HOTPLUG_PROBE_PERIOD_TICKS`
(20, so 5 % of the ticks) is a probe instead of a data round; with no slave
polled at all every tick probes. A probe is a normal Layer 1 round (write,
repeated START, read) to the absent address with a dummy `0x00` out; the byte
read back is dropped and no ring or `l1_rounds` is touched. A probe that ends
with only `AF` set (address NACK, nobody there) is the expected result and does
not count towards `MAX_CONSEC_ERRORS` or the 8-of-32 window resync. Every other
probe failure does: HAL refusing to start the round (BUSY locked, handle not
READY), BERR, ARLO, a round timeout. With no slave polled only probes run, so
these are the only way a resync can still happen. Probes rotate over the absent
slots; a slot that answered its last probe is probed again on the next probe
tick, so a new board is confirmed in `HOTPLUG_ADD_OK_ROUNDS` probe ticks.
With all slots ACTIVE no probes are sent.

**All boards pulled, then plugged back** (`date_noi_9`, first version of this
branch): about 30 ms of contact bounce on SDA while the boards went in, then
both lines high and no START for the rest of the capture (18 s), with no resync
in the log. The likely cause is the bounce locking the BUSY flag of the
master's I2C1 (ES0182) or leaving the HAL handle not READY; either way
`HAL_I2C_Master_Seq_Transmit_DMA` returns `HAL_BUSY` without touching the bus. With slaves polled those refusals
used to reach `MAX_CONSEC_ERRORS` and the resync cleared BUSY; with only probes
running they were not counted, so nothing resynced and the boards were never
found. They are counted now, so 10 refusals in a row
resync I2C1 and the probes find the boards again.

**Removal.** Layer 1 keeps two saturating counters per slot, written from the
I2C ISR: `hp_ok_streak` and `hp_fail_streak`. Only a round whose HAL
`ErrorCode` is exactly `AF` (address NACK, nobody answered) counts as failed;
`HOTPLUG_REMOVE_FAIL_ROUNDS` (24) of them in a row take the slot out. A
timeout, BERR, ARLO or a refused start only clears `hp_ok_streak`: it is the
bus misbehaving, usually because another board is being plugged in, and says
nothing about this slave. (In `date_noi_10`, plugging mobility in gave 27 round
timeouts in a row and took the connected comm out for a few ms, costing one
request, while timeouts still counted.) A healthy slave never comes close (the
validation capture had 0 NACKs in 120 814 rounds). With two slaves a pulled
board is out about 48 ms later.

While a board goes in or out, rounds can time out every few ms for seconds
(`date_noi_10`, 69.5–72.5 s: no START on the bus, ~500 timeouts and ~45
`MAX_CONSEC_ERRORS` resyncs, then clean probes again). `L1 resync (timeout)` is
logged at most once a second, with the count since the previous line (`x37`).

**Link reset (CMD `0x9B`).** A slave that comes back may have kept running
while it was off the bus (old SEQs, an unACKed answer in its TX ring), and the
master may have restarted. When a slot goes LINKING the master calls
`PARSECS_Link_Sync_Start`: `PARSECS_Reset_Link` on its side, then a CMD frame
through the ARQ:

    7E 01 SEQ 9B 01 CRC16hi CRC16lo      # DATA[0] = PARSECS_CMD_LINK_RESET

- The slave's Layer 3 runs `PARSECS_Link_Reset_Local`: drops its ARQ frame,
  queued TX request and unread RX frames, and records the CMD SEQ as the last
  delivered SEQ, so the master's first DATA (CMD SEQ + 1) is new. A frame that
  Layer 2 is still sending is let out (the ISR may be inside it) but marked
  `FG_ARQ_STALE_TX`, so the slave does not wait for its ACK. Then it ACKs the
  CMD with its SEQ.
- Until that ACK arrives (`link_sync_pending`), the master ACKs DATA from the
  slave, so the slave stops resending it, but does not deliver it: it belongs
  to the old link. `PARSECS_TRANSMIT_APP` stays busy, so no request leaves.
- A resent CMD (lost ACK) resets again, which is harmless before any DATA.
- No ACK after `ARQ_MAX_RETRIES` (for example a board with older firmware that
  ACKs its address but not the CMD): LINK_DOWN, the slot goes back to ABSENT and
  is probed again later.

Both ends bump `SLAVE.link_resets` on every reset (`PARSECS_Get_Link_Epoch`).
`PARSECS_Protocol_Interface_Task` re-initialises the board's L4 descriptor when
it changes (half-assembled segments are dropped), and on the master marks the
board available only while its slot is ACTIVE. `PARSECS_Protocol_USER_Send` /
`USER_Receive` return `PARSECS_PROTOCOL_ERROR_BOARD_NOT_AVAILABLE` for any other
state.

API:

| Function | Returns |
|---|---|
| `PARSECS_Get_Slave_State(id)` | `PARSECS_SLAVE_ABSENT` / `LINKING` / `ACTIVE` (always ACTIVE on a slave), -1 / -2 |
| `PARSECS_Get_HotPlug_Stats(id, &added, &removed)` | 0, or -1 / -2 |
| `PARSECS_Get_Link_Epoch(id)` | link reset counter |
| `PARSECS_Link_Sync_Start(id)` | master only; 0, or -1 / -2 |
| `PARSECS_HotPlug_Active_Count()` | slots in the round-robin |
| `PARSECS_Protocol_Board_Available(board)` | true while USER_Send / USER_Receive accept the board |
| `PARSECS_Protocol_Board_State(board)` | hot-plug state of the board's slot, -1 if not configured |

Host tests: `make -C test/hotplug run`. The real Low Level task, hot-plug code,
Layer 2/3 and Low Level API of the master run against boards with their own
stack over a simulated Layer 1. They cover discovery at boot and on an empty
bus, removal and the tick share after it, no probes when all boards are
active, a board plugged in while the master polls, a flaky probe, a board
that kept stale state while unplugged, a board that never ACKs the CMD, and
two boards answering at the same time.

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
| Master, comm | `CORE_TX_WIT_COMM_BOARD` (6) | motherboard (0) → comm (6) |
| Master, mobility | `CORE_TX_WIT_MOBILITY_BOARD` (7) | motherboard (0) → mobility (7) |
| Slave | `CORE_TX_WIT_MOTHERBOARD` (0) | this slave's board → motherboard (0) |

On the master, `MAX_BOARD_COUNT` is 2 (`CORE_TX_Wit_Boards[0]` comm,
`[1]` mobility). A slave image has `MAX_BOARD_COUNT` 1 and its descriptor is
`CORE_TX_WIT_MOTHERBOARD`. Lookup is by the `boardAddress` field so the WIT
enum is not used as an array index. Layer 1's I2C address is separate: comm
`0x08`, mobility `0x09`, stored with `PARSECS_Set_Slave_I2C_Address`.

## DMA

Transfers are driven by `HAL_I2C_*_Seq_*_DMA`. `hdma_i2c1_rx` (DMA1 Stream0,
Channel 1) and `hdma_i2c1_tx` (DMA1 Stream6, Channel 1) are configured in
`HAL_I2C_MspInit` (`i2c.c`). Completion callbacks are the same HAL names used
for `_IT` transfers.

L2 runs in task context and L1 callbacks run in ISR context, so every ring
push/pop goes through `PARSECS_Port.h` (`__disable_irq` / restore).

## Test payload

`PARSECS_Protocol_Interface_Task_Init()` on the master calls `PARSECS_Add_Slave`
twice with no-op select/deselect, then `PARSECS_Set_Slave_I2C_Address`
(`0x08` comm, `0x09` mobility). The demo loops, retrying `USER_Send` until it
returns `PARSECS_PROTOCOL_OK`:

1. `GetRequest` to comm, TypeID `0x01`, BER Null, OperationControl `0xFF`.
   Comm answers `GetResponse` BER integer `42`, `PARSECS_APP_Success`.
2. The same request to mobility. Mobility answers `43`.
3. Settle 100 ms (`DEMO_SETTLE_MS`) with Layer 1 still running so the last
   ACK can leave the tx ring.
4. Hold SCL then SDA low for 500 ms (`DEMO_BUS_HOLD_MS`), then release SDA
   then SCL and re-init I2C1.
5. Repeat.

A slave that does not answer within `DEMO_RESPONSE_TIMEOUT_MS` (2000 ms) is
logged `FAIL` and the cycle continues. Each slave image accepts another
request after it has queued its response. `PARSECS_SLAVE_INDEX` 0 is comm;
1 is mobility. Flash the slave project once per index.

Expected USB CDC for one pass:

```
[Master] comm queued GetRequest TypeID=0x01
[Slave] comm rx GetRequest TypeID=0x01
[Slave] comm queued GetResponse TypeID=0x01 value=42
[Master] comm rx GetResponse TypeID=0x01 value=42 PASS
[Master] mobility queued GetRequest TypeID=0x01
[Slave] mobility rx GetRequest TypeID=0x01
[Slave] mobility queued GetResponse TypeID=0x01 value=43
[Master] mobility rx GetResponse TypeID=0x01 value=43 PASS
[Master] holding SCL and SDA low
```

The two slave lines come from the two slave boards, not from one log.

Saleae still shows L2 `7E … 8C` DATA (WIT PDU + BER, not ASCII PING) then L3
ACK `7E 00 xx 7C …`. Leading/trailing `00` bytes are L1 dummy. Dummy `0x00` L1
bytes keep the link pumping when a TX ring is empty, matching PARSECS Layer 1
idle behaviour. The byte-by-byte timeline of one cycle, from one bus-low gap
to the next, is in [SALEAE.md](SALEAE.md).

## Error handling

- Master watchdog: if a round holds the bus longer than
  `TRANSACTION_TIMEOUT_TICKS`, re-init I2C (and re-link the DMA handles).
  Layer 1 does not retry a specific byte.
- Both sides: after `MAX_CONSEC_ERRORS`, `HAL_I2C_DeInit` + `MX_I2C1_Init`.
  While the master is holding the lines low, Layer 1 does not start a round
  and does not resync. `PARSECS_Layer1_HoldLinesLow` waits until the bus is
  idle (or `TRANSACTION_TIMEOUT_TICKS`), deinits I2C1, then drives PB6 low
  and PB7 low as open-drain outputs. `PARSECS_Layer1_ReleaseLines` releases
  PB7, then PB6, then calls `MX_I2C1_Init`.
- A byte popped from `l1_buffer_tx` is not put back on error; a byte dropped
  because `l1_buffer_rx` was full is also not retried at Layer 1. Recovery is
  at Layer 3. An L2 CRC failure raises `FG_NACK`, and the peer sends a NACK
  frame, which triggers a retransmission. A frame that is lost entirely is resent
  after the ARQ timeout. See [Retransmission (ARQ)](#retransmission-arq).

## Constants

| Constant | Default | Notes |
|---|---|---|
| `RAW_RING_BUFFER_SIZE` | 16 | L1 ring capacity |
| `DUMMY_BYTE` | 0x00 | filler when `l1_buffer_tx` is empty |
| Comm I2C address | 0x08 | `PARSECS_I2C_ADDR_COMM_7BIT`, slave index 0 |
| Mobility I2C address | 0x09 | `PARSECS_I2C_ADDR_MOBILITY_7BIT`, slave index 1 |
| Other I2C addresses | 0x0A..0x0E | pm, board2..board5, slave index 2..6 |
| `TRANSACTION_TIMEOUT_TICKS` | 50 | master L1 watchdog, and hold wait |
| `MAX_CONSEC_ERRORS` | 10 | errors before a resync |
| `ARQ_MAX_RETRIES` | 3 | retransmissions before a frame is dropped |
| `ARQ_RTO_ROUNDS` | 70 | base retransmission timeout, in L1 rounds |
| `ARQ_RTO_MAX_ROUNDS` | 280 | timeout backoff cap |
| `PARSECS_L1_FAULT_INJECT_EVERY` | undefined | test aid: corrupt every Nth L1 byte |
| `SPI_DATA_LENGTH` | 254 | max L2 DATA payload |
| `MAX_BOARD_COUNT` | 7 / 1 | master: one per known I2C address / one descriptor on a slave |
| `HOTPLUG_ADD_OK_ROUNDS` | 3 | good probes in a row before a slot is registered |
| `HOTPLUG_REMOVE_FAIL_ROUNDS` | 24 | failed rounds in a row before a slot leaves the round-robin |
| `HOTPLUG_PROBE_PERIOD_TICKS` | 20 | one probe tick in this many while some slot is absent |
| `PARSECS_CMD_LINK_RESET` | `0x01` | DATA[0] of the link reset CMD frame |
| `PARSECS_WIT_FRAME_LENGTH` | 1024 | L4 assembled WIT FRAME |
| Demo TypeID | `0x01` | GetRequest / GetResponse |
| Demo values | `42`..`48` | comm, mobility, pm, board2..board5 BER integers |
| `DEMO_SETTLE_MS` | 100 | ACK drain before the bus is held |
| `DEMO_BUS_HOLD_MS` | 500 | SCL and SDA held low between repeats |
| `DEMO_RESPONSE_TIMEOUT_MS` | 2000 | per-slave wait before FAIL |
