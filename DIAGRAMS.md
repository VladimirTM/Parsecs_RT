# Diagrams

Visual reference only - see [TRANSMISSION_PROTOCOL.md](TRANSMISSION_PROTOCOL.md) for the full written walkthrough (callback-by-callback, error handling, constants) and [README.md](README.md) for the project overview.

## Hardware wiring

    Master                         Comm slave (0x08)          Mobility slave (0x09)
      PB6 SCL o--------------------o PB6 SCL ----------------o PB6 SCL
      PB7 SDA o--------------------o PB7 SDA ----------------o PB7 SDA
      GND     o--------------------o GND     ----------------o GND
      USB <-> PC                   USB <-> PC                USB <-> PC

    [4k7] pull-ups to 3.3V on SDA and SCL, once for the whole bus.

## One round on the wire

    write (M->S):  S  0x10 A  d0  P        (1 byte, popped from master's l1_buffer_tx)
    read  (M<-S):  Sr 0x11 A  d0  N  P     (1 byte, popped from slave's l1_buffer_tx)

    S = START, Sr = repeated START, A = ACK, N = NACK, P = STOP
    0x10 / 0x11 = comm slave address 0x08 shifted, write / read
    0x12 / 0x13 = mobility slave address 0x09 shifted, write / read

Between demo repeats the master releases I2C1 and drives SCL low, then SDA
low, for 500 ms. Both lines go back high (SDA first) before the next round.

Each byte is a complete standalone `I2C_FIRST_AND_LAST_FRAME` DMA transfer
(`HAL_I2C_*_Seq_*_DMA`) — Layer 1 is still the byte pump; Layer 2 assembles
those bytes into `SPI_BASE_FRAME`. The write phase pushes into the slave's
`l1_buffer_rx`; the read phase pops the slave's `l1_buffer_tx` back into the
master's `l1_buffer_rx`. The transfer runs under the I2C1/DMA1 interrupts
while `PARSECS_LowLevelTask` / `PARSECS_Protocol_Interface_Task` / `MyDemoTask`
run each 1 ms tick.

## Boot

    main():  HAL_Init -> SystemClock (168 MHz) -> MX_GPIO_Init -> MX_DMA_Init
          -> MX_I2C1_Init -> PARSECS_LowLevelTaskInit
          -> PARSECS_Protocol_Interface_Task_Init -> DemoTask_Init
          -> osKernelInitialize -> MX_FREERTOS_Init -> osKernelStart
    tasks:   defaultTask (USB init, then osDelay 1)
             myTestTask  (PARSECS_LowLevelTask, PARSECS_Protocol_Interface_Task,
                          MyDemoTask, then osDelay 1)
