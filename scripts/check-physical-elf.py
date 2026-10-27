#!/usr/bin/env python3
"""Check the actual ARM ELF, load segments, SRAM image and static RAM budgets."""
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
assert data[:7] == b'\x7fELF\x01\x01\x01', 'Expected little-endian ELF32'
header = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = header
assert kind == 2 and machine == 40 and version == 1 and ehsize == 52, 'Expected ARM executable'
assert flags & 0x400, 'Expected hard-float ARM ABI'
image = path.with_suffix('.bin').read_bytes()
assert entry == struct.unpack_from('<I', image, 4)[0], 'ELF entry differs from reset vector'
regions = {
    'SRAM': (0x24000000, 480 * 1024), 'DTCM': (0x20000000, 128 * 1024),
    'D2 DMA': (0x30000000, 32 * 1024), 'D2': (0x30008000, 256 * 1024),
    'SDRAM': (0xc0000000, 64 * 1024 * 1024),
    'Backup': (0x38800000, 4 * 1024),
}
used = {name: 0 for name in regions}
for index in range(phnum):
    typ, off, vaddr, paddr, filesz, memsz, perm, align = struct.unpack_from('<IIIIIIII', data, phoff + index * phsize)
    if typ != 1 or memsz == 0:
        continue
    assert filesz <= memsz and off + filesz <= len(data), 'Invalid ELF load segment'
    matches = [name for name, (base, size) in regions.items() if base <= vaddr and vaddr + memsz <= base + size]
    assert len(matches) == 1, f'Load segment outside permitted regions: {vaddr:#x}/{memsz}'
    name = matches[0]
    used[name] = max(used[name], vaddr + memsz - regions[name][0])
    if filesz:
        assert 0x24000000 <= paddr and paddr + filesz <= 0x24000000 + len(image), 'File data outside BOOT_SRAM image'
        start = paddr - 0x24000000
        assert image[start:start + filesz] == data[off:off + filesz], 'BIN differs from ELF'
assert regions['DTCM'][1] - used['DTCM'] >= 8192, 'Less than 8 KiB static DTCM stack reserve'
# NOLOAD DSP data may reside in external SDRAM or in the explicitly reserved
# AXI section. Program headers alone cannot establish heap/object separation.
sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shsize) for i in range(shnum)]
assert shsize == 40 and shstr < shnum, 'Invalid ELF section table'
strings_header = sections[shstr]
names = data[strings_header[4]:strings_header[4] + strings_header[5]]
def name_at(table, offset):
    return table[offset:table.index(b'\0', offset)].decode('ascii')
named = {name_at(names, s[0]): s for s in sections}
symbols = {}
for section in sections:
    if section[1] != 2:
        continue
    strings_header = sections[section[6]]
    strings = data[strings_header[4]:strings_header[4] + strings_header[5]]
    assert section[9] == 16, 'Invalid ELF symbol size'
    for offset in range(section[4], section[4] + section[5], 16):
        name, value, size, info, other, index = struct.unpack_from('<IIIBBH', data, offset)
        if name:
            symbols[name_at(strings, name)] = (value, size, index)

def object_named(fragment):
    matches = [s for name, s in symbols.items() if fragment in name and s[1]]
    assert len(matches) == 1, f'Missing/ambiguous DSP object: {fragment}'
    return matches[0]

arena, sine = object_named('synth_storage'), object_named('lut_sine')
assert arena[1] >= 65536 and sine[1] == 5121 * 4, 'DSP/LUT reservation missing'
axi = named.get('.axi_bss')
if axi and axi[5]:
    assert axi[1] == 8, 'AXI DSP section must be NOLOAD/NOBITS'
    start, stop = axi[3], axi[3] + axi[5]
    assert 0x24000000 + len(image) <= start, 'AXI DSP overlaps BOOT_SRAM image'
    assert stop + 8192 <= 0x24078000, 'Insufficient AXI DSP/code growth reserve'
    for address, size, index in (arena, sine):
        assert start <= address and address + size <= stop, 'DSP object outside AXI reservation'
    heap = symbols.get('end')
    assert heap and 0x30008000 <= heap[0] < 0x30048000, 'Invalid D2 heap start'
    assert not start <= heap[0] < stop, 'DSP overlaps heap'
    print(f'PASS AXI DSP: {axi[5]} bytes; {0x24078000 - stop} bytes growth reserve; D2 heap separate')
else:
    for address, size, index in (arena, sine):
        assert 0xc0000000 <= address and address + size <= 0xc4000000, 'DSP object outside SDRAM'
    assert used['SDRAM'] >= 65536, 'DSP/reverb reservation missing'
for name, (_, capacity) in regions.items():
    print(f'PASS {name}: {used[name]} / {capacity} bytes; static reserve {capacity - used[name]}')
print('PASS ARM hard-float ELF, reset entry, load regions and byte-identical BOOT_SRAM image')
