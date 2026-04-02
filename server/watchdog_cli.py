#!/usr/bin/env python3
"""
Watchdog Relay CLI

Command-line tool for interacting with the ESP32 watchdog relay over serial.
Supports both one-shot and interactive modes.

Usage:
    watchdog_cli.py --port /dev/ttyUSB0 status
    watchdog_cli.py --port /dev/ttyUSB0 set timeout 600
    watchdog_cli.py --port /dev/ttyUSB0 get timeout
    watchdog_cli.py --port /dev/ttyUSB0 ping
    watchdog_cli.py --port /dev/ttyUSB0 --interactive
"""

import argparse
import json
import sys
import time

try:
    import readline  # noqa: F401 – imported for side-effect (tab completion)
    _READLINE_AVAILABLE = True
except ImportError:
    _READLINE_AVAILABLE = False

try:
    import serial
except ImportError:
    print("Error: pyserial not installed.  Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)

DEFAULT_PORT    = "/dev/ttyUSB0"
DEFAULT_BAUD    = 115200
DEFAULT_TIMEOUT = 2   # seconds – short to avoid blocking after last response line

COMMANDS = [
    "ping", "status", "get", "set",
    "reset_timer", "reset_attempts",
    "relay", "reboot", "help",
]

PARAMETERS = [
    "timeout", "off_period", "max_attempts",
    "relay_pin", "led_pin", "button_pin",
    "serial_mode", "wifi_ssid", "wifi_password", "wifi_hidden",
    "oled_sda_pin", "oled_scl_pin", "oled_i2c_addr", "oled_enabled",
]


class WatchdogCLI:
    def __init__(self, port, baud, timeout=DEFAULT_TIMEOUT):
        self.port    = port
        self.baud    = baud
        self.timeout = timeout
        self.ser     = None

    # ── Connection ──────────────────────────────────────────────────────────

    def connect(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
            return True
        except serial.SerialException as exc:
            print(f"Error: cannot open {self.port}: {exc}", file=sys.stderr)
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    # ── Send / receive ──────────────────────────────────────────────────────

    def send_command(self, command):
        """
        Send *command* (without trailing newline) and collect the response.

        Returns a list of response lines, or None on error.
        Terminal conditions (stop collecting):
          - A line starting with "OK" or "ERROR"
          - A line starting with "{" (JSON – STATUS response)
          - Read timeout with no data arriving
        """
        if not self.ser or not self.ser.is_open:
            if not self.connect():
                return None
        try:
            self.ser.reset_input_buffer()
            self.ser.write((command.strip() + "\n").encode("utf-8"))
            self.ser.flush()

            lines = []
            start = time.monotonic()
            while time.monotonic() - start < max(self.timeout * 2, 10):
                raw = self.ser.readline()
                if not raw:
                    # Timeout – stop if we already have data
                    if lines:
                        break
                    continue
                line = raw.decode("utf-8", errors="ignore").rstrip("\r\n")
                lines.append(line)
                # Detect end of response
                if (
                    line.startswith("OK")
                    or line.startswith("ERROR")
                    or line.startswith("{")
                ):
                    break
            return lines if lines else None
        except serial.SerialException as exc:
            print(f"Serial error: {exc}", file=sys.stderr)
            self.disconnect()
            return None

    # ── Formatting ──────────────────────────────────────────────────────────

    @staticmethod
    def _format_ms(ms):
        if ms < 1000:
            return f"{ms} ms"
        if ms < 60_000:
            return f"{ms / 1000:.1f} s"
        if ms < 3_600_000:
            m = ms // 60_000
            s = (ms % 60_000) // 1000
            return f"{m}m {s}s"
        h = ms // 3_600_000
        m = (ms % 3_600_000) // 60_000
        return f"{h}h {m}m"

    @staticmethod
    def _print_status(data):
        print("Device Status:")
        print(f"  Uptime:              {WatchdogCLI._format_ms(data.get('uptime_ms', 0))}")
        print(f"  Watchdog remaining:  {WatchdogCLI._format_ms(data.get('watchdog_remaining_ms', 0))}")
        print(f"  Restart attempts:    "
              f"{data.get('restart_attempts', 0)} / {data.get('max_restart_attempts', 0)}")
        print(f"  Relay state:         {'ON' if data.get('relay_state') else 'OFF'}")
        print(f"  Timeout:             {WatchdogCLI._format_ms(data.get('timeout_ms', 0))}")
        print(f"  Off period:          {WatchdogCLI._format_ms(data.get('off_period_ms', 0))}")

    def format_and_print(self, command_upper, lines):
        if not lines:
            print("No response received.", file=sys.stderr)
            return
        for line in lines:
            if line.startswith("{"):
                try:
                    data = json.loads(line)
                    if command_upper == "STATUS":
                        self._print_status(data)
                    else:
                        print(json.dumps(data, indent=2))
                except json.JSONDecodeError:
                    print(line)
            else:
                print(line)

    # ── Command execution ───────────────────────────────────────────────────

    def run_command(self, command):
        lines = self.send_command(command)
        first_token = command.strip().upper().split()[0] if command.strip() else ""
        self.format_and_print(first_token, lines)
        # Return True if the response indicates success
        if lines and lines[-1].startswith("OK"):
            return True
        return False

    # ── Interactive mode ────────────────────────────────────────────────────

    def run_interactive(self):
        print(f"Watchdog Relay CLI  --  connected to {self.port}")
        print("Type 'help' for available commands, 'exit' or Ctrl-D to quit.\n")

        if _READLINE_AVAILABLE:
            def completer(text, state):
                buf   = readline.get_line_buffer()
                words = buf.split()
                # First word: complete command names
                if len(words) <= 1:
                    opts = [c for c in COMMANDS if c.startswith(text.lower())]
                # Second word after get/set: complete parameter names
                elif len(words) == 2 and words[0].lower() in ("get", "set"):
                    opts = [p for p in PARAMETERS if p.startswith(text.lower())]
                else:
                    opts = []
                return opts[state] if state < len(opts) else None

            readline.set_completer(completer)
            readline.parse_and_bind("tab: complete")

        while True:
            try:
                line = input("watchdog> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break
            if not line:
                continue
            if line.lower() in ("exit", "quit"):
                break
            self.run_command(line)


# ── CLI entry point ─────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Watchdog Relay CLI",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--port",        default=DEFAULT_PORT,
                        help="Serial port device")
    parser.add_argument("--baud",        type=int, default=DEFAULT_BAUD,
                        help="Serial baud rate")
    parser.add_argument("--timeout",     type=float, default=DEFAULT_TIMEOUT,
                        help="Read timeout in seconds")
    parser.add_argument("--interactive", "-i", action="store_true",
                        help="Start interactive command shell")
    parser.add_argument("command",       nargs="*",
                        help="Command to execute (e.g. 'status', 'get timeout')")
    args = parser.parse_args()

    cli = WatchdogCLI(args.port, args.baud, args.timeout)
    if not cli.connect():
        sys.exit(1)

    try:
        if args.interactive or not args.command:
            cli.run_interactive()
        else:
            success = cli.run_command(" ".join(args.command))
            sys.exit(0 if success else 1)
    finally:
        cli.disconnect()


if __name__ == "__main__":
    main()
