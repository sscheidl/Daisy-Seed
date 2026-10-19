#!/usr/bin/env python3
"""Validate Nebulae's ARM image and derive format limits from actual linker symbols."""
import json
import pathlib
import struct
import sys
import subprocess

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
assert data[:7] == b'\x7fELF\x01\x01\x01', 'Expected little-endian ELF32'
kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
assert kind == 2 and machine == 40 and version == 1 and flags & 0x400
image = path.with_suffix('.bin').read_bytes()
assert entry == struct.unpack_from('<I', image, 4)[0]
regions = {
    'SRAM': (0x24000000, 480*1024), 'DTCM': (0x20000000, 128*1024),
    'D2_DMA': (0x30000000, 32*1024), 'D2': (0x30008000, 256*1024),
    'SDRAM': (0xc0000000, 64*1024*1024), 'Backup': (0x38800000, 4096),
}
used = {k: 0 for k in regions}
for i in range(phnum):
    typ, offset, address, physical, file_bytes, memory_bytes, perm, align = struct.unpack_from('<IIIIIIII', data, phoff+i*phsize)
    if typ != 1 or not memory_bytes:
        continue
    matches = [k for k, (base, size) in regions.items() if base <= address and address+memory_bytes <= base+size]
    assert len(matches) == 1, f'Invalid RAM segment {address:#x}+{memory_bytes}'
    k = matches[0]
    used[k] = max(used[k], address+memory_bytes-regions[k][0])
    assert file_bytes <= memory_bytes and offset+file_bytes <= len(data)
    if file_bytes:
        start = physical-0x24000000
        assert 0 <= start and start+file_bytes <= len(image), 'Data outside BOOT_SRAM image'
        assert data[offset:offset+file_bytes] == image[start:start+file_bytes]
sections = [struct.unpack_from('<IIIIIIIIII', data, shoff+i*shsize) for i in range(shnum)]
symbols = {}
for section in sections:
    if section[1] != 2:
        continue
    strings = sections[section[6]]
    table = data[strings[4]:strings[4]+strings[5]]
    for off in range(section[4], section[4]+section[5], section[9]):
        name, value, size, info, other, index = struct.unpack_from('<IIIBBH', data, off)
        label = table[name:table.find(b'\0', name)].decode()
        symbols[label] = (value, size)
start = symbols['__neb_file_start'][0]
end = symbols['__neb_file_end'][0]
assert start % 32 == end % 32 == 0
assert 0xc0000000+16*1024*1024 < start < end == 0xc3800000
assert end <= 0xc4000000-8*1024*1024
assert regions['DTCM'][1]-used['DTCM'] >= 16*1024
assert symbols['__neb_heap_end'][0]-symbols['__neb_heap_start'][0] == 65536
assert 0x30008000 <= symbols['__neb_heap_start'][0] < symbols['__neb_heap_end'][0] <= 0x30048000
assert '_sbrk' in symbols
sector = symbols['sector_buffer']
assert sector[0] % 32 == 0 and sector[1] == 512
assert 0x24000000+len(image) <= sector[0] < sector[0]+512 <= 0x24078000
for fragment in ('live_banks', 'engine_storage'):
    matches = [(n, v, size) for n, (v, size) in symbols.items() if fragment in n]
    assert len(matches) == 1
    _, address, size = matches[0]
    assert 0xc0000000 <= address and address+size <= start
limits = [{'rate': sr, 'channels': ch, 'max_frames': (end-start)//(ch*2),
           'max_seconds': ((end-start)//(ch*2))/sr} for sr in (44100, 48000) for ch in (1, 2)]
report = {'bin_bytes': len(image), 'file_start': hex(start), 'file_end': hex(end),
          'file_capacity_bytes': end-start, 'used_region_bytes': used, 'limits': limits}
print(json.dumps(report, indent=2))
if '--host-test' in sys.argv[2:]:
    binary = path.parent / 'host' / 'test_nebulae'
    if not binary.is_file():
        binary = binary.with_suffix('.exe')
    assert binary.is_file(), 'Build host tests before --host-test'
    subprocess.run([str(binary.resolve()), '--wav-capacity', str(end-start)], check=True)
    print(f'PASS production WAV boundary tests with actual ELF capacity {end-start} bytes')
print('PASS ARM hard-float, byte-identical BIN/ELF, isolated AXI SD DMA, 16 KiB stack floor and 8 MiB physical SDRAM reserve')
