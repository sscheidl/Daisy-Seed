#!/usr/bin/env python3
"""Builds the Mod on Daisy hardware-test package locally (HARDWARE_TEST_RUN.md).

    python scripts/hardware_test_package.py                 # build everything, then package
    python scripts/hardware_test_package.py --no-build      # package existing images only
    python scripts/hardware_test_package.py --editor-exe "build/editor/.../Mod on Daisy Editor.exe"

Output: build/hardware-test/ with all firmware images, SHA256SUMS, mod_probe.py,
the browser editor (also as a single file) and the run sheet as README.md.
Nothing is flashed; flashing stays a manual step (docs/FLASHING.md).
"""
import argparse
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'build' / 'hardware-test'
BENCH = ROOT / 'modules' / 'mod-on-daisy' / 'bench'


def run(*args, cwd=ROOT):
    print('+', ' '.join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def version(project):
    out = subprocess.run(['make', '-s', f'PROJECT={project}', 'version'], cwd=ROOT, check=True,
                         capture_output=True, text=True)
    return out.stdout.strip().splitlines()[-1]


def images():
    """(source path, name in the package, purpose) of every image of the run sheet."""
    tp, pm, mb = version('toy-piano'), version('physical-modeling'), version('mod-bench')
    pm_dir = ROOT / 'instruments' / 'physical-modeling' / 'build'
    return [
        (ROOT / 'instruments' / 'toy-piano' / 'build' / f'ToyPiano-v{tp}.bin', f'ToyPiano-v{tp}.bin',
         'Toy Piano mit Mod on Daisy (Checkliste T1–T15)'),
        (pm_dir / 'mod' / f'PhysicalModelingMod-v{pm}.bin', f'PhysicalModelingMod-v{pm}.bin',
         'Physical Modeling mit Matrix (Checkliste R1–R10)'),
        (pm_dir / f'PhysicalModeling-v{pm}.bin', f'PhysicalModeling-v{pm}.bin',
         'reguläre Physical-Modeling-Fassung zum Vergleich'),
        (BENCH / 'build' / f'ModBench-v{mb}.bin', f'ModBench-v{mb}.bin', 'Messfirmware Block 48 (H1–H8, S0–S8)'),
        (BENCH / 'build' / 'b256-max8' / f'ModBench-v{mb}.bin', f'ModBench-v{mb}-block256.bin',
         'Messfirmware Block 256'),
        (BENCH / 'build' / 'b48-max64' / f'ModBench-v{mb}.bin', f'ModBench-v{mb}-max64.bin',
         'Messfirmware 64 aktive Zellen (S6)'),
    ]


def build():
    for project in ('toy-piano', 'physical-modeling', 'physical-modeling-mod', 'mod-bench'):
        run('make', f'PROJECT={project}', '-j4')
    run('make', '-C', BENCH, 'BLOCK=256', '-j4')
    run('make', '-C', BENCH, 'MAX_ACTIVE=64', '-j4')


def rewrite_links(text, source_dir):
    """Relative links of a copied document, pointing back into this checkout."""
    def repl(m):
        target = m.group(2)
        if re.match(r'[a-z]+:|#', target):
            return m.group(0)
        path, _, anchor = target.partition('#')
        resolved = (source_dir / path).resolve()
        if not resolved.exists():
            raise SystemExit(f'{source_dir}: broken link {target}')
        rel = os.path.relpath(resolved, OUT).replace(os.sep, '/')
        return f'{m.group(1)}({rel}{"#" + anchor if anchor else ""})'
    return re.sub(r'(\[[^\]]*\])\(([^)\s]+)\)', repl, text)


def single_file_editor(web):
    html = (web / 'index.html').read_text(encoding='utf-8')
    js = (web / 'mod_protocol.js').read_text(encoding='utf-8')
    tag = '<script src="mod_protocol.js"></script>'
    if html.count(tag) != 1 or '</script' in js:
        raise SystemExit('editor: cannot inline mod_protocol.js')
    return html.replace(tag, '<script>\n' + js + '\n</script>')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--no-build', action='store_true', help='package the existing images without building')
    ap.add_argument('--editor-exe', type=pathlib.Path, help='JUCE editor .exe to include (CI artifact or own build)')
    args = ap.parse_args()

    if not args.no_build:
        build()
    entries = images()
    missing = [str(src.relative_to(ROOT)) for src, _, _ in entries if not src.exists()]
    if missing:
        raise SystemExit('missing images (build first or drop --no-build):\n  ' + '\n  '.join(missing))
    # The checker validates names, vectors and embedded versions at the original paths.
    for src, _, _ in entries:
        run(sys.executable, ROOT / 'scripts' / 'check-firmware.py', src)

    if OUT.exists():
        shutil.rmtree(OUT)
    (OUT / 'editor').mkdir(parents=True)
    for src, name, _ in entries:
        shutil.copy2(src, OUT / name)
    shutil.copy2(ROOT / 'modules' / 'mod-on-daisy' / 'tools' / 'mod_probe.py', OUT / 'mod_probe.py')
    web = ROOT / 'editors' / 'mod-on-daisy' / 'web'
    for name in ('index.html', 'mod_protocol.js'):
        shutil.copy2(web / name, OUT / 'editor' / name)
    (OUT / 'editor' / 'ModOnDaisyEditor.html').write_text(single_file_editor(web), encoding='utf-8')
    if args.editor_exe:
        shutil.copy2(args.editor_exe, OUT / 'editor' / 'ModOnDaisyEditor.exe')
    sheet = ROOT / 'modules' / 'mod-on-daisy' / 'docs' / 'HARDWARE_TEST_RUN.md'
    note = ('> Paket aus `scripts/hardware_test_package.py`. Die Links zeigen in den Checkout, '
            'aus dem es gebaut wurde.\n\n')
    (OUT / 'README.md').write_text(note + rewrite_links(sheet.read_text(encoding='utf-8'), sheet.parent),
                                   encoding='utf-8')

    commit = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, capture_output=True,
                            text=True).stdout.strip() or 'unbekannt'
    lines = []
    for path in sorted(p for p in OUT.rglob('*') if p.is_file() and p.name != 'SHA256SUMS'):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        lines.append(f'{digest}  {path.relative_to(OUT).as_posix()}')
    (OUT / 'SHA256SUMS').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'\nPaket: {OUT} (Commit {commit})')
    for _, name, purpose in entries:
        print(f'  {name:36} {purpose}')
    if not args.editor_exe:
        print('  JUCE-Editor: ModOnDaisyEditor.exe aus der CI (Actions → Editor) oder --editor-exe')


if __name__ == '__main__':
    main()
