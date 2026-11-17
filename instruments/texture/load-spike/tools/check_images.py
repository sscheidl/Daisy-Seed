#!/usr/bin/env python3
"""Placement check of the spike images (README.md): vectors, identity and where the buffers live.

usage: check_images.py <build dir>   (expects a/, a-itcm/, b/ and b-itcm/; checks the newest .bin/.elf of each)
The repository's check-elf-placement.py does not know D3 SRAM or a BOOT_QSPI image, hence this script.
It reuses the ELF symbol reader of scripts/check-tape-elf.py.
"""
import importlib.util
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location('tape_elf', ROOT / 'scripts' / 'check-tape-elf.py')
tape_elf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tape_elf)

REGIONS = {'ITCM': (0x00000040, (64 << 10) - 0x40), 'DTCM': (0x20000000, 128 << 10), 'AXI': (0x24000000, 512 << 10), 'D2': (0x30008000, 256 << 10),
           'D3': (0x38000000, 64 << 10), 'SDRAM': (0xC0000000, 64 << 20), 'QSPI': (0x90040000, 7936 << 10)}
BANK = 256 << 10
# symbol substring -> (region, size) per variant
EXPECT = {
    'A': {'bank_sdram': ('SDRAM', 3 * BANK), 'reverb_buffer': ('D3', 65536), 'usb_midi_storage': ('SDRAM', None),
          '6engineE': ('DTCM', None)},
    'AI': {'bank_sdram': ('SDRAM', 3 * BANK), 'reverb_buffer': ('D3', 65536), 'usb_midi_storage': ('SDRAM', None),
           '6engineE': ('DTCM', None), '_ZN12_GLOBAL__N_113AudioCallback': ('ITCM', None),
           'GranularProcessorILNS_10ResolutionE2ELi192000ELi1EE15ProcessInternal': ('ITCM', None),
           '_ZN5beads5Synth6Render': ('ITCM', None)},
    'B': {'bank_d1': ('AXI', 2 * BANK), 'bank_d2': ('D2', BANK), 'reverb_buffer': ('D3', 65536),
          'usb_midi_storage': ('SDRAM', None), '6engineE': ('DTCM', None)},
    'BI': {'bank_d1': ('AXI', 2 * BANK), 'bank_d2': ('D2', BANK), 'reverb_buffer': ('D3', 65536),
           'usb_midi_storage': ('SDRAM', None), '6engineE': ('DTCM', None),
           '_ZN12_GLOBAL__N_113AudioCallback': ('ITCM', None),
           'GranularProcessorILNS_10ResolutionE2ELi192000ELi1EE15ProcessInternal': ('ITCM', None),
           '_ZN5beads5Synth6Render': ('ITCM', None), '_ZN5beads8lut_sineE': ('DTCM', 5124),
           '_ZN6stmlib20lut_pitch_ratio_highE': ('DTCM', None), '_ZN6stmlib19lut_pitch_ratio_lowE': ('DTCM', None),
           '_ZN5beads20wav_integrated_wavesE': ('SDRAM', 99840)},
}


def region(address, size):
    for name, (base, cap) in REGIONS.items():
        if base <= address and address + size <= base + cap:
            return name
    return None


def check(folder, variant):
    newest = lambda pattern: sorted(folder.glob(pattern), key=lambda f: f.stat().st_mtime)[-1:]
    bins, elfs = newest('*.bin'), newest('*.elf')
    assert len(bins) == 1 and len(elfs) == 1, f'{folder}: expected a .bin and an .elf'
    image = bins[0].read_bytes()
    sp, reset = struct.unpack_from('<II', image, 0)
    assert sp == 0x20020000, f'{variant}: stack {sp:#x}'
    base = 0x90040000 if variant.startswith('B') else 0x24000000
    assert base <= (reset & ~1) < base + len(image) and reset & 1, f'{variant}: reset vector {reset:#x}'
    marker = b'TEXTURE-SPIKE-VERSION='
    at = image.find(marker)
    assert at >= 0, f'{variant}: no firmware identity'
    ident = image[at:image.index(b'\0', at)].decode()
    assert ident.endswith('-' + variant), f'{variant}: identity {ident}'
    syms = list(tape_elf.symbols(elfs[0].read_bytes()))
    for key, (want, size) in EXPECT[variant].items():
        found = [(n, v, s) for n, v, s in syms if key in n and s]
        assert len(found) == 1, f'{variant}: symbol {key}: {found}'
        name, value, got = found[0]
        assert region(value, got) == want, f'{variant}: {key} at {value:#x} is not in {want}'
        assert size is None or got == size, f'{variant}: {key} has {got} bytes, expected {size}'
    sym = lambda name: next(v for n, v, s in syms if n == name)
    if variant.endswith('I'):  # the copy of the hot code sits right after the vector table
        start, end, load = sym('_sitcm_text'), sym('_eitcm_text'), sym('_siitcm_text')
        assert start == 0x40 and start < end <= 0x10000, f'{variant}: ITCM range {start:#x}..{end:#x}'
        assert load == base + 0x298, f'{variant}: ITCM load {load:#x}'  # 0x298 bytes of vector table before it
        assert load + (end - start) <= base + len(image), f'{variant}: ITCM copy outside the image'
        assert any(n == 'texture_spike_copy_itcm' or n.endswith('texture_spike_copy_itcm') for n, v, s in syms)
    if variant == 'BI':  # the hot tables: DTCM copy loaded from QSPI right after the hot code
        start, end, load = sym('_sdtcm_rodata'), sym('_edtcm_rodata'), sym('_sidtcm_rodata')
        assert start == 0x20000000 and start < end <= sym('_sdata'), f'BI: DTCM tables {start:#x}..{end:#x}'
        assert load >= sym('_siitcm_text') + (sym('_eitcm_text') - sym('_sitcm_text')), f'BI: table load {load:#x}'
        assert load + (end - start) <= base + len(image), 'BI: table copy outside the image'
        start, end, load = sym('_ssdram_rodata'), sym('_esdram_rodata'), sym('_sisdram_rodata')
        sdram_bss = (sym('_ssdram_bss'), sym('_esdram_bss'))
        assert 0xC0000000 <= start < end and (end <= sdram_bss[0] or start >= sdram_bss[1]), f'BI: SDRAM table {start:#x}..{end:#x}'
        assert base <= load and load + (end - start) <= base + len(image), 'BI: SDRAM table copy outside the image'
    ebss = next(v for n, v, s in syms if n == '_ebss')
    assert ebss <= 0x20020000 - (16 << 10), f'{variant}: DTCM stack reserve below 16 KiB'
    print(f'PASS {variant}: {bins[0].name}, {len(image)} bytes, reset {reset:#010x}, {ident}, '
          f'.bss end {ebss:#x} (stack reserve {(0x20020000 - ebss) >> 10} KiB)')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build = pathlib.Path(sys.argv[1])
    check(build / 'a', 'A')
    check(build / 'a-itcm', 'AI')
    check(build / 'b', 'B')
    check(build / 'b-itcm', 'BI')
