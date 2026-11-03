# Drive

Saturation with the Soft, Hard, Tape and Fold curves from Tape FX (shared in
[`common/dsp/saturation.h`](../../common/dsp/saturation.h), origin MixForge), tone and
automatic level compensation around the drive. Core per FX contract 1.0 (wet-only), its own
firmware `drive-fx` for the stereo line inputs.

| Status | |
| --- | --- |
| Version | Core and firmware 0.1.0 (`src/version.h`) |
| Software | Host, sanitizer and ARM checks passed (see CHANGELOG) |
| Hardware | NOT MEASURED, [hardware test](docs/HARDWARE_TEST.md) |

## Controls (firmware `drive-fx`)

| Element | Function |
| --- | --- |
| VR1 | Drive 0–100 % (up to +30 dB into the curve) |
| VR2 | Tone: low-pass 1–20 kHz |
| VR3 | Level −24 … +12 dB |
| VR4 | Compensation 0–100 % (auto level) |
| VR5 | Target offset −3 … 0 dB |
| S1–S4 | Curve Soft, Hard, Tape, Fold (LED D1–D4) |
| S5 | Auto level on/off (LED D5) |
| SW17 | Drive on/off, 10 ms crossfade (LED D8). Off = exactly the dry signal |
| S9 + S16 (5 s) | Bootloader |

Input: stereo. A mono signal in the left input feeds both channels. MIDI CC 20–27
(omni, USB and DIN) and SQ parameters over USB per the
[parameter contract](../../sequencers/step-seq/docs/PARAMETERS.md). Pots and SW17 take effect
when they are moved. Until then the value last set via CC or panel applies.
D16 indicates an overrun of the callback deadline.

## Auto level

Measured only around the drive, between the input and output of curve and tone, before Level.
Delay, Dry/Wet and limiter lie outside.

1. **Fixed compensation** per curve based on the level into the curve, i.e. the input RMS of the
   last 300 ms window plus the drive gain. The table
   [`include/drive/autolevel_table.h`](include/drive/autolevel_table.h) is generated from the
   phase 0 reference fixtures (`make -C effects/drive autolevel-table`, median over the
   fixtures).
2. **Slow correction** from 300 ms RMS windows, limited to ±3 dB, held below
   −60 dBFS.
3. **Target level** = input RMS + target offset. Compensation scales the correction linearly
   in dB (0 % = none).

Acceptance in the host test: output RMS within ±1 dB of the target level for all curves,
drive 0–100 %, target offset 0 and −1.5 dB, with the phase 0 fixtures and the 303 fixture
(largest deviation 0.41 dB). **Limit:** Fold on square-wave signals. The flat tops
fold to an almost constant value; the result gets up to 17 dB quieter, but never
louder than the target.

No oversampling (D5). Aliasing of Hard and Fold is assessed in the listening test.

## Build and test

```bash
make PROJECT=drive-fx -j4                 # build/DriveFx-v0.1.0.bin
make PROJECT=drive-fx test                # core and firmware on fakes
make PROJECT=drive-fx test-sanitize
make test-fx                              # shared FX contract test (with Drive)
python3 scripts/check-firmware.py effects/drive/build/DriveFx-v0.1.0.bin
python3 scripts/check-elf-placement.py effects/drive/build/DriveFx-v0.1.0.elf
```

`include/drive/drive.h` contains the core, `host.h` the host's 10 ms crossfade and
`params.h` the parameters. There are no libm calls in the audio path
(`common/dsp/fastmath.h`).
