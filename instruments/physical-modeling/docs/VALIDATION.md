# Validation — Daisy Physical Modeling 0.1.0

Software validation: 2026-10-19. Hardware follow-up: 2026-10-22.
**FUNCTIONAL HARDWARE ACCEPTANCE REPORTED — FOUR-VOICE CPU MEASURED.**
See [hardware results and raw data](HARDWARE_RESULTS_2026-10-22.md).

| Gate | Status | Evidence |
| --- | --- | --- |
| 0 repository/toolchain | PASS | origin/main 03b98f0; pinned dependencies; xPack ARM GCC 13.2.1-1.1; source MIT notices |
| 1 DSP | PASS | actual six-model audio, independent strikes, four slots, spectral pitch and raw finite-output tests; ARM build |
| 2 controls | PASS | production callback + pinned DigitalControl with targeted hardware fake; 16 keys/LEDs, switches, pot endpoints/jitter/ownership |
| 3 reverb/output | PASS | six wet paths, model 6 polarity routing, 30-second impulse tail, maximum parameters, strict output bound |
| 4 MIDI | PASS | notes 0..127, velocity, CC20..33, bend, 300-event FIFO backpressure, parser fragments/running status/recovery |
| 5 integration/CI | PASS | corrected local/CI ARM, host, ASan/UBSan, ThreadSanitizer, release preflight, all-project regressions and downloaded artifact verification |

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
Review corrections (F1/F3 and additional regressions) are documented in
[REVIEW_CORRECTIONS](REVIEW_CORRECTIONS.md). Original baseline 5829b86 and its
134612-byte image were independently reviewed; the corrected image is a new
build, not byte-identical to that baseline.

Current local corrected image: **137784 bytes**. SHA-256:

```text
eb8f412a973a9b58dde92bf63c7c0314d137cb6e929e6f4ad8e3c7daa8ce0fc2
```

Local xPack ARM build, full project builds, corrected production host suite and
ASan/UBSan/float-cast-overflow tests pass. New regular review tests verify
individual chord fundamentals, preservation/steal, sounding pitch bend, peak
bounds, wet-only tail waveform continuity and synthetic pot noise. Production
callback tests cover switch/MIDI ownership and the real CpuLoadMeter with a fake
clock. The optional Python CPU decoder passes three offline protocol tests.

Verified corrected implementation: `03eee48dc5ee409abbf517a98e254fed868332ce`.
[Successful CI run](https://github.com/sscheidl/Daisy-Seed/actions/runs/39322262313),
[verified BIN/ELF/MAP/SHA256SUMS artifact](https://github.com/sscheidl/Daisy-Seed/actions/runs/39322262313/artifacts/12221160675),
[existing PR #13](https://github.com/sscheidl/Daisy-Seed/pull/13).
The downloaded corrected BIN is byte-identical to the local Windows ARM build;
its checksum, embedded version, vectors, ELF load segments and static reserves
were checked again locally. Documentation-only follow-ups do not change this image.

All three CI matrix jobs pass. The corrected run actually executed:

* `make test-release` (release guard regression suite).
* Host and ASan/UBSan tests for Toy Piano, Ambient Reverb and Physical Modeling;
  Physical Modeling also includes float-cast-overflow, review regressions and
  the offline Python decoder suite.
* `make PROJECT=toy-piano test-thread` and
  `make PROJECT=physical-modeling test-thread`; the latter covers both ordered
  MIDI queue payloads and coherent multi-field CPU snapshots with 100000 events.
* ARM builds for all three projects, including `make all-projects`.
* `make test-projects`, image/vector/version and physical ELF/load-region checks.
* BIN, ELF, MAP and SHA256SUMS upload; final artifact download and revalidation.

Existing Toy Piano/Reverb sources and both library gitlinks are unchanged.
Local generated files are under the ignored project `build/`; the downloaded
corrected copy is `build/ci-03eee48/`. Generated files are not committed.

| Region | Used bytes | Capacity | Static reserve |
| --- | ---: | ---: | ---: |
| SRAM load image (code + initial data) | 137784 | 491520 | 353736 |
| SRAM runtime code/read-only data | 134772 | 491520 | 356748 |
| DTCM | 107792 | 131072 | 23280 |
| D2 DMA | 18084 | 32768 | 14684 |
| D2 general | 0 | 262144 | 262144 |
| D3 | 0 | 65536 | 65536 |
| SDRAM | 197740 | 67108864 | 66911124 |
| Backup SRAM | 12 | 4096 | 4084 |

SRAM load and runtime rows describe the same region and must not be added.
The single reverb delay buffer remains 65536 bytes. No application FLASH/QSPI/ITCM
allocation. Static reserves are linker measurements, not stack high-water data. Largest
application/DSP frame in the audio path is now 272 bytes (Synth::Render); the
main-loop MIDI service has a 312-byte frame.

Real callback CPU utilization was measured on 2026-10-22: four voices in all six
models, average readings approximately 33.07–42.69%, highest captured peak 56.71%,
and zero deadline overruns across 174084 captured blocks. The completed run used
maximum reverb Decay and a muted final master; it does not exercise the final
limiter knee or establish parameter/model-transition worst cases. See the
[measurement conditions, averaging method and raw CSV](HARDWARE_RESULTS_2026-10-22.md).
Host clock mocks remain mechanism tests only. The block deadline is 1 ms, and
polyphony remains four; six/eight voices and a modulation matrix are unmeasured.

## Remaining user validation

The user reports successful flashing, click-free model changes with continuing
reverb tails, four-voice polyphony, expected stereo distribution, MIDI/pitch bend
with S1 (Studio One), and functioning knobs with familiar Rings behavior.
The agent subsequently read hardware telemetry with explicit authorization;
flashing was performed by the user.

Manual CPU capture during knob sweeps/model changes, analog output-level
measurements, physical switch orientation as a separately recorded check, full
IRQ budget, stack high-water and stationary-pot noise remain open. Board
revision/codec identity and flashed-image checksum were not independently
verified. Use [the hardware checklist](HARDWARE_TEST.md) for further checks.
Optional NRPN, aftertouch assignment, MPE and DIN MIDI are not part of this
implementation.


**SOFTWARE VALIDATED — FUNCTIONAL HARDWARE ACCEPTANCE REPORTED — FOUR-VOICE CPU MEASURED**
