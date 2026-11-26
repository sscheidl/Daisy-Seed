"""Negative tests against an actual built ARM image; no hardware access."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

def main():
    source = Path(sys.argv[1]).resolve()
    checker = Path(__file__).resolve().parents[3] / 'scripts/check-physical-elf.py'
    data = source.read_bytes()
    header = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    shoff, shsize, shnum, shstr = header[5], header[10], header[11], header[12]
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff+i*shsize) for i in range(shnum)]
    def string(table, offset):
        return table[offset:table.index(b'\0', offset)].decode('ascii')
    names_header = sections[shstr]
    names = data[names_header[4]:names_header[4]+names_header[5]]
    symbols = {}
    axi_index = None
    for index, section in enumerate(sections):
        if string(names, section[0]) == '.axi_bss' and section[5]:
            axi_index = index
        if section[1] != 2:
            continue
        strings_header = sections[section[6]]
        strings = data[strings_header[4]:strings_header[4]+strings_header[5]]
        for offset in range(section[4], section[4]+section[5], 16):
            name = struct.unpack_from('<I', data, offset)[0]
            if name:
                symbols[string(strings, name)] = offset
    arena = next(offset for name, offset in symbols.items() if 'synth_storage' in name)
    sine = next(offset for name, offset in symbols.items() if 'lut_sine' in name)
    cases = [('arena outside DSP region', arena+4, 0x20001000),
             ('LUT reservation truncated', sine+8, 4)]
    if axi_index is not None:
        cases += [('AXI overlaps image', shoff+axi_index*shsize+12, 0x24000000),
                  ('heap moved into AXI', symbols['end']+4, sections[axi_index][3])]
    with tempfile.TemporaryDirectory(prefix='pm-elf-') as folder:
        target = Path(folder) / source.name
        target.with_suffix('.bin').write_bytes(source.with_suffix('.bin').read_bytes())
        for name, offset, replacement in cases:
            changed = bytearray(data)
            struct.pack_into('<I', changed, offset, replacement)
            target.write_bytes(changed)
            result = subprocess.run([sys.executable, str(checker), str(target)], capture_output=True)
            assert result.returncode != 0, name
            assert b'AssertionError' in result.stderr, result.stderr
            print('PASS rejection:', name)

if __name__ == "__main__":
    main()
