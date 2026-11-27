# Otherworld vol. 1 bank for fv1-vm (`fv1-bank-otherworld`)

**Status: component 0.1.0, 2026-11-22.** Host tests pass (bank build, release match, all 32 programs on
`fv1-vm` 0.2.0). Hardware and listening: NOT MEASURED.

Thirty-two FV-1 programs by Andreas Jansson, [Otherworld vol. 1](https://github.com/andreasjansson/otherworld),
**MIT licence** ([upstream/LICENSE](upstream/LICENSE)). They were written for a Eurorack module's FV-1; here they run
on the [FV-1 interpreter](../../fv1-vm/README.md) inside the FV-1 FX Suite firmware (`make PROJECT=fv1-fx-suite`).
Pinned upstream state and the changes made for assembly: [UPSTREAM.md](UPSTREAM.md).

## Contents

| Path | Content |
| --- | --- |
| `upstream/` | `effects/*.spn`, `bank.json`, `LICENSE`, unchanged copies of the pinned upstream commit |
| `include/fv1bank/otherworld_programs.h` | **Generated**, committed: `kPrograms[32][128]`, ids, names, groups, clock flags, `@render` pots |
| `include/fv1bank/otherworld_version.h` | `SEED_FV1_BANK_OTHERWORLD_VERSION` |
| `otherworld_programs.js` | **Generated**: program list for the control page (`fv1vm_control.html?bank=otherworld`) |
| `tools/build_bank.py` | Generator; uses the vendored [asfv1](../third_party/asfv1/README.md) |
| `tests/` | `test_bank.py` (fresh build, release checksums, layout), `test_bank_vm.cpp` (every program on fv1-vm) |

```bash
make -C effects/fv1-banks/otherworld bank   # regenerate after an upstream or generator change
make -C effects/fv1-banks/otherworld test   # needs python3 and a host C++17 compiler
```

## Programs and MIDI banks

Upstream slot order, grouped by category. MIDI Bank Select = group, Program Change = position (0-based).

| Bank | Group | Programs (slot) |
| --- | --- | --- |
| 0 | Slicer (17) | Prism Scatter 1, Prism Scatter (clocked) 2, Grain Bloom 3, Grain Bloom (clocked) 4, Zeno's Arrow 5, Zeno's Arrow (clocked) 6, Quantum Tunneling 7, Quantum Tunneling (clocked) 8, Turtle Grammar 9, Turtle Grammar (clocked) 10, Step Mutator 11, Step Mutator (clocked) 12, Tesseract 13, Tesseract (clocked) 14, Mitosis 15, Noocytes 16, Brownian Dust 17 |
| 1 | Reverb (4) | Strata 18, Fog Bank 19, Comet Harp 20, Ventifact 21 |
| 2 | Distortion (4) | Avalanche 22, Numbers Station 23, Numbers Station (clocked) 24, Pitch-Locked Decimator 25 |
| 3 | Voice (7) | Transmutation 26, Wavetable Thief 27, Two Whales 28, Bellfounder 29, Phase Ghost 30, Talking Chip 31, Moonquake (clocked) 32 |

Each program has three knobs (POT0..2); their names are in the generated list and in each source header.
**Clocked programs** (`kClocked`) take mono audio on input L and a square clock on input **R**; their output is
stereo. The suite firmware passes the line inputs straight through, so the clock has to be patched into R.

## Differences from the original module

* **Sample rate:** the original module clocks its FV-1 at 32 774 Hz; `fv1-fx-suite` runs at 32 000 Hz, so fixed
  times, LFOs and fixed tunings are about 2.4 % slower and lower. Pitches that a program tracks from the input stay
  consistent.
* **Arithmetic:** fv1-vm computes in `float`; it does not model the chip's delay-RAM word format, MULX truncation or
  LOG/EXP grids. A host experiment with those details (2026-11-22, not committed) changed no program's liveness;
  whether they are audible is NOT MEASURED.
* **Input noise:** Moonquake draws its random steps from the lowest input bits, so it needs the ADC's own noise. The
  host test adds about -90 dBFS; on the kit the codec supplies it (NOT MEASURED).
