# Daisy Seed3 Hardware Diagnostic Suite

Hardware diagnostics for the **Electro-Smith Seed3 Desktop Development Kit**:
C++ firmware and a Windows application (Python/PySide6), communicating over USB CDC
through the carrier's **J7** connector. The GUI and documentation are in English.

> **Implemented and hardware-tested in the scope below:** SD diagnostics,
> Windows SD card reader, both CV channels and both gate inputs. Results are
> real-board measurements, separate from software/simulation tests.

## Control panel 0.3.5

App **0.3.5** (0.3.4 plus coloured CV/gate jack rings) splits the control panel into six categories (USB, Buttons, Pots,
CV / Gate, SD card, Log; plus Simulation in simulation mode), keeps the
firmware's **Buttons → LEDs** behaviour as the default instead of
per-LED checkboxes, and fixes the host findings of the independent milestone review
(stale SD result in saved reports, CV min/max aliasing, stuck SD-test lock).
Firmware stays at 0.3.2. [Change and validation](docs/CONTROL_PANEL_0.3.4.md) (0.3.4 and 0.3.5).
The existing 0.3.2 downloads below remain the published binary baseline.

## Hardware validation

**GEMESSEN — 31 October 2026.** These functional checks passed on the Seed3 Desktop:

| Feature | Implemented | Observed hardware result | Firmware / evidence |
| --- | --- | --- | --- |
| SD filesystem diagnostics | Card detection and read-only FAT32 mount/root-directory test | FR_OK before and after card-reader handover | CardReader 0.3.2; [report](docs/HARDWARE_CARD_READER_0.3.2_2026-10-31.md) |
| Windows SD card reader | Read/write USB MSC on J7 alongside CDC; explicit ownership and eject | Create/read/edit/rename/delete, hashes after eject/remount; one physical card removal during writing stayed responsive, quarantined and recovered after explicit acknowledgement | 0.3.2; [report](docs/HARDWARE_CARD_READER_0.3.2_2026-10-31.md) |
| CV OUT 1 → CV IN 1 | DAC output and normalized/raw ADC input | Fixed codes and slow stepped ramps followed monotonically and repeatably | 0.3.1; [report](docs/HARDWARE_CV_LOOPBACK_2026-10-31.md) |
| CV OUT 2 → CV IN 2 | Same for channel 2 | Fixed codes and slow stepped ramps followed monotonically and repeatably | 0.3.1; [report](docs/HARDWARE_CV2_LOOPBACK_2026-10-31.md) |
| GATE IN 1 | State and rising-edge counter | Held High counted once; 20 more slow pulses gave exactly 21 edges | 0.3.1; [report](docs/HARDWARE_GATE1_LOOPBACK_2026-10-31.md) |
| GATE IN 2 | State and rising-edge counter | Same result; other channel stayed unchanged | 0.3.1; [report](docs/HARDWARE_GATE2_LOOPBACK_2026-10-31.md) |

SD hardware results use the CardReader image; the protected 0.3.2 image has software
validation only. CV/gate results remain pinned to 0.3.1. They are functional loopbacks,
not independently calibrated voltage measurements. Endpoint deviations are recorded.

A later brief **0.3.2 native CV probe** observed 2-s RAMP and SAW periods.
RAMP passed its functional criterion; SAW failed the strict correlation criterion
at two reset samples and requires timing/ADC-pipeline review. Outputs were returned
to Off. [Raw evidence and limits](docs/HARDWARE_NATIVE_CV_2026-10-31.md).

**Open acceptance checks:** absolute/negative CV voltages, SAW timing interpretation and timeout
behavior during blocking SDTEST, gate thresholds/short pulses, large-file/sustained SD
load, other media and additional interruption timings. The known CV OUT shutdown delay
during blocking SDTEST remains open. No further hardware tests are scheduled for now.

Software validation for 0.3.2: **239 Python/GUI tests**, C++ host/MSVC and ASan/UBSan,
both ARM images and linked-symbol policies, packaged EXE startup — all passed.
See [review fixes](docs/PR70_REVIEW_FIXES_2026-10-31.md) and the
[test report](docs/TEST_REPORT.md). Earlier H1–H10 tests passed on 17 October with
0.1.0 (H3 shortened to about three minutes; some operator reports); H11 remains open.

![Diagnostic GUI in simulation mode](docs/images/gui-simulation.png)

## Features

| Area | Firmware | Windows GUI |
| --- | --- | --- |
| 16 buttons S1–S16 | libDaisy debounce, held state, press counts | Two rows of eight, highlighting and counts |
| 16 LEDs D1–D16 | Off / on / individual / chase / follow buttons, brightness | Commanded state and controls |
| 8 knobs VR1–VR8 | Normalized value and raw ADC value | Knobs, percentage, min/max |
| SW17 / SW18 | Two-position / three-position switches; SW18 debounce | Position display |
| microSD | Card detection; optional **read-only** filesystem test | No card / detected / readable / error |
| Optional SD card reader | Separate writable CDC + MSC firmware variant; exclusive card ownership | Explicit share, Windows eject, return or interruption acknowledgement |
| Audio inputs L/R | Input jack detection only; outputs have no detection | Input connector indicators |
| CV IN 1/2, GATE IN 1/2 | Normalized CV and raw ADC; gate state and edge counters (1 kHz) | Values at the jacks, CV / Gate panel with min/max |
| CV OUT 1/2 | Test signals Off / Fixed / Ramp / Saw as DAC codes; off when the session ends | Mode, codes and period |
| USB | DDP v1: handshake, 10–100 Hz state stream, CRC, keepalive | Discovery, timeouts, reconnection |
| Simulation | — | Device model speaking the same protocol |

Audio processing, DSP, MIDI DIN, USB-MIDI, and USB host functions are
marked **Not implemented** or **N/I**. CV and gate values are shown normalized or as
DAC codes; volts are not displayed until they have been measured (H12–H15). Displayed LED states are targets, not optical
or electrical feedback from the LEDs.

## Binary downloads

**Validated 0.3.2 build:** [successful GitHub Actions run](https://github.com/sscheidl/Daisy-Seed/actions/runs/41245983087),
source `b4df35163e9ba7517410cac34437348c7841292f`:

- [Windows EXE](https://github.com/sscheidl/Daisy-Seed/actions/runs/41245983087/artifacts/13058439316)
- [CardReader firmware](https://github.com/sscheidl/Daisy-Seed/actions/runs/41245983087/artifacts/13059542324)
- [Protected read-only firmware](https://github.com/sscheidl/Daisy-Seed/actions/runs/41245983087/artifacts/13059378117)

All three ZIPs include checksums and source information. Both CI firmware hashes
match the local builds; the CardReader hash matches the flashed hardware-test image.
These build artifacts are retained for 14 days, until **14 November 2026**;
they are not permanent Release assets. Documentation-only follow-ups do not change
the validated binary source.

The [Hardware diagnostics workflow](https://github.com/sscheidl/Daisy-Seed/actions/workflows/diagnostics.yml)
builds firmware (`DaisyDiagnostic.bin`, `.elf`) and Windows (`DaisyDiagnostic.exe`).
Open a successful run and download its ZIP files under **Artifacts**; each
includes `SHA256SUMS`. Access to the private Daisy-Seed repository is required. Actions
artifacts expire according to their retention period. Releases are separate,
versioned snapshots and are not created automatically.

Choose `DaisyDiagnostic-readonly-firmware-*` for the strictly protected diagnostic
image (`DaisyDiagnostic.bin`), or `DaisyDiagnostic-cardreader-firmware-*` for
`DaisyDiagnostic-CardReader.bin`. The latter can write card sectors after explicit
sharing. `DaisyDiagnostic-windows-*` contains the application for both variants.
See [card reader instructions and hardware checks](docs/SD_CARD_READER.md).

## Quick start

```bash
git clone https://github.com/sscheidl/Daisy-Seed.git
cd Daisy-Seed
git submodule update --init libDaisy
git -C libDaisy submodule update --init --recursive --jobs 4
cd diagnostics

# Firmware: Windows / Git Bash
bash scripts/install-toolchain-windows.sh
bash scripts/build-firmware.sh
# -> firmware/build/DaisyDiagnostic.bin

# Python application and tests, from diagnostics/
python -m pip install -r windows/requirements.txt
python -m pytest tests/python
```

Start the GUI on Windows with `windows\run.cmd --sim` (simulation) or
`windows\run.cmd` (hardware). Build the EXE with `windows\build_exe.cmd`.
Host firmware tests: `make -C firmware test test-sanitize` (Linux/g++), or
`scripts\run-host-tests-msvc.cmd` (Windows/MSVC).

## Layout

```text
firmware/   C++ firmware, hardware glue, portable application logic
windows/    PySide6 application and EXE packaging
protocol/   DDP specification, C++ framing, shared test vectors
tests/      C++ host tests and Python/protocol/session/GUI tests
scripts/    Windows toolchain installer, builds, binary checks, MSVC tests
docs/       Setup, operation, firmware details, validation, handover
```

## Documentation

* [Installation and usage](docs/INSTALLATION_AND_USAGE.md)
* [Build and dependencies](docs/BUILD.md)
* [Flashing](docs/FLASHING.md)
* [Firmware architecture and limits](docs/FIRMWARE.md)
* [DDP v1 protocol](protocol/PROTOCOL.md)
* [Software and hardware test report](docs/TEST_REPORT.md)
* [Technical handover](docs/HANDOVER.md)
* [Repository migration](docs/MIGRATION.md)

The suite is part of [Daisy-Seed](../README.md). It uses the shared libDaisy
submodule and toolchain, but not DaisySP or any instrument source.
