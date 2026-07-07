# I2C transmission protocol

How the two boards exchange data in the non-blocking demo. Code is in
`PARSECS_RT_master/Application/demo_task.c` and the slave equivalent.

## Wire format

Each round is two transfers to slave `0x08` on I2C1 (400 kHz, 7-bit):

    write (M->S):  "hello 7\0..."   16 bytes
    read  (M<-S):  "HELLO 7\0..."   16 bytes

16 bytes each way, fixed, NUL-padded (no length byte). Byte 15 is always `'\0'`.

## Byte-by-byte framing

Both sides hand the HAL one byte per sequential call and advance a cursor in the
per-byte completion callback:

    byte 0      FIRST -> START + address
    bytes 1..14 NEXT  -> stream
    byte 15     LAST  -> STOP (write) / terminating NACK (read)

The master uses `I2C_FIRST_AND_NEXT_FRAME` for byte 0, the slave uses
`I2C_FIRST_FRAME`. The slave flags its reply's last byte `I2C_LAST_FRAME` so the
master's terminating NACK is seen as a normal end-of-read (`ListenCpltCallback`),
not an error. The slave's receive never uses `LAST` - the master's STOP ends it.

## One round

1. Master queues `"hello N"` and, if the bus is idle, sends byte 0.
2. Slave `AddrCallback` (write) arms its first receive byte.
3. Each byte fires `MasterTxCplt` / `SlaveRxCplt`, which arm the next byte; on the
   last received byte the slave upper-cases the request into the reply buffer.
4. The write STOP hits the slave's `ListenCplt`, which re-arms listen.
5. From its last `TxCplt` the master turns the bus around and starts the read.
6. Slave `AddrCallback` (read) arms its first reply byte; `TxCplt` streams the rest.
7. On the last echo byte the master frees the TX slot and sets `echo_ready`.
8. The read's NACK hits the slave's `ListenCplt` -> round complete, log `{rx, tx}`.
9. Master task verifies and prints the echo; slave task drains the log to USB.

Steps 2-8 run in the I2C ISR and finish within one task tick.

## Error handling

- Master watchdog: if a round holds the bus longer than
  `TRANSACTION_TIMEOUT_TICKS`, re-init I2C and retry the same slot.
- Both sides: after `MAX_CONSEC_ERRORS`, `HAL_I2C_DeInit` + `MX_I2C1_Init`.
- A failed round never advances `read_index`, and the slave always re-arms listen,
  so both sides re-sync on the next good exchange.

## Constants

| Constant | Default | Notes |
|---|---|---|
| `MSG_LEN` | 16 | must match both boards |
| `SLAVE_ADDRESS` / `OwnAddress1` | 0x08 | |
| `PRODUCER_PERIOD_TICKS` | 250 | how often a new message is queued |
| `TX_SLOTS` / `LOG_SLOTS` | 8 | power of two |
| `TRANSACTION_TIMEOUT_TICKS` | 50 | watchdog |
| `MAX_CONSEC_ERRORS` | 10 | errors before a resync |
