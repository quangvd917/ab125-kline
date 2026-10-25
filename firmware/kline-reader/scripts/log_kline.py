#!/usr/bin/env python3
"""Read the ESP32 K-Line reader over USB serial and save every line to a .txt log.

Usage:
    python scripts/log_kline.py                 # auto-detect port, log until Ctrl+C
    python scripts/log_kline.py -d 120          # stop after 120 seconds
    python scripts/log_kline.py -p /dev/cu.usbserial-140 --reset

Each line is prefixed with the computer's time, e.g.:
    2026-10-15 20:31:05.123  rpm=1622 km/h=0 tps=0 ect=63C iat=29C batt=14.5V | raw ...
"""

import argparse
import glob
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing: run with ~/.platformio/penv/bin/python or `pip install pyserial`")

BAUD = 115200
LOG_DIR = Path(__file__).resolve().parent.parent / "logs"


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))  # ESP32-C3 native USB first
    ports += sorted(glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.SLAB*") + glob.glob("/dev/cu.wchusbserial*"))
    if not ports:
        sys.exit("No ESP32 serial port found. Is the board plugged in?")
    return ports[0]


def open_port(port, reset):
    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD
    s.timeout = 0.2
    # Keep DTR/RTS released so opening the port does not reboot the ESP32.
    s.dtr = False
    s.rts = False
    s.open()
    if reset:
        s.rts = True
        time.sleep(0.1)
        s.rts = False
    return s


def main():
    ap = argparse.ArgumentParser(description="Log ESP32 K-Line output to a text file")
    ap.add_argument("-p", "--port", help="serial port (default: auto-detect)")
    ap.add_argument("-d", "--duration", type=float, help="stop after N seconds (default: until Ctrl+C)")
    ap.add_argument("-o", "--output", help="log file path (default: logs/kline_<date>_<time>.txt)")
    ap.add_argument("--reset", action="store_true", help="reboot the ESP32 first (shows INIT and table scan)")
    ap.add_argument("-q", "--quiet", action="store_true", help="do not echo lines to the terminal")
    args = ap.parse_args()

    port = args.port or find_port()
    if args.output:
        out_path = Path(args.output)
    else:
        LOG_DIR.mkdir(exist_ok=True)
        out_path = LOG_DIR / datetime.now().strftime("kline_%Y%m%d_%H%M%S.txt")

    s = open_port(port, args.reset)
    print(f"Port: {port}  ->  {out_path}  (Ctrl+C to stop)")

    start = time.time()
    lines = data_lines = lost = 0
    pending = b""
    with open(out_path, "w", encoding="utf-8") as f:
        try:
            while args.duration is None or time.time() - start < args.duration:
                pending += s.read(1024)
                while b"\n" in pending:
                    raw, pending = pending.split(b"\n", 1)
                    text = raw.decode("utf-8", errors="replace").rstrip("\r")
                    if not text:
                        continue
                    stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                    f.write(f"{stamp}  {text}\n")
                    lines += 1
                    if text.startswith("rpm="):
                        data_lines += 1
                    elif text.startswith("Lost connection"):
                        lost += 1
                    if not args.quiet:
                        print(f"{stamp}  {text}")
                f.flush()
        except KeyboardInterrupt:
            pass
        finally:
            s.close()

    elapsed = time.time() - start
    rate = data_lines / elapsed if elapsed > 0 else 0
    print(f"\nSaved {lines} lines to {out_path}")
    print(f"{data_lines} data readings in {elapsed:.0f}s ({rate:.1f}/s), {lost} lost connections")


if __name__ == "__main__":
    main()
