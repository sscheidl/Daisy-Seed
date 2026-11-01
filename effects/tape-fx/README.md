# Tape FX

Reusable stereo tape machine component (`tape-fx` 0.1.0), ported from
the drumlogue master effects **TapeVibe** (core, sonic reference) and
**MixForge** (switchable additional stages) in `sscheidl/drumlogue_fx_units`. Plus
two build targets for listening:

| Project | Content | Status |
| --- | --- | --- |
| `tape-fx` | Stereo line input → Tape FX → output | Host/sanitizer/ARM checks passed, hardware NOT MEASURED |
| `toy-piano-tape` | Toy Piano → Tape FX → output ([description](../../instruments/toy-piano/docs/TAPE.md)) | Host/sanitizer/ARM checks passed, hardware NOT MEASURED |

Functions: wow, flutter, drive with dynamic tape compression, head bump,
tone/age, hiss and dropouts in three characters (Studio, Tape, Cassette) – all
TapeVibe. Optional from MixForge: further drive curves (TAPE with pre-/de-emphasis,
HARD, FOLD), stereo-linked compressor and bitcrusher.

## API

```cpp
#include "effects/tape-fx/src/tape_fx.h"    // DSP, wet only (common/audio/fx_contract.h)
#include "effects/tape-fx/src/tape_mixer.h" // host: Dry/Wet, bypass, output protection

static DSY_SDRAM_BSS seed::fx::TapeFx::Storage memory; // 16 KiB, the host chooses the region
seed::fx::TapeFx tape;
seed::fx::TapeMixer mixer;

seed::fx::tape::Parameters p;   // default = TapeVibe initial values, MixForge off
tape.SetParameters(p);          // any time; values glide over 15 ms
tape.Prepare({48000, 48}, memory); // only at startup; 48 kHz only
mixer.Reset(0.4f, false);

tape.Process({in_l, in_r, out_l, out_r, n}); // wet; in-place per channel allowed
mixer.Process(in_l, in_r, out_l, out_r, n);  // out = mix of dry and wet, protected
tape.Reset();                                 // state and noise sequence as in TapeVibe
```

`Process` is free of allocation, locks and I/O and bounded. Parameters are
normalized (`drive`, `tone`, `age`, `wow`, `flutter`, `hiss`, `dropout` 0..1;
`Parameters::FromPercent(n)` is the exact drumlogue conversion). Invalid values
are clamped, NaN/Inf replaced by default values.

## Build and tests

```bash
make PROJECT=tape-fx test            # DSP host tests + firmware controls
make PROJECT=tape-fx test-sanitize   # ASan/UBSan
make PROJECT=tape-fx test-thread     # TSan
make PROJECT=tape-fx -j4             # ARM image build/TapeFx-v0.1.0.bin
make PROJECT=tape-fx manifest
make -C effects/tape-fx bench        # host timing (relative only)
make test-fx                         # shared FX contract, all three adapters
python3 scripts/check-tape-elf.py effects/tape-fx/build/TapeFx-v0.1.0.elf
```

The host tests compare the reference path bit-exactly with the unmodified
drumlogue code in `tests/reference/` ([origin](tests/reference/UPSTREAM.md)) and
measure wow/flutter rates and depths, drive curves, hiss level,
dropout rates, stereo separation, bypass, parameter changes, reset and 10 minutes
of runtime at extreme values.

* [Technical report](docs/REPORT.md): architecture, deviations, optimizations,
  CPU and memory requirements.
* [Hardware listening test](docs/HARDWARE_TEST.md): controls and test procedure for both images.
* Ready-made listening test images with checksums, user manual and cheat sheet (PDF):
  [releases/tape-fx-0.1.0](../../releases/tape-fx-0.1.0/). PDF sources: `docs/print/`.

Stardust (`instruments/looper/`) is not changed. The component is prepared for a
later integration there (16 KiB delay memory in the remaining SDRAM).
