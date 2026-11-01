# Toy Piano + Ambient: local 32 kHz test build

Prepared on 2026-11-01 from `main` commit
`40b26baad82bb03c0364b2fdf0d5095405bc8bd6` for a user-operated hardware test.
This is a local development package, not a published GitHub release. Only minimal
software checks were requested. The subsequent user-operated hardware test is
recorded below.

## Image and composition

| Item | Value |
| --- | --- |
| Product / image identity | `toy-piano-ambient 0.1.2` / `ToyPianoAmbient-v0.1.2.bin` |
| Toy Piano component | `0.4` |
| Ambient Reverb core / wet-only adapter | `0.4.0` / `0.2.0` |
| Toolchain | xPack GCC `13.2.1-1.1` |
| BIN size | 185,268 bytes |
| BIN SHA-256 | `db784be15a87d5afef8d6834df6f40d618f5ce3f70bb6339fcb7b6353d61f337` |
| Static DTCM / reserve | 119,584 / 11,488 bytes |
| SDRAM | 1,129,688 bytes |

The shared FX sources enforce a **32 kHz maximum wet rate**, reduced
exponentially to 12 kHz by Stretch (VR8). The codec and dry path remain at
48 kHz. Mode-specific pitch-shifter gating is included. Diagnostic memory
variants `AMB_WA`, `AMB_COMPACT` and `AMB_FIR` are all disabled.

The existing product version is retained pending the separate FX and product
acceptance described in [the FX performance report](../../../effects/reverb/docs/PERFORMANCE.md).
The filename alone does not distinguish this build from historical 0.1.2 images;
use the checksum, source commit and `COMPONENTS.json`.

## Local package

Relative to the repository root, the ignored output directory is:

```text
instruments/toy-piano/build/releases/toy-piano-ambient-0.1.2-fx0.4.0-40b26ba/
```

It contains BIN, ELF, map, `COMPONENTS.json`, `SHA256SUMS` and an English README.
An adjacent ZIP bundles these files. These are local artifacts, not committed
binaries. The generated manifest's dirty flag reflects pre-existing untracked
files; tracked sources and the pinned libDaisy/DaisySP submodules were unchanged.

## Minimal validation

**GEMESSEN 2026-11-01, Windows:** all checks below passed.

* ARM build: `make -C instruments/toy-piano AMBIENT=1 -j4`, with the five
  firmware translation units forced to rebuild using make's `-W` option.
* Shared FX `effects/reverb/tests/test_performance.cpp`, compiled with Clang
  using `-std=c++17 -O2 -Wall -Wextra -Werror -pthread`: actual 32–12 kHz core
  work at five codec rates, unchanged dry path, pitch gating/crossfades, shared
  adapter policy and constant-time pitch restart.
* Composed `instruments/toy-piano/tests/test_ambient.cpp`, compiled with the
  same host flags and production Ambient defines: dry reference, keys/LEDs,
  transpose, six modes, stereo tail, MIDI/protocol, echo guard, bootloader
  simulation and no audio-callback allocation. CPU snapshot tests use a fake
  clock and do not measure hardware performance.
* `python scripts/check-firmware.py instruments/toy-piano/build/ambient/ToyPianoAmbient-v0.1.2.bin`
* `python scripts/check-elf-placement.py instruments/toy-piano/build/ambient/ToyPianoAmbient-v0.1.2.elf --sdram fx_storage:1114112`
* `python scripts/component_manifest.py --project toy-piano-ambient --built`

The Torus-specific `check-physical-elf.py` was also attempted, but rejects this
product because it expects `synth_storage` and `lut_sine`. The generic ELF
checker above verifies ARM hard-float ABI, reset entry, load regions, BIN/ELF
identity, SDRAM FX storage and at least 8 KiB static DTCM reserve.

Sanitizer, thread and full release suites were not run, as requested.

At package preparation, hardware testing was **NICHT GEMESSEN**. No device was
flashed by the agent.

## User-reported hardware result

**GEMESSEN 2026-11-01, reported by the user:** Toy Piano with Ambient Reverb runs
stably on the kit (referred to as "Ambient / Slöer Reverb"). The user explicitly reports **no distortion, glitches or
artifacts**. This report follows testing of the package identified above
(`db784be15a87d5afef8d6834df6f40d618f5ce3f70bb6339fcb7b6353d61f337`),
with the shared Ambient Reverb 0.4.0 32–12 kHz wet-clock optimization.

This records the observed stability and audible result of this composed product.

**NICHT GEMESSEN:** numerical CPU load and overrun counts, analog levels, switch
orientation and stack high-water. Test duration, mode-by-mode coverage and the
exact control/MIDI scenario were not specified. This result does not establish
standalone FX or Rings/Torus product acceptance, or a long-duration test.

Controls and hardware test guidance: [AMBIENT.md](AMBIENT.md).
