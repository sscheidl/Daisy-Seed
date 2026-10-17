#!/usr/bin/env python3
"""Sanity-check the BOOT_SRAM image: vector table and embedded version (does not replace a hardware test).

Usage: check-firmware.py [image.bin]   (default: build/ToyPiano-v<version>.bin)
"""
import pathlib
import re
import struct
import sys

root = pathlib.Path(__file__).resolve().parent.parent
match = re.search(r'TOY_PIANO_VERSION\s+"([^"]+)"', (root / 'src' / 'version.h').read_text())
assert match, "Could not read the version from src/version.h"
version = match.group(1)

path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build' / f'ToyPiano-v{version}.bin'
assert path.name == f'ToyPiano-v{version}.bin', f"File name {path.name} does not match version {version}"
image = path.read_bytes()
assert 8 <= len(image) <= 480 * 1024, "Image exceeds official BOOT_SRAM linker region"
stack, reset = struct.unpack_from('<II', image)
assert 0x20000000 < stack <= 0x20020000, f"Unexpected DTCM stack: {stack:#x}"
assert reset & 1, "Reset vector must select Thumb mode"
assert 0x24000000 <= (reset & ~1) < 0x24000000 + len(image), "Reset vector outside SRAM image"
embedded = re.findall(rb'TOYPIANO-FW-VERSION=([0-9.]+)\x00', image)
assert embedded == [version.encode()], f"Embedded version {embedded} does not match src/version.h ({version})"
print(f"PASS: v{version}, {len(image)} bytes, stack={stack:#010x}, reset={reset:#010x}, BOOT_SRAM vectors")
