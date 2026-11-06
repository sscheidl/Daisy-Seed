#!/usr/bin/env python3
"""Builds the local Windows hardware-test package of Nebulae on Daisy MOD.

    python scripts/nebulae_mod_test_package.py --panel-exe <DaisyDiagnostic.exe> --smoke

Needs a built `make PROJECT=granular-mod` and the JUCE editor (docs in
editors/mod-on-daisy/README.md). Output: build/nebulae-mod-test/ with the firmware, both
Windows apps, the single entry point, the test sheet, MANIFEST.json and SHA256SUMS.

--smoke runs the packaged apps and the entry point in simulation only. Nothing here
flashes a board, opens a real device or publishes anything.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
GRANULAR = ROOT / 'instruments' / 'granular'
EDITOR_NAME = 'Mod on Daisy Editor.exe'
PANEL_NAME = 'DaisyDiagnostic.exe'
DEFAULT_EDITOR = ROOT / 'build' / 'editor' / 'ModOnDaisyEditor_artefacts' / 'Release' / EDITOR_NAME
HARDWARE_STATUS = 'NOT MEASURED'

# What this package was validated with; results are in instruments/granular/docs/VALIDATION.md.
COMMANDS = [
    'make PROJECT=granular test HOST_CXX=clang++ HOST_CC=clang',
    'make PROJECT=granular test-sanitize HOST_CXX=clang++ HOST_CC=clang',
    'make PROJECT=granular -j8',
    'make PROJECT=granular-mod -j8',
    'python scripts/check-firmware.py instruments/granular/build/mod/NebulaeMod-v<version>.bin',
    'python scripts/check-granular-elf.py instruments/granular/build/mod/NebulaeMod-v<version>.elf --host-test',
    'python scripts/check-sd-symbols.py <instruments/granular sd-check flags> <NebulaeMod ELF>',
    'python scripts/component_manifest.py --project granular-mod --built',
    'python -m pytest diagnostics/tests/python -q',
    'node editors/mod-on-daisy/web/tests/e2e.cjs <mod_sim>',
    'build/editor/.../Mod on Daisy Editor.exe --selftest <dir>',
    'python scripts/nebulae_mod_test_package.py --panel-exe <exe> --smoke',
]


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def version_of(path, name):
    match = re.search(name + r'\s*=?\s*"([^"]+)"', path.read_text(encoding='utf-8'))
    if not match:
        raise SystemExit(f'No {name} in {path}')
    return match.group(1)


def run(*args, **kwargs):
    print('+', ' '.join(str(a) for a in args), flush=True)
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def launcher(out, *args):
    """The packaged entry point in simulation. Returns (exit code, output)."""
    command = ['powershell.exe', '-NoProfile', '-STA', '-ExecutionPolicy', 'Bypass', '-File',
               str(out / 'Nebulae-Mod-Test.ps1'), '-Simulate', '-Yes', *args]
    print('+ entry point', ' '.join(args), flush=True)
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    return result.returncode, (result.stdout + result.stderr).strip()


def expect(out, action, active):
    code, text = launcher(out, '-Action', action)
    if code != 0 or active not in text:
        raise SystemExit(f'Entry point {action}: exit {code}, expected "{active}", got: {text}')
    return f'{action}: {text.splitlines()[-1]}'


def running_clients():
    """Independent of the entry point: what Windows itself lists."""
    tasks = subprocess.run(['tasklist', '/fo', 'csv', '/nh'], capture_output=True, text=True, check=True).stdout
    return [name for name in (EDITOR_NAME, PANEL_NAME) if f'"{name}"' in tasks]


def smoke(out):
    """Simulation only: MOD_ON_DAISY_NO_AUTOCONNECT and --sim keep a connected kit untouched."""
    lines = []
    env = dict(os.environ, MOD_ON_DAISY_NO_AUTOCONNECT='1')
    editor, panel = out / 'windows' / EDITOR_NAME, out / 'windows' / PANEL_NAME
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        run(editor, '--selftest', scratch / 'editor', env=env, cwd=scratch, capture_output=True, timeout=300)
        lines.append('Mod on Daisy Editor --selftest: exit 0')
        for mode in ('--sim', '--sim-nebulae', '--sim-looper'):
            shot = scratch / f'panel{mode}.png'
            run(panel, mode, '--selftest', '4', '--screenshot', shot, env=env, cwd=scratch, timeout=120)
            if not shot.is_file():
                raise SystemExit(f'Panel {mode}: no window was shown')
            lines.append(f'DaisyDiagnostic {mode} --selftest 4: exit 0, window shown')
    try:
        lines.append(expect(out, 'Modulation', 'Active: Modulation'))
        lines.append(expect(out, 'SdCard', 'Active: SD card'))
        lines.append(expect(out, 'Modulation', 'Active: Modulation'))
        # A client that is still starting must count as open: close it straight away.
        lines.append(expect(out, 'SdCard', 'Active: SD card'))
    finally:
        launcher(out, '-Action', 'Close')
    code, text = launcher(out, '-Action', 'Status')
    if code != 0 or 'Active: none' not in text or running_clients():
        raise SystemExit(f'Entry point left a client open: {text} {running_clients()}')
    lines.append('Close during start-up: Active: none, tasklist agrees')
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--panel-exe', type=pathlib.Path, required=True)
    parser.add_argument('--editor-exe', type=pathlib.Path, default=DEFAULT_EDITOR)
    parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'build' / 'nebulae-mod-test')
    parser.add_argument('--smoke', action='store_true', help='run the packaged apps in simulation')
    parser.add_argument('--allow-dirty', action='store_true', help='package a working tree with tracked changes')
    args = parser.parse_args()

    dirty = bool(git('status', '--porcelain', '--untracked-files=no'))
    if dirty and not args.allow_dirty:
        raise SystemExit('Tracked changes: commit first so the package names one exact revision.')
    version = version_of(GRANULAR / 'src' / 'version.h', 'NEBULAE_VERSION')
    build = GRANULAR / 'build' / 'mod'
    image = build / f'NebulaeMod-v{version}.bin'
    for required in (image, args.panel_exe, args.editor_exe):
        if not required.is_file():
            raise SystemExit(f'Missing: {required}')

    run(sys.executable, ROOT / 'scripts' / 'check-firmware.py', image)
    run(sys.executable, ROOT / 'scripts' / 'component_manifest.py', '--project', 'granular-mod', '--built')

    out = args.out
    if running_clients():  # before anything is deleted: a running app locks its exe
        raise SystemExit(f'Close these first: {", ".join(running_clients())}')
    if out.exists():
        for name in ('firmware', 'windows'):
            shutil.rmtree(out / name, ignore_errors=True)
    (out / 'firmware').mkdir(parents=True, exist_ok=True)
    (out / 'windows').mkdir(exist_ok=True)
    shutil.copy2(image, out / 'firmware')
    shutil.copy2(build / 'COMPONENTS.json', out / 'firmware')
    shutil.copy2(args.editor_exe, out / 'windows' / EDITOR_NAME)
    shutil.copy2(args.panel_exe, out / 'windows' / PANEL_NAME)
    shutil.copy2(ROOT / 'editors' / 'mod-on-daisy' / 'COPYING.AGPL-3.0.txt', out / 'windows')
    tools = GRANULAR / 'tools'
    shutil.copy2(tools / 'Start-Nebulae-Mod-Test.cmd', out)
    shutil.copy2(tools / 'Nebulae-Mod-Test.ps1', out)
    shutil.copy2(GRANULAR / 'docs' / 'MOD_HARDWARE_TEST.md', out / 'TEST_SHEET.md')

    smoke_lines = smoke(out) if args.smoke else []
    components = json.loads((build / 'COMPONENTS.json').read_text(encoding='utf-8'))
    manifest = {
        'package': 'nebulae-mod-hardware-test',
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'),
        'revision': git('rev-parse', 'HEAD'),
        'branch': git('rev-parse', '--abbrev-ref', 'HEAD'),
        'tracked_changes': dirty,
        'hardware_status': HARDWARE_STATUS,
        'firmware': {'file': f'firmware/{image.name}', 'bytes': image.stat().st_size, 'sha256': sha256(image)},
        'composition': components,
        'windows': {
            'editor': {'file': f'windows/{EDITOR_NAME}', 'sha256': sha256(args.editor_exe),
                       'version': next(c['version'] for c in components['compatible_control_panels']
                                       if c['id'] == 'mod-editor-juce')},
            'panel': {'file': f'windows/{PANEL_NAME}', 'sha256': sha256(args.panel_exe),
                      'version': version_of(ROOT / 'diagnostics' / 'windows' / 'daisy_diag' / '__init__.py', '__version__'),
                      'source': str(args.panel_exe)},
        },
        'commands': COMMANDS,
        'simulation_smoke_test': smoke_lines or 'not run',
    }
    (out / 'MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')

    listing = []
    for path in sorted(p for p in out.rglob('*') if p.is_file() and p.name != 'SHA256SUMS'):
        listing.append(f'{sha256(path)}  {path.relative_to(out).as_posix()}')
    # LF on every platform: sha256sum -c reads a CR as part of the file name.
    (out / 'SHA256SUMS').write_bytes(('\n'.join(listing) + '\n').encode('utf-8'))
    for line in smoke_lines:
        print('PASS', line)
    print(f'Package: {out}')
    print(f'Revision {manifest["revision"]}, image {image.name} {manifest["firmware"]["sha256"]}')
    print(f'Hardware: {HARDWARE_STATUS}. Nothing was flashed.')


if __name__ == '__main__':
    main()
