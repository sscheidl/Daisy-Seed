# Mod on Daisy — Test plan phase 2 (prototype and first measurement)

**As of:** 23 October 2026 · **Status:** phase 2 approved (DECISIONS N1–N6). § 2
is implemented and green, § 3 is built, § 3.2 and § 4 run on the hardware. Results
in [VALIDATION.md](VALIDATION.md).

## 1. Scope

| In phase 2 | Not in phase 2 |
| --- | --- |
| `include/mod_on_daisy/` (ARCHITECTURE § 1) | changes to Toy Piano, Physical Modeling, Nebulae |
| host tests with ASan/UBSan/TSan, CI job | flash write functions (D8) |
| measurement firmware `bench/` for the Desktop Kit | JUCE editor (phase 3) |
| `tools/mod_probe.py`: small command-line script for SysEx tests and measurement runs under Windows | audio rate (D5) |
| measurement report `docs/VALIDATION.md` | VST3 |

Conventions as in the existing projects: `make test`, `make test-sanitize`,
`make test-thread` in the module directory; fakes instead of hardware; fixed seeds for
randomness.

## 2. Host tests

### 2.1 Sources

| Test | Expectation |
| --- | --- |
| LFO frequency at tick size 12, 32, 48, 64, 256 samples, 0.02/1/20 Hz, 600 s simulated | phase after the same number of samples **bit-identical** for all tick sizes; frequency error < 0.05 % (quantization of the 32-bit increment, at 0.02 Hz ≈ 0.024 %) |
| LFO shapes | range −1…+1; sine error against `std::sin` < 0.1 %; triangle and saw continuous at the corners or jumping exactly once per period |
| Rate mapping | `r = 0` → 0.02 Hz, `r = 16383` → 20 Hz, monotonic |
| Retrigger | if the note-on counter changes, the phase starts at 0; without retrigger no change |
| Random stepped | value stays constant between two clocks, lies in [−1, 1], sequence reproducible with a fixed seed |
| Random smooth | continuous: change per tick ≤ 2 · n · f / fs |
| CV/MIDI smoothing | step response with time constant 2 ms or 10 ms ± 5 % at n = 48 and n = 256; converges exactly (snap), no denormals |
| CV dead zone | \|x\| < 0.02 → 0; continuous at the edge of the dead zone; ±1 stays ±1 |

### 2.2 Patch, validation, CRC

| Test | Expectation |
| --- | --- |
| Serialization | fixed 109 bytes, little endian, round trip identical |
| CRC-32 | check value `CRC32("123456789") = 0xCBF43926`; patch CRC the same on the x86-64 host and ARM (test vector in the repository) |
| Validation | depth ±101 → rejected; 9 active cells → rejected; active, disallowed cell → rejected; disabled cell with depth → allowed; name with byte > 127 → rejected |
| Allowed cells | target without a name → whole column locked; poly AT on `latched` → locked; poly AT global depending on the rule last note or locked; source in `unavailable_sources` → row locked |

### 2.3 Engine and matrix

| Test | Expectation |
| --- | --- |
| **Bit-exactly neutral** | empty patch: `Apply(t, base)` returns the same bit pattern as `base` for 10⁵ values `base ∈ [0, 1]`; `ApplyVoice(t, base, p)` likewise for every p |
| Sum and clamping | +50 % and −50 % of the same source on one target give 0; a sum > 1 is clamped to 1 after the sum, not per connection |
| span | depth 100 with span 0.25 and source 1 → offset 0.25; depth 1 → 0.0025 |
| Only used sources | `source_mask` contains exactly the sources of active connections (counter in the test build) |
| Program change | LFO phase continues without a jump on the change |
| Fixed bounds | run time of `Tick()` independent of the content (operation count in the test build): at most 8 sources and `kMaxActive` connections |
| No allocation | `operator new`/`malloc` overridden and counted in the test: 0 calls in `Tick`, `Apply`, `HandleMidi`, `Service` |

### 2.4 Voice path and notes

| Test | Expectation |
| --- | --- |
| Pressure table | note on sets 0; poly pressure only for a held note; note off and velocity 0 set 0; CC123 clears everything |
| Last note | A, B pressed → B; B released → A; 17 notes → list overflows, the oldest drops out, no crash |
| Voice stealing | simulated voice switches from note 60 (pressure 100) to 64 (pressure 0): `VoicePressure` glides to 0 in 10 ms; immediately with `Reset()` |
| Same note twice | two voices on note 72 get the same pressure |
| Poly AT global | poly AT on a global target uses the last-note pressure, on a `per_voice` target only `voice_gain` |

### 2.5 Concurrency (ThreadSanitizer)

* **Triple buffer**: one thread publishes 10⁶ Programs with an embedded checksum, a
  second one reads at tick rate. Required: no data races, never a mixed Program, the
  latest state is taken over.
* **Atomic raw values and pressure table**: the main thread writes note and pressure
  sequences, the audio thread reads. No data races.

### 2.6 Protocol codec

| Test | Expectation |
| --- | --- |
| Round trip of all commands | encode → decode identical; all frames ≤ 120 bytes; `GET_MATRIX` exactly 100 bytes, `SET_MATRIX` 95 bytes |
| `packed`/`mask64` | limit values (−100, 0, +100, all bits) correct |
| Checksum | every single flipped bit in the frame → `NAK 1` or `NAK 2` |
| **libDaisy truncation** | a 130-byte frame, as the parser truncates it to 128 bytes → `NAK 2`, state unchanged |
| Foreign frames | different manufacturer ID or project identifier → `HandleMidi` returns `false`; different device ID → consumed, no reply |
| Fuzzing | 10⁶ random SysEx contents (length 0–140) under ASan/UBSan: no crash, no undefined behavior, patch changed only by valid frames |

### 2.7 Controller

| Test | Expectation |
| --- | --- |
| Repeat with the same `seq` | same reply, change executed only once |
| Limit 8 | 9th `SET_CELL` active → `NAK 7`, patch CRC unchanged |
| CRC confirmation | reply CRC = CRC of the editor model (test model with the same `patch.h`) |
| Send limit | 100 requests in one call → at most 1 reply per `Service()` and ms |
| Monitor | ends 3 s after the last `MONITOR` request; rate ≤ 25 Hz |
| Not implemented | `STORE`, `RECALL`, `SET_POWER_ON` → `NAK 9` |

## 3. Measurement firmware `bench/` (D10)

### 3.1 Structure

* **Location and build**: `modules/mod-on-daisy/bench/`, own Makefile with
  `common/desktop.mk`.
  * Today `desktop.mk` sets `DAISY_ROOT := ../..` for projects two levels below the
    repository. `bench/` is three levels deep. The proposal is a small,
    backwards-compatible change to `DAISY_ROOT ?= ../..`. The alternative would be a
    separate directory `modules/mod-on-daisy-bench/`.
  * Entry `mod-bench` in `projects.mk` and in the CI matrix, i.e. build, firmware check
    and artifact as for the instruments.
* **Content**:
  * Desktop Kit, 48 kHz, block 48. A second build variant uses block 256.
  * A **test instrument**: sine oscillator bank with 4 or 32 voices and 8 targets
    (5 `per_voice`, 3 global). It calls `Apply`/`ApplyVoice` the way a real instrument
    would, and outputs an audible tone.
  * A variant with `kMaxActive = 64` for the load test.
* **Measured quantities**:

| Quantity | Method |
| --- | --- |
| Cycles of the matrix share (`Tick` + all `Apply`/`ApplyVoice`) | DWT `CYCCNT` directly before and after; minimum, mean, maximum |
| Callback load | `daisy::CpuLoadMeter` (as in Physical Modeling), mean and peak |
| Overruns | callback duration > block budget, counted |
| Main loop | maximum duration of `HandleMidi` + `Service`, separate from the send duration `SendMessage` (up to 3 × 100 µs blocking) |
| MIDI losses | gap detection in a numbered test stream from `mod_probe.py`; libDaisy does not itself report FIFO overflows |
| Memory | `arm-none-eabi-size`, MAP file, `nm -C -S` for the object `modulation` (whole module state) and the code symbols `mod::`; `-fstack-usage` (`.su`) for `Tick`, `HandleMidi`, `Service` |

* **Control and readout**:
  * The scenario is selected via `SET_SCENARIO`, the results come via `GET_STATS`. The
    measurement firmware thereby checks SysEx receive and send at the same time.
  * If SysEx sending does not arrive on the computer, there is a fallback via ordinary
    CCs on channel 16, following the pattern of the Physical Modeling telemetry.

### 3.2 Scenarios

| No. | Scenario | Purpose |
| --- | --- | --- |
| S0 | test instrument, matrix not called (bypass) | baseline |
| S1 | module included, **matrix inactive** (empty patch) | base cost |
| S2 | **1 connection**: LFO 1 → global target | few connections |
| S3 | **4 connections**: LFO 1, LFO 2, Random, Modwheel | few connections |
| S4 | **8 connections**, each of the 8 sources once, poly AT on a voice target (Level), all voices read 5 voice targets, 4 voices | **maximum v0.1** |
| S5 | like S4, 32 voices | voice path as in Toy Piano |
| S6 | 64 active cells, 32 voices (variant `kMaxActive = 64`) | load test above the limit (E6) |
| S7 | S4 + editor load: `SET_CELL` 50/s and `MONITOR` 25 Hz | main loop and Program change |
| S8 | S4 + MIDI flood: poly AT 16 notes × 100 Hz, CC1 and channel AT 100 Hz each | MIDI load (E9) |

Each scenario runs at block 48 and block 256, each time for **60 s after `RESET_STATS`**,
three times. `mod_probe.py bench` runs the sequence automatically and writes a CSV file.

### 3.3 Documentation of the results

`docs/VALIDATION.md` names the firmware commit (SHA), toolchain, block size and scenario.
It contains:

* per scenario matrix cycles (min/mean/max and µs), callback load (mean/max in %),
  overruns, main loop maximum duration, discarded MIDI events;
* the memory table (DTCM, SRAM, code, stack per function);
* the **comparison with the estimate** in ARCHITECTURE § 8. For deviations beyond a
  factor of 3 the estimate is corrected and justified.

Measured values are marked `MEASURED`; values that are still missing as
`NOT MEASURED`, as in the existing validation reports.

### 3.4 Target values (acceptance)

| Quantity | Target value |
| --- | --- |
| Matrix share S4, maximum | ≤ 2 000 cycles (≈ 4.2 µs, 0.4 % of the 1 ms block) |
| Matrix share S5, maximum | ≤ 4 000 cycles |
| Matrix share S6, maximum | ≤ 6 000 cycles (information, not acceptance) |
| Overruns | 0 in S1–S8 |
| Module RAM total | ≤ 2 KiB |
| DTCM usage by the module (`modulation`) | ≤ 2 KiB |
| Main loop per `Service` without `SendMessage` | ≤ 50 µs |
| MIDI losses in S8 | 0 |

If target values are missed, no speculative optimization follows. First the cause is
named in the measurement report; then a decision is made.

## 4. Hardware function tests (Desktop Kit, Windows)

| No. | Test | Expectation / record the result |
| --- | --- | --- |
| H1 | USB enumeration | "Daisy Seed External" visible under Windows |
| H2 | SysEx receive | `IDENTIFY` from `mod_probe.py` is answered; frames of 10–120 bytes accepted; 130-byte frame → `NAK 2` |
| H3 | **SysEx send (E9)** | under Windows, replies of lengths 10–120 bytes (including lengths around multiples of 3) arrive **byte-exact** as `F0 … F7`. Check with `mod_probe.py` and a MIDI monitor. The result decides on a separate USB framing in the adapter |
| H4 | Port conflict (E10) | Studio One open with the port active → can `mod_probe.py` open the port? The result feeds into phase 3 |
| H5 | CV inputs (E8) | values with an open jack, 0 V and, if available, ±5 V: zero point, noise (standard deviation), scaling → define dead zone and calibration |
| H6 | Live MIDI | CC1, channel AT and poly AT from a keyboard or Studio One visible in the monitor; poly AT correctly assigned per note |
| H7 | Listening test | test tone with LFO 0.02–20 Hz, modwheel sweep and poly AT without zipper noise at block 48; assess block 256 separately (E7) |
| H8 | Synchronization (N4) | LFO on MIDI clock from Studio One (1/4, 1/8T, 1 bar): runs in tempo, start sets it to the start of the bar, stop holds. LFO on gate 1/2 with pulses of 1 ms, 5 ms and 20 ms length: counts pulses (monitor) – decides on EXTI for gates (E8) |

## 5. Completion of phase 2

Phase 2 is complete when:

1. all host tests are green in CI (release, ASan/UBSan, TSan);
2. `mod-bench` is built and checked in CI;
3. `VALIDATION.md` contains the scenarios S0–S8 and H1–H7 with values or a justified
   `NOT MEASURED`;
4. the results for H3 (SysEx send) and H4 (port conflict) are available, because they
   determine the editor in phase 3.
