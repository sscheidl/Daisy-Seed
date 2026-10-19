# Validation — Daisy Physical Modeling 0.1.0

Date: 2026-10-19. Hardware validation: **PENDING — USER TEST**.

| Gate | Status | Evidence |
| --- | --- | --- |
| 0 repository/toolchain | PASS | origin/main 03b98f0; pinned dependencies; xPack ARM GCC 13.2.1-1.1; source MIT notices |
| 1 DSP | PASS | actual six-model audio, independent strikes, four slots, spectral pitch and raw finite-output tests; ARM build |
| 2 controls | PASS | production callback + pinned DigitalControl with targeted hardware fake; 16 keys/LEDs, switches, pot endpoints/jitter/ownership |
| 3 reverb/output | PASS | six wet paths, model 6 polarity routing, 30-second impulse tail, maximum parameters, strict output bound |
| 4 MIDI | PASS | notes 0..127, velocity, CC20..33, bend, 300-event FIFO backpressure, parser fragments/running status/recovery |
| 5 integration/CI | PASS | ARM, host, ASan/UBSan, ThreadSanitizer, release preflight, all-project builds/tests and downloaded CI BIN/ELF verified |

## Executed software validation

Local Windows: portable LLVM/MinGW 20261006 host compiler and xPack ARM GCC
13.2.1-1.1 (download SHA verified). `make all-projects`, `make test-projects
HOST_CXX=clang++`, `make PROJECT=physical-modeling test`, and the production DSP,
callback and MIDI parser suite with ASan + UBSan + float-cast-overflow and
`-fno-sanitize-recover=all`. Hardware-specific calls are replaced only in callback
tests; synthesis and controls run production code.

Tests cover distinct audible DSP for every model, all local notes (including two
C5 keys), repeated strikes, round-robin steal, simultaneous notes inside one
callback, MIDI 0..127, velocity zero/127, all required CC endpoints, exact bend
endpoints, malformed events, partial MIDI streams, realtime interleaving, running
status, MTC/SysEx recovery, pot takeover and switch bounce. Raw Torus samples are
checked for finite values before final safety protection. Reverb is rendered for
30 seconds at .98 feedback. Spectral Modal checks: A4 440 Hz, octave 880 Hz and
+2-semitone bend approximately 494.19 Hz (1 Hz spectral search resolution).

Corrected failures found during these tests: original LUT endpoint overread;
undefined FM phase conversion and out-of-band carrier at extreme MIDI+transpose;
model 6 reverb-send cancellation; insufficient initial DTCM reserve; and libDaisy
parser uninitialized subtype / MTC subtype leakage. See UPSTREAM and ARCHITECTURE.
No failing conditions were removed to make a broken implementation pass.
The reverb wet-impulse test accounts for the algorithm's actual allpass feedthrough.

## Memory and image

`APP_TYPE=BOOT_SRAM`, little-endian ELF32 ARM executable, hard-float ABI, Thumb
reset entry and valid DTCM initial stack. The application is loaded at 0x24000000;
maximum image 491520 bytes. `check-firmware.py` validates vectors, embedded
version and filename; `check-physical-elf.py` validates actual load segments,
BIN/ELF byte equivalence and at least 8192 bytes of static DTCM reserve.
Verified implementation: commit `ccb8e19b90e6d7db3d9f31a3dd52b60f024fe45a`.
[CI run](https://github.com/sscheidl/Daisy-Seed/actions/runs/39314050298),
[downloaded and verified firmware artifact](https://github.com/sscheidl/Daisy-Seed/actions/runs/39314050298/artifacts/12216784191),
[pull request](https://github.com/sscheidl/Daisy-Seed/pull/13).
Subsequent documentation-only commits do not change the firmware.

Firmware `PhysicalModeling-v0.1.0.bin`: **134612 bytes**.
SHA-256 (identical local Windows and Linux CI binaries):

```text
3ad1ed30f65d4750ee2747b28566c97a53884fa43a1c8fd96f85ff87837d987d
```

The artifact includes BIN, ELF (2361836 bytes with debug information), MAP and
SHA256SUMS. The downloaded ELF and BIN were independently checked locally.
Local generated outputs: `instruments/physical-modeling/build/`; downloaded CI
copy: `instruments/physical-modeling/build/ci-ccb8e19/`.

CI Ubuntu 24.04 actually executed `make test-release`, project host tests,
`make PROJECT=physical-modeling test-sanitize` (ASan/UBSan/float-cast-overflow),
`make PROJECT=physical-modeling test-thread` (100000 concurrent SPSC events under
ThreadSanitizer), ARM cross-compilation, `make all-projects`, `make test-projects`,
version/vector checks, ELF memory validation and artifact upload. The aggregate
checks build and test unchanged Toy Piano v0.2 and Ambient Reverb v0.1 as well.
Their source trees and both library gitlinks have no diff against origin/main.
Local checker regression also accepted all three real firmware images and
rejected six corrupted stack/reset/version/size cases.

Measured static allocations: SDRAM 197736 bytes (DSP 177252 + sine LUT
20484); reverb delay buffer exactly 65536 bytes. DTCM 107632 bytes including
initialized data, hardware/USB objects and library state; static reserve 23440
bytes. D2 DMA 18084/32768; D2 0/262144; backup SRAM 12/4096. SRAM load-image
allocation is reported separately from runtime code and DTCM load data by the ELF
checker. Compiler stack usage reports are generated in `build/*.su`; these are
static frame sizes, not a hardware high-water measurement.

Real CPU utilization: **NOT MEASURED**. Audio callback deadline: 1 ms.

## Remaining user validation

Physical flash, audio listening/levels, physical switch orientation, real CPU/IRQ
budget, stack high-water and Studio One USB enumeration: **PENDING — USER TEST**.
Use HARDWARE_TEST.md. No attached hardware was accessed or flashed. Optional NRPN,
aftertouch assignment, MPE and DIN MIDI are not part of this implementation.


| Region | Used bytes | Capacity | Static reserve |
| --- | ---: | ---: | ---: |
| SRAM load image (code + initial data) | 134612 | 491520 | 356908 |
| SRAM runtime code/read-only data | 131604 | 491520 | 359916 |
| DTCM | 107632 | 131072 | 23440 |
| D2 DMA | 18084 | 32768 | 14684 |
| D2 general | 0 | 262144 | 262144 |
| D3 | 0 | 65536 | 65536 |
| SDRAM | 197736 | 67108864 | 66911128 |
| Backup SRAM | 12 | 4096 | 4084 |

SRAM load and runtime rows describe the same region and must not be added.
No application FLASH/QSPI/ITCM allocation. Largest compiler-reported application/
DSP frame: 328 bytes (Synth::Render); stack/interrupt nesting was not measured.

**SOFTWARE READY — HARDWARE TEST PENDING**
