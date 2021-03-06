spi_slave_dma

This example demonstrates using DMA with SPI as a slave.
10 bytes, 0x00 - 0x09, are transmitted on MOSI.
10 bytes are received on MISO. Expected values are 0xA0 - 0xA9.

How to connect the master board to slave board:
Connect master CS to slave CS
Connect master SCLK to slave SCLK
Connect master MOSI to slave MOSI
Connect master MISO to slave MISO

How To Test:
1. Build the project and download to the Starter Kit
2. Build spi_slave project and download to a Starter Kit
3. Place breakpoint in master's main() per comments
4. Run spi_dma_slave 
5. Run spi_dma_master 
6. After hitting the breakpoint, observe RxBuffer[] in IDE variables/expressions window.
   RxBuffer[] should contain 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09.

Peripherals Used:
HFRCO  - 14 MHz
USART1 - Synchronous (SPI) mode, CLK = 1 MHz

Board:  Silicon Labs EFM32TG Starter Kit (EFM32TG_STK3300)
Device: EFM32TG840F32
PD0 - USART1 MOSI - EXP Header Pin 4
PD1 - USART1 MISO - EXP Header Pin 6
PD2 - USART1 CLK  - EXP Header Pin 8
PD3 -  USART1 CS  - EXP Header Pin 10