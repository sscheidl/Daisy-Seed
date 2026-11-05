# FV-1 interpreter FX component (`fv1-vm`)

**Status: phase 1 (host only), version 0.1.0, 2026-11-05.**
* Research / feasibility study.
* No firmware build and no Toy Piano composition yet.
* Hardware, CPU load and sound: **NOT MEASURED**.

## Purpose

Executes a 128-word Spin FV-1 program per sample as an FX component behind the
[FX interface 1.0](../../common/audio/fx_contract.h). It exists to measure on the Seed3 whether FV-1-class effects,
here the FX AID catalogue analysed in the owner's L2A case `fx-aid-catalog`, fit next to an instrument. It also
serves as a reference for later native ports. Planned product composition: Toy Piano with this component as its
stereo FX (owner decision 2026-11-05, modular policy).

## Contract

| Item | Value |
| --- | --- |
| Startup | `Prepare(Config, Storage<T>&)`; the host provides `Storage<T>`, 32 768 words (128 KiB as `float`; SDRAM placement intended) |
| Program | `LoadProgram(const uint32_t words[128])` at startup or on an explicit program change |
| Parameters | `SetParameters(Parameters)`: POT0..POT2 in 0..1 |
| Processing | `Process(StereoBlock)`: wet only; bounded (128 instructions per sample); no allocation, locks or I/O |
| Rate | the program's own rate. FV-1 programs assume 32 768 Hz; run the FX at 32 kHz (`SAI_32KHZ`, about 2.3 % slow) or resample in the host |

## Semantics

The interpreter implements the assumptions A1–A7 and A5b of the L2A model (`fv1_model.py` in the case), for example
saturating ACC, PACC as the ACC before the current instruction, WLDR keeping the ramp phase, and LOG(0) → −16.
Delay-RAM compression, MULX truncation and silicon LFO details are **not** modelled. The template parameter
selects `float` (board) or `double` (reference).

## Programs and licensing

**This component contains no program data.** FX AID programs are used under the owner's decision of 2026-11-05
(research / feasibility study only; no products, releases or redistribution). Their binaries and any generated
program tables stay outside this repository and must not be committed, released or uploaded as CI artifacts.
Openly licensed FV-1 programs (Spin free programs with attribution, Otherworld MIT) may be used for public test
material.

## Validation (MEASURED 2026-11-05, host)

* `make test HOST_CXX=clang++` (llvm-mingw from `.tools/`): 16 hand-encoded instruction tests pass. They cover the
  decoder, gain, delay, saturation, WRHX/WRLX PACC, SKP RUN/NEG, LOG/EXP incl. zero, AND mask, WLDR phase, JAM, RMPA,
  static CHO interpolation, WRAP allpass, MULX and block-contract checks.
* **Cross-check with the L2A reference model** (run locally from the case folder with `make tool`; vendor data never
  enters this repository):
  * 240 programs × 2 pot settings × 6000 samples, identical LCG noise input;
  * `double` interpreter vs model: **0 failures, worst |Δ| 4.4e-16**;
  * `float` vs `double`: 162 programs below 1e-4, but 40 above 1e-2 (up to 1.87). These are programs whose noise or
    branching depends on exact bits (bit-op random generators, clocked/sync logic, random modulation); their
    sample-exact equality is lost in float32, while sound equivalence is not assessed.
* Not run: `test-sanitize` (llvm-mingw on Windows has no ASan/UBSan runtime); ARM build; hardware.

## Next (phase 2)

* Standalone FX firmware: a FV-1 program from a git-ignored generated header, 32 kHz, DWT cycle telemetry per block.
* Toy Piano composition flag; telemetry via the existing CC-packet pattern (`instruments/physical-modeling/src/cpu_telemetry.h`).
* Board measurements: cost of `float` vs `double`, SDRAM vs AXI SRAM placement, worst case over the 240 programs.
  Flashing only after the owner has put the kit into update mode and asked for it.
