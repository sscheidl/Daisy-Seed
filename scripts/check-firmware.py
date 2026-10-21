#!/usr/bin/env python3
"""Sanity-check a BOOT_SRAM image: vector table and embedded project version.

Usage: check-firmware.py [image.bin]
Default: instruments/toy-piano/build/ToyPiano-v<version>.bin.
Also accepts effects/reverb/build/AmbientReverb-v<version>.bin,
PhysicalModeling-v<version>.bin and instruments/looper/build/Looper-v<version>.bin.
This does not replace a hardware test.
"""
import pathlib
import re
import struct
import sys

root = pathlib.Path(__file__).resolve().parent.parent
project = root / 'instruments' / 'toy-piano'
match = re.search(r'TOY_PIANO_VERSION\s+"([^"]+)"', (project / 'src' / 'version.h').read_text())
assert match, "Could not read the version from src/version.h"
version = match.group(1)

path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else project / 'build' / f'ToyPiano-v{version}.bin'
if path.name.startswith('NebulaeOnDaisy-v'):
    granular = root / 'instruments' / 'granular'
    match = re.search(r'NEBULAE_VERSION\s+"([^"]+)"', (granular / 'src' / 'version.h').read_text())
    assert match, "Could not read the Nebulae version"
    version = match.group(1)
    expected_name = f'NebulaeOnDaisy-v{version}.bin'
    marker = rb'NEBULAE-FW-VERSION=([0-9.]+)\x00'
elif path.name.startswith('PhysicalModeling-v'):
    physical = root / 'instruments' / 'physical-modeling'
    match = re.search(r'PHYSICAL_MODELING_VERSION\s+"([^"]+)"', (physical / 'src' / 'version.h').read_text())
    assert match, "Could not read the Physical Modeling version"
    version = match.group(1)
    expected_name = f'PhysicalModeling-v{version}.bin'
    marker = rb'PHYSICAL-MODELING-VERSION=([0-9.]+)\x00'
elif path.name.startswith('AmbientReverb-v'):
    reverb = root / 'effects' / 'reverb'
    match = re.search(r'AMBIENT_REVERB_VERSION\s+"([^"]+)"', (reverb / 'src' / 'version.h').read_text())
    assert match, "Could not read the Ambient Reverb version"
    version = match.group(1)
    expected_name = f'AmbientReverb-v{version}.bin'
    marker = rb'AMBIENT-REVERB-VERSION=([0-9.]+)\x00'
elif path.name.startswith('Looper-v'):
    looper = root / 'instruments' / 'looper'
    match = re.search(r'LOOPER_VERSION\s+"([^"]+)"', (looper / 'src' / 'version.h').read_text())
    assert match, "Could not read the Looper version"
    version = match.group(1)
    expected_name = f'Looper-v{version}.bin'
    marker = rb'LOOPER-VERSION=([0-9.]+)\x00'
else:
    expected_name = f'ToyPiano-v{version}.bin'
    marker = rb'TOYPIANO-FW-VERSION=([0-9.]+)\x00'
assert path.name == expected_name, f"File name {path.name} does not match version {version}"
image = path.read_bytes()
assert 8 <= len(image) <= 480 * 1024, "Image exceeds official BOOT_SRAM linker region"
stack, reset = struct.unpack_from('<II', image)
assert 0x20000000 < stack <= 0x20020000, f"Unexpected DTCM stack: {stack:#x}"
assert reset & 1, "Reset vector must select Thumb mode"
assert 0x24000000 <= (reset & ~1) < 0x24000000 + len(image), "Reset vector outside SRAM image"
embedded = re.findall(marker, image)
assert embedded == [version.encode()], f"Embedded version {embedded} does not match src/version.h ({version})"
print(f"PASS: v{version}, {len(image)} bytes, stack={stack:#010x}, reset={reset:#010x}, BOOT_SRAM vectors")
