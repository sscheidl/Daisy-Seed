# Streichwurst

Standalone string/solo instrument with ensemble and FX for the
**Electro-Smith Seed3 Desktop Development Kit**. The goal is an approximation of the
Waldorf Streichfett that is as close to the original as possible, based on local,
static plugin research and self-developed DSP data and algorithms.

**Status 2026-11-01: firmware prototype 0.1.0** (instrument 0.1.0, FX 0.1.0,
control app 0.3.0). Host, sanitizer, app and ARM builds passed locally
([validation status](docs/VALIDATION.md)). **Hardware, CPU load and sound comparison with the
original: NOT MEASURED** ([run sheet](docs/HARDWARE_TEST.md)). Deviations from the
original per module: [FIDELITY.md](docs/FIDELITY.md).

## Modular structure

| Building block | Location, component ID, version | Content |
| --- | --- | --- |
| Instrument | [include/streichwurst/](include/streichwurst/), `streichwurst-instrument`, `STREICHWURST_INSTRUMENT_VERSION` | String section (twelve pitch-class clocks, divider chain, event core, leaky integration, envelopes, registration, octaves) and eight soli |
| FX | [effects/streichwurst-fx/](../../effects/streichwurst-fx/README.md), `streichwurst-fx`, `STREICHWURST_FX_VERSION` | Animate, Ensemble (3/5/3 taps), Phaser in the sum branch, Reverb; string stage and global stage separate |
| MIDI/control | [src/register.h](src/register.h), [src/controls.h](src/controls.h), SQ device from `sequencers/step-seq`, Mod on Daisy | one parameter register for kit, MIDI CC, app and matrix |
| Firmware product | [src/](src/), build ID `streichwurst`, `STREICHWURST_VERSION` | Audio graph ([product.h](src/product.h)), memory, board, queues, bootloader entry |
| Control app | [editors/mod-on-daisy](../../editors/mod-on-daisy/README.md) 0.3.0 | Pages Strings/Solo/Keyboard/FX/Global, matrix on the same port, `.swpatch` |

Signal flow (evidence in [FIDELITY.md](docs/FIDELITY.md)): strings → Animate shaping →
Ensemble sum → Phaser → stereo tap correction; soli bypass the string FX; balance;
global slot (Reverb or any other wet stage of FX contract 1.0, e.g. the
Stereo Delay); Ensemble gain word × master; output protection. The product layer is
build-time composition; there is no runtime loader.

## Build and test

```bash
make PROJECT=streichwurst                         # ARM image build/Streichwurst-v0.1.0.bin
make -C effects/streichwurst-fx test               # FX alone
make -C instruments/streichwurst test              # instrument, product, firmware on host fakes
make -C instruments/streichwurst test-sanitize
make -C instruments/streichwurst compare-rates     # 40 kHz + resampler versus 48 kHz
make -C instruments/streichwurst render            # our own test signals to build/render/*.wav
python scripts/check-firmware.py instruments/streichwurst/build/Streichwurst-v0.1.0.bin
make PROJECT=streichwurst manifest                 # product manifest with component versions
```

On Windows, first run `. ./instruments/streichwurst/tools/enter-local.ps1` and use
`HOST_CXX=clang++`. Flashing only by the human ([FLASHING.md](../../docs/FLASHING.md)).

## Controls

Kit: [CONTROLS.md](docs/CONTROLS.md) (three control pages, soft takeover, buttons S1–S16,
S9+S16 5 s = bootloader). MIDI: USB (J7) and DIN, channel adjustable (default omni),
CC plan and all parameters: [PARAMETERS.md](docs/PARAMETERS.md). Mod matrix: eight dedicated
targets, 1 kHz. Control app: Connect reads SQ first, then the matrix; "Matrix"/
"Sound parameters" switches the view.

## User manual (English)

[Streichwurst-Manual.pdf](../../releases/streichwurst-0.1.0-test1/Streichwurst-Manual.pdf),
generated from [docs/print/](docs/print/) (`python build_manual.py && node render.js`;
the parameter tables come from `contracts/parameters.json`).

## Documents

[Work order](docs/CLAUDE_AUFTRAG.md) · [Module contract](docs/MODULAR_ARCHITECTURE.md) ·
[Provenance](docs/PROVENANCE.md) · [Research](docs/research/) · [Sound fidelity](docs/FIDELITY.md) ·
[Parameters](docs/PARAMETERS.md) · [Controls](docs/CONTROLS.md) ·
[Sample rate](docs/RATE_COMPARISON.md) · [Validation status](docs/VALIDATION.md) ·
[Hardware test](docs/HARDWARE_TEST.md) · [Changes](CHANGELOG.md)

Our own code is under the repository's MIT license. Manufacturer programs, original data,
copied tables and raw analysis output do not belong here.
