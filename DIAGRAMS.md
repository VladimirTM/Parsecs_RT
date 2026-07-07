# Diagrams

## Hardware wiring

    Master (STM32F407 Disc)              Slave (STM32F407 Disc)
      PB6 SCL o---------+----------------o PB6 SCL
      PB7 SDA o------+--|----------------o PB7 SDA
      GND     o------|--|----------------o GND
      USB <-> PC     |  |                USB <-> PC
                   [4k7][4k7] to 3.3V (pull-ups on SDA/SCL)

## One round on the wire

    write (M->S):  S  0x10 A  d0..d15  P        (16 data bytes, one Seq call each)
    read  (M<-S):  Sr 0x11 A  d0..d15  N  P     (slave flags d15 LAST_FRAME)

    S = START, Sr = repeated START, A = ACK, N = NACK, P = STOP
    0x10 / 0x11 = slave address 0x08 shifted, write / read

Each data byte is one HAL sequential call; the cursor advances in the per-byte
completion callback (FIRST -> NEXT -> LAST). The transfer runs under the I2C1
interrupt while the FreeRTOS task sleeps in `osDelay(1)`.

## Boot

    main():  HAL_Init -> SystemClock (168 MHz) -> MX_GPIO_Init -> MX_I2C1_Init
          -> osKernelInitialize -> MX_FREERTOS_Init -> osKernelStart
    tasks:   defaultTask (USB init, then osDelay 1)
             myTestTask  (MyDemoTask, then osDelay 1)
