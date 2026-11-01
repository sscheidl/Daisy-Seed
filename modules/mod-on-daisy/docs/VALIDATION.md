# Mod on Daisy — Validation phase 2

**As of:** 24 October 2026 · module v0.1.0, measurement firmware ModBench v0.1.0
**Environment:** Linux x86-64, GCC (host), xPack ARM GCC 13.2.1-1.1, pinned libDaisy
`f715e48`. Hardware results are added by the owner on the Seed3 Desktop Kit under
Windows.

Marking: `MEASURED` = executed in this state; `NOT MEASURED` = pending.

## 1. Host tests (`MEASURED`)

`make -C modules/mod-on-daisy test`, `test-sanitize` (ASan + UBSan) and `test-thread`
(TSan): **all green**.

| Test group (TEST_PLAN § 2) | Result |
| --- | --- |
| 2.1 Sources | LFO phase at tick 12/32/48/64/256 after 288 000 samples **bit-identical**; 20 Hz → **12 000 cycles in 600 s**; increment error < 0.05 % at 0.02/1/20 Hz; sine approximation max. **0.109 %**; retrigger; S&H 10 Hz → 9–11 changes/s; random reproducible, smooth continuous; smoothing 10 ms ± 5 % at n = 48/256, exact convergence; CV dead zone and calibration |
| 2.1 Synchronization | MIDI clock 120 BPM: period 1000 ± 30 samples detected; start → start of the bar; clock stop → LFO holds. Gate 5 Hz, division 2/1: **89 LFO cycles** and **44 random steps** in 9 s (expected 90/45, deviation due to the measurement starting in the middle of a period) |
| 2.2 Patch | CRC check value `CBF43926`; 1000 random patches round trip; validation (range, limit 8, locked cells, division matching the sync source, 7-bit name); patch test vector `E2340CC6` identical in C++ and Python |
| 2.3 Engine | empty patch bit-exactly neutral for 100 001 values × 8 targets, also after removing a connection; ±50 % cancel exactly; clamping after the sum; span; Program change without phase jump; **0 allocations** in Tick/Apply/HandleMidi/Service over 1000 runs |
| 2.4 Voice path | pressure table, last note with overflow (17 notes), voice stealing (glide 10 ms, `Reset`), same note in two voices, poly AT global → last note |
| 2.5 Concurrency | triple buffer 200 000 publications without mixed or older values; main/audio threads with notes, pressure, CC, clock, SysEx and monitor: **no report from TSan** |
| 2.6 Codec | all commands round trip; `GET_MATRIX` = **100 bytes**, `SET_MATRIX` = **95 bytes**; every flipped bit of a frame detected; 128-byte body truncated by libDaisy → `NAK 2`; wrong version → `NAK 6`; foreign SysEx → to the instrument; **10⁶ fuzz frames** without crash, patch always valid |
| 2.7 Controller | instrument key up to 24 characters complete in `IDENTIFY` (`physical-modeling` = 17, review finding); repeat with the same `seq` executed once, read commands always fresh, local change clears the repeat memory; 9th active cell → `NAK 7`, CRC unchanged; queue 2, ≤ 1 reply/ms; monitor ~20 Hz, ends after 3 s; without a USB host no queue jams; storage commands → `NAK 9` |
| Variant `kMaxActive = 64` | the same tests (without limit check 8) green; scenario S6 with 64 active cells valid |
| Scenarios | S0–S8 valid for the test instrument; S4 uses each source exactly once |

Two bugs were found and fixed before the commit:

* The tests found a reply buffer for state changes that was one byte too small; the CRC
  was cut off.
* During review it was noticed that the repeat memory also answered read commands. A
  newly started client with the same `seq` would have received stale values, such as
  old statistics.

## 1a. Browser editor against the real controller (`MEASURED`)

`make -C modules/mod-on-daisy test-editor`:
* Headless Chromium (Playwright) with a simulated Web MIDI port.
* The frames go to `tools/mod_sim`, i.e. the firmware's controller and engine code on
  the PC.
* Checked with the columns of Toy Piano and Physical Modeling as well as offline.
* After every step the patch CRC of editor and device match: keyboard, dragging, mouse
  wheel, controls, on/off, sources, limit 8, monitor, file save/load.

The test found two bugs in the module, which are fixed:

* **Protocol**: the reply to `MONITOR` (0x30 | 0x40 = 0x70) had the same code as the
  monitor data frames. A client could not tell the confirmation apart from the data;
  `mod_probe.py monitor` would have run into a timeout. The monitor frames now use
  `0x7E`. A test ensures that no reply code collides with them.
* **Sync division**: when switching MIDI clock ↔ gate, the division index stayed the
  same. MIDI 1/4 thus became gate 4/1. Now each sync type starts with its default (1/4
  or 1/1).

The browser codec (`editors/mod-on-daisy/web/mod_protocol.js`) returns the same
reference values as C++ and Python: CRC check value, patch test vector `E2340CC6`,
frame sizes 95/100 bytes. The device starts with the default patch CRC `61C264C4`.

## 1b. JUCE editor against the real controller (`MEASURED`)

* **Core** (`make -C editors/mod-on-daisy test`, also with ASan/UBSan):
  * The core of the JUCE editor runs against the firmware's controller and engine.
  * Checked: connecting, columns, locked cells, editing, limit 8 (also for an enabled
    cell set to 0), sources, name, clearing, file upload with abort, disconnecting
    without late retries, no reply.
  * 100 values while dragging go to the device as 6 `SET_CELL`.
  * 400 random changes with 5 % each of lost requests, lost and corrupted replies (three
    seeds): editor and device end on the same patch CRC.
* **Window** (`--selftest`, Linux under Xvfb): keyboard, dragging, mouse wheel, controls,
  double-click, sources, limit, name and files against the simulator with Toy Piano and
  Physical Modeling columns. After every step the patch CRC matches.
* **Files**: a file saved by the JUCE editor gives the same patch CRC (`506290C2`) in the
  browser editor; a file from the browser editor loads unchanged in the core.
* **Windows** (MSVC): build, core test and self-test run in CI
  (`.github/workflows/editor.yml`).

### On the device: JUCE editor with `PhysicalModelingMod-v0.1.0.bin` (`MEASURED` 23.10.2026)

Observed and reported by the owner under Windows with `ModOnDaisyEditor.exe` (state of
PR #24). The bugs have been reproduced on the host.

| No. | Result |
| --- | --- |
| E1 | passed: connection via "Daisy Seed External", status `physical-modeling 0.1.0-mod · ID 0`, all 8 columns |
| E2–E8 | passed except for the findings below; patch CRC confirmed (e.g. `09EC4D1C` with 7 connections) |
| E9 | Poly AT → Brightness confirmed; Poly AT × Excitation hatched |
| R8 | MIDI clock synchronizes the LFOs |
| other R | `NOT MEASURED` |

Findings:

1. **Context menu** always opened at the same place instead of at the mouse pointer, and
   in the default colors. Fixed: the menu opens at the mouse pointer, in the app's
   colors.
2. **Fast LFOs**: at a high rate the display seemed to get stuck. The cause is aliasing:
   the live display arrives at 20 frames/s, so a 20 Hz LFO is always sampled at the same
   phase. Fixed: above 5 Hz the bar shows a band instead of a seemingly stationary value.
3. **Studio One** opens the Daisy port exclusively.
   * The editor keeps controlling as long as it stays connected, but no longer receives
     replies or live data ("no answer from the device").
   * After disconnecting, reconnecting is not possible as long as Studio One holds the
     port. Once the device was visible again for 1–2 s; the mechanism is unknown.
   * Partially fixed: the message names the probable cause, and the editor shows
     "no live data" instead of frozen bars. The actual solution (H4/E7) is open.
4. **LFO display in MIDI clock mode**, also after switching back to free, stayed off.
   * In the simulator with the firmware code, the values keep running in clock mode and
     afterwards in free mode; on clock stop the LFO holds as intended (N4).
   * The most likely cause is finding 3: Studio One as the clock source holds the input,
     so no live data arrives.
   * In addition, the firmware does not compute sources without an active connection.
     Their bars are now dimmed; the tooltip explains this.
5. **"Load from device"** had no recognizable effect. That is correct as long as nothing
   else has changed the device. Now the editor reports "State loaded from the device
   (patch CRC …)", and the tooltip explains the button.
6. **Column "Reverb"** (reverb time) has hardly any effect. By decision of the owner,
   column 7 is now **Level** (output level 0…2, exactly 1 with an empty matrix); IDENTIFY
   reports `0.1.0-mod2`. Host: Modwheel → Level −100 fades out to silence; the golden
   hash of the empty matrix is unchanged. On the device `NOT MEASURED`.
7. **Pot positions** were not displayed. Depending on the position, the same modulation
   acts differently, because the offset is added to the pot value and clamped to 0…1.
   * Implemented as a backwards-compatible protocol extension: base values at the end of
     the monitor frame (52 instead of 36 bytes), capability bit 4. Enabled only in the
     Physical Modeling build `MOD=1`.
   * Both editors show the pot position as a line and the path of the modulation as a
     bar; `mod_probe.py monitor` prints them as well.
   * Toy Piano v0.3, the regular Physical Modeling build and all ModBench variants are
     byte-identical to the hand-over package.
   * On the device `NOT MEASURED`.

### On the device: `0.1.0-mod3` and JUCE editor from the state of PR #29 (`MEASURED` 24.10.2026)

Locally under Windows 11; commands by the agent, operation and observation by the owner.

| No. | Result |
| --- | --- |
| IDENTIFY (`mod_probe.py`, 19:14) | `physical-modeling 0.1.0-mod3`, capabilities 122 (`0x7A`), schema `942B2D07`; `targets` with Level in column 6; empty matrix, patch CRC `C422B385` |
| USB after flashing | first Device Manager code 10 (`VID_0483 PID_5740`, without name); after unplugging/replugging at J7 "Daisy Seed External", OK |
| E9b | `NOT MEASURED`: the editor was connected to the built-in simulator, not to the kit (simulators since then only with `--simulators`) |
| E7b | **failed** (reported by the owner): with Studio One running, the modulations are not passed through. The older editor version (state of PR #24) worked in parallel operation. **Cause found** (21:45–21:50, Studio One playing, `mod_probe.py` in parallel): Windows MIDI Services lets both programs onto the port, commands arrive and take effect (LFO 1 → Pitch audible). But Studio One sends everything from the Daisy back via input monitoring; the firmware answered every returned reply frame with NAK, which resulted in endless NAK loops (around 500 frames/s, `7F` frames with fixed seq numbers) that crowded out the replies. IDENTIFY mostly still got through, `MONITOR`, `GET_TARGET`, `SET_SOURCE` did not. Fixed in `0.1.0-mod4` (PROTOCOL § 2, step 2a) |

Findings: under Windows, `mod_probe.py` did not find the input and aborted at `matrix` in
the cp1252 console (fixed in `bee9023`). Requests from the test: current value in the
column header and active model in the connection area (implemented in `0.1.0-mod4`, § 1d).

### On the device: `0.1.0-mod4` and JUCE editor from PR #33 (`MEASURED` 24.10.2026, from 21:55)

Image `PhysicalModelingMod-v0.1.0.bin` SHA-256 `9517db27…`, flashed by the owner with the
Daisy Web Programmer. Studio One was running and playing the kit (Daisy as input and
output).

| No. | Result |
| --- | --- |
| `mod_probe.py` in parallel with Studio One (agent) | IDENTIFY 3/3: `0.1.0-mod4`, capabilities 122, capabilities 2 = 1; `modes`: all six models with bank hint; monitor 61 frames in 3 s; base values = pot positions; MIDI clock from Studio One detected; model 3 (FM Voice), origin device |
| E7b | **passed** in the order Studio One first, then the editor (reported by the owner): status green, modulation audible, changes confirmed with patch CRC. Reverse order: `NOT MEASURED` |
| E9b | column values and line follow the pots (reported by the owner). Modwheel → Level −100: `NOT MEASURED` |
| E18 | model changes with SW17/SW18 (reported by the owner). CC28 with "(MIDI)": `NOT MEASURED` |
| E19, E11–E17, R1–R7, R9, R10 | `NOT MEASURED` |

## 1d. Active model and column values (`MEASURED` 24.10.2026, host)

* Firmware `0.1.0-mod4`: capabilities 2 (appended to IDENTIFY), `GET_MODE`, monitor 91
  bytes (PROTOCOL.md § 3.6). Module test `TestModes`, Physical Modeling test `TestModel`
  (switch → String & Reverb, CC28 → FM Voice with origin MIDI).
* Byte-identical to the hand-over (built locally, xPack GCC 13.2.1): Toy Piano v0.3
  `6dd79871…`, regular Physical Modeling `eb8f412a…`, ModBench `9f50a57b…` / `5cdeb0f6…` /
  `7f209c15…`. `PhysicalModelingMod-v0.1.0.bin` (mod4 without feedback protection):
  171 828 bytes.
* Editors: core test `TestModeAndValueLabels`, browser codec test (91-byte frames,
  column values), JUCE self-test under Windows (VS Build Tools 2022) passed.
* Feedback: reply frames are never answered; module test `TestEchoedReplies` sends every
  outgoing frame back: 20 monitor frames/s, no NAK. `PhysicalModelingMod-v0.1.0.bin`
  afterwards 171 684 bytes; the other images still byte-identical.
* Toy Piano v0.4 (`MEASURED` 25.10.2026, host/ARM under Windows): the same rule
  individually with `MOD_ON_DAISY_IGNORE_REPLIES=1`; module test additionally in this
  variant (`test_mod_echo`), Toy Piano test via the USB path: 20 monitor frames/s, no NAK.
  Other images byte-identical to `main` (Physical Modeling mod `9517db27…`). On the
  device `NOT MEASURED`.
* On the device: `NOT MEASURED` (E18, E19, E9b, E7b with mod4).

## 1e. VST3 plug-in (`MEASURED` 24.10.2026, host)

* Built under Windows 11 with VS Build Tools 2022 and JUCE 9.0.4 (VST3 interface from
  JUCE, no separate SDK): `Mod on Daisy.vst3`, manufacturer Daisy-Seed, Fx|Tools.
* `ModOnDaisyPluginCheck` (loads the `.vst3` like a host, without a kit connection): one
  effect, stereo in/out, audio passed through bit-identically, the project state is the
  `.modpatch` text and survives the round trip into a second instance, the window opens
  twice. All checks passed.
* Self-test of the standalone editor: new step "patch loaded offline from the project,
  sent to the device on connecting" passed against the simulator; core test passed.
* In Studio One and on the kit: `NOT MEASURED` (run sheet P1–P5).

## 1c. Correction request after the review (`MEASURED` 24.10.2026, host)

Implemented: compatibility check before loading (F1), port release in all paths
(F2), range display for fast sources and targets with base value and clamping
(F3/F4), connection state (F5), common aftertouch display (owner decision),
regression tests for loading/clearing/setting to zero (F7), plus the LFO
cross-modulation. Firmware side: extended monitor and `fm` behind
`MOD_ON_DAISY_EXTENSIONS`, enabled at run time only in the Physical Modeling build
`MOD=1` (PROTOCOL § 3.6).

| Command | Result |
| --- | --- |
| `make -C modules/mod-on-daisy test` | green, also the variant with extensions (`test_mod_ext`): 89-byte frames, range, clock state, aftertouch, FM (LFO with cross-modulation: 6 → 25 cycles in 10 s) |
| `make -C modules/mod-on-daisy test-sanitize` | green (ASan/UBSan) |
| `make -C modules/mod-on-daisy test-thread` | green, also with extensions; no TSan report (measurement window via atomic window number) |
| `make -C modules/mod-on-daisy test-editor` | green: browser against `mod_sim`, incl. counted `close()` calls (input in use, IDENTIFY timeout, disconnecting), compatibility dialog with `legacy-reverb.modpatch`, connection state, range, aftertouch, FM, setting to zero, clearing, default patch |
| `node editors/mod-on-daisy/web/tests/protocol.test.cjs` | green: format 2 CRC `C422B385` / `9D6E020E` as in C++ and Python, monitor 36/89 bytes, target range, compatibility |
| `make -C editors/mod-on-daisy test`, `test-sanitize` | green: compatibility (old file `E337FC28` → device `942B2D07`, only column Level affected), connection state, regressions § 7, target range 0.1/0.5/0.9 ± 0.5 → 0…0.6 / 0…1 / 0.4…1, aftertouch cases (value 0, first of two notes, note off, All Notes Off, both types, editing stays with the cell), FM |
| JUCE self-test under Xvfb | `PASS`, incl. FM control, range, clock state, connection state, compatibility question, port release after a failed connect |
| `mod_probe.py selftest` | green |
| `make PROJECT=toy-piano test` | green, golden `52580874FA24361F` |
| `make PROJECT=physical-modeling test`, `PROJECT=physical-modeling-mod test` | green, golden `37DE0BF9D93B81CE` in both builds |
| ARM builds (xPack 13.2.1-1.1) | byte-identical: `ToyPiano-v0.3.bin` `6dd79871…`, `PhysicalModeling-v0.1.0.bin` `eb8f412a…`, ModBench `9f50a57b…` / `5cdeb0f6…` / `7f209c15…`. Changed only the separate build `PhysicalModelingMod-v0.1.0.bin` (mod3): 170 312 bytes, `ccabdaa9…`, DTCM 110 352 B (reserve 20 720); `check-firmware.py` and `check-physical-elf.py` passed |

The new regression test found a bug, which is fixed: if a file transfer ran into a
timeout, the device had already taken over part of it, but the editor showed the whole
file. Now the editor reloads the device state as soon as the device sends something
again.

**Limits:** everything above ran against the firmware code on the PC (simulator,
`mod_sim`). On the device `NOT MEASURED`: E7b (both orders), E9b, E11–E17 and the port
release under Windows. The target range adds up the window ranges of the sources; with
several connections it is the reachable range, not the measured one.

## 2. ARM build of the measurement firmware (`MEASURED`)

| Variant | BIN | `mod::Module` (RAM) | `Engine::Tick` code | `Controller::Execute` code |
| --- | ---: | ---: | ---: | ---: |
| Block 48, kMaxActive 8 (`build/ModBench-v0.1.0.bin`) | 138 608 B | **1 936 B** | 3 406 B | 8 628 B |
| Block 256, kMaxActive 8 (`build/b256-max8/`) | 138 624 B | 1 936 B | 3 406 B | 8 628 B |
| Block 48, kMaxActive 64 (`build/b48-max64/`) | 143 448 B | 3 280 B | 3 332 B | 12 504 B |

* `scripts/check-firmware.py`: **PASS** (BOOT_SRAM vectors, embedded version).
* Stack `Engine::Tick` 40 B (`-fstack-usage`).
* DTCM total (default variant) 108 772 B of 131 072 B (83 %). Most of it is used by
  libDaisy (MIDI FIFO about 37 KB, USB); the module contributes 1.9 KB.
* Toy Piano builds unchanged with the modified `common/desktop.mk`
  (`check-firmware.py`: PASS, v0.2).

## 3. Hardware (`NOT MEASURED`)

Instructions: [bench/README.md](../bench/README.md). Enter the results here.

### 3.1 Scenarios (TEST_PLAN § 3.2), block 48

| Scenario | Matrix cycles min/mean/max | Callback load mean/max | Overruns | MIDI lost | Main max (dispatch/service) |
| --- | --- | --- | --- | --- | --- |
| S0 bypass | NOT MEASURED | | | | |
| S1 inactive | NOT MEASURED | | | | |
| S2 1 connection | NOT MEASURED | | | | |
| S3 4 connections | NOT MEASURED | | | | |
| S4 8 connections | NOT MEASURED | | | | |
| S5 S4 + 32 voices | NOT MEASURED | | | | |
| S6 64 cells (variant max64) | NOT MEASURED | | | | |
| S7 S4 + editor load | NOT MEASURED | | | | |
| S8 S4 + MIDI flood | NOT MEASURED | | | | |

The same table is created for block 256 (variant `b256-max8`).

### 3.2 Function tests (TEST_PLAN § 4)

| No. | Test | Result |
| --- | --- | --- |
| H1 | USB enumeration | NOT MEASURED |
| H2 | SysEx receive (`mod_probe.py lengths`) | NOT MEASURED |
| H3 | SysEx send, byte-exact | NOT MEASURED |
| H4 | Port conflict with Studio One | NOT MEASURED |
| H5 | CV zero point, noise, scaling (`mod_probe.py monitor`) | NOT MEASURED |
| H6 | Live MIDI (CC1, Channel AT, Poly AT) | NOT MEASURED |
| H7 | Listening test zipper | NOT MEASURED |
| H8 | MIDI clock and gate sync | NOT MEASURED |
