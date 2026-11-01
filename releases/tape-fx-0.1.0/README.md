# Tape FX 0.1.0 – listening test images

Test release for the hardware listening test, not an accepted version. Built on
2026-10-26 from commit `bc0c7e9` (clean working tree, see `COMPONENTS-*.json`)
with xPack GCC 13.2.1-1.1. No image has been tested on a device:
sound, CPU load, levels and switch direction are **NOT MEASURED**.

| File | Content |
| --- | --- |
| `TapeFx-v0.1.0.bin` | Stereo effect `tape-fx` 0.1.0 (line input → Tape FX → output), 125 264 B |
| `ToyPianoTape-v0.1.0.bin` | `toy-piano-tape` 0.1.0 (Toy Piano → Tape FX), 173 956 B |
| `SHA256SUMS` | Checksums of both images (`sha256sum -c SHA256SUMS`) |
| `COMPONENTS-*.json` | Component manifests (versions, commit, size, SHA-256) |
| `Tape-FX-Bedienungsanleitung.pdf` | User manual (German): controls, expectations, test procedure to fill in |
| `Tape-FX-Cheatsheet.pdf` | One page: assignments, ranges, quick test |

Flash manually as described in [FLASHING.md](../../docs/FLASHING.md). Both images
pass `scripts/check-firmware.py`; the ELF check (`scripts/check-tape-elf.py`)
ran on the build output of the same commit.

Rebuild and compare:

```bash
make PROJECT=tape-fx -j4
make PROJECT=toy-piano-tape -j4
sha256sum effects/tape-fx/build/TapeFx-v0.1.0.bin instruments/toy-piano/build/tape/ToyPianoTape-v0.1.0.bin
```

The PDFs are generated from `effects/tape-fx/docs/print/` (`node render.js`, Playwright/Chromium).
Details: [technical report](../../effects/tape-fx/docs/REPORT.md),
[listening test run sheet](../../effects/tape-fx/docs/HARDWARE_TEST.md).
