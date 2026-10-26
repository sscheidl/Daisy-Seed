#!/usr/bin/env python3
"""Reference input fixtures for the Drive and Delay tests (Acid Seq, phase 0).

Mono, 48 kHz, 1 s, float32 little-endian (.f32). The files are the reference; this
script documents how they were made and must reproduce them within 1e-6:

    python tests/fixtures/acid-seq/generate.py --write
    python tests/fixtures/acid-seq/generate.py --check
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

FIXTURE_VERSION = '1.0.0'
RATE = 48000
LENGTH = RATE
PERIOD = 512                      # 93.75 Hz: an exact number of samples per cycle
F0 = RATE / PERIOD
PEAK = 0.5                        # -6 dBFS after the filter
LOWPASS_STAGES = (300.0, 1200.0, 5000.0)
RESONANCE = (1000.0, 8.0)         # cutoff Hz, Q
HERE = Path(__file__).resolve().parent


def band_limited(odd_only):
    """One cycle of a saw (all harmonics) or square (odd harmonics) below 20 kHz."""
    count = int(20000 / F0)
    cycle = []
    for n in range(PERIOD):
        phase = 2.0 * math.pi * n / PERIOD
        value = 0.0
        for k in range(1, count + 1):
            if odd_only and k % 2 == 0:
                continue
            if odd_only:
                value += math.sin(k * phase) / k
            else:
                value += (-1.0 if k % 2 == 0 else 1.0) * math.sin(k * phase) / k
        cycle.append(value)
    return [cycle[i % PERIOD] for i in range(LENGTH)]


def lowpass(signal, cutoff, q):
    """RBJ biquad low-pass, direct form I in double precision."""
    w = 2.0 * math.pi * cutoff / RATE
    alpha = math.sin(w) / (2.0 * q)
    cos_w = math.cos(w)
    a0 = 1.0 + alpha
    b0 = (1.0 - cos_w) / 2.0 / a0
    b1 = (1.0 - cos_w) / a0
    b2 = b0
    a1 = -2.0 * cos_w / a0
    a2 = (1.0 - alpha) / a0
    x1 = x2 = y1 = y2 = 0.0
    out = []
    for x in signal:
        y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, x, y1, y
        out.append(y)
    return out


def normalized(signal):
    peak = max(abs(v) for v in signal)
    return [v * PEAK / peak for v in signal]


def fixtures():
    saw = band_limited(odd_only=False)
    square = band_limited(odd_only=True)
    result = {}
    for stage, cutoff in enumerate(LOWPASS_STAGES, start=1):
        result[f'saw_lp{stage}'] = (normalized(lowpass(saw, cutoff, math.sqrt(0.5))),
                                    f'band-limited saw {F0} Hz, RBJ low-pass {cutoff:g} Hz Q 0.707, peak -6 dBFS')
        result[f'square_lp{stage}'] = (normalized(lowpass(square, cutoff, math.sqrt(0.5))),
                                       f'band-limited square {F0} Hz, RBJ low-pass {cutoff:g} Hz Q 0.707, peak -6 dBFS')
    result['saw_resonance'] = (normalized(lowpass(saw, *RESONANCE)),
                               f'band-limited saw {F0} Hz, RBJ low-pass {RESONANCE[0]:g} Hz Q {RESONANCE[1]:g} (resonance peak), peak -6 dBFS')
    result['impulse'] = ([1.0] + [0.0] * (LENGTH - 1), 'unit impulse at sample 0')
    result['silence'] = ([0.0] * LENGTH, 'digital silence')
    return result


def pack(signal):
    return struct.pack(f'<{len(signal)}f', *signal)


def stats(data):
    values = struct.unpack(f'<{len(data) // 4}f', data)
    rms = math.sqrt(sum(v * v for v in values) / len(values))
    return {'peak': round(max(abs(v) for v in values), 6), 'rms': round(rms, 6)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    manifest = {'format_version': 1, 'fixture_version': FIXTURE_VERSION, 'sample_rate': RATE,
                'channels': 1, 'frames': LENGTH, 'format': 'float32 little-endian', 'files': {}}
    worst = 0.0
    for name, (signal, description) in fixtures().items():
        data = pack(signal)
        path = HERE / f'{name}.f32'
        if args.write:
            path.write_bytes(data)
        elif args.check:
            stored = path.read_bytes()
            ref = struct.unpack(f'<{len(stored) // 4}f', stored)
            new = struct.unpack(f'<{len(data) // 4}f', data)
            worst = max(worst, max(abs(a - b) for a, b in zip(ref, new)))
            data = stored
        manifest['files'][name] = {'file': path.name, 'description': description,
                                   'sha256': hashlib.sha256(data).hexdigest(), **stats(data)}
    text = json.dumps(manifest, indent=2) + '\n'
    if args.write:
        (HERE / 'manifest.json').write_text(text, encoding='utf-8', newline='\n')
    elif args.check:
        if worst > 1e-6:
            parser.exit(1, f'fixtures differ from the generator by {worst}\n')
        print(f'PASS: fixtures reproduce within {worst:.2e}')
    else:
        print(text, end='')


if __name__ == '__main__':
    main()
