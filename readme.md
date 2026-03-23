# ESP32-C6 Serial Watchdog Relay

ESP32-C6 based watchdog device that monitors serial communication and cycles a relay if no communication is received within a configurable timeout.

## Features

- **Serial Communication**: Support for both TTL UART and USB CDC modes
- **Watchdog Timer**: Configurable timeout (default 15 minutes)
- **Relay Control**: Cycles relay to restart monitored device
- **Restart Attempt Tracking**: Persisted to flash, survives power loss
- **WiFi Access Point**: Built-in web interface for configuration
- **LED Status Indicators**: Visual feedback for device state

## Hardware

- Seeedstudio XIAO ESP32-C6 or similar ESP32-C6 board
- Relay module
- Status LED

## Quick Start

1. Install ESP-IDF v5.x
2. Clone this repository
3. Build and flash:
   ```bash
   idf.py set-target esp32c6
   idf.py build
   idf.py flash monitor
