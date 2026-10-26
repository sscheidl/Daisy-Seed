#!/usr/bin/env python3
"""Memory placement of an ARM ELF (Acid Seq, R15): load regions, BIN identity, static DTCM
stack reserve and, optionally, symbols that must live in SDRAM.

Usage: check-elf-placement.py <image.elf> [--dtcm-reserve BYTES] [--sdram SYMBOL:MIN_BYTES ...]
Symbols match by substring of the mangled name (e.g. delay_memory). Uses the ELF readers
of check-tape-elf.py; this does not replace the hardware resource PASS.
"""
import argparse
import importlib.util
import pathlib
import struct

spec = importlib.util.spec_from_file_location('tape_elf', pathlib.Path(__file__).with_name('check-tape-elf.py'))
tape_elf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tape_elf)
REGIONS, region_of, symbols = tape_elf.REGIONS, tape_elf.region_of, tape_elf.symbols


def check(path, dtcm_reserve=8192, sdram=()):
    data = path.read_bytes()
    assert data[:7] == b'\x7fELF\x01\x01\x01', 'Expected little-endian ELF32'
    kind, machine, version, entry, phoff, _, flags, ehsize, phsize, phnum = struct.unpack_from('<HHIIIIIHHH', data, 16)
    assert kind == 2 and machine == 40 and version == 1 and ehsize == 52, 'Expected ARM executable'
    assert flags & 0x400, 'Expected hard-float ARM ABI'
    image = path.with_suffix('.bin').read_bytes()
    assert entry == struct.unpack_from('<I', image, 4)[0], 'ELF entry differs from reset vector'
    used = {name: 0 for name in REGIONS}
    for index in range(phnum):
        typ, off, vaddr, paddr, filesz, memsz, _, _ = struct.unpack_from('<IIIIIIII', data, phoff + index * phsize)
        if typ != 1 or memsz == 0:
            continue
        name = region_of(vaddr, memsz)
        assert name, f'Load segment outside permitted regions: {vaddr:#x}/{memsz}'
        used[name] = max(used[name], vaddr + memsz - REGIONS[name][0])
        if filesz:
            assert 0x24000000 <= paddr and paddr + filesz <= 0x24000000 + len(image), 'File data outside BOOT_SRAM image'
            start = paddr - 0x24000000
            assert image[start:start + filesz] == data[off:off + filesz], 'BIN differs from ELF'
    reserve = REGIONS['DTCM'][1] - used['DTCM']
    assert reserve >= dtcm_reserve, f'Static DTCM stack reserve {reserve} < {dtcm_reserve} bytes'
    lines = []
    table = list(symbols(data))
    for requirement in sdram:
        name, minimum = requirement.rsplit(':', 1)
        found = [(n, v, s) for n, v, s in table if name in n and s > 0]
        assert found, f'Symbol {name} not found'
        for n, v, s in found:
            assert s >= int(minimum), f'{n}: {s} bytes, expected >= {minimum}'
            assert region_of(v, s) == 'SDRAM', f'{n} at {v:#x} is not in SDRAM'
            lines.append(f'PASS {n}: {s} bytes at {v:#010x} (SDRAM)')
    for name, (_, capacity) in REGIONS.items():
        lines.append(f'PASS {name}: {used[name]} / {capacity} bytes; static reserve {capacity - used[name]}')
    lines.append(f'PASS ARM hard-float ELF, reset entry, load regions, byte-identical BOOT_SRAM image, '
                 f'DTCM reserve {reserve} >= {dtcm_reserve}')
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('elf', type=pathlib.Path)
    parser.add_argument('--dtcm-reserve', type=int, default=8192)
    parser.add_argument('--sdram', action='append', default=[])
    args = parser.parse_args()
    for line in check(args.elf, args.dtcm_reserve, args.sdram):
        print(line)


if __name__ == '__main__':
    main()
