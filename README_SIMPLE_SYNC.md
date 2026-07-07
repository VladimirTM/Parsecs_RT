# Simple synced I2C demo (feature/i2c-simple-sync)

Minimal blocking demo: the master sends the letter `'A'` to the slave over I2C1
and both boards print a counter over USB CDC.

To keep the two counters from drifting, the master puts its sequence number in the
payload and the slave just echoes back whichever number it receives (it does not
run its own counter). A failed round is retried with the same number, so every
successful round prints the same `N` on both sides. If the slave sees a gap it logs
a warning and adopts the new number.

## Payload

    byte 0-1 : sequence number (uint16, little-endian)
    byte 2   : letter ('A')

## Notes

- Both task loops use `osDelay(1)` so the slave sits inside
  `HAL_I2C_Slave_Receive` almost all the time and rarely misses the master.
- A stuck bus (`HAL_BUSY`) is recovered with `HAL_I2C_DeInit` + `MX_I2C1_Init`
  after 10 consecutive errors; idle-bus timeouts are ignored.
- Single master, so no arbitration is needed; the hardware ACK already confirms
  the slave heard each byte.
