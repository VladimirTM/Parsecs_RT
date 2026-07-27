# Diagrams

Visual reference only - see [TRANSMISSION_PROTOCOL.md](TRANSMISSION_PROTOCOL.md) for the full written walkthrough (callback-by-callback, error handling, constants) and [README.md](README.md) for the project overview.

## Hardware wiring

    Master (STM32F407 Disc)              Slave (STM32F407 Disc)
      PB6 SCL o---------+----------------o PB6 SCL
      PB7 SDA o------+--|----------------o PB7 SDA
      GND     o------|--|----------------o GND
      USB <-> PC     |  |                USB <-> PC
                   [4k7][4k7] to 3.3V (pull-ups on SDA/SCL)

## One round on the wire

    write (M->S):  S  0x10 A  d0  P        (1 byte, popped from master's tx_ring)
    read  (M<-S):  Sr 0x11 A  d0  N  P     (1 byte, popped from slave's tx_ring)

    S = START, Sr = repeated START, A = ACK, N = NACK, P = STOP
    0x10 / 0x11 = slave address 0x08 shifted, write / read

Each byte is a complete standalone `I2C_FIRST_AND_LAST_FRAME` DMA transfer
(`HAL_I2C_*_Seq_*_DMA`) - there is no per-byte cursor loop like a multi-byte
frame would need. The write phase pushes into the slave's `rx_ring`; the read
phase pops the slave's `tx_ring` back into the master's `rx_ring`. The
transfer runs under the I2C1/DMA1 interrupts while the FreeRTOS task sleeps
in `osDelay(1)`.

## Boot

    main():  HAL_Init -> SystemClock (168 MHz) -> MX_GPIO_Init -> MX_DMA_Init
          -> MX_I2C1_Init -> DemoTask_Init (seed tx_ring)
          -> osKernelInitialize -> MX_FREERTOS_Init -> osKernelStart
    tasks:   defaultTask (USB init, then osDelay 1)
             myTestTask  (MyDemoTask, then osDelay 1)
