# Test report

## Native CV quick probe and control-panel candidate, 31.10.2026

RAMP/SAW CV-1 loopback on firmware 0.3.2: RAMP functional criterion passed;
SAW raw trace has a failed correlation assertion and unresolved timing at the
reset. 878 frames without transport errors; OFF confirmed. [Evidence](HARDWARE_NATIVE_CV_2026-10-31.md).
App 0.3.4 (01.11.2026) replaces the 0.3.3 candidate: control panel in six
categories, buttons drive LEDs by default, and the host findings of the independent
milestone review fixed; firmware unchanged, simulator-only validation.
[Change and validation](CONTROL_PANEL_0.3.4.md). The firmware findings of the review
(SD initialisation bound, CV OUT during blocking SD access) remain open.

## Review-Korrekturen 0.3.2, 31.10.2026 — Software

239 Python-/GUI-Tests bestanden; C++-Suites unter Clang normal/ASan/UBSan und
MSVC /W4 /WX bestanden; beide ARM-Varianten samt Vektor-/Symbolprüfung bestanden.
DMA-Suite: 4262 einzelne Prüfungen (einschließlich 4096 Bytevergleichen),
Produktions-Abbruchadapter: 42 Prüfungen; keine Zählung unabhängiger Fehlerszenarien.
**CardReader 0.3.2: GEMESSEN.** Flash, kleine Dateizugriffe, Auswerfen/Remount,
physische Kartenentnahme beim Schreiben ohne Firmware-Hänger, Quarantäne nach
Wiedereinsetzen und expliziter Wiederanlauf bestanden. Windows-Dirty-Markierung
nach CHKDSK /F gelöscht; erneuter Schreib-/Auswerfzyklus bleibt sauber.
[Neues Hardwareprotokoll](HARDWARE_CARD_READER_0.3.2_2026-10-31.md).
Die folgenden CV-/Gate- und älteren Reader-Protokolle gelten weiterhin für 0.3.1.
Abbruchkorrektur, Prüfgrenzen und Binärhashes:
[Korrekturbericht](PR70_REVIEW_FIXES_2026-10-31.md).

## Gate-Loopback, 31.10.2026

**GEMESSEN mit 0.3.1:** CV OUT 1 → GATE IN 1. Gehaltenes High über 2 s zählt
einmal; 20 weitere langsame Impulse ergeben insgesamt exakt 21 steigende Flanken.
Low/High-Zustand korrekt, keine zusätzliche Zählung beim Abfallen oder Halten.
1014 Rahmen ohne Protokollfehler oder Verluste; abschließend OFF und beide
Gates Low bestätigt. Anschließend **GEMESSEN für CV OUT 1 → GATE IN 2**:
ebenfalls exakt 21 steigende Flanken, keine zusätzliche Zählung, 1013 Rahmen
ohne Fehler oder Verlust; abschließend OFF und beide Gates Low.
H13 teilweise gemessen für beide Eingänge; kurze Pulse und elektrische
Schaltschwellen bleiben offen. Aufbau und Grenzen:
[Gate-1-Protokoll](HARDWARE_GATE1_LOOPBACK_2026-10-31.md),
[Gate-2-Protokoll](HARDWARE_GATE2_LOOPBACK_2026-10-31.md).

## CV-Loopback, 31.10.2026

**GEMESSEN mit 0.3.1:** CV OUT 1 → CV IN 1, mehrere feste DAC-Codes und zwei
langsame, vom Host gesteuerte Treppendreieckzyklen. Der Eingang folgt monoton;
Code 2048 ergibt normalisiert etwa 0,50007. Endpunkte: Code 0 etwa +0,0056,
Code 4095 etwa +0,9910. Keine unabhängige Volt-Kalibrierung. 1889 gültige Rahmen
ohne CRC-/Inhaltsfehler, Verlust oder Wiederverbindung. Abschließend OFF bestätigt.
Anschließend **GEMESSEN für CV OUT 2 → CV IN 2**: Code 2048 etwa 0,50053,
Endpunkte etwa +0,0070 und +0,9902; ebenfalls 1889 Rahmen ohne Fehler und
abschließend OFF bestätigt. H15 ist für beide einzeln gepatchten Kanäle gemessen;
Kurze Gate-Pulse und unabhängige Spannungsmessungen bleiben offen. Messwerte und Grenzen:
[CV-1-Protokoll](HARDWARE_CV_LOOPBACK_2026-10-31.md) und
[CV-2-Protokoll](HARDWARE_CV2_LOOPBACK_2026-10-31.md).

## Kartenleser-Hardwaretest, 31.10.2026

**GEMESSEN mit 0.3.0:** CDC/MSC werden unter Windows erkannt, DDP und direkter
FAT32-SD-Lesetest funktionieren. Die Windows-Freigabe wechselt jedoch nach etwa
einer Sekunde von SHARED auf ERROR. Ein lesbares Windows-Laufwerk oder
erfolgreicher Dateischreibtest wurde damit nicht nachgewiesen.

**GEMESSEN mit 0.3.1:** Windows G: (FAT32) zugänglich; Schreiben, Lesen, Ergänzen,
Umbenennen und Löschen eigener kleiner Testdateien erfolgreich. SHA-256 nach
Auswerfen/Rückgabe/erneutem Einbinden unverändert. Auswerfen bei laufender EXE
erfolgreich; CDC bleibt verbunden. Keine Protokollfehler in der Dateioperationsprobe.
Die Korrektur mit Single-Buffer-IDMA und SDRAM-Puffer besteht damit den Grundtest;
große Dateien, Unterbrechung, andere Medien und Langzeitbetrieb bleiben NICHT
GEMESSEN. Vollständiger Aufbau, Binärhash,
Beobachtungen und Grenzen: [Hardwareprotokoll](HARDWARE_CARD_READER_2026-10-31.md).

## Version 0.3.0 CV and gate jacks

Historical software validation for the CV/gate extension. Later CV and gate
loopback results are listed above; independent voltage and short-pulse
hardware tests remain open.
Local results on 25 October 2026 (Windows 11):

| Check | Command | Result |
| --- | --- | --- |
| ARM build and image/SD symbol policy, both variants | `bash scripts/build-firmware.sh` | Passed |
| C++ host tests, MSVC `/W4 /WX` | `scripts\run-host-tests-msvc.cmd` | Passed: core 397, main loop 659, card reader 4631 checks, reader application, USB adapter 53 |
| Same suites with clang `-Wall -Wextra -Werror`, and with ASan + UBSan | `make -C firmware test` / `test-sanitize` with `HOST_CXX=clang++` (llvm-mingw) | Passed |
| Python protocol, session, simulator and GUI tests | `python -m pytest tests/python` | 221 passed |

New host tests cover gate edge counting (held level, one-tick pulse, modulo 256),
CV OUT modes (fixed codes, monotonic saw, triangle without jumps, inverse channel),
`CVOUT` validation including overlong values and the session requirement, CV OUT
switching off after BYE and after the 3 s timeout, gate changes triggering frames,
the maximum ST length, byte-identical firmware/host formatting of the new ST vector,
strict host parsing of the CV/gate block and acceptance of frames without it.

Pending hardware tests (status **NICHT GEMESSEN**):

| # | Test | Status |
| --- | --- | --- |
| H12 | CV IN 1/2 with known voltages (e.g. −5, 0, +5 V if within the jack's range): normalized value, raw ADC, mapping CV IN 1/2 | **Not measured** |
| H13 | GATE IN 1/2: state and edge counter with a gate/trigger source; polarity; short pulses | **Partially GEMESSEN, 31.10.2026:** both gates, held levels and slow pulses; short pulses and threshold remain NICHT GEMESSEN |
| H14 | CV OUT 1/2 with a multimeter/oscilloscope: codes 0, 2048, 4095 in Fixed; Ramp and Saw shape and period; Off after closing the application | **Not measured** |
| H15 | Loop test: patch CV OUT 1/2 → CV IN 1/2, compare codes with read values | **GEMESSEN, 31.10.2026:** both channels separately, fixed codes and host-controlled stepped ramps; no independent voltage calibration |

Do not connect CV OUT to equipment with an unknown input range before H14.

## Version 0.2.0 SD card reader

Historical 0.2.0 validation (superseded for reader hardware status by the dated
0.3.1 results above): this extension had software-only validation. Both ARM variants build with separate
symbol policies. Host tests execute the actual portable ownership controller,
MSC BOT/SCSI implementation, application command handling and composite USB adapter
(with HAL stubs). Python tests exercise the DDP extension, session and GUI. The
simulator models ownership/status only; it does not create a Windows disk or
execute USB MSC. New code has not been flashed and no physical card was written.
Exact verification commands/results and pending physical checks:
[SD_CARD_READER_REVIEW.md](SD_CARD_READER_REVIEW.md).

> Imported software-validation record, supplemented by hardware results on
> 17 October 2026. See [MIGRATION.md](MIGRATION.md) for repository history.

## A. Software and simulation validation

The original software checks ran on Windows 11 Pro 10.0.26300, branch
`fix/codex-review-findings`, with no Seed3 connected. COM3 (VID `17CC`) belonged to
another device and was correctly ignored. Review fixes are documented in
[HANDOVER.md, section 9](HANDOVER.md#9-review-of-6f709ec-findings-and-fixes).

| # | Check | Command / method | Result |
| --- | --- | --- | --- |
| 1 | Clean ARM cross-build | `bash scripts/build-firmware.sh` | Passed; no warnings from project code with `-Wall` |
| 2 | BOOT_SRAM vectors | `make -C firmware check` | Passed: 122,152 bytes, stack `0x20020000`, reset `0x240003f1` |
| 3 | No SD write/erase/lock path at SDMMC, HAL, BSP, or FatFs level | `make check` and nm | Passed; an injected `f_open(..., FA_WRITE)` was detected |
| 4 | Reproducibility | Two clean builds | Identical SHA-256 |
| 5 | C++ core/protocol/main-loop host tests | `scripts\run-host-tests-msvc.cmd`, MSVC 19.51 `/W4 /WX` | 339 + 635 checks, zero failures |
| 6 | Python tests | `python -m pytest tests/python` | Original correction branch: 162; with GUI design tests and English text: 167 |
| 7 | Source GUI simulation startup | `python -m daisy_diag --sim --selftest 3` | Passed, exit 0 |
| 8 | EXE packaging | `windows\build_exe.cmd`, PyInstaller 6.19.0 | Passed; original EXE about 46.9 MB |
| 9 | Packaged simulation startup | `DaisyDiagnostic.exe --sim --selftest 5` | Passed, exit 0 |
| 10 | Hardware-mode startup without a device | `DaisyDiagnostic.exe --selftest 4` | No crash; expected exit 1 because no device data was received |

Original Python environment: Python 3.12.10, PySide6 6.8.3, pytest 9.0.3.
GUI and EXE startup tests use `QT_QPA_PLATFORM=offscreen`. Windows font lookup uses
`QT_QPA_FONTDIR=C:\Windows\Fonts`. CI runs g++ ASan/UBSan as well as ARM and Windows builds.

The English UI revision was checked separately on 17 October 2026: all 167 Python
tests passed, the source simulation started successfully, and the rebuilt EXE
passed `--sim --selftest 5` (exit 0). Startup with an explicitly nonexistent port
and `--selftest 3` exited with the expected code 1. The simulation screenshot was
regenerated and inspected. No hardware was flashed or exercised for this revision.

### Software coverage

| Area | Coverage | Layer |
| --- | --- | --- |
| Protocol | Shared CRC vectors generated independently using `binascii.crc_hqx`; round trips; noise, truncation, CRC/format/overflow errors, CRLF, lowercase CRC; field parsers; byte-identical ST formatting | C++, Python |
| All 16 buttons | Bit positions; press/release through the host stack and GUI; count wrap; short presses between frames; no duplicate count after contact dropout | C++, Python, GUI |
| All 8 knobs | Field positions; 0/25/100% values; GUI drag and min/max; rounding and NaN/out-of-range inputs | C++, Python, GUI |
| Toggle switches | U/D and U/C/D; SW18 rejects disturbances shorter than eight reads | C++, Python, GUI |
| LEDs | Five modes, all individual LEDs, chase positions, brightness, invalid-command atomicity, board clicks | C++, Python, GUI |
| microSD | No card, detection, readable filesystem, FR_NO_FILESYSTEM, removal; simulated six-second blocking test without disconnection | C++, Python, GUI |
| Connection | Unplug/replug, restart, frozen firmware/timeouts, lost first HELLO, foreign devices, port failures, incompatible protocol, BYE, host timeout | Python, GUI |
| Main loop | Actual portable `diag_app` with fake board/USB; busy endpoint, output ownership, overflow/resync, ring index wrap/full state, LED rate limit, SD requests | C++ |

### What software tests alone do not prove

The Python simulator is a **model**, not the firmware. The portable main loop
`diag_app` executes as actual C++ with fake hardware; `firmware/src/main.cpp`
(audio callback, CDC guard, SD driver, LED hardware) is compiled but not executed
by host tests. ASan/UBSan requires g++ and runs in CI. Simulation does not establish
actual USB-driver behavior, ADC noise, physical debounce, or board timing.

## B. Hardware test on the Seed3 Desktop Dev Kit — 17 October 2026

The flashed `DaisyDiagnostic.bin` had SHA-256:

```text
5246380984e199368a30c545b45d1263493903ad02a864bde2f04f17c0d15dc0
```

It is bit-identical to the CI firmware from `8c0522d` and
[release v0.1.0](https://github.com/sscheidl/Daisy-Seed3-Desktop-Diagnostics/releases/tag/v0.1.0).
Testing used J7 on Windows 11 with the Daisy bootloader (`0483:DF11`), reported by
libDaisy as `v6_1` (v6.1 or newer).

**Recorded** means values were captured through the diagnostic protocol.
**Operator test** means checked on the board and in the Windows application and
reported as successful, without per-control measurements.

| # | Test | Result | Evidence |
| --- | --- | --- | --- |
| H1 | J7 flashing, startup, USR LED | **Passed** | Recorded: bootloader `0483:DF11`, then firmware `0483:5740` on COM14; no reboot loop |
| H2 | COM port, discovery, HELLO, bootloader display | **Passed** | Recorded: firmware 0.1.0, protocol 1, BOOT_SRAM, `bl=v6_1`, libDaisy f715e48 |
| H3 | Control/state rate and frame errors | **Passed, shortened** | About three minutes rather than ten: 1000 Hz control, 50 Hz state, zero frame/content/lost-frame/receive errors |
| H4 | S1–S16 mapping, debounce, counts | **Passed** | Operator test |
| H5 | VR1–VR8 mapping/endpoints | **Passed** | VR1 recorded in both directions: 0.0–100.0%, raw 1–65534; only VR1 changed. VR2–VR8: operator test |
| H6 | SW17/SW18 physical/displayed directions | **Passed** | Operator test |
| H7 | D1–D16 modes and brightness, visually | **Passed** | Operator test |
| H8 | microSD detection/read test | **Passed** | Operator test |
| H9 | Left/right audio input jack detection | **Passed** | Operator test; audio outputs have no jack detection |
| H10 | USB unplug/replug and application close/reopen | **Passed** | Operator test; immediate reconnection after port close/open was also recorded |
| H11 | EXE on a second Windows 11 PC without Python | **Open** | Not performed |

After closing the port without BYE, firmware reported 117 dropped frames (`txdrop`).
This is the documented behavior (up to three seconds of continued transmission),
not a failure; see [HANDOVER.md, risk 7](HANDOVER.md#4-known-issues-and-risks).

Still unmeasured: idle knob noise; ten-minute long-term stability; exFAT, cards
without a filesystem, and card removal during a test (H8 subcases unless covered
by the operator test). The English UI update does not add hardware measurements.

Procedure: [HANDOVER.md, section 8](HANDOVER.md#8-hardware-test-procedure).
