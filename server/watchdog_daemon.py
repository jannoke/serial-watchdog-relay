#!/usr/bin/env python3
"""
Watchdog Relay Keepalive Daemon

Sends periodic PING commands to the ESP32 watchdog relay to prevent it from
triggering.  Supports optional alert scripts on communication failures.

Usage:
    watchdog_daemon.py --port /dev/ttyUSB0 --interval 60
    watchdog_daemon.py --config /etc/watchdog-relay.conf
"""

import argparse
import configparser
import logging
import os
import signal
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    print("Error: pyserial not installed.  Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)

DEFAULT_PORT     = "/dev/ttyUSB0"
DEFAULT_BAUD     = 115200
DEFAULT_INTERVAL = 60   # seconds between PINGs
DEFAULT_TIMEOUT  = 5    # serial read timeout


def setup_logging(log_file=None, verbose=False):
    level    = logging.DEBUG if verbose else logging.INFO
    handlers = [logging.StreamHandler()]
    if log_file:
        handlers.append(logging.FileHandler(log_file))
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
        handlers=handlers,
    )


def load_config_file(path):
    cfg = configparser.ConfigParser()
    cfg.read(path)
    return cfg


def run_script(script_path, description):
    """Execute an alert script, ignoring errors."""
    if not script_path:
        return
    if not os.path.isfile(script_path):
        logging.warning("Alert script not found: %s", script_path)
        return
    try:
        logging.info("Running %s script: %s", description, script_path)
        subprocess.run([script_path], timeout=30, check=False)
    except Exception as exc:
        logging.error("Error running %s script: %s", description, exc)


class WatchdogDaemon:
    def __init__(self, port, baud, interval,
                 warning_script=None, failure_script=None, max_failures=3):
        self.port            = port
        self.baud            = baud
        self.interval        = interval
        self.warning_script  = warning_script
        self.failure_script  = failure_script
        self.max_failures    = max_failures
        self.ser             = None
        self.running         = False
        self.consecutive_failures = 0

    # ── Serial helpers ──────────────────────────────────────────────────────

    def connect(self):
        try:
            self.ser = serial.Serial(
                self.port,
                self.baud,
                timeout=DEFAULT_TIMEOUT,
                write_timeout=DEFAULT_TIMEOUT,
            )
            logging.info("Connected to %s at %d baud", self.port, self.baud)
            return True
        except serial.SerialException as exc:
            logging.error("Failed to connect to %s: %s", self.port, exc)
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser = None

    def send_ping(self):
        """Send a PING and check for OK response.  Returns True on success."""
        try:
            self.ser.reset_input_buffer()
            self.ser.write(b"PING\n")
            self.ser.flush()
            response = (
                self.ser.readline()
                .decode("utf-8", errors="ignore")
                .strip()
            )
            if response == "OK":
                logging.debug("PING OK")
                self.consecutive_failures = 0
                return True
            logging.warning("Unexpected PING response: %r", response)
            self.consecutive_failures += 1
            return False
        except serial.SerialException as exc:
            logging.error("Serial error during PING: %s", exc)
            self.consecutive_failures += 1
            self.disconnect()
            return False

    # ── Main loop ───────────────────────────────────────────────────────────

    def run(self):
        self.running = True
        signal.signal(signal.SIGTERM, self._handle_signal)
        signal.signal(signal.SIGINT, self._handle_signal)

        logging.info(
            "Watchdog daemon started (port=%s, interval=%ds)",
            self.port,
            self.interval,
        )

        while self.running:
            # Reconnect if needed
            if self.ser is None or not self.ser.is_open:
                if not self.connect():
                    logging.info("Retrying connection in 10 seconds...")
                    self._interruptible_sleep(10)
                    continue

            # Send keepalive
            success = self.send_ping()
            if success:
                logging.info("Keepalive sent successfully")
            else:
                logging.warning(
                    "PING failed (%d/%d consecutive failures)",
                    self.consecutive_failures,
                    self.max_failures,
                )
                if self.consecutive_failures >= self.max_failures:
                    logging.error(
                        "Max consecutive failures reached – running failure script"
                    )
                    run_script(self.failure_script, "failure")
                    self.consecutive_failures = 0
                else:
                    run_script(self.warning_script, "warning")

            self._interruptible_sleep(self.interval)

        logging.info("Watchdog daemon stopped")
        self.disconnect()

    def _interruptible_sleep(self, seconds):
        """Sleep in small increments so we react quickly to stop signals."""
        deadline = time.monotonic() + seconds
        while self.running and time.monotonic() < deadline:
            time.sleep(0.1)

    def _handle_signal(self, signum, frame):
        logging.info("Received signal %d, stopping...", signum)
        self.running = False


# ── CLI entry point ─────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Watchdog Relay Keepalive Daemon",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--port",     default=DEFAULT_PORT,
                        help="Serial port device")
    parser.add_argument("--baud",     type=int, default=DEFAULT_BAUD,
                        help="Serial baud rate")
    parser.add_argument("--interval", type=int, default=DEFAULT_INTERVAL,
                        help="PING interval in seconds")
    parser.add_argument("--log-file", metavar="FILE",
                        help="Write log output to FILE")
    parser.add_argument("--verbose",  "-v", action="store_true",
                        help="Enable debug logging")
    parser.add_argument("--warning-script", metavar="SCRIPT",
                        help="Script to execute on a PING failure warning")
    parser.add_argument("--failure-script", metavar="SCRIPT",
                        help="Script to execute after repeated PING failures")
    parser.add_argument("--max-failures", type=int, default=3, metavar="N",
                        help="Consecutive failures before running failure script")
    parser.add_argument("--config", metavar="FILE",
                        help="INI config file (overrides other options where set)")
    args = parser.parse_args()

    # Start with CLI values, then overlay config file
    port            = args.port
    baud            = args.baud
    interval        = args.interval
    log_file        = args.log_file
    verbose         = args.verbose
    warning_script  = args.warning_script
    failure_script  = args.failure_script
    max_failures    = args.max_failures

    if args.config:
        cfg = load_config_file(args.config)
        if cfg.has_section("daemon"):
            d = cfg["daemon"]
            port         = d.get("port",     fallback=port)
            baud         = d.getint("baud",  fallback=baud)
            interval     = d.getint("interval", fallback=interval)
            log_file     = d.get("log_file", fallback=log_file)
        if cfg.has_section("alerts"):
            a = cfg["alerts"]
            warning_script = a.get("warning_script", fallback=warning_script)
            failure_script = a.get("failure_script", fallback=failure_script)

    setup_logging(log_file, verbose)

    daemon = WatchdogDaemon(
        port=port,
        baud=baud,
        interval=interval,
        warning_script=warning_script,
        failure_script=failure_script,
        max_failures=max_failures,
    )
    daemon.run()


if __name__ == "__main__":
    main()
