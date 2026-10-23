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

EXPECT_ONE = re.compile(
    r"target 1: x=\s*-0\.782 m\s+y=\s*1\.713 m\s+range=\s*1\.883 m\s+"
    r"azimuth=\s*-24\.5 deg\s+speed=\s*-0\.16 m/s"
)


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

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    port = server.getsockname()[1]

    qemu = subprocess.Popen(
        [args.qemu, "-machine", "esp32s3", "-display", "none",
         "-drive", f"file={flash},if=mtd,format=raw",
         "-serial", "stdio",
         "-serial", f"tcp:127.0.0.1:{port}"],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )

    lines = queue.Queue()

    def reader():
        for raw in qemu.stdout:
            line = raw.decode(errors="replace").rstrip()
            print(f"[esp32s3] {line}", flush=True)
            lines.put(line)

    threading.Thread(target=reader, daemon=True).start()

    server.settimeout(30)
    radar, _ = server.accept()

    # Phase 1: one target must be decoded exactly; phase 2: it must disappear.
    phases = [(FRAME_ONE_TARGET, EXPECT_ONE, "one target"),
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
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
