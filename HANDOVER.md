# Handover — continuing work locally

**Mandatory modularity, user decision 2026-11-01:**
The [development policy](docs/MODULAR_DEVELOPMENT_POLICY.md) and AGENTS.md apply
to all new Daisy developments and substantial extensions: instrument, FX,
MIDI/control, modulation, control app and firmware composition stay separate;
compatible FX are replaceable; modules with their own contracts, versions and tests
evolve independently and are reintegrated after testing. Existing architecture
documents refer to it; no broad migration of existing code has been started.

**Toy Piano on Cumulonimbus, Stage 1, 2026-11-09 (branch `claude/toy-piano-granular-fx`, worktree
`Daisy-Seed-worktrees/toy-piano-granular-fx`, from `origin/main` 241f583; not pushed):** Work order
`L2A/L2A-cases/granular-cross-comparison/CLAUDE_CODE_WORK_ORDER.md`, owner request "Toypiano on
Cumulonimbus". "Cumulonimbus" = the Clouds granular path (eurorack f4cfa76) cross-checked against Nimbus and
Cumuloid ([provenance and audit](effects/cumulonimbus/UPSTREAM.md)). New component
[`effects/cumulonimbus`](effects/cumulonimbus/) 0.1.0 (renderer separate from the live-ring scheduler,
48 kHz kernel or 32 kHz behind a 48↔32 kHz converter, Torus reverb chain, profiling meter, probe), standalone
images F48/F32 and [Toy Piano on Cumulonimbus](instruments/toy-piano/docs/CUMULONIMBUS.md) 0.1.0 (P48/P32
playable, D48/D32 profiling; SW17 pages with pickup, SW18 Freeze/Run/Bypass). Local validation: component and
Piano host tests at both rates, ASan/UBSan, GCC `-Wextra -Werror` compile check, registry/selection tests; all
seven images build and pass `check-firmware.py`; the existing Toy Piano images are byte-identical to 241f583.
TSan not available on this Windows host (Linux only). Package: `build/cumulonimbus-stage1/` in that worktree
([run sheet](effects/cumulonimbus/docs/STAGE1_RUN_SHEET.md)). **Board timing, listening, switch polarity
and flashing: NOT MEASURED.** Next: the joint board session (owner + Codex), flash order P0 → F48/F32 →
D48/D32 → selected P48/P32; then pick rate and grain cap from the measured curve; the SD Grain Sampler
(work order § 5) starts only after that gate. Codex review of the branch is welcome before the session.

**Cumulonimbus 0.1.1 — response to the Codex review of PR #108, 2026-11-11 (commit d4b89be on 0b55f82;
report `L2A/L2A-cases/granular-cross-comparison/review-pr108/REVIEW.md`).** For Codex's cross-check:

| ID | Status | Where | Test |
| --- | --- | --- | --- |
| R1 | Fixed: per-source last-value mailboxes (`RateMailboxes`) with a per-source editor generation and a valid bit replace the queue and the shared stamp; refused CC writes counted (`control_drops`) | `instruments/toy-piano/src/cumulonimbus_host.h` (`RateAudio`, `RateSync`), `main.cpp` | `TestRateSync` (cross-source, stale word once, 100 edits in a row, post-edit pot accepted) |
| R2 | Fixed: CC119=1 → main calls `StageMeter::RequestSnapshot()` directly; CC119=2 → own `Requests::reset` flag; the audio thread resets in the same block (`ResetNow`) | `effects/cumulonimbus/src/diagnostics.h`, `stage_meter.h`, both `main.cpp` | `TestTelemetryInterleaving` (reset before/between/after the audio block, snapshot never pre-reset) |
| R3 | Fixed: starts gated on total active grains (all 64 slots); quality and normalization use the total; refused starts counted once per event / overdue period | `src/clouds/live_scheduler.h` | `TestLoweredCap` (your 12 → cap 8 + trigger case stays 12) |
| R4 | Fixed: fade over the N captured samples (tail 0..N-1, gain (N-1)/N → 0; equals Clouds for N = 256); Freeze during a fade captures afresh | `src/clouds/pcm_ring.h` | `TestShortFreeze` (24-sample Freeze, refreeze during a fade, full tail) |
| R5 | Fixed in two parts. (a) `MOD_ON_DAISY_LOCAL_EDITS` (this product only; other images byte-identical) adds `LoadPatchLocal`: the retry cache survives a pot edit and a retried editor write is answered with the current CRC, so the editor reloads instead of the write executing again. (b) Owner decision 2026-11-11, "Editor polls CRC": the editors (standalone/VST3 core 0.5.1, browser 0.4.1) read the patch CRC with `GET_MATRIX` every 2 s while idle and reload on a difference; no protocol or firmware change | `modules/mod-on-daisy/include/mod_on_daisy/controller.h`, `editors/mod-on-daisy/src/core/Session.h` (`PollCrc`), `web/index.html` (`pollCrc`) | `TestEditorRetryAfterPotEdit`; editor core `TestDevicePolling` (also with lost replies); browser e2e `testDevicePolling` (`mod_sim` command `L`) |
| R6 | Fixed: B0 timing on the D48/D32 dry route; P0 = sound reference/rollback only; logical pot mapping noted; results template updated | `docs/STAGE1_RUN_SHEET.md`, package script | — |
| R7 | Fixed: meter 0.2.0 snapshots carry their own window (audio ms) and the metadata of the same block; set 2 adds control drops, stale rates, meter accounting cost (outside the peak), untouched DTCM stack (painted at boot); probe fails invalid windows (`FAIL: invalid window`, ends `--stop-on-fail`) and any MIDI/control drop | `stage_meter.h`, `board_stack.h`, `tools/cumulonimbus_probe.py` | meter test, `test_cumulonimbus_probe.py` (incl. the C++ encoder bytes) |
| R8 | Fixed: unsupported blocks and an unprepared effect are rejected without touching any buffer | `src/cumulonimbus.h` | `TestContract` (in-place 47-frame block keeps the dry input) |

Scope notes taken over: the long-tail Bypass/resume listening case is in the run sheet; the reverb state
policy and the NaN-at-zero-mix note are in the README. Stack headroom is now reported by the profiling
images (`stack_free`) for the D32 soak decision. Hardware: NOT MEASURED.

**Cross-check request for Codex (owner request 2026-11-11).** Please verify the fixes before the board
session. No pushes, merges, flashing or package changes; report only.

* **Range:** `git diff 0b55f82..fec1cc3` on `claude/toy-piano-granular-fx` (PR #108). `d4b89be` = R1–R4 and
  R6–R8 plus the controller half of R5; `cf94c0c` = this handover; `fec1cc3` = editor CRC polling (R5, owner
  decision "Editor polls CRC").
* **Per finding:** confirm that each original failure no longer reproduces and that the fix has no new edge case.
  Your `review-pr108/repro.cpp` uses the removed `RateSync`/`PackRateReport` stamp API: port it to
  `RateMailboxes`/`RateAudio`/`RateSync` (`instruments/toy-piano/src/cumulonimbus_host.h`) and to
  `HandleControlChange(...)` returning `ControlResult` (`effects/cumulonimbus/src/diagnostics.h`).
* **Points I would like a second opinion on:**
  1. R1: the per-source 10-bit generation and its wrap; the editor mailbox being overwritten before audio
     applies it (only the newest edit is applied, intended); refused stale words counted once per word.
  2. R2/R7: `StageMeter::ResetNow()` inside a callback (the current block opens the new window, missed-period
     detection continues); `window_ms = last − first` and the probe's `expected = window_ms + 1` with the
     tolerance `max(5, window/500)`; snapshot copy of `app_` and the release/acquire hand-off.
  3. R3: occupancy gating over all 64 slots, the quality rule on the total active count, and the changed
     `rejected` semantics (once per refused trigger/random birth, once per overdue regular period). At cap =
     reference, scheduling should equal Clouds'.
  4. R4: the fade over N captured samples (`tail[N-1-counter]`, gain `counter/N`), refreeze during a fade, and
     that N = 256 keeps Clouds' gains.
  5. R5: `MOD_ON_DAISY_LOCAL_EDITS` in `modules/mod-on-daisy/include/mod_on_daisy/controller.h` is compiled
     only into the Cumulonimbus builds. Is rebuilding the cached reply with the current CRC correct for all
     `RepliesWithCrc` commands, and is keeping `SetCalibration`/`SetScenario` cached replies unchanged right?
     Editor polling (`Session::PollCrc`, `web/index.html` `pollCrc`): no poll during edits or pulls, a
     reply ignored once an edit started, silent failure, behaviour in the VST3 inside a DAW, traffic.
  6. R8: rejection without touching buffers, and whether hosts (`WetHost`) can see an uninitialized wet
     buffer if an effect ever rejects (currently unreachable at 48 frames).
  7. Stack measurement (`effects/cumulonimbus/src/board_stack.h`): painting from
     `__reserved_for_stack_end__` to SP − 256 B in `main()` before the audio starts; is anything else
     (heap via `end`, other DTCM users) in that gap with `ld/toy_piano_axi.lds`?
* **Commands (Windows host as used here):** put `.tools/llvm-mingw-*/bin`, `xpack-arm-none-eabi-gcc*/bin` and
  `xpack-windows-build-tools*/bin` on PATH (test EXEs need the llvm-mingw DLLs). From this worktree, pass
  `LIBDAISY_DIR=../../../../Daisy-Seed/libDaisy DAISYSP_DIR=../../../../Daisy-Seed/DaisySP` (the worktree's
  own libDaisy lacks the nested CMSIS submodules), and `HOST_CXX=clang++`.
  * `make -C effects/cumulonimbus test` (`PYTHON=python`), `make -C instruments/toy-piano CUMULO=1 test`,
    and both with ASan/UBSan `HOST_FLAGS` (the toy-piano `test-sanitize` target fails on the unquoted
    `$(MAKE)` path, so pass the flags to `test` directly).
  * `make -C modules/mod-on-daisy test` and `make -C editors/mod-on-daisy test` with
    `HOST_FLAGS=... -D_USE_MATH_DEFINES`; run the `python3` steps with `python`.
  * Browser e2e: `node editors/mod-on-daisy/web/tests/e2e.cjs modules/mod-on-daisy/build/host/mod_sim.exe`
    with `PLAYWRIGHT_MODULE` pointing at a wrapper that launches the installed Edge (`E2E_ONLY=polling`
    for the new test).
  * ARM: all seven images (`make -C effects/cumulonimbus RATE=48|32`,
    `make -C instruments/toy-piano CUMULO=1 CUMULO_RATE=48|32 CUMULO_PROFILE=0|1`) and
    `python scripts/check-firmware.py <bin>`. Confirm that the regular `toy-piano`, `-fv1`, `-filter`,
    `-tape` and `-ambient` images are still byte-identical to 241f583 (the SHA-256 prefixes measured here:
    cb029b94…, 778315f6…, 0f539c8d…, a730cb2a…, 3e8efb3e…).
  * The local logs from my runs are in `.work/logs/`, and the verified package is in `build/cumulonimbus-stage1/`.
* **Result:** please write `review-pr108/CROSSCHECK.md` next to your `REVIEW.md` (one line per R1–R8:
  confirmed / not confirmed / new finding, with evidence), plus any new findings as R9+. Then add a short
  verdict line under this section. If everything is confirmed, the next step is the joint board session with
  the package as staged (images and logs from `d4b89be`; the later commits change docs and editors only).

**Codex cross-check, 2026-11-11 (0b55f82..af53b32):** R2/R3/R4/R6/R8 confirmed; R1/R5/R7 not fully confirmed (new P2 findings R9-R11); P3 WetHost rejection finding R12. Host/sanitizer tests, browser polling and incremental ARM validations pass; package unchanged. Fix R9-R11 before hardware acceptance. Report: `L2A/L2A-cases/granular-cross-comparison/review-pr108/CROSSCHECK.md`. Hardware: NOT MEASURED.

**PR #108 R9-R12 fixed locally, 2026-11-12 (Codex):** Generation-free consumed rate mailboxes;
cached NAKs preserved; drop counters captured at audio reset/snapshot boundaries; WetHost handles
fallible-effect rejection and retains legacy void adapters. Toy Piano/standalone FX firmware 0.1.2,
WetHost 0.1.1; DSP unchanged. Targeted host/ASan/UBSan tests, browser polling, all seven ARM images,
legacy image hashes and composition manifests PASS. Details: [fix/validation record](instruments/toy-piano/docs/PR108_FIXES.md).
The preserved Stage 1 package is unchanged and does not contain these fixes; refresh the package/store
before a board session. No push, merge or flashing. Hardware and TSan: NOT MEASURED.

**Claude independent review requested, 2026-11-12:** [Review order for R9-R12](instruments/toy-piano/docs/CLAUDE_REVIEW_PR108_R9_R12.md). Review the uncommitted fixes above af53b32, report per finding and R13+ findings, and link the result here. Report only; preserve the older package. No commit, push, merge or flashing.

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

**Addendum 2026-11-06 (Nebulae MOD):** New product `granular-mod`
(`NebulaeMod-v0.2.1.bin`): Mod on Daisy on the eight primaries, with Windows SD sharing
unchanged. The regular image stays byte-identical. The matrix ticks on the control timer,
not in the 256-frame callback, so offsets cross to audio through a triple buffer.
[Modulation](instruments/granular/docs/MODULATION.md),
[test sheet](instruments/granular/docs/MOD_HARDWARE_TEST.md). Hardware **NOT MEASURED**.

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
