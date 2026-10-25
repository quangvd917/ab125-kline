#!/usr/bin/env python3
"""Grab a screenshot from the CYD dashboard over USB serial and save it as PNG.

Usage:
    ~/.platformio/penv/bin/python tools/screenshot.py [-p PORT] [-o out.png]
"""

import argparse
import glob
import struct
import sys
import time
import zlib

import serial


def write_png(path, w, h, rgb):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port")
    ap.add_argument("-o", "--output", default="screenshot.png")
    ap.add_argument("--wait", type=float, default=4, help="seconds to let the dashboard run after a reboot")
    args = ap.parse_args()

    port = args.port or (sorted(glob.glob("/dev/cu.usbserial*")) or [None])[0]
    if not port:
        sys.exit("No serial port found")

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 1
    s.dtr = s.rts = False  # do not reboot the board
    s.open()
    # Opening the port may reboot the board: let it boot and draw a few frames.
    time.sleep(args.wait)
    s.reset_input_buffer()

    deadline = time.time() + 15
    header = b""
    next_ask = 0
    while time.time() < deadline and not header:
        if time.time() >= next_ask:
            s.write(b"s")
            next_ask = time.time() + 1.5
        line = s.readline()
        if line.startswith(b"SCREENSHOT"):
            header = line
    if not header:
        sys.exit("No screenshot header received")
    _, w, h = header.split()
    w, h = int(w), int(h)

    need = w * h * 2
    data = b""
    deadline = time.time() + 60
    while len(data) < need and time.time() < deadline:
        data += s.read(need - len(data))
    s.close()
    if len(data) < need:
        sys.exit(f"Short read: {len(data)}/{need} bytes")

    # TFT_eSPI readRect returns RGB565 with bytes swapped relative to host order.
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        v = (data[2 * i] << 8) | data[2 * i + 1]
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        rgb[3 * i] = r << 3 | r >> 2
        rgb[3 * i + 1] = g << 2 | g >> 4
        rgb[3 * i + 2] = b << 3 | b >> 2
    write_png(args.output, w, h, bytes(rgb))
    print(f"Saved {args.output} ({w}x{h})")


if __name__ == "__main__":
    main()
