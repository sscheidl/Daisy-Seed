#!/usr/bin/env python3
"""Stores the Daisy Control Panel build in the apps/ store and assembles the SD page
hardware-test package from the stores only (docs/dcp/SD_PAGE_WORK_ORDER.md phase 5,
docs/FIRMWARE_BINARIES.md section 8).

    python scripts/dcp_sd_test_package.py --store     # build/editor -> apps/daisy-control-panel/<version>+<commit7>/
    python scripts/dcp_sd_test_package.py --package   # apps/ + firmware/ -> build/dcp-sd-test/ (verified)
    python scripts/dcp_sd_test_package.py --store --package --selftest

The stores live at the root of the main checkout (the parent of `git rev-parse
--git-common-dir`), also from a worktree. Nothing here flashes a board or opens a device;
--selftest runs the packaged EXE against its simulators from the package folder.
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

ROOT = pathlib.Path(__file__).resolve().parent.parent
EDITOR = ROOT / 'editors' / 'mod-on-daisy'
APP_ID = 'daisy-control-panel'
EXE_NAME = 'Daisy Control Panel.exe'
VST3_NAME = 'Mod on Daisy.vst3'
LICENSE_NAME = 'COPYING.AGPL-3.0.txt'
HARDWARE_STATUS = 'NOT MEASURED'
# The four images of docs/dcp/SD_PAGE_WORK_ORDER.md section 3: store folder -> file name.
IMAGES = [
    ('granular-mod/0.2.1+85f8a13-dirty', 'NebulaeMod-v0.2.1.bin'),
    ('granular/0.2.1+da5f574', 'NebulaeOnDaisy-v0.2.1.bin'),
    ('looper-mod/0.6.1+unknown', 'LooperMod-v0.6.1.bin'),
    ('looper/0.6.1+unknown', 'Looper-v0.6.1.bin'),
]
COMMANDS = [
    'make -C editors/mod-on-daisy test',
    'make -C editors/mod-on-daisy test HOST_FLAGS="<ASan/UBSan flags>"',
    'cmake -S editors/mod-on-daisy -B build/editor -G "Visual Studio 18 2026" -A x64 -DMOD_EDITOR_JUCE_DIR=<JUCE 9.0.4>',
    'cmake --build build/editor --config Release --parallel',
    '"Daisy Control Panel.exe" --selftest <dir>',
    'ModOnDaisyPluginCheck.exe "<Mod on Daisy.vst3>" editors/mod-on-daisy/web/tests/legacy-reverb.modpatch',
    'E2E_ONLY=nebulae node editors/mod-on-daisy/web/tests/e2e.cjs <mod_sim>',
    'python -m pytest diagnostics/tests/python',
    'python scripts/check-firmware.py <image>',
    'python scripts/dcp_sd_test_package.py --store --package --selftest',
]


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def main_checkout():
    common = pathlib.Path(git('rev-parse', '--git-common-dir'))
    if not common.is_absolute():
        common = ROOT / common
    return common.resolve().parent


def version():
    match = re.search(r'project\([^\s]+\s+VERSION\s+([^\s)]+)', (EDITOR / 'CMakeLists.txt').read_text(encoding='utf-8'))
    if not match:
        raise SystemExit('No project version in editors/mod-on-daisy/CMakeLists.txt')
    return match.group(1)


def files_under(folder):
    return sorted(p for p in folder.rglob('*') if p.is_file())


def write_sums(folder, skip=('SHA256SUMS',)):
    lines = [f'{sha256(p)}  {p.relative_to(folder).as_posix()}' for p in files_under(folder) if p.name not in skip]
    # LF on every platform: sha256sum -c reads a CR as part of the file name.
    (folder / 'SHA256SUMS').write_bytes(('\n'.join(lines) + '\n').encode('utf-8'))
    return lines


def check_sums(folder, require_all=True):
    """Every line of SHA256SUMS matches; with require_all every file is listed (the firmware
    store lists only its images, not BUILD.txt and STATUS.md)."""
    listed = {}
    for line in (folder / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        digest, _, name = line.partition('  ')
        listed[name] = digest
    for name, digest in listed.items():
        actual = sha256(folder / name)
        if actual != digest:
            raise SystemExit(f'{folder / name}: SHA-256 {actual} differs from SHA256SUMS ({digest})')
    present = {p.relative_to(folder).as_posix() for p in files_under(folder) if p.name != 'SHA256SUMS'}
    missing = present - set(listed)
    if require_all and missing:
        raise SystemExit(f'{folder}: files not in SHA256SUMS: {sorted(missing)}')
    return listed


def store(build_dir, apps):
    commit = git('rev-parse', 'HEAD')
    dirty = bool(git('status', '--porcelain', '--untracked-files=no', '--ignore-submodules=all'))
    ver = version()
    folder = apps / APP_ID / (f'{ver}+{commit[:7]}' + ('-dirty' if dirty else ''))
    exe = build_dir / 'ModOnDaisyEditor_artefacts' / 'Release' / EXE_NAME
    vst3 = build_dir / 'ModOnDaisyPlugin_artefacts' / 'Release' / 'VST3' / VST3_NAME
    for required in (exe, vst3, EDITOR / LICENSE_NAME):
        if not required.exists():
            raise SystemExit(f'Missing: {required}')
    if folder.exists():
        raise SystemExit(f'{folder} exists already; a store folder is written once')
    folder.mkdir(parents=True)
    shutil.copy2(exe, folder / EXE_NAME)
    shutil.copytree(vst3, folder / VST3_NAME)
    shutil.copy2(EDITOR / LICENSE_NAME, folder / LICENSE_NAME)
    branch = git('rev-parse', '--abbrev-ref', 'HEAD')
    built = datetime.datetime.fromtimestamp(exe.stat().st_mtime).strftime('%Y-%m-%d %H:%M')
    (folder / 'BUILD.txt').write_text(
        f'app: Daisy Control Panel {ver} (standalone EXE) and Mod on Daisy VST3 {ver}, version source editors/mod-on-daisy/CMakeLists.txt\n'
        f'sha256 {EXE_NAME}: {sha256(exe)}\n'
        f'commit: {commit} on {branch}{" (dirty working tree)" if dirty else ""}\n'
        f'built: {built} local time (EXE file time), same build directory for the VST3\n'
        f'build: cmake -S editors/mod-on-daisy -B build/editor -G "Visual Studio 18 2026" -A x64 '
        f'-DMOD_EDITOR_JUCE_DIR=<.tools/juce-9.0.4/src>; cmake --build build/editor --config Release --parallel\n'
        f'toolchain: MSVC (Visual Studio 18 2026 Build Tools), JUCE 9.0.4, not bit-reproducible\n'
        f'stored {datetime.date.today().isoformat()} by scripts/dcp_sd_test_package.py from: {build_dir}\n',
        encoding='utf-8')
    (folder / 'STATUS.md').write_text(
        f'# {APP_ID}/{folder.name}\n\n'
        f'* {datetime.date.today().isoformat()} `candidate`: stored from the SD page work order (docs/dcp/SD_PAGE_WORK_ORDER.md, '
        f'PR branch claude/dcp-sd-page). Self-test against the simulated Nebulae MOD, Looper MOD, Nebulae and Looper '
        f'passed; VST3 plugin check passed; VST3 identity unchanged (name, codes, bundle). Meant for NebulaeMod 0.2.1, '
        f'NebulaeOnDaisy 0.2.1, LooperMod 0.6.1 and Looper 0.6.1. Hardware {HARDWARE_STATUS}.\n',
        encoding='utf-8')
    write_sums(folder)
    print(f'Stored: {folder}')
    return folder


def newest_store_folder(apps):
    base = apps / APP_ID
    candidates = sorted(p for p in base.iterdir() if p.is_dir()) if base.is_dir() else []
    if not candidates:
        raise SystemExit(f'No stored app under {base}; run --store first')
    return max(candidates, key=lambda p: p.stat().st_mtime)


def check_firmware(image, roots):
    """scripts/check-firmware.py compares the file name with the version headers of its own
    tree; an image built from another commit (the Looper 0.6.1 images while this branch is
    on 0.6.0) is checked with the exported tree given by --check-root. Returns the root that
    passed; raises when none does."""
    errors = []
    for root in roots:
        result = subprocess.run([sys.executable, str(root / 'scripts' / 'check-firmware.py'), str(image)],
                                capture_output=True, text=True)
        if result.returncode == 0:
            print(result.stdout.strip(), f'(checker: {root})')
            return root
        errors.append(f'{root}: {(result.stderr or result.stdout).strip().splitlines()[-1]}')
    raise SystemExit(f'check-firmware.py failed for {image.name}: ' + ' / '.join(errors))


def package(out, apps, firmware, app_folder, selftest, check_root, check_note):
    check_sums(app_folder)
    for folder, name in IMAGES:
        if not (firmware / folder / name).is_file():
            raise SystemExit(f'Image missing from the store: {firmware / folder / name}')
        if name not in check_sums(firmware / folder, require_all=False):
            raise SystemExit(f'{firmware / folder}: {name} is not listed in SHA256SUMS')
    if out.exists():
        shutil.rmtree(out)
    (out / 'apps').mkdir(parents=True)
    (out / 'firmware').mkdir()
    shutil.copy2(app_folder / EXE_NAME, out / 'apps' / EXE_NAME)
    shutil.copytree(app_folder / VST3_NAME, out / 'apps' / VST3_NAME)
    shutil.copy2(app_folder / LICENSE_NAME, out / 'apps' / LICENSE_NAME)
    shutil.copy2(app_folder / 'BUILD.txt', out / 'apps' / 'BUILD.txt')
    images = []
    for folder, name in IMAGES:
        src = firmware / folder / name
        shutil.copy2(src, out / 'firmware' / name)
        checked_with = check_firmware(out / 'firmware' / name, [ROOT] + ([check_root] if check_root != ROOT else []))
        images.append({'file': f'firmware/{name}', 'store': f'firmware/{folder}', 'bytes': src.stat().st_size,
                       'sha256': sha256(src), 'checked_with': str(checked_with)})
    shutil.copy2(ROOT / 'docs' / 'dcp' / 'SD_PAGE_TEST_SHEET.md', out / 'TEST_SHEET.md')
    shutil.copy2(ROOT / 'editors' / 'mod-on-daisy' / 'README.md', out / 'README-Daisy-Control-Panel.md')
    selftest_result = 'not run'
    if selftest:
        env = dict(os.environ, MOD_ON_DAISY_NO_AUTOCONNECT='1')
        result = subprocess.run([str(out / 'apps' / EXE_NAME), '--selftest', 'selftest'], cwd=out, env=env,
                                capture_output=True, text=True, timeout=600)
        log = (out / 'selftest' / 'selftest.txt')
        text = log.read_text(encoding='utf-8') if log.is_file() else ''
        if result.returncode != 0 or not text.rstrip().endswith(')'):
            raise SystemExit(f'Packaged EXE self-test failed (exit {result.returncode}):\n{text[-2000:]}')
        selftest_result = f'exit 0, {sum(1 for l in text.splitlines() if l.startswith("PASS"))} PASS lines'
        shutil.rmtree(out / 'selftest')  # screenshots are not part of the package
    manifest = {
        'package': 'dcp-sd-hardware-test',
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'),
        'revision': git('rev-parse', 'HEAD'),
        'branch': git('rev-parse', '--abbrev-ref', 'HEAD'),
        'tracked_changes': bool(git('status', '--porcelain', '--untracked-files=no', '--ignore-submodules=all')),
        'hardware_status': HARDWARE_STATUS,
        'app': {
            'id': APP_ID, 'version': version(), 'store': f'apps/{APP_ID}/{app_folder.name}',
            'exe': {'file': f'apps/{EXE_NAME}', 'sha256': sha256(app_folder / EXE_NAME)},
            'vst3': {'file': f'apps/{VST3_NAME}', 'sha256': sha256(app_folder / VST3_NAME / 'Contents' / 'x86_64-win' / VST3_NAME)},
        },
        'firmware': images,
        'firmware_check': {'root': str(check_root), 'note': check_note},
        'commands': COMMANDS,
        'packaged_exe_selftest': selftest_result,
    }
    (out / 'MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    write_sums(out)
    check_sums(out)
    print(f'Package: {out}')
    print(f'Revision {manifest["revision"]}, app {app_folder.name}, packaged EXE self-test: {selftest_result}')
    for image in images:
        print(f'  {image["file"]}: {image["sha256"]} ({image["store"]})')
    print(f'Hardware: {HARDWARE_STATUS}. Nothing was flashed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--store', action='store_true', help='copy the build into the apps/ store')
    parser.add_argument('--package', action='store_true', help='assemble build/dcp-sd-test from apps/ and firmware/')
    parser.add_argument('--selftest', action='store_true', help='run the packaged EXE --selftest from the package folder')
    parser.add_argument('--build-dir', type=pathlib.Path, default=ROOT / 'build' / 'editor')
    parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'build' / 'dcp-sd-test')
    parser.add_argument('--app-folder', type=pathlib.Path, help='store folder to package (default: the newest)')
    parser.add_argument('--check-root', type=pathlib.Path, default=ROOT,
                        help='checkout whose version headers match the stored images (check-firmware.py compares '
                             'the file name with the source tree; default: this checkout)')
    parser.add_argument('--check-note', default='this checkout', help='recorded in MANIFEST.json next to the check root')
    args = parser.parse_args()
    main = main_checkout()
    apps, firmware = main / 'apps', main / 'firmware'
    folder = store(args.build_dir, apps) if args.store else None
    if args.package:
        package(args.out, apps, firmware, args.app_folder or folder or newest_store_folder(apps), args.selftest,
                args.check_root.resolve(), args.check_note)
    if not (args.store or args.package):
        parser.print_help()


if __name__ == '__main__':
    main()
