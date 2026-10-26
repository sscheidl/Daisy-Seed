#!/usr/bin/env python3
"""Check a Tape FX ARM ELF (tape-fx, toy-piano-tape): load regions, BIN identity,
DTCM reserve and the placement of the Tape FX delay memory.

Usage: check-tape-elf.py <image.elf>
The 16 KiB TapeVibe delay lines must be in SDRAM, never in the default .bss (DTCM).
"""
import pathlib
import struct
import sys

STORAGE_BYTES = 16384
REGIONS = {
    'SRAM': (0x24000000, 480 * 1024), 'DTCM': (0x20000000, 128 * 1024),
    'D2 DMA': (0x30000000, 32 * 1024), 'D2': (0x30008000, 256 * 1024),
    'SDRAM': (0xc0000000, 64 * 1024 * 1024), 'Backup': (0x38800000, 4 * 1024),
}


def region_of(address, size):
    names = [n for n, (base, cap) in REGIONS.items() if base <= address and address + size <= base + cap]
    return names[0] if len(names) == 1 else None


def symbols(data):
    """(name, value, size) of every symbol in .symtab."""
    shoff, = struct.unpack_from('<I', data, 32)
    shentsize, shnum = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize) for i in range(shnum)]
    for sh in sections:
        if sh[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[sh[6]]
        for k in range(sh[5] // 16):
            name, value, size, _, _, _ = struct.unpack_from('<IIIBBH', data, sh[4] + k * 16)
            start = strtab[4] + name
            end = data.index(b'\0', start)
            yield data[start:end].decode(errors='replace'), value, size


def main():
    path = pathlib.Path(sys.argv[1])
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
    assert REGIONS['DTCM'][1] - used['DTCM'] >= 8192, 'Less than 8 KiB static DTCM stack reserve'
    # The delay memory: a TapeFx Storage object (tape_memory) or inside the Toy Piano host.
    found = []
    for name, value, size in symbols(data):
        if name in ('_ZL11tape_memory', 'tape_memory') or 'fx_storage' in name:
            found.append((name, value, size))
    assert found, 'Tape FX storage symbol not found'
    for name, value, size in found:
        assert size >= STORAGE_BYTES, f'{name}: {size} bytes, expected >= {STORAGE_BYTES}'
        assert region_of(value, size) == 'SDRAM', f'{name} at {value:#x} is not in SDRAM'
        print(f'PASS {name}: {size} bytes at {value:#010x} (SDRAM, not DTCM)')
    for name, (_, capacity) in REGIONS.items():
        print(f'PASS {name}: {used[name]} / {capacity} bytes; static reserve {capacity - used[name]}')
    print('PASS ARM hard-float ELF, reset entry, load regions and byte-identical BOOT_SRAM image')


if __name__ == '__main__':
    main()
