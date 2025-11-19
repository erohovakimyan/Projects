# Wireless Environmental Monitoring System
Using Microchip PolarFire SoC Discovery Kit

This project implements a wireless temperature and humidity monitoring system using two Microchip PolarFire SoC Discovery Kits. One board functions as the Transmitter (TX) that measures environmental data, and the other functions as the Receiver (RX) that displays the received data on an OLED screen. Communication between nodes is performed using the nRF24L01 wireless transceiver.

## Features
- Real-time temperature and humidity measurement using HDC2080 (I2C)
- Wireless data transmission using nRF24L01 (SPI)
- Live data display on SSD1351 OLED (SPI)
- Bare-metal C firmware for PolarFire SoC Discovery Kit
- Fully reproducible hardware and software setup

## Project Structure
/TX_Firmware        - Transmitter firmware
/RX_Firmware        - Receiver firmware
/docs               - Documentation and diagrams
/README.md          - Project description

## System Architecture

### Transmitter Node (TX)
- PolarFire SoC Discovery Kit
- HDC2080 temperature and humidity sensor (I2C)
- nRF24L01 wireless module (SPI)
Function: Reads temperature and humidity, packs data into an 8-byte payload, and transmits it wirelessly.

### Receiver Node (RX)
- PolarFire SoC Discovery Kit
- nRF24L01 wireless module (SPI)
- OLED display with SSD1351 controller (SPI)
Function: Receives data packets and displays values on the OLED screen.

## Hardware Connections

### Transmitter (TX)
- I2C0 → HDC2080
- SPI0 → nRF24L01
- UART0 → Debug output

### Receiver (RX)
- SPI1 → nRF24L01
- SPI0 → OLED SSD1351
- UART0 → Debug output

Note: For nRF24L01, it is recommended to add a 10 µF capacitor between 3.3 V and GND to improve RF stability.

## Software Overview
The firmware is written in C using Microchip SoftConsole.
Drivers used:
mpfs_hal
mss_spi
mss_i2c
mss_gpio
mss_uart

Both transmitter and receiver include:
- Peripheral initialization
- Communication drivers
- Main application loops

## Transmitter Logic (TX)
Initialize UART, I2C, SPI
Initialize HDC2080 sensor
Configure nRF24L01 in TX mode (channel 76, 1 Mbps)

Loop forever:
    Read temperature and humidity
    Convert values to centi-units
    Pack into 8-byte payload
    Send packet through nRF24L01
    Delay 1 second

## Receiver Logic (RX)
Initialize UART, SPI0 (OLED), SPI1 (nRF24L01)
Initialize SSD1351 OLED
Configure nRF24L01 in RX mode (channel 76, 1 Mbps)

Loop forever:
    If packet received:
        Read 8-byte payload
        Decode temperature and humidity
        Format display strings
        Draw values on OLED

## Getting Started

### 1. Install Required Tools
- Microchip Libero SoC
- Microchip SoftConsole
- PolarFire SoC bare-metal driver examples

### 2. Clone the Repository
git clone https://github.com/yourusername/Wireless-Environmental-Monitoring-System.git
cd Wireless-Environmental-Monitoring-System

### 3. Import the Project into SoftConsole
- Open SoftConsole
- File → Import → Existing Projects
- Select this repository
- Build TX_Firmware and RX_Firmware

### 4. Flash the Boards
- Flash TX firmware to the first board
- Flash RX firmware to the second board

## Implementation Stages
1. OLED display driver test
2. HDC2080 sensor read test
3. nRF24L01 wireless transmission test
4. Full TX → RX → OLED integration

## Future Improvements
- Multi-node network support
- SD card or cloud-based data logging
- RTC for timestamping
- Adaptive transmission intervals
- Low-power optimization

## License
This project is licensed under the MIT License.
