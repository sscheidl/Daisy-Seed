#!/usr/bin/env python3
"""Assemble the Otherworld vol. 1 bank (MIT) into the committed program header and control-page list.

Inputs (all in this repository): upstream/effects/*.spn and upstream/bank.json, unchanged copies of
github.com/andreasjansson/otherworld at the commit in UPSTREAM.md, and the vendored assembler
../third_party/asfv1/asfv1.py (asfv1 1.2.7, MIT, unmodified).

The assembly matches the programs of the upstream release firmware word for word (tests/test_bank.py):
* coefficients round half away from zero, as SpinASM does (asfv1 uses Python's round-half-to-even);
* out-of-range literals such as 2.0 clamp to the largest value (asfv1 --clamp);
* RMPA carries ADDR_PTR (0x18) in its address field, as SpinASM writes it (asfv1 writes 0; the field is
  not used by the instruction);
* Ventifact's register symbol `oR` is renamed in memory before assembly, because asfv1 reads it as the
  mnemonic OR. The source file stays unchanged.

Outputs: include/fv1bank/otherworld_programs.h and otherworld_programs.js. Standard library only.
Usage: build_bank.py [--check]   (--check: fail if the committed outputs differ from a fresh build)
"""
import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import re
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent
UPSTREAM = HERE / 'upstream'
ASFV1 = HERE.parent / 'third_party' / 'asfv1' / 'asfv1.py'
HEADER = HERE / 'include' / 'fv1bank' / 'otherworld_programs.h'
SCRIPT = HERE / 'otherworld_programs.js'
UPSTREAM_COMMIT = '8505568090db52310a255be3f95a0018145e90e6'
# bank.json category -> MIDI bank order and display title (the upstream slot order is already grouped).
GROUPS = [('slicer', 'Slicer'), ('reverb', 'Reverb'), ('distortion', 'Distortion'), ('voice', 'Voice')]
# Source workarounds for asfv1, applied in memory: program id -> [(regex, replacement)].
WORKAROUNDS = {'215_ventifact': [(r'\boR\b', 'o_r_')]}
RMPA_ADDR_PTR = 0x18


def round_half_away(x, ndigits=None):
    if ndigits is not None:
        return round(x, ndigits)
    return int(math.floor(abs(x) + 0.5)) * (1 if x >= 0 else -1)


def load_asfv1():
    spec = importlib.util.spec_from_file_location('asfv1_vendored', ASFV1)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.round = round_half_away  # module-level name shadows the builtin inside asfv1 only
    return module


def assemble(asfv1, program_id, text):
    for pattern, replacement in WORKAROUNDS.get(program_id, []):
        text = re.sub(pattern, replacement, text)
    errors = []

    def fail(message):
        errors.append(message)
        raise SystemExit(f'{program_id}: {message}')

    parser = asfv1.fv1parse(text, clamp=True, spinreals=False, wfunc=lambda m: None, efunc=fail)
    parser.parse()
    words = list(struct.unpack('>128I', bytes(parser.program)))
    return [(w | (RMPA_ADDR_PTR << 5)) if (w & 0x1F) == 1 else w for w in words]


def header_tags(text):
    tags = {}
    for line in text.splitlines():
        m = re.match(r';\s*@(\w+)\s*(.*)$', line)
        if m and m.group(1) not in tags:
            tags[m.group(1)] = m.group(2).strip()
    return tags


def c_string(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def build():
    asfv1 = load_asfv1()
    bank = json.loads((UPSTREAM / 'bank.json').read_text(encoding='utf-8'))
    entries = []
    for slot, s in enumerate(bank['slots'], 1):
        text = (UPSTREAM / 'effects' / (s['id'] + '.spn')).read_text(encoding='utf-8')
        tags = header_tags(text)
        entries.append({
            'slot': slot, 'id': s['id'], 'name': tags.get('name', s['id']).replace('_clk', ' (clocked)'),
            'category': s['category'], 'pots': [p.strip() for p in tags.get('pots', '').split('|')],
            'clocked': s['id'].endswith('_clk'), 'words': assemble(asfv1, s['id'], text),
            'render': [float(v) for v in tags['render'].split(',')],
        })
        if len(entries[-1]['render']) != 3 or not all(0 <= v <= 1 for v in entries[-1]['render']):
            raise SystemExit(f"{s['id']}: @render needs three pot values in 0..1")
    # Groups follow the upstream order; a category that reappears after another one is an error.
    groups, order = [], [key for key, _ in GROUPS]
    for i, e in enumerate(entries):
        if e['category'] not in order:
            raise SystemExit(f"unknown category {e['category']!r} in slot {e['slot']}")
        if groups and groups[-1]['key'] == e['category']:
            groups[-1]['size'] += 1
            continue
        if any(g['key'] == e['category'] for g in groups):
            raise SystemExit(f"category {e['category']!r} is not contiguous at slot {e['slot']}")
        groups.append({'key': e['category'], 'title': dict(GROUPS)[e['category']], 'offset': i, 'size': 1})
    digest = hashlib.sha256(b''.join(struct.pack('>128I', *e['words']) for e in entries)).hexdigest()
    return entries, groups, digest


def render_header(entries, groups, digest):
    lines = [
        '#pragma once',
        '// SPDX-License-Identifier: MIT',
        '// GENERATED by effects/fv1-banks/otherworld/tools/build_bank.py - do not edit.',
        '// Otherworld vol. 1 by Andreas Jansson (MIT, upstream/LICENSE), github.com/andreasjansson/otherworld',
        f'// at {UPSTREAM_COMMIT}, assembled with asfv1 1.2.7 (MIT). Upstream slot order.',
        f'// {len(entries)} programs, {len(groups)} groups; SHA-256 of the big-endian words: {digest}',
        '#include <cstdint>',
        '',
        'namespace fv1_bank_otherworld {',
        f'inline constexpr unsigned kCount = {len(entries)};',
        f'inline constexpr unsigned kGroupCount = {len(groups)};',
        'inline constexpr uint16_t kGroupOffset[kGroupCount] = {' + ', '.join(str(g['offset']) for g in groups) + '};',
        'inline constexpr uint16_t kGroupSize[kGroupCount] = {' + ', '.join(str(g['size']) for g in groups) + '};',
        'inline constexpr const char* kGroupTitle[kGroupCount] = {' + ', '.join(c_string(g['title']) for g in groups) + '};',
        'inline constexpr const char* kId[kCount] = {',
    ]
    lines += [f'    {c_string(e["id"])},' for e in entries]
    lines += ['};', 'inline constexpr const char* kName[kCount] = {']
    lines += [f'    {c_string(e["name"])},' for e in entries]
    lines += ['};', '// 1: input R carries a square clock (the _clk twins); 0: stereo audio input.',
              'inline constexpr uint8_t kClocked[kCount] = {' + ', '.join('1' if e['clocked'] else '0' for e in entries) + '};',
              '// POT0..POT2 of the upstream listening page (@render), in 0..1.',
              'inline constexpr float kRender[kCount][3] = {']
    lines += ['    {' + ', '.join(f'{v!r}f' for v in e['render']) + '},' for e in entries]
    lines += ['};', 'inline constexpr uint32_t kPrograms[kCount][128] = {']
    for e in entries:
        lines.append(f'    {{ // {e["slot"]:2d} {e["name"]}')
        for row in range(0, 128, 8):
            lines.append('        ' + ', '.join(f'0x{w:08X}u' for w in e['words'][row:row + 8]) + ',')
        lines.append('    },')
    lines += ['};', '} // namespace fv1_bank_otherworld', '']
    return '\n'.join(lines)


def render_script(entries, groups, digest):
    programs = []
    for g_number, g in enumerate(groups):
        for position in range(g['size']):
            e = entries[g['offset'] + position]
            programs.append({'index': g['offset'] + position, 'id': str(e['slot']), 'name': e['name'],
                             'category': g['title'], 'group': g['key'], 'bank': g_number, 'position': position,
                             'ref': e['id'], 'pots': e['pots'], 'clocked': e['clocked'], 'render': e['render']})
    data = {'label': 'Otherworld vol. 1 (MIT)', 'sha256': digest, 'license': 'MIT, Andreas Jansson',
            'groups': groups, 'programs': programs}
    return ('// GENERATED by effects/fv1-banks/otherworld/tools/build_bank.py - do not edit.\n'
            '// Otherworld vol. 1 by Andreas Jansson, MIT licence (upstream/LICENSE).\n'
            'window.FV1_BANK_OTHERWORLD = ' + json.dumps(data, ensure_ascii=True, indent=1) + ';\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='compare with the committed outputs, write nothing')
    args = parser.parse_args(argv)
    entries, groups, digest = build()
    outputs = {HEADER: render_header(entries, groups, digest), SCRIPT: render_script(entries, groups, digest)}
    if args.check:
        stale = [p for p, text in outputs.items()
                 if not p.exists() or p.read_text(encoding='ascii').replace('\r\n', '\n') != text]
        for p in stale:
            print(f'stale: {p.relative_to(HERE)}')
        return 1 if stale else 0
    for path, text in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='ascii', newline='\n')
    print(f'{len(entries)} programs, {len(groups)} groups, sha256 {digest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
