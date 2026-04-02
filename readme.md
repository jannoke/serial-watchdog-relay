# ESP32-C6 Serial Watchdog Relay

ESP32-C6 based watchdog device that monitors serial communication and cycles a relay if no communication is received within a configurable timeout.

## Features

- **Serial Communication**: Support for both TTL UART and USB CDC modes
- **Serial Command Protocol**: Full text-based command interface for configuration and control
- **Watchdog Timer**: Configurable timeout (default 15 minutes)
- **Relay Control**: Cycles relay to restart monitored device
- **Restart Attempt Tracking**: Persisted to flash, survives power loss
- **WiFi Access Point**: Built-in web interface for configuration
- **LED Status Indicators**: Visual feedback for device state
- **OLED Display**: 128×32 I2C SSD1306 display showing watchdog countdown, attempt counter, WiFi status, and heartbeat indicator
- **Server Tools**: Python keepalive daemon and CLI utility

## Hardware

- Seeedstudio XIAO ESP32-C6 or similar ESP32-C6 board
- Relay module
- Status LED (external, connected to D0 / GPIO 2)
- 128×32 SSD1306 OLED module (I2C, connected to D4/SDA and D5/SCL)

## Default Pin Assignments

| Pin Function | Default GPIO | XIAO Label | Config Parameter |
|---|---|---|---|
| Relay | GPIO 4 | D2 / A2 | `relay_pin` |
| Status LED | GPIO 2 | D0 / A0 | `led_pin` |
| Button | GPIO 9 | BOOT button | `button_pin` |
| TTL UART TX | GPIO 16 | D6 / TX | *(fixed in firmware)* |
| TTL UART RX | GPIO 17 | D7 / RX | *(fixed in firmware)* |
| OLED SDA | GPIO 6 | D4 / SDA | `oled_sda_pin` |
| OLED SCL | GPIO 7 | D5 / SCL | `oled_scl_pin` |

> **Note:** GPIO 8 on the XIAO ESP32-C6 drives the on-board WS2812 RGB LED and
> cannot be used as a plain GPIO output. GPIO 15 is a strapping pin — avoid
> connecting external circuitry to it.  All pin assignments except the fixed
> UART pins can be changed via `SET <param> <value>` and are persisted to flash.

## Quick Start

1. Install ESP-IDF v5.x
2. Clone this repository
3. Build and flash:
   ```bash
   idf.py set-target esp32c6
   idf.py build
   idf.py flash monitor
   ```

## Serial Command Protocol

The device exposes a text-based command interface over the serial port (115200 baud, 8N1).
Commands are terminated with `\n` or `\r\n`; all responses end with `\r\n`.
Command names are **case-insensitive**.

> **Important:** Only the `PING` and `RESET_TIMER` commands reset the watchdog timer.
> `STATUS` and `GET` can be used for monitoring without affecting the watchdog countdown.

### Commands

| Command | Description | Resets Watchdog |
|---|---|---|
| `PING` | Keepalive — resets watchdog timer | ✅ Yes |
| `STATUS` | Get device status as JSON | ❌ No |
| `GET <param>` | Get a specific parameter value | ❌ No |
| `SET <param> <value>` | Set a parameter value (persists to NVS) | ❌ No |
| `RESET_TIMER` | Reset watchdog timer explicitly | ✅ Yes |
| `RESET_ATTEMPTS` | Reset restart attempt counter to zero | ❌ No |
| `RELAY ON\|OFF\|CYCLE [ms]` | Manual relay control | ❌ No |
| `REBOOT` | Reboot the device | N/A |
| `HELP` | Show available commands | ❌ No |

### Response Format

```
OK                  – command successful
OK <value>          – command successful with a return value
ERROR <message>     – command failed
{ ... }             – JSON object (STATUS response)
```

### STATUS Response

```json
{
  "uptime_ms": 123456,
  "watchdog_remaining_ms": 890000,
  "restart_attempts": 0,
  "max_restart_attempts": 5,
  "relay_state": true,
  "timeout_ms": 900000,
  "off_period_ms": 5000
}
```

### GET / SET Parameters

| Parameter | Description | Unit |
|---|---|---|
| `timeout` | Watchdog inactivity timeout | seconds |
| `off_period` | Relay off duration during a cycle | seconds |
| `max_attempts` | Maximum relay-cycle attempts before halting | count |
| `relay_pin` | Relay GPIO pin number | — |
| `led_pin` | LED GPIO pin number | — |
| `button_pin` | Button GPIO pin number | — |
| `serial_mode` | Serial mode: 0 = TTL UART, 1 = USB CDC | — |
| `wifi_ssid` | WiFi AP SSID | — |
| `wifi_password` | WiFi AP password | — |
| `wifi_hidden` | Hide WiFi AP: 0 = visible, 1 = hidden | — |
| `oled_sda_pin` | OLED I2C SDA GPIO pin | — |
| `oled_scl_pin` | OLED I2C SCL GPIO pin | — |
| `oled_i2c_addr` | OLED I2C address (hex accepted, e.g. `0x3C`) | — |
| `oled_enabled` | Enable OLED display: 0 = off, 1 = on | — |

### Quick Terminal Test

```bash
# Linux – using screen
screen /dev/ttyUSB0 115200

# Or using minicom
minicom -D /dev/ttyUSB0 -b 115200

# Type commands, e.g.:
PING
STATUS
GET timeout
SET timeout 600
```

---

## Server-Side Tools (`server/`)

Python tools for communicating with the device from a Linux/macOS host.

### Installation

```bash
cd server
pip install -r requirements.txt
```

### watchdog_daemon.py — Keepalive Daemon

Sends periodic `PING` commands to keep the watchdog alive.
Handles serial port reconnection automatically.

```
usage: watchdog_daemon.py [-h] [--port PORT] [--baud BAUD]
                          [--interval INTERVAL] [--log-file FILE]
                          [-v] [--warning-script SCRIPT]
                          [--failure-script SCRIPT]
                          [--max-failures N] [--config FILE]

optional arguments:
  --port PORT            Serial port device (default: /dev/ttyUSB0)
  --baud BAUD            Serial baud rate (default: 115200)
  --interval INTERVAL    PING interval in seconds (default: 60)
  --log-file FILE        Write log output to FILE
  -v, --verbose          Enable debug logging
  --warning-script SCRIPT  Script to run on a PING failure warning
  --failure-script SCRIPT  Script to run after repeated failures
  --max-failures N       Consecutive failures before failure script (default: 3)
  --config FILE          INI config file
```

#### Examples

```bash
# Run in foreground
python3 watchdog_daemon.py --port /dev/ttyUSB0 --interval 60 --verbose

# Use config file
python3 watchdog_daemon.py --config /etc/watchdog-relay.conf
```

### watchdog_cli.py — Command Line Interface

Sends individual commands or starts an interactive shell.

```
usage: watchdog_cli.py [-h] [--port PORT] [--baud BAUD]
                       [--timeout TIMEOUT] [-i] [command ...]

optional arguments:
  --port PORT      Serial port device (default: /dev/ttyUSB0)
  --baud BAUD      Serial baud rate (default: 115200)
  --timeout SECS   Read timeout in seconds (default: 2)
  -i, --interactive  Start interactive command shell
```

#### Examples

```bash
# Get device status
python3 watchdog_cli.py --port /dev/ttyUSB0 status

# Set watchdog timeout to 10 minutes
python3 watchdog_cli.py --port /dev/ttyUSB0 set timeout 600

# Read a parameter
python3 watchdog_cli.py --port /dev/ttyUSB0 get timeout

# Send keepalive
python3 watchdog_cli.py --port /dev/ttyUSB0 ping

# Interactive shell (with tab completion)
python3 watchdog_cli.py --port /dev/ttyUSB0 -i
```

---

## Systemd Service Setup

Install the keepalive daemon as a systemd service so it starts automatically at boot.

1. **Copy files to the system:**
   ```bash
   sudo mkdir -p /opt/watchdog-relay
   sudo cp server/watchdog_daemon.py /opt/watchdog-relay/
   sudo pip3 install pyserial
   ```

2. **Create a config file:**
   ```bash
   sudo cp server/watchdog-relay.conf.example /etc/watchdog-relay.conf
   # Edit /etc/watchdog-relay.conf as needed
   ```

3. **Install and enable the service:**
   ```bash
   sudo cp server/watchdog-relay.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable watchdog-relay
   sudo systemctl start watchdog-relay
   ```

4. **Check status:**
   ```bash
   sudo systemctl status watchdog-relay
   sudo journalctl -u watchdog-relay -f
   ```

> **Note:** If the serial port is `/dev/ttyACM0` (USB CDC mode) rather than
> `/dev/ttyUSB0`, update the `ExecStart` line in the service file or the
> `port` setting in the config file.
