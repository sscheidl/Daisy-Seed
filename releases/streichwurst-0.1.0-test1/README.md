# Streichwurst 0.1.0 – test build 1

Test build for trying on the kit; not an accepted release. The firmware images are
attached to the GitHub draft release "Streichwurst 0.1.0 – Testbuild 1" (built from
`main` @ `0a29966`). Sound, controls, MIDI, CPU load and the comparison with the
original on hardware are **NOT MEASURED**.

| File | Content |
| --- | --- |
| `Streichwurst-Manual.pdf` | English user manual: installation, connections, controls, MIDI/CC plan, control app and matrix, FX image, parameter reference, troubleshooting, test sheet |

Rebuild the manual from `instruments/streichwurst/docs/print/`:

```bash
python build_manual.py && node render.js
```
