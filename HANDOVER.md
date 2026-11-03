# Handover — continuing work locally

**Mandatory modularity, user decision 2026-11-01:**
The [development policy](docs/MODULAR_DEVELOPMENT_POLICY.md) and AGENTS.md apply
to all new Daisy developments and substantial extensions: instrument, FX,
MIDI/control, modulation, control app and firmware composition stay separate;
compatible FX are replaceable; modules with their own contracts, versions and tests
evolve independently and are reintegrated after testing. Existing architecture
documents refer to it; no broad migration of existing code has been started.

**Streichwurst, preparation 2026-11-01:** New instrument under
`instruments/streichwurst/`, its own branch `claude/streichwurst`,
[Claude work order](instruments/streichwurst/docs/CLAUDE_AUFTRAG.md). Target: Seed3
Desktop, operation modeled on the Streichfett, sound as faithful to the original as possible.
Research and our own Python model live with the project; originals and raw data
stay outside Git. Firmware not yet implemented, build ID `streichwurst`
registered as `planned`. Locally its own worktree `.work/streichwurst/`;
the existing main checkout and its unrelated changes are preserved.

**Streichwurst 0.1.0, 2026-11-01 (branch `claude/streichwurst`, PR #77):**
Firmware prototype implemented: instrument (`streichwurst-instrument` 0.1.0),
separate FX (`streichwurst-fx` 0.1.0), product `streichwurst` 0.1.0 with one
parameter register for kit, MIDI CC, SQ 1.1 and the Mod on Daisy matrix; control app
0.3.0 (JUCE, VST3, browser) with Streichwurst pages, matrix on the same port and
`.swpatch`. Host, sanitizer, app and ARM builds passed locally
([validation status](instruments/streichwurst/docs/VALIDATION.md)); hardware, CPU load and
sound comparison NOT MEASURED ([run sheet](instruments/streichwurst/docs/HARDWARE_TEST.md),
[sound fidelity](instruments/streichwurst/docs/FIDELITY.md)).

**Streichwurst clarification:** Instrument = string section and soli; separate
FX component under `effects/streichwurst-fx/`. The firmware connects both
through a shared MIDI/control layer. The user chose to extend the
existing Mod on Daisy app. The [module contract](instruments/streichwurst/docs/MODULAR_ARCHITECTURE.md)
and the Claude work order have been extended accordingly; app integration is mandatory scope.

**Ambient FX 2026-11-01:** The work from PR #68 continues FX-first:
shared core `ambient-reverb 0.4.0`, adapter `0.2.0`, wet clock 32–12 kHz,
pitch shifters only where needed. Standalone FX, Toy Piano and Physical Modeling
use the same FX sources. The sound change is intentional; accept the FX
on the device first, then each instrument separately. Historical measurements from
27/28 October concern the old version. Current state, validation and
acceptance order: [FX performance](effects/reverb/docs/PERFORMANCE.md).

**Diagnostics 2026-10-31:** PR #70 merged, suite 0.3.2. SD card reader with
file operations, physical write interruption and recovery MEASURED;
both CV channels and both gate inputs functional with 0.3.1 MEASURED.
[Status, limits and current EXE/BIN downloads](diagnostics/README.md#hardware-validation).
The further hardware test series was ended at the user's request; open
precision/long-term cases remain documented. Control panel adjustments are
being clarified as the next work item.

**Acid Seq status 2026-10-28:** Phases 0–6 (PR #54–#60) and the follow-up review (PR #61)
are in `main`. Codex did two reviews: the review of the work order
([CODEX_REVIEW.md](docs/acid-seq/CODEX_REVIEW.md), R01–R16, incorporated before
implementation) and the bug hunt across all phases
([CODEX_FEHLERSUCHE.md](docs/acid-seq/CODEX_FEHLERSUCHE.md), state `d3297fd`, verdict
at the time NOT READY: 6 P1, 11 P2, 1 P3). All 18 findings F01–F18 are fixed with
regression tests ([work order § 10](docs/acid-seq/ARBEITSAUFTRAG.md));
Q1–Q4 decided (D16). After the merge to `main` @ `d2a3e1f`, MEASURED 2026-10-27:
ASan/UBSan and TSan PASS for all eleven targets, JUCE 9.0.4 self-test PASS, browser e2e
green, goldens unchanged. There is no repeated Codex review of the fixes;
the verdict in CODEX_FEHLERSUCHE.md refers to the state before the fixes.
Open: hardware tests ([run sheet](sequencers/acid-seq/docs/HARDWARE_TEST.md), all
NOT MEASURED) and the release draft `acid-seq-v0.1.0-test1` (draft, not
published). The user decides both.

**Integration 2026-10-25/26:** Branch `integration/shimmer-bootloader` combines PR #40
(bootloader entry), #38 (modular FX) and #41 (Toy Piano + Ambient with Shimmer).
Test images: `toy-piano-ambient 0.1.2` and `reverb-fx 0.3.1`; component
`bootloader-entry 1.0.0`. Also re-versioned because of the bootloader entry: Toy Piano 0.3.1,
Physical Modeling 0.1.1 (MOD-IDENTIFY `0.1.1-mod4`), PM-FX 0.2.1, Nebulae 0.1.1, Looper 0.3.1,
Mod bench 0.1.1, diagnostics 0.2.1, panels 0.1.1. Test report and run sheet:
[docs/HARDWARE_TEST_INTEGRATION.md](docs/HARDWARE_TEST_INTEGRATION.md). Hardware
**NOT MEASURED**.

**Addendum 2026-10-26 (Tape FX):** New component `tape-fx 0.1.0` in
`effects/tape-fx/` (port of the drumlogue TapeVibe, reference path bit-identical, optional
MixForge stages) with two new build targets: `tape-fx` (stereo effect) and
`toy-piano-tape` (Toy Piano → Tape FX). Existing images byte-identical, Stardust/Looper
unchanged. [Report](effects/tape-fx/docs/REPORT.md),
[listening test run sheet](effects/tape-fx/docs/HARDWARE_TEST.md). Hardware **NOT MEASURED**.

**Addendum 2026-10-25:** `feat/toy-piano-ambient` carries the separate product
`toy-piano-ambient 0.1.1`, which depends on the modular FX foundation in PR #38.
[Operation, build, test results and hardware run sheet](instruments/toy-piano/docs/AMBIENT.md).
Host tests, ASan/UBSan and ARM build passed; Toy Piano v0.3 stays byte-identical.
Six real reverb modes including Shimmer (B / bottom); VR7 controls its
octave amount. Core `ambient-reverb 0.3`, pure FX version: Shimmer on S6.
Combined CPU load and audio test on the kit are **NOT MEASURED**.
The older sections below describe their respective dated state.

**Status:** 24 October 2026, evening (PR #29 merged, mod3 flashed, local session; § 3a).
Before that: correction order implemented in the cloud session. From now on work continues locally on
Windows with Claude Code and Codex, with USB access to the Seed3
Desktop Kit. For a new chat the starter text in § 7 is sufficient.

## 1. Where the code is

| Location | Content |
| --- | --- |
| `main` | PR #22 (module), #23 (Toy Piano v0.3, Physical Modeling `MOD=1`, browser editor), #24 (JUCE editor, handover), #27 (cost-aware CI) |
| `main` since 2026-10-24 evening | plus PR #29: editor fixes from the first hardware test, Physical Modeling mod2 (Level column, pot positions), correction order of 2026-10-24, LFO cross modulation (mod3) and `mod_probe.py` for Windows (`bee9023`) |
| Branch **`feat/editor-no-simulators`** (PR #33) | Physical Modeling **mod4** (active model, protection against the Studio One feedback loop), editor without simulators, column values, model display; tested on the kit with Studio One (§ 3b) |
| Branch **`docs/modular-kernel-idea`** | this handover and the idea [Modular firmware kernel](docs/MODULAR_KERNEL_IDEA.md) |

```bash
git fetch origin
git checkout main && git pull                     # latest state
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
```

**CI** (since PR #27):
* `build.yml` (firmware) and `editor.yml` (JUCE editor with Windows `.exe`) only start
  manually: Actions → workflow → "Run workflow", choose the branch.
* `quick.yml` runs the affected host tests on push and PR.

## 2. What is done

| Part | Status | Where |
| --- | --- | --- |
| Mod on Daisy (module) | Core, host tests, measurement firmware, `mod_probe.py`, protocol 1.0; extension "base values in the monitor" (optional, backward compatible) | `modules/mod-on-daisy/` |
| Toy Piano v0.3 | Matrix integrated; with an empty matrix bit-identical to v0.2; release **draft** v0.3 on GitHub; does not send base values yet | `instruments/toy-piano/` |
| Physical Modeling + matrix | own build `MOD=1`, IDENTIFY `0.1.0-mod3`: column 7 = **Level** (instead of Reverb), reports base values, extended monitor, LFO cross modulation (capabilities `0x7A`); regular build byte-identical | `instruments/physical-modeling/` |
| JUCE editor | Standalone (Windows `.exe` from CI), simulator, self-test, pot positions in the column headers | `editors/mod-on-daisy/src/` |
| Browser editor | Quick variant, same files and displays | `editors/mod-on-daisy/web/` |
| Handover tools | Run sheet, package script, agent rules | [HARDWARE_TEST_RUN.md](modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md), `scripts/hardware_test_package.py`, [AGENTS.md](AGENTS.md) |

Reference values for recognition:

| What | Value |
| --- | --- |
| Golden hash Toy Piano (v0.2 = v0.3 with empty matrix) | `52580874FA24361F` |
| Golden hash Physical Modeling (regular = `MOD=1` with empty matrix) | `37DE0BF9D93B81CE` |
| SHA-256 `ToyPiano-v0.3.bin` (= release draft) | `6dd79871…` |
| SHA-256 `ToyPiano-v0.4.bin` (feedback protection, branch `feat/toy-piano-echo-guard`) | `20a5ea23…` |
| SHA-256 `PhysicalModeling-v0.1.0.bin` (regular) | `eb8f412a…` |
| SHA-256 `ModBench-v0.1.0.bin` / `-block256` / `-max64` | `9f50a57b…` / `5cdeb0f6…` / `7f209c15…` |
| SHA-256 `PhysicalModelingMod-v0.1.0.bin` (mod3, branch) | `ccabdaa9…` |
| Default patch CRC / patch test vector | `61C264C4` / `E2340CC6` |
| the same in format 2 (with LFO cross modulation) | `C422B385` / `9D6E020E` |

## 3. Hardware test: status

Run sheet: [modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md](modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md).
Results: [VALIDATION.md](modules/mod-on-daisy/docs/VALIDATION.md) § 1b "On the device".

**`MEASURED` on 2026-10-23** (Physical Modeling `MOD=1`, state of PR #24, JUCE editor):

| Step | Result |
| --- | --- |
| E1 | passed: connection, all 8 columns |
| E2–E8 | passed except for the findings below |
| E9 | Poly AT → Brightness confirmed, Poly AT × Excitation hatched |
| R8 | MIDI clock synchronizes the LFOs |

**Findings and what became of them** (branch, not yet re-checked on the device):

1. Context menu at a fixed position and in system colors → now opens at the mouse pointer, in
   app colors.
2. Fast LFOs seemed to freeze (20 Hz sampling) → band above 5 Hz.
3. Studio One holds the port exclusively → the message names the cause, display "no
   live data". **Solution open**, see § 4.
4. LFO display in clock mode stayed off → not reproducible with the simulator.
   Probably a consequence of 3. Sources without an active connection are now dimmed.
5. "Load from device" without visible effect → now reports the loaded checksum,
   with tooltip.
6. Reverb column without effect → **Level** (operator's decision).
7. Pot positions were missing → base values in the monitor, line and bar in the editors
   (operator's decision).

**Still open on the device:**
* Re-check Physical Modeling with the branch state (`mod3`): E9b (base value, Level), E7b in
  both orders (Windows MIDI Services), E11–E17 (ranges, aftertouch, FM,
  compatibility question, port release, connection state), R1–R7, R9, R10.
* Toy Piano T1–T15 and E1–E10 with Toy Piano.
* Measurement firmware H1–H8, S0–S8.

Rebuild for the test (built locally on Windows on 2026-10-24: xPack GCC from `.tools/`,
editor with VS Build Tools 2022):
* Firmware: `make PROJECT=physical-modeling-mod` or `python scripts/hardware_test_package.py`.
* Editor: `.exe` via Actions → Editor on the branch, or locally with CMake.

## 3a. Hardware test on the evening of 2026-10-24 (local, Windows 11)

**`MEASURED` on 2026-10-24:**

| What | Result |
| --- | --- |
| Image | `PhysicalModelingMod-v0.1.0.bin` built locally, SHA-256 `ccabdaa9…` (= handover value mod3), flashed with the Daisy Web Programmer |
| USB after flashing | initially Device Manager error code 10 (`VID_0483 PID_5740`, without name/serial number); after unplugging and replugging at J7 cleanly recognized as "Daisy Seed External" |
| IDENTIFY (19:14, `mod_probe.py`) | `physical-modeling`, `0.1.0-mod3`, capabilities 122 (`0x7A`), protocol 1.0, module 0.1.0, schema `942B2D07` |
| `targets` | Pitch, Structure, Brightness, Damping, Position, Excitation (velocity), **Level**, Dry/Wet |
| `matrix` | empty, patch CRC `C422B385` (default patch format 2) |

**Findings:**
* `mod_probe.py` did not find the input on Windows (rtmidi appends a different number per
  direction: "Daisy Seed External 18"/"… 19"), `matrix` aborted in the cp1252 console →
  fixed in `bee9023`, on `main`.
* The JUCE editor was connected to a **simulator** instead of the kit ("toy-piano 0.3",
  then "Simulator: Physical Modeling" with its fixed base values). The simulator reports
  the same IDENTIFY as the firmware and was not recognizable as a simulator → branch
  `feat/editor-no-simulators`. **E9b has therefore not yet run on the kit** (`NOT MEASURED`).

**Collected wishes for the next editor/firmware update** (not yet implemented except 1–2):
1. Simulators only with `--simulators` — branch `feat/editor-no-simulators`.
2. The Daisy port is also suggested when the kit appears only after the editor — same branch.
3. The percentage display in the column header shows the **current value** of the knob position instead of the fixed
   amount "100 %" (amount e.g. small next to it or as a tooltip).
4. **Active model** (SW17/SW18 or CC28) at the top of the connection area, live. Needs firmware
   and protocol (optional monitor field), i.e. a new image.
5. **FX alternatives** to the Torus reverb ("Rings into Clouds": stereo delay, Ambient Reverb,
   Clouds-style granular) — absorbed into the idea [Modular firmware kernel](docs/MODULAR_KERNEL_IDEA.md).

Items 1–4 are implemented in PR #33 (§ 3b).

## 3b. mod4 on the kit, Studio One in parallel (2026-10-24, from 21:55)

* **Cause of E7b found** (agent, `mod_probe.py` in parallel with Studio One): Windows MIDI
  Services lets both programs onto the port; commands took effect, replies were missing. Studio One
  sends everything from the Daisy back via input monitoring; the firmware answered every
  returned reply frame with NAK → endless NAK loops (about 500 frames/s).
  Fixed in mod4: frames with the reply bit are never answered (PROTOCOL § 2, step 2a).
* **`MEASURED`** with mod4 (`9517db27…`): IDENTIFY, `modes` and monitor (61 frames in 3 s) in parallel
  with Studio One. Reported by the operator: **E7b passed** (Studio One first, then editor:
  status green, modulation audible, CRC confirmed), column values follow the pots, model changes
  with SW17/SW18. Details: VALIDATION § 1b.
* `NOT MEASURED`: reverse order (editor first), mod wheel → Level, CC28 "(MIDI)", E19,
  E11–E17, R1–R7, R9, R10.
* **Toy Piano v0.3 does not yet have the feedback protection** (the image is meant to stay byte-identical to the
  release draft) and would have the same problem with Studio One → implemented in
  **Toy Piano v0.4** (2026-10-25, operator approval; branch `feat/toy-piano-echo-guard`):
  initially only the rule from PROTOCOL § 2, step 2a. **After the merge of PR #42
  (2026-10-26)** with all Mod on Daisy extensions like Physical Modeling: base values
  (pot positions), extended monitor, LFO cross modulation; sound with empty matrix
  unchanged. On the kit `NOT MEASURED` (T16/T17 in
  [HARDWARE_TEST_V0.4](instruments/toy-piano/docs/HARDWARE_TEST_V0.4.md)).

**Continue with:** merge PR #33; remaining steps from the run sheet.

## 4. Open decisions and tasks

| Topic | Status | Next step |
| --- | --- | --- |
| Studio One + editor at the same time | **solved** with mod4 (§ 3b): Windows MIDI Services shares the port, the feedback loop is fixed | still check the reverse order; a VST3 version remains desirable but is no longer necessary for this |
| PR #33 (mod4, editor) | tested on the kit, quick checks | merge |
| Feedback protection for Toy Piano | implemented in v0.4 (branch `feat/toy-piano-echo-guard`), host tests and ARM build locally on Windows | check T16/T17 on the kit; Linux golden hash and TSan still open |
| Modular firmware kernel | idea, [MODULAR_KERNEL_IDEA.md](docs/MODULAR_KERNEL_IDEA.md), GitHub milestone | write a detailed plan, later |
| Publish Toy Piano | draft v0.3 waits for T1–T15; v0.4 = v0.3 + feedback protection | the human decides (v0.3 draft or v0.4) |
| Base values for Toy Piano | enabled in v0.4 (full panel like Physical Modeling) | check T18/T19 on the kit |
| License | **decided 2026-10-24**: open source; JUCE editor and VST3 plugin AGPLv3, everything else of our own MIT (README § License) | repo is public (Actions minutes free for it); still check content and history for third-party material |
| Storage (QSPI) | specified only (D8) | needs its own approval |
| Editor polish | open: undo/redo, choice on reconnect, schema cache | after the hardware tests |
| Nebulae as third pilot, VST3 | planned | later |

## 5. Local setup (Windows)

| For | What is needed |
| --- | --- |
| Building firmware | `make` and xPack GCC **13.2.1-1.1** (also for Windows) as in the previous Nebulae build; alternatively WSL with `bash scripts/install-toolchain.sh` |
| Testing only | Images from Actions → **Firmware** (start manually) |
| JUCE editor | `.exe` from Actions → **Editor** or Visual Studio 2022 + CMake ([editor README](editors/mod-on-daisy/README.md)) |
| `mod_probe.py`, package script | Python 3, `pip install mido python-rtmidi` |
| Flashing | Daisy Web Programmer (Chrome/Edge) or `dfu-util` ([docs/FLASHING.md](docs/FLASHING.md)) |

Quick check:

```bash
make PROJECT=toy-piano test
make -C modules/mod-on-daisy test
make -C editors/mod-on-daisy test
python modules/mod-on-daisy/tools/mod_probe.py selftest
```

## 6. Important documents

* Rules for agents: [AGENTS.md](AGENTS.md). Hardware and releases stay with the human;
  results only as observed; golden tests; pinned versions.
* Module: [DECISIONS](modules/mod-on-daisy/docs/DECISIONS.md),
  [ARCHITECTURE](modules/mod-on-daisy/docs/ARCHITECTURE.md),
  [PROTOCOL](modules/mod-on-daisy/docs/PROTOCOL.md) (SysEx 0x7D, monitor 0x7E, base values § 3.3),
  [STORAGE](modules/mod-on-daisy/docs/STORAGE.md), [VALIDATION](modules/mod-on-daisy/docs/VALIDATION.md).
* Instruments: [Toy Piano HARDWARE_TEST_V0.3](instruments/toy-piano/docs/HARDWARE_TEST_V0.3.md),
  [Physical Modeling MODULATION](instruments/physical-modeling/docs/MODULATION.md).
* Editors: [README](editors/mod-on-daisy/README.md) (incl. Studio One / Windows MIDI
  Services), [GUI.md](editors/mod-on-daisy/docs/GUI.md).

## 7. Starter text for a new chat

To copy into a new chat (Claude Code or Codex, locally in the repo folder):

```text
Project: sscheidl/Daisy-Seed, Mod on Daisy (8x8 modulation matrix for Seed3 Desktop),
locally on Windows with USB access to the kit.

Please read first: HANDOVER.md, AGENTS.md, modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md
and modules/mod-on-daisy/docs/VALIDATION.md § 1b.

Status: main contains PR #22–#24, #27 and #29 (Physical Modeling mod3 with LFO cross modulation,
editor fixes, mod_probe for Windows). mod3 is flashed on the kit, IDENTIFY confirmed
(HANDOVER.md § 3a). PR #33 (branch feat/editor-no-simulators): Physical Modeling mod4 with
model display and protection against the Studio One feedback loop, editor without simulators; tested
on the kit with Studio One (§ 3b). Idea for later:
docs/MODULAR_KERNEL_IDEA.md (GitHub milestone "Modularer Firmwarekern").

Next tasks:
1. Merge PR #33 (if still open).
2. With the JUCE editor on the kit, go through the open steps from § 3b and E11–E17 (I listen
   and operate, you run the commands and record the results as MEASURED with the date).
3. Feedback protection for Toy Piano: v0.4, still to be checked on the kit (T16/T17).
4. Then Toy Piano T1–T15 and measurement firmware H/S; plan the VST3 version.

Rules: flash only after my approval in bootloader mode, do not publish
releases, results only as observed, do not accidentally change golden hashes and byte-identical
images (Toy Piano v0.3, regular Physical Modeling, ModBench).
```
