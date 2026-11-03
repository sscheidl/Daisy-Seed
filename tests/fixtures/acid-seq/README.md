# Acid Seq reference fixtures

Input signals for the level and impulse tests of Drive and Delay (phase 0, R11).
Mono, 48 kHz, 1 s, float32 little-endian (`.f32`). The files are the reference.
[`generate.py`](generate.py) documents how they are produced and must reproduce them to within
1e-6. [`manifest.json`](manifest.json) contains the version, SHA-256,
peak and RMS of each file.

| File | Content |
| --- | --- |
| `saw_lp1…3.f32` | band-limited saw 93.75 Hz through RBJ low-pass 300 Hz, 1.2 kHz, 5 kHz (Q 0.707), peak −6 dBFS |
| `square_lp1…3.f32` | band-limited square, otherwise as above |
| `saw_resonance.f32` | saw through RBJ low-pass 1 kHz with Q 8 (resonance peak) |
| `impulse.f32` | unit impulse at sample 0 |
| `silence.f32` | digital silence |

93.75 Hz = 48 000 / 512: one period has exactly 512 samples. The 303 fixture for
the auto level acceptance is added, versioned, from phase 2.

```bash
python3 tests/fixtures/acid-seq/generate.py --check
```
