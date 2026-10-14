# Saleae: one demo cycle between two bus lows

What a logic capture of SCL (PB6) and SDA (PB7) shows for one pass of the
dual-slave GetRequest demo. The window starts when both lines leave the held-low
gap and ends when the master drives them low again.

Probe SCL and SDA, add an I2C analyzer at 400 kHz, 7-bit addresses. The analyzer
prints `0x08` and `0x09`. On the raw wire the address byte is that value shifted
left, with the read bit in bit 0: write `0x10` / `0x12`, read `0x11` / `0x13`.

This picture is the first window after reset, so every DATA sequence number is
`0x00`. The next window is the same conversation with sequence `0x01`, then
`0x02`, and so on. Each slave has its own counter.

## The two lows

| Edge | What the lines do | What it is not |
|---|---|---|
| Enter the gap | SCL falls while SDA is still high, then SDA falls | not a START |
| During the gap | both lines stay low for about 500 ms (`DEMO_BUS_HOLD_MS`) | no I2C |
| Leave the gap | SDA rises while SCL is still low, then SCL rises | not a STOP |

After the release the bus sits idle-high until the first START.

## How each byte shows up

Layer 1 never sends a repeated START. One "round" is two complete transactions,
a few tens of microseconds apart at 400 kHz:

```
START  addr+W  one byte  STOP
START  addr+R  one byte  NACK  STOP
```

The master visits one slave per millisecond tick, and it alternates. Comm is
`0x08`, mobility is `0x09`. So the capture is pairs, about 1 ms apart, switching
address every pair:

```
write 0x08, read 0x08
write 0x09, read 0x09
write 0x08, read 0x08
...
```

A frame is the write column of one address, or the read column of one address.
The other column is often `0x00`. Reassemble a frame by following only one
direction on one address and ignoring the other slave's pairs.

`0x00` is the dummy byte Layer 1 sends when that direction's ring is empty.
`0x7E` is `START_OF_FRAME` and is the byte to search for.

## Timeline of one window

1. Both lines low, then released to idle-high.
2. A short run of dummy pairs (`0x00` written, `0x00` read) on `0x08` and
   `0x09`. The count is not fixed. It is however long the stack takes before
   the comm GetRequest sits in the transmit ring.
3. Comm GetRequest, then the comm board's ACK, then its GetResponse, then the
   master's ACK. Mobility pairs of `0x00` sit between those bytes.
4. The same four frames on `0x09` (destination mobility, value `43`). Comm
   pairs of `0x00` sit between those bytes.
5. About 100 ms of dummy pairs while the last ACK drains (`DEMO_SETTLE_MS`).
6. Both lines low again.

## Frames, in the order they appear

L2 on the wire:

```
7E  LEN  SEQ  TYPE  DATA[LEN]  CRC_hi  CRC_lo
```

`7E` is not part of the CRC. CRC16 covers `LEN`, `SEQ`, `TYPE`, and `DATA`,
init `0xFFFF`, result inverted (the PPP FCS used in `crc16.c`).

ACK has `LEN = 0`, so it is six bytes and has no `DATA`. For sequence `0x00`
the CRC is `7F 27`:

```
7E 00 00 7C 7F 27
```

| Step | Address | Direction in Saleae | Frame |
|---|---|---|---|
| 1 | `0x08` | master write | GetRequest DATA, `SEQ 00`, `TYPE 8C` |
| 2 | `0x08` | master read | ACK of that request: `7E 00 00 7C 7F 27` |
| 3 | `0x08` | master read | GetResponse DATA, value 42, `SEQ 00`, `TYPE 8C` |
| 4 | `0x08` | master write | ACK of that response: `7E 00 00 7C 7F 27` |
| 5 | `0x09` | master write | GetRequest DATA, `SEQ 00`, `TYPE 8C` |
| 6 | `0x09` | master read | ACK: `7E 00 00 7C 7F 27` |
| 7 | `0x09` | master read | GetResponse DATA, value 43, `SEQ 00`, `TYPE 8C` |
| 8 | `0x09` | master write | ACK: `7E 00 00 7C 7F 27` |

Steps 2 and 3 are back-to-back on the read column. The slave sends the ACK
before the GetResponse. While those reads are in progress the write column on
the same address is already `0x00`, unless the master's own ACK (step 4) has
started. Do not expect the ACK read to begin on the same pair as the last
request byte.

## What the DATA bytes are

`TYPE` is `8C`. `LEN` is the WIT PDU size. The PDU is one segment:

```
00          flags (not a multi-packet PDU)
01          total packets
01          this packet
NN          BER length
<NN bytes>  BER
```

So `LEN` is `4 + NN`.

GetRequest BER, comm (`0x08`), then the same shape for mobility with
destination `7` instead of `6`:

| Field | Value |
|---|---|
| structure, 2 integers | source `0` (motherboard), destination `6` (comm) or `7` (mobility) |
| structure, 3 fields + data | operation GetRequest (`1`), control NULL (`0xFF` on the API, encoded as BER NULL), TypeID `1` |
| operation data | BER NULL |

GetResponse BER, same header, with:

| Field | Comm | Mobility |
|---|---|---|
| source | `6` | `7` |
| destination | `0` | `0` |
| operation | GetResponse (`2`) | GetResponse (`2`) |
| control | Success (`0`) | Success (`0`) |
| TypeID | `1` | `1` |
| operation data | BER integer `42` | BER integer `43` |

The two CRC bytes that follow `DATA` change with that payload. They are not the
ACK CRC above.

## First bytes of the comm request, as Saleae lists them

`LL` is the L2 length byte (`4 + BER length`). After `7E LL 00 8C` the write
column is the WIT PDU, then two CRC bytes. Every other pair is mobility idle.

```
0x08 W  7E          0x08 R  00
0x09 W  00          0x09 R  00
0x08 W  LL          0x08 R  00
0x09 W  00          0x09 R  00
0x08 W  00          0x08 R  00          SEQ
0x09 W  00          0x09 R  00
0x08 W  8C          0x08 R  00          TYPE_DATA
0x09 W  00          0x09 R  00
0x08 W  00          0x08 R  00          WIT flags
0x09 W  00          0x09 R  00
0x08 W  01          0x08 R  00          total packets
...
```

The read column stays `00` until the slave's ACK. That ACK is the six bytes
`7E 00 00 7C 7F 27` on `0x08 R`, still with a `0x09` dummy pair between each
of them.
