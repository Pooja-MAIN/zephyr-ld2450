# SPDX-License-Identifier: Apache-2.0
"""Run the ESP32-S3 sample in Espressif QEMU with a simulated LD2450 on UART1."""

import argparse
import queue
import re
import socket
import subprocess
import sys
import threading
import time

FLASH_SIZE = 8 * 1024 * 1024

# Target 1: X = -782 mm, Y = 1713 mm, speed = -16 cm/s, resolution = 320 mm.
FRAME_ONE_TARGET = bytes(
    [0xAA, 0xFF, 0x03, 0x00,
     0x0E, 0x03, 0xB1, 0x86, 0x10, 0x00, 0x40, 0x01]
    + [0x00] * 16
    + [0x55, 0xCC]
)
FRAME_EMPTY = bytes([0xAA, 0xFF, 0x03, 0x00] + [0x00] * 24 + [0x55, 0xCC])

EXPECT_ONE = re.compile(r"target 1: x=-782 mm\s+y=1713 mm\s+speed=-160 mm/s")
EXPECT_NEAREST = re.compile(r"nearest: 1883 mm")


def make_flash(app_bin, address, out):
    image = bytearray(b"\xff" * FLASH_SIZE)
    data = open(app_bin, "rb").read()
    image[address:address + len(data)] = data
    open(out, "wb").write(image)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--bin", required=True)
    parser.add_argument("--address", required=True)
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()

    flash = "qemu-flash.bin"
    make_flash(args.bin, int(args.address, 0), flash)

    def listener():
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", 0))
        s.listen(1)
        s.settimeout(30)
        return s

    console_server = listener()
    radar_server = listener()

    # UART0 is the console, UART1 is wired to the simulated LD2450.
    qemu = subprocess.Popen(
        [args.qemu, "-machine", "esp32s3", "-display", "none",
         "-drive", f"file={flash},if=mtd,format=raw",
         "-serial", f"tcp:127.0.0.1:{console_server.getsockname()[1]}",
         "-serial", f"tcp:127.0.0.1:{radar_server.getsockname()[1]}"],
        stdin=subprocess.DEVNULL,
    )

    console, _ = console_server.accept()
    radar, _ = radar_server.accept()

    lines = queue.Queue()

    def reader():
        pending = b""
        while True:
            try:
                chunk = console.recv(4096)
            except OSError:
                return
            if not chunk:
                return
            pending += chunk
            while b"\n" in pending:
                raw, pending = pending.split(b"\n", 1)
                line = raw.decode(errors="replace").rstrip()
                print(f"[esp32s3] {line}", flush=True)
                lines.put(line)

    threading.Thread(target=reader, daemon=True).start()

    # Phase 1: one target must be decoded exactly; phase 2: it must disappear.
    phases = [(FRAME_ONE_TARGET, EXPECT_NEAREST, "nearest distance"),
              (FRAME_ONE_TARGET, EXPECT_ONE, "one target"),
              (FRAME_EMPTY, re.compile(r"^targets: 0$"), "no targets")]
    deadline = time.monotonic() + args.timeout
    ok = True

    for frame, expect, name in phases:
        seen = False
        next_send = 0.0
        while not seen and time.monotonic() < deadline and qemu.poll() is None:
            now = time.monotonic()
            if now >= next_send:
                radar.sendall(frame)
                next_send = now + 0.1
            try:
                line = lines.get(timeout=0.05)
            except queue.Empty:
                continue
            seen = bool(expect.search(line))
        print(f"{'PASS' if seen else 'FAIL'}: {name}", flush=True)
        ok = ok and seen
        if not seen:
            break

    radar.close()
    qemu.kill()
    qemu.wait()
    console.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
