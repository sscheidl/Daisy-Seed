#!/usr/bin/env python3
"""Stage the Stage 1 board package for Toy Piano on Cumulonimbus (local, git-ignored).

    python scripts/cumulonimbus_stage1_package.py [--out build/cumulonimbus-stage1] [--rollback <bin>]
        [--logs .work/logs] [--toolchain <arm-none-eabi bin dir>]

Expects the images to be built (P0 = regular Toy Piano, F48/F32 standalone, D48/D32 profiling,
P48/P32 playable). Writes images, SHA256SUMS, manifests, versions, link-map and flash-layout
summaries, build/test logs, the probes, RUN_SHEET.md and results_template.csv, then verifies the
staged package from a clean re-read. Nothing is flashed; the .elf/.map stay in the build folders.
"""
import argparse
import csv
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import sys
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts'))
import component_manifest as manifest  # noqa: E402

TP = ROOT / 'instruments' / 'toy-piano'
FX = ROOT / 'effects' / 'cumulonimbus'
IMAGES = [
    # label, project, image path relative to ROOT (version filled in), purpose
    ('P0', 'toy-piano', 'instruments/toy-piano/build/ToyPiano-v{v}.bin',
     'Regular Toy Piano: dry sound reference and rollback (no callback meter), B0'),
    ('F48', 'cumulonimbus-fx', 'effects/cumulonimbus/build/fx48/CumulonimbusFx48-v{v}.bin',
     'Standalone grains + reverb, 48 kHz kernel, true stereo, scenarios, B1'),
    ('F32', 'cumulonimbus-fx-32', 'effects/cumulonimbus/build/fx32/CumulonimbusFx32-v{v}.bin',
     'Standalone, 32 kHz kernel behind 48<->32 kHz, B1'),
    ('D48', 'toy-piano-cumulonimbus-profile',
     'instruments/toy-piano/build/cumulonimbus48-profile/ToyPianoCumulonimbus48Profile-v{v}.bin',
     'Toy Piano + grains + reverb, 48 kHz kernel, profiling scenarios, B0/B2/B3'),
    ('D32', 'toy-piano-cumulonimbus-32-profile',
     'instruments/toy-piano/build/cumulonimbus32-profile/ToyPianoCumulonimbus32Profile-v{v}.bin',
     'Same, 32 kHz kernel, B0/B2/B3'),
    ('P48', 'toy-piano-cumulonimbus', 'instruments/toy-piano/build/cumulonimbus48/ToyPianoCumulonimbus48-v{v}.bin',
     'Playable Toy Piano on Cumulonimbus, 48 kHz kernel, B4'),
    ('P32', 'toy-piano-cumulonimbus-32',
     'instruments/toy-piano/build/cumulonimbus32/ToyPianoCumulonimbus32-v{v}.bin',
     'Playable, 32 kHz kernel, B4'),
]
REGIONS = [  # name, start, size (ld/toy_piano_axi.lds and the official libDaisy BOOT_SRAM script)
    ('ITCMRAM', 0x00000000, 64 * 1024), ('DTCMRAM', 0x20000000, 128 * 1024),
    ('SRAM (AXI)', 0x24000000, 512 * 1024), ('RAM_D2_DMA', 0x30000000, 32 * 1024),
    ('RAM_D2', 0x30008000, 256 * 1024), ('RAM_D3', 0x38000000, 64 * 1024),
    ('BACKUP_SRAM', 0x38800000, 4 * 1024), ('QSPIFLASH', 0x90000000, 8 * 1024 * 1024),
    ('SDRAM', 0xC0000000, 64 * 1024 * 1024), ('FLASH', 0x08000000, 128 * 1024),
]


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def tool(toolchain, name):
    exe = name + ('.exe' if sys.platform == 'win32' else '')
    if toolchain:
        candidate = pathlib.Path(toolchain) / exe
        if candidate.exists():
            return str(candidate)
    return shutil.which(name) or name


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def sections(objdump, elf):
    out = subprocess.check_output([objdump, '-h', str(elf)], text=True)
    rows = []
    lines = out.splitlines()
    for i, line in enumerate(lines):
        parts = line.split()
        if len(parts) >= 7 and parts[0].isdigit():
            name, size, vma, lma = parts[1], int(parts[2], 16), int(parts[3], 16), int(parts[4], 16)
            flags = lines[i + 1].strip() if i + 1 < len(lines) else ''
            rows.append((name, size, vma, lma, flags))
    return rows


def region(address):
    for name, start, size in REGIONS:
        if start <= address < start + size:
            return name
    return '?'


def link_summary(rows):
    used = {}
    for name, size, vma, _, flags in rows:
        if size and 'ALLOC' in flags:
            used.setdefault(region(vma), []).append((name, size))
    text = ['| Region | Used bytes | Region size | Use | Sections |', '| --- | --- | --- | --- | --- |']
    for name, start, size in REGIONS:
        if name not in used:
            continue
        total = sum(s for _, s in used[name])
        secs = ', '.join(f'{n} {s}' for n, s in used[name])
        text.append(f'| {name} | {total} | {size} | {100.0 * total / size:.1f} % | {secs} |')
    return '\n'.join(text)


def flash_layout(image, rows):
    data = pathlib.Path(image).read_bytes()
    stack, reset = struct.unpack_from('<II', data)
    lines = [f'* Image {pathlib.Path(image).name}: {len(data)} bytes, written by the Daisy bootloader to QSPI at '
             f'0x90040000 and copied to AXI SRAM 0x24000000 for execution (BOOT_SRAM).',
             f'* Vector table: initial stack 0x{stack:08x} (DTCM), reset 0x{reset:08x} (Thumb, AXI SRAM).',
             '', '| Section | Size | VMA | LMA | Region |', '| --- | --- | --- | --- | --- |']
    for name, size, vma, lma, flags in rows:
        if size and 'ALLOC' in flags:
            lines.append(f'| {name} | {size} | 0x{vma:08x} | 0x{lma:08x} | {region(vma)} |')
    return '\n'.join(lines)


def results_template(path):
    rows = []
    # B0 timing comes from the D48/D32 dry route; P0 has no callback meter and is the sound reference only.
    rows.append(['B0', 'P0', 'sound reference', '', 'play --voices 8', 'same logical settings as D48',
                 'P0 VR1 transpose/VR2 attack/VR3 decay; Piano page VR1 attack/VR2 decay/VR3 transpose'])
    for image in ('D48', 'D32'):
        for stim in range(5):
            rows.append(['B0', image, '1 (dry)', '16', str(stim), '0/1/8/16 MIDI, 16 MIDI + 16 keys',
                         'timing; compare by ear with P0'])
    for image in ('F48', 'F32'):
        for scenario in ('4 (write only)', '5 (render only)', '2 (grains only)', '0 (full)'):
            for cap in (8, 16, 24, 32):
                rows.append(['B1', image, scenario, str(cap), '3 (noise)', '', 'also L/R impulses: routing'])
    for image in ('D48', 'D32'):
        for cap in (8, 16, 24, 32):
            rows.append(['B2', image, '2 (grains only)', str(cap), '4', 'pot snapshot both pages', ''])
        rows.append(['B2', image, '3 (reverb only)', '16', '4', 'pot snapshot both pages', ''])
        for cap in (8, 16, 24, 32):
            rows.append(['B3', image, '0 (full)', str(cap), '4', 'long overlap / short high birth / spread / pitch',
                         'freeze, bypass, page flips, play --cc-traffic'])
        rows.append(['B3', image, '0 (full)', '8', '4', 'deliberate over-cap (dense)', 'account rejected starts'])
    for image in ('P48 or P32',):
        rows.append(['B4', image, 'playable', 'boot 16', 'play --voices 16 --cc-traffic', 'heaviest viable B3 setting',
                     '30 min soak, listening'])
    with open(path, 'w', newline='') as handle:
        w = csv.writer(handle)
        w.writerow(['row', 'image', 'scenario', 'cap', 'stimulus', 'settings', 'notes', 'date', 'measured_s',
                    'peak_load_pct', 'mean_load_pct', 'p99_load_pct', 'missed_periods', 'overruns', 'blocks',
                    'window_ms', 'grains_peak', 'rejected', 'voices_peak', 'midi_overruns', 'control_drops',
                    'stack_free', 'accounting_pct', 'verdict',
                    'listening (MEASURED)', 'status'])
        for r in rows:
            w.writerow(r + [''] * 18 + ['NOT MEASURED'])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=pathlib.Path, default=ROOT / 'build' / 'cumulonimbus-stage1')
    store = pathlib.Path('firmware') / 'toy-piano' / '0.2+6135417' / 'ToyPiano-v0.2.bin'
    candidates = [ROOT / store, ROOT.parent.parent / 'Daisy-Seed' / store]  # this checkout, or the main one beside a worktree
    ap.add_argument('--rollback', type=pathlib.Path, default=next((c for c in candidates if c.exists()), candidates[0]),
                    help='known-good rollback image (default: released Toy Piano v0.2 from the local firmware store)')
    ap.add_argument('--logs', type=pathlib.Path, default=ROOT / '.work' / 'logs')
    ap.add_argument('--toolchain', type=pathlib.Path)
    args = ap.parse_args()
    out = args.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    for sub in ('images', 'manifests', 'linkmap', 'logs', 'tools'):
        (out / sub).mkdir(parents=True)
    objdump, size_tool, gcc = (tool(args.toolchain, n) for n in ('arm-none-eabi-objdump', 'arm-none-eabi-size',
                                                                 'arm-none-eabi-gcc'))
    registry = manifest.load_registry()
    table, elf_paths, checks = [], [], []
    for label, project, pattern, purpose in IMAGES:
        product = registry['products'][project]
        source = product.get('version') or registry['components'][product['version_component']]['version']
        version = manifest.read_version(source)
        image = ROOT / pattern.format(v=version)
        if not image.exists():
            raise SystemExit(f'Missing image {image} ({label}); build it first')
        target = out / 'images' / image.name
        shutil.copy2(image, target)
        description = manifest.describe_product(project, target)
        (out / 'manifests' / f'{label}-{project}.json').write_text(json.dumps(description, indent=2) + '\n')
        elf = image.with_suffix('.elf')
        rows = sections(objdump, elf)
        (out / 'linkmap' / f'{label}-{image.stem}.md').write_text(
            f'# {label} {image.name}\n\n## Memory regions\n\n{link_summary(rows)}\n\n## Flash layout\n\n'
            f'{flash_layout(target, rows)}\n')
        check = subprocess.run([sys.executable, str(ROOT / 'scripts' / 'check-firmware.py'), str(target)],
                               capture_output=True, text=True)
        checks.append(f'{label} {image.name}: {check.stdout.strip()} {check.stderr.strip()}'.strip())
        if check.returncode:
            raise SystemExit(f'check-firmware failed for {image.name}: {check.stderr}')
        elf_paths.append(f'{label}: {elf} | {image.with_suffix(".map")}')
        table.append((label, image.name, project, version, len(target.read_bytes()), sha256(target), purpose))
    if args.rollback.exists():
        target = out / 'images' / ('ROLLBACK-' + args.rollback.name)
        shutil.copy2(args.rollback, target)
        table.append(('P0R', target.name, 'toy-piano (released v0.2, firmware store)', '0.2', target.stat().st_size,
                      sha256(target), 'Known-good rollback (GitHub Release v0.2), differs from P0'))
    else:
        table.append(('P0R', '-', '-', '-', 0, '-', f'UNAVAILABLE: {args.rollback} not found'))
    (out / 'logs' / 'check-firmware.txt').write_text('\n'.join(checks) + '\n')
    if args.logs.exists():
        for log in sorted(args.logs.glob('*.log')):
            shutil.copy2(log, out / 'logs' / log.name)
    for src in (FX / 'tools' / 'cumulonimbus_probe.py', FX / 'tools' / 'test_cumulonimbus_probe.py',
                ROOT / 'modules' / 'mod-on-daisy' / 'tools' / 'mod_probe.py'):
        shutil.copy2(src, out / 'tools' / src.name)
    (out / 'ELF_MAP_LOCATIONS.txt').write_text('Local only, not in the package:\n' + '\n'.join(elf_paths) + '\n')
    results_template(out / 'results_template.csv')
    compiler = subprocess.check_output([gcc, '--version'], text=True).splitlines()[0]
    commit = git('rev-parse', 'HEAD')
    dirty = git('status', '--porcelain', '--untracked-files=normal')
    versions = [
        f'Package built {datetime.now(timezone.utc).isoformat(timespec="seconds")}',
        f'Git revision {commit} ({"DIRTY working tree" if dirty else "clean working tree"}), branch {git("rev-parse", "--abbrev-ref", "HEAD")}',
        f'libDaisy {git("rev-parse", "HEAD:libDaisy")}, DaisySP {git("rev-parse", "HEAD:DaisySP")} (pinned gitlinks)',
        f'Compiler: {compiler}; -O3 -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard, APP_TYPE=BOOT_SRAM, '
        'linker script instruments/toy-piano/ld/toy_piano_axi.lds (Cumulonimbus images), official BOOT_SRAM (P0)',
        'Board profile: Seed3 Desktop Kit, Daisy bootloader v6.4 (extdfu, 2000 ms), 480 MHz, codec 48 kHz, 48-frame blocks',
        'Components: ' + ', '.join(f'{k} {manifest.read_version(v["version"])}' for k, v in registry['components'].items()
                                   if k.startswith('cumulonimbus') or k in ('torus-reverb', 'toy-piano', 'mod-on-daisy',
                                                                            'fx-contract', 'wet-host', 'callback-diagnostics',
                                                                            'bootloader-entry')),
    ]
    golden = args.logs / 'test-toy-piano-regular.log'
    if golden.exists():
        hashes = [line.strip() for line in golden.read_text(errors='replace').splitlines()
                  if 'golden performance hash' in line]
        versions.append('Dry reference (regular Toy Piano golden performance, this host compiler): ' +
                        ('; '.join(hashes) or 'not printed') + '. The pinned Linux v0.2 hash 52580874FA24361F is '
                        'checked by Linux CI; Windows clang/libm never matches it (known).')
    (out / 'VERSIONS.txt').write_text('\n'.join(versions) + '\n')
    header = ['# RUN SHEET — Toy Piano on Cumulonimbus, Stage 1 package', '',
              'Generated by scripts/cumulonimbus_stage1_package.py. Hardware timing, listening and flashing: '
              '**NOT MEASURED**.', '', *[f'* {v}' for v in versions], '',
              '| Label | Image | Project | Version | Bytes | SHA-256 | Purpose |',
              '| --- | --- | --- | --- | --- | --- | --- |']
    header += [f'| {a} | `{b}` | {c} | {d} | {e} | `{f}` | {g} |' for a, b, c, d, e, f, g in table]
    header += ['', 'Recommended flash order: **P0 → F48 / F32 → D48 / D32 → selected P48 / P32**. '
               'Memory regions and flash layout per image: `linkmap/`. Manifests: `manifests/`. '
               'Firmware checks: `logs/check-firmware.txt`. ELF/MAP: `ELF_MAP_LOCATIONS.txt`.', '', '---', '']
    sheet = (FX / 'docs' / 'STAGE1_RUN_SHEET.md').read_text(encoding='utf-8')
    (out / 'RUN_SHEET.md').write_text('\n'.join(header) + sheet, encoding='utf-8')
    files = sorted(p for p in out.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
    (out / 'SHA256SUMS').write_text(''.join(f'{sha256(p)}  {p.relative_to(out).as_posix()}\n' for p in files))
    # Clean re-read: every listed file exists with the recorded hash; nothing unlisted.
    listed = {}
    for line in (out / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        listed[name] = digest
    actual = {p.relative_to(out).as_posix() for p in out.rglob('*') if p.is_file() and p.name != 'SHA256SUMS'}
    assert actual == set(listed), 'package contents changed during staging'
    for name, digest in listed.items():
        assert sha256(out / name) == digest, f'hash mismatch {name}'
    probe = subprocess.run([sys.executable, '-I', str(out / 'tools' / 'cumulonimbus_probe.py'), 'selftest'],
                           capture_output=True, text=True)
    assert probe.returncode == 0, probe.stderr
    print(f'VERIFIED: {len(listed)} files, probe {probe.stdout.strip()}')
    print(out)


if __name__ == '__main__':
    main()
