# FV-1 interpreter FX component (`fv1-vm`)

**Status: firmware 0.2.0 (component 0.1.0), 2026-11-05, research / feasibility study.**
* Component, standalone firmware `fv1-vm-fx` and the composed `toy-piano-fv1` are built and host-tested.
* Hardware: CPU sweeps over all 240 programs, standalone (`float`/SDRAM) and inside `toy-piano-fv1`, MEASURED
  2026-11-05 without an overrun (see [Hardware results](#hardware-results)); `double`, AXI and sound NOT MEASURED.

## Purpose

Executes a 128-word Spin FV-1 program per sample as an FX component behind the
[FX interface 1.0](../../common/audio/fx_contract.h). It exists to measure on the Seed3 whether FV-1-class effects,
here the FX AID catalogue analysed in the owner's L2A case `fx-aid-catalog`, fit next to an instrument. It also
serves as a reference for later native ports. Composition: Toy Piano with this component as its stereo FX
(owner decision 2026-11-05, modular policy), see [docs/FV1.md](../../instruments/toy-piano/docs/FV1.md).

| Project | Content | Status |
| --- | --- | --- |
| `fv1-vm-fx` | Stereo line input (or internal noise) → FV-1 interpreter at 32 kHz → output, CPU telemetry | Host tests and ARM build passed; hardware NOT MEASURED |
| `toy-piano-fv1` | Toy Piano → variable-rate adapter → FV-1 interpreter at 8–32 kHz (VR8) → output | Host tests and ARM build passed; hardware NOT MEASURED |

## Contract

| Item | Value |
| --- | --- |
| Core | `include/fv1vm/fv1_vm.h`: `Vm<T>`, `T` = `float` (board) or `double` (reference) |
| Startup | `Prepare(Config, Storage<T>&)`; the host provides `Storage<T>`, 32 768 words (128 KiB as `float`) |
| Program | `LoadProgram(const uint32_t words[128])`; `Reset()` clears state and delay RAM (not real-time cheap) |
| Parameters | `SetParameters(Parameters)`: POT0..POT2 in 0..1 |
| Processing | `Process(StereoBlock)`: wet only; bounded (128 instructions per sample); no allocation, locks or I/O |
| Rate | FV-1 programs assume 32 768 Hz. `Vm` runs at the host rate; the standalone firmware uses 32 kHz (`SAI_32KHZ`, 2.3 % slow) |
| 48 kHz hosts, variable rate | `include/fv1vm/fv1_fxvar.h`: `FxVar<T>` runs the program at 8–32 kHz (`SetRate`) between two Kaiser-windowed-sinc fractional resamplers (12 zero crossings, cutoff 0.82 × internal Nyquist); used by `toy-piano-fv1` since 0.5.0 |
| 48 kHz hosts, fixed 32 kHz | `include/fv1vm/fv1_fx48.h`: `Fx48<T>` runs the program at 32 kHz between two 3:2 polyphase FIR resamplers (96 taps, Kaiser β 7, cutoff 14.5 kHz); blocks must be multiples of 3 |
| Test programs | `include/fv1vm/fv1_asm.h` encoders; own demo programs in `src/fv1_firmware.h` (0 thru, 1 echo) |

The interpreter implements the assumptions A1–A7 and A5b of the L2A model (`fv1_model.py` in the case), for example
saturating ACC, PACC as the ACC before the current instruction, WLDR keeping the ramp phase, and LOG(0) → −16.
Delay-RAM compression, MULX truncation and silicon LFO details are **not** modelled.

## Programs and licensing

**This repository contains no FV-1 program data except the two own demo programs.** FX AID programs are used under
the owner's decision of 2026-11-05 (research / feasibility study only; no products, releases or redistribution).
They enter a build only as a local, git-ignored table:

```bash
python effects/fv1-vm/tools/gen_programs.py --label "..." <512-byte program files ...>   # -> effects/fv1-vm/generated/
make -C effects/fv1-vm PROGRAMS=local          # Fv1VmFxLocal-v0.3.0.bin, marker FV1VM-PROGRAMS=LOCAL-NOT-FOR-RELEASE
make -C instruments/toy-piano FV1=1 FV1_PROGRAMS=local   # ToyPianoFv1Local-v0.4.0.bin
```

Local tables and images built from them must never be committed, released or uploaded as CI artifacts. Openly
licensed FV-1 programs (Spin free programs with attribution, Otherworld MIT) may be used for public test material.

## Standalone firmware `fv1-vm-fx`

| Control | Function |
| --- | --- |
| VR1..VR3 | POT0..POT2 |
| S1 / S2 | program −1 / +1 (wraps) |
| S3 / S4 | previous / next group (−10 / +10 without groups) |
| S5 | bypass (input copied to the output) |
| S6 | input: line ↔ internal noise (the LCG of the host runner, amplitude 0.3) |
| LEDs S1..S8 | position in the group (program number without groups), binary, 1-based |
| LEDs S9..S12 | group number, binary, 1-based (off without groups) |
| LEDs S13 / S14 / S15 / S16 | noise input / bypass / double build / a block over budget since the last telemetry reset |
| S9 + S16 for 5 s | Daisy bootloader (shared `common/bootloader`) |

A program change loads the program, clears the delay RAM and mutes that one block; that block is not measured.

**Remote control and telemetry** (USB-MIDI on J7, channel 16): CC22/CC23 program index (high/low 7 bits, 0-based),
CC24 input (0 line, 1 noise), CC25..27 POT0..2 (value/127, held until a panel pot moves by more than 0.03),
CC28 bypass, CC119 = 1 read / 2 reset and read. The reply is twenty channel-16 CCs, CC100..119, protocol id 2
(the physical-modeling decoder ignores it): average and peak cycles per block in 1/10 000 of the block budget,
blocks, overruns, program index and flags. `tools/fv1vm_probe.py` decodes it and can sweep all programs.
toy-piano-fv1 (0.7.0+) follows each reply with a **status packet**, channel-16 CC80..89 (id 1): internal rate in
10 Hz units (CC81-82), VR1 transposition in cents + 2400 (CC83-84), dry/wet % (CC85), bypass (CC86 bit 0),
CC89 = 64. The control page shows it; the probe ignores it.

**Standard MIDI program selection** (both firmwares, any channel): Bank Select MSB (CC0) or, if the MSB is 0,
LSB (CC32) sets *bank*; Program Change *n* then selects a program.
* **With groups** (since firmware 0.3.0 / toy-piano-fv1 0.4.0; table generated with `--groups`): bank = group,
  *n* = position in the group (0-based). A bank or position beyond the table is ignored. The local FX AID table uses
  11 functional groups, alphabetical inside each group (REV, DLY, GRN, MOD, FLT, PIT, SAT, DYN, ENV, UTL, GEN).
* **Without groups** (demo programs, or a table without `--groups`): index *bank* × 128 + *n*, wrapping around.

CC22/CC23 always select the running index over the whole table, in table order.

**Control page** `tools/control/fv1vm_control.html` (Web MIDI, Chrome or Edge): searchable program list with
categories, selection by CC22/CC23 or Program Change, prev/next with ↑/↓, a test chord, live CPU telemetry, and
the standalone-only remote pots, noise input and bypass. It contains no program data; with
`gen_programs.py --names <csv>` it shows the names from the local, git-ignored
`generated/fv1vm_programs.local.js`, otherwise the two demo programs. Open the file directly or serve the
component folder on `127.0.0.1`; other software must not hold the MIDI port at the same time.

```bash
python effects/fv1-vm/tools/gen_programs.py --label "..." --names catalogue.csv <program files ...>  # header + names
python effects/fv1-vm/tools/gen_programs.py --label "..." --names grouped.csv --groups <files in group order ...>
```

Probe usage:

```bash
python effects/fv1-vm/tools/fv1vm_probe.py --list
python effects/fv1-vm/tools/fv1vm_probe.py --input "..." --output "..." --noise --pots 64 64 64 --sweep 240 --seconds 3 --csv fv1-cpu.csv
```

Measurement variants, each with its own build directory and the same image name:

| Variable | Values | Effect |
| --- | --- | --- |
| `PRECISION` | `float` (default), `double` | interpreter sample type |
| `MEMORY` | `sdram` (default), `axi` | delay RAM in SDRAM or in AXI SRAM behind the image (generated linker script from libDaisy's pinned BOOT_SRAM script) |
| `PROGRAMS` | `builtin` (default), `local` | demo programs or the local table |

`PROGRAMS=local PRECISION=double MEMORY=axi` does not fit: the linker stops with "insufficient AXI reserve"
(image 240 KiB + 256 KiB delay RAM > 480 KiB).

## Build and tests

```bash
make -C effects/fv1-vm test                 # interpreter, firmware logic, both 48 kHz adapters, Python tools
make PROJECT=fv1-vm-fx -j4                  # ARM image effects/fv1-vm/build/Fv1VmFx-v0.2.0.bin
make PROJECT=fv1-vm-fx manifest
make PROJECT=toy-piano-fv1 test             # composed end-to-end host test
make PROJECT=toy-piano-fv1 -j4
python3 scripts/check-firmware.py effects/fv1-vm/build/Fv1VmFx-v0.2.0.bin
python3 scripts/check-elf-placement.py effects/fv1-vm/build/Fv1VmFx-v0.2.0.elf --sdram vm_memory:131072
make -C effects/fv1-vm tool                 # host runner for cross-checks with an external reference
```

## Validation (MEASURED 2026-11-05, host and build only)

Windows host, llvm-mingw clang++ 23.1.3 (host), xPack arm-none-eabi-gcc 13.2.1-1.1 (ARM):

* `make test`: 16 interpreter tests, 5 firmware-logic tests, 4 adapter tests and 3 Python tests pass.
* **Cross-check with the L2A reference model** (run locally from the case folder with `make tool`; vendor data never
  enters this repository): 240 programs × 2 pot settings × 6000 samples, identical LCG noise input;
  `double` interpreter vs model **0 failures, worst |Δ| 4.4e-16**; `float` vs `double`: 162 programs below 1e-4,
  40 above 1e-2 (up to 1.87), programs whose noise or branching depends on exact bits.
* **Variable-rate adapter** (thru program, `tests/test_fxvar.cpp`): 32 kHz flat within 0.002 dB to 9 kHz, 20 kHz
  rejected by 88 dB; 8 kHz flat within 0.03 dB to 2.5 kHz, 6 kHz rejected by 97 dB; steady-state residual −87 to
  −98 dB at 8–32 kHz; the demo echo at 16 kHz lasts exactly 3 × 16 383 host samples; a 32 → 8 → 32 kHz sweep shows no
  output step larger than the sine slope.
* **48 kHz adapter** (thru program): gain 1.0002 at 1 kHz, 0.9999 at 10 kHz, 1.0006 at 12 kHz; rejection
  −80.4 dB at 20 kHz and −80.1 dB at 17.5 kHz; the demo echo keeps its 16 383-sample 32 kHz length
  (24 574 samples at 48 kHz); added latency 48 samples (1 ms).
* **Composed `toy-piano-fv1`**: keys → piano → `Fv1Host` → output bit-identical to the separately run components
  over 2500 blocks; VR6 = 0 (VR4 before 0.3.0) and SW17 bypass give the exact dry piano; CC22/23 program change and CC119 telemetry; no allocation
  in the audio callback.
* **ARM builds** (all pass `check-firmware.py` and `check-elf-placement.py`):

  | Image | AXI SRAM | DTCM | SDRAM |
  | --- | --- | --- | --- |
  | `Fv1VmFx` float, SDRAM | 119 184 B | 111 016 B (84.7 %) | 128 KiB |
  | `Fv1VmFx` double, SDRAM | 124 756 B | 112 344 B | 256 KiB |
  | `Fv1VmFx` float, AXI | 250 272 B | 111 016 B | 0 |
  | `Fv1VmFx` double, AXI | 386 912 B | 112 344 B | 0 |
  | `Fv1VmFxLocal` float, SDRAM (240 programs) | 242 088 B | 111 016 B | 128 KiB |
  | `ToyPianoFv1` | 172 632 B | 119 664 B (91.3 %) | 139 684 B |
  | `ToyPianoFv1Local` | 295 536 B | 119 664 B | 139 684 B |

  The standalone interpreter object (decoded program) is in DTCM; in `toy-piano-fv1` the whole host lives in
  SDRAM because DTCM has no room (cache behaviour NOT MEASURED).
* Regular `toy-piano`, `toy-piano-tape` and `toy-piano-ambient` ARM images are byte-identical with and without
  this change (SHA-256 compared).
* Not run: `test-sanitize` and `test-thread` (llvm-mingw on Windows has no sanitizer runtimes); Linux CI does not
  run these tests (no workflow change without the owner's approval); hardware.
* The Toy Piano golden test fails with this Windows host compiler also on unmodified sources (hash pinned for the
  Linux toolchain); it is unaffected by this change.

## Hardware results

**Board run 1 (MEASURED 2026-11-05, owner's Seed3 Desktop Kit):** `Fv1VmFxLocal-v0.1.0.bin` (local table of the
240 FX AID programs, `float`, delay RAM in SDRAM, 32 kHz, block 32) built from 4dab672, flashed with
`make PROGRAMS=local program-dfu`. `fv1vm_probe.py --noise --pots 64 64 64 --sweep 240 --seconds 2`:

| Quantity | Value |
| --- | --- |
| Programs answered | 240 / 240 |
| Overruns | 0 |
| Whole-callback average load | 15.4 % min, 42.6 % median, 64.1 % max of the block budget |
| Peak load | 65.4 % max (a dual pitch shifter); the pitch shifters and dual shimmers lead |
| Cycles per sample (average) | about 2 300 to 9 600 |

Correlation of the average load over the 240 programs: interpolated delay reads r = 0.63, active instruction slots
r = 0.60, delay size r = 0.34. Only one pot setting and one input signal were measured; `double` and AXI delay RAM
are NOT MEASURED.

**Board run 2 (MEASURED 2026-11-05):** `ToyPianoFv1Local-v0.1.0.bin` (same 240 programs inside `toy-piano-fv1`),
`fv1vm_probe.py --notes 4 --sweep 240 --seconds 2` (four MIDI note-ons every 100 ms keep all 16 MIDI voices
sounding; pots at their physical positions):

| Quantity | Value |
| --- | --- |
| Programs answered / overruns | 240 / 0 |
| Whole-callback average load | 42.5 % min, 70.2 % median, 88.6 % max |
| Peak load | 92.7 % max; 5 programs (dual and barberpole pitch shifters) ≥ 90 %, 45 programs ≥ 80 % |
| Extra over the standalone run | 22.5 to 33.4 points (median 27.7): piano, 48 kHz adapter, host state in SDRAM | The owner heard the programs process the noise test signal; no
fault was reported, a listening assessment was not done.

## Next

Board measurements (owner's kit, flashing only after the owner has put it into update mode and asked):
[docs/HARDWARE_TEST.md](docs/HARDWARE_TEST.md).
