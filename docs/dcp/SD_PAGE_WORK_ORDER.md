# Work order: Daisy Control Panel 0.5.0, modulation and SD card in one app

Prepared 2026-11-07 by Claude for a new session (planned model: Fable). Owner decisions are
fixed in section 2. Work through the phases in order **without asking**, unless a HOLD
criterion applies. At the end there is a runnable product: an app, a test package and a test
sheet. The owner then runs the hardware test.

## 1. Goal and definition of done

Today an instrument with Mod on Daisy and SD card sharing needs **two programs**:

* the **Mod on Daisy editor 0.4.0** (`editors/mod-on-daisy`, JUCE 9.0.4) for the matrix;
* **Daisy Diagnostic 0.6.0** (`diagnostics/windows`, Python/PySide6) for its **SD share** tab.

Only one of them can open the kit's USB port J7, so the Nebulae MOD test package switches
between them with a launcher, and the owner saves and reloads the volatile patch by hand.

**Done** means all of the following:

* **App:** the standalone app **Daisy Control Panel 0.5.0** (DCP step 1) has two pages:
  * the matrix, with the editor's existing function;
  * an **SD card** page that shares the kit's card with Windows and brings it back. The app
    follows the kit through both USB modes and saves and restores the patch by itself.
* **Firmware covered:** it works with Nebulae MOD 0.2.1, Looper MOD 0.6.1, and plain Nebulae
  0.2.1 and Looper 0.6.1 (those show only the SD card page).
* **Evidence:**
  * every PASS criterion below is met;
  * the build sits in `apps/`;
  * a test package and a test sheet are ready;
  * a PR is open, not merged.
* **Hardware status:** NOT MEASURED until the owner reports results.

Naming: in this work "control panel" means only the new **Daisy Control Panel**. The Python
program is called **Daisy Diagnostic**. Its EXE keeps the name `DaisyDiagnostic.exe` and stays
unchanged as the diagnostics tool.

## 2. Owner decisions (2026-11-07, fixed)

| # | Decision |
| --- | --- |
| D1 | **Base: branch `codex/nebulae-mod-sd`** (PR #102, open, Nebulae MOD only exists there). New branch `claude/dcp-sd-page` from it. The PR targets `main` and says it depends on #102; after #102 merges, rebase onto `main`. Do not push to Codex's branch. |
| D2 | **SD card page in the standalone app only.** The VST3 keeps the matrix unchanged and shows on the SD page: "Sharing the SD card needs the Daisy Control Panel app (the kit restarts, and the DAW loses the Daisy MIDI port)". |
| D3 | **Name now: "Daisy Control Panel"** for the standalone app (window title, EXE name `Daisy Control Panel.exe`, about box). The VST3 keeps its name, plugin and manufacturer codes and bundle name `Mod on Daisy.vst3`, so existing Studio One projects keep working. Internal names (module "Mod on Daisy", SysEx `0x7D`, CMake target names) may stay. |
| D4 | **The SD card page is offered for instruments without a matrix too** (plain Nebulae 0.2.1, Looper 0.6.1). |

Defaults set by Claude (change only with the owner):

| # | Default |
| --- | --- |
| D5 | Before SHARE the app saves the current patch automatically (`.modpatch`, under `%LOCALAPPDATA%\Daisy Control Panel\autosave\`) and sends it back after RETURN; the user is told. Manual Save/Open stays. |
| D6 | No firmware change, no protocol change, no golden-hash change. Version: CMake project 0.5.0 (continues the editor line), `web/version.js` only if the browser is touched. App ID in `apps/`: `daisy-control-panel`. |
| D7 | Independent review in phase 7 by a fresh subagent that gets the code, not the conclusions. A later Codex review is the owner's call. |

## 3. Facts to start from (verify; they date from 2026-11-07)

**Two USB personalities of the kit** (`diagnostics/docs/NEBULAE_PROFILE.md`,
`diagnostics/docs/LOOPER_PROFILE.md`):

| Personality | USB | Found by |
| --- | --- | --- |
| Performance | USB-MIDI only, no COM port | STATUS SysEx answered within 1 s on an output named "Daisy" (Nebulae `NB`, Looper `F0 7D 4C 50 01 01 F7`; each firmware ignores the other's request) |
| SD management | USB CDC (DDP v1) + mass storage | COM-port session, HELLO `app=NebulaeOnDaisy` / `LooperOnDaisy`, capability `nebmgmt` / `loopmgmt` |

The workflow runs in five steps:

1. SHARE SysEx over MIDI, answered with CONFIRM_REQUIRED if a Live loop would be lost.
2. The kit restarts into SD management.
3. `READER a=SHARE` goes over the COM port, and Windows sees a drive.
4. The user ejects the drive in Windows.
5. `READER a=RETURN` and then `NEB a=PERFORM` (or the looper equivalent); the kit restarts
   and answers over MIDI again.

The panel's rules:

* It shows "ejected" only for the firmware state EJECTED.
* A timeout means "ownership unknown".
* INTERRUPTED and ERROR need an explicit acknowledge.
* Closing the app, a reconnect or BYE never returns or reshares the card.
* Timeouts: STATUS 3 s, SHARE reply 2 s, restart 20 s.
* Risk R1: the kit restarts into the wrong personality.

Protocols: `instruments/granular/docs/CONTROL_PROTOCOL.md` and
`instruments/looper/docs/SD_MANAGEMENT.md`.

**Python reference** (Daisy Diagnostic 0.6.0, `diagnostics/windows/daisy_diag/`):

* `nebulae.py` (658 lines): SysEx codec and workflow;
* `looper.py` (119);
* `session.py` (565): DDP session;
* `protocol.py` (697): DDP v1;
* `gui/main_window.py`: SD share tab, texts and buttons;
* simulators `nebulae_sim.py`, `looper_sim.py`;
* tests in `diagnostics/tests/python/` (`test_nebulae.py`, `test_gui_nebulae.py`, looper tests).

The simulator mode is `--sim-nebulae` / `--sim-looper`.

**Editor 0.4.0** (`editors/mod-on-daisy/src/`):

* `core/Session.h`: Mod on Daisy session;
* `core/SqSession.h`: SQ 1.1;
* `core/Bootloader.h`;
* `core/PatchFile.h`: `.modpatch`;
* `core/Simulator.h`, `SimInstruments.h`;
* `ui/MidiPort.h`: JUCE MIDI;
* `ui/EditorComponent.cpp`;
* `app/Main.cpp`;
* `plugin/Plugin.cpp`: VST3;
* `web/`: browser editor;
* `docs/GUI.md`.

It has **no serial/COM code** and no SD logic. On the base branch, the firmware itself describes
the Nebulae MOD columns (Start, Speed, Size, Density, Pitch, Overlap, Blend, Window).

**DCP direction** (`docs/dcp/MIDI_ARCHITECTURE.md`, section 8; on branch `dcp`, commit `10f906f`):

* SQ is the single protocol.
* One communication core for standalone and VST3 (PortHub; **not implemented**, not part of
  this work).
* No second USB-MIDI interface, no Windows MIDI Services, no own drivers.
* No blocking MIDI calls on the audio thread.
* Firmware protocols stay unchanged.

Bring `docs/dcp/*` from `dcp` into the new branch (cherry-pick `10f906f`) together with this
work order.

**Builds and store** (`docs/FIRMWARE_BINARIES.md`; section 8 for `apps/` is in PR #105, open,
branch `claude/app-store-rules`): the images needed are in `firmware/` of the main checkout:

* `granular-mod/0.2.1+85f8a13-dirty`
* `granular/0.2.1+da5f574`
* `looper/0.6.1+unknown`
* `looper-mod/0.6.1+unknown`

The current apps:

* `apps/mod-editor/0.4.0+85f8a13/`
* `apps/daisy-diagnostic/0.6.0+unknown/` (only copy of the tested 0.6.0 EXE, SHA-256 `39510a84…`)

**Windows build environment**: memory note "Windows local build recipes". In short:

* Toolchains are in `<main checkout>/.tools`.
* JUCE is in `.tools/juce-9.0.4/src` (`-DMOD_EDITOR_JUCE_DIR=...`).
* Configure CMake with `-G "Visual Studio 18 2026" -A x64` inside the repo, not under the
  scratchpad (MAX_PATH).
* The self-test needs `MOD_ON_DAISY_NO_AUTOCONNECT=1`.
* llvm-mingw `bin` must be on PATH for host test EXEs.
* For the browser e2e, use the Edge wrapper (`PLAYWRIGHT_MODULE`).
* Delete stale JUCE `.rc` files after a version bump.

## 4. Rules for the whole job

* `AGENTS.md`, `docs/MODULAR_DEVELOPMENT_POLICY.md` and English first (code, GUI texts, docs,
  commits, PR) apply.
* **Card safety comes first.** The C++ port keeps every ownership rule of the Python reference
  exactly. A wrong state can destroy the owner's samples or loops.
* Real-time: no blocking call on the JUCE message thread or the audio thread. Serial and MIDI
  I/O run on their own threads with bounded queues.
* Worktree:
  * Create it with `git worktree add` under `Playground/Daisy-Seed-worktrees/` (short path).
  * Link `libDaisy`, `DaisySP` and `.tools` to the main checkout only as junctions.
  * Never `git worktree remove --force` while junctions exist; remove the links first with
    `[System.IO.Directory]::Delete(path, $false)`.
  * Check ignored files before any removal.
* Hardware belongs to the owner: never flash, never `program-dfu` or `program-boot`.
* CI: no manual workflow dispatch. Only the automatic `quick.yml` runs on the PR.
* Commit in small steps on `claude/dcp-sd-page`. Push and open the PR in phase 7.
* Report only what was observed: MEASURED with date, otherwise NOT MEASURED.

## 5. Phases with PASS and HOLD

General HOLD: stop, write a short **HOLD report** (what, why, options) at the end of this work
order or in the PR, and ask the owner. A HOLD always applies when:

* a firmware, protocol or golden-hash change would be needed;
* a pinned version (JUCE, libDaisy, DaisySP, toolchain) would have to change;
* a card-safety rule of section 3 cannot be kept;
* flashing, a driver, admin rights or a manual CI run would be needed;
* the same failure persists after three serious attempts without a clear cause;
* the work would go beyond D1–D7 (for example the SD page in the VST3, PortHub, or new mod sources).

### Phase 0: worktree and baseline

Create the worktree and branch from `origin/codex/nebulae-mod-sd`. Cherry-pick `10f906f`, add
this work order, and record the baseline on the unchanged base.

* PASS:
  * `make -C editors/mod-on-daisy test test-sanitize` green;
  * the JUCE build (EXE + VST3) succeeds;
  * `"Mod on Daisy Editor.exe" --selftest <dir>` PASS;
  * browser e2e `E2E_ONLY=nebulae` green;
  * `python -m pytest diagnostics/tests/python` green;
  * all results written to `docs/dcp/SD_PAGE_LOG.md` with commands, date and exit codes.
* HOLD: a baseline test fails, and it is not one of the known false failures in the memory note.

### Phase 1: specification of the port

Write `docs/dcp/SD_SHARE_PORT.md`:

* every state, event and transition of the Python workflow, Nebulae and Looper side by side;
* every message with its bytes;
* every timeout;
* every user text and button rule of the SD share tab;
* the patch save and restore around a share (D5).

Add a table mapping every test case in the Python tests (`test_nebulae.py`, the looper tests,
the workflow parts of the GUI tests) to its planned C++ test.

* PASS:
  * every Python test case is mapped or justified as GUI-only;
  * every rule of the profile documents is assigned to a state.
* HOLD: Python and the protocol documents contradict each other on a point that matters for
  card safety.

### Phase 2: C++ core `SdShare` with simulator

In `editors/mod-on-daisy/src/core/`, free of JUCE UI:

* the SysEx codecs (Nebulae, Looper) and a DDP v1 client for HELLO/READER/NEB/LOOPER;
* the workflow state machine with injectable clock and transport;
* a device simulator for both instruments and both personalities, ported from
  `nebulae_sim.py` / `looper_sim.py`, including Windows eject, USB bus reset and restart
  into the wrong personality.

Add the tests from the phase-1 table.

* PASS:
  * all mapped tests green, also under ASan/UBSan;
  * **Byte cross-check**: for SHARE, CONFIRM, RETURN, PERFORM and STATUS, C++ and Python
    produce identical bytes. Use a small Python script that writes test vectors from the
    reference; the C++ test reads them.
  * Every timeout is tested with the fake clock.
* HOLD: a behaviour can only be reached with a protocol change.

### Phase 3: Windows serial transport and device discovery

Win32 COM port (overlapped I/O or its own thread). Enumerate the ports and identify the kit by
HELLO (`app=` and capability), as the Python session does. Handle a port vanishing during USB
re-enumeration without a hang. MIDI side: open and close the existing JUCE MIDI port under the
control of the workflow.

* PASS:
  * unit tests with a fake port pass: open, HELLO, timeout, port disappears, reconnect;
  * no call blocks the message thread longer than 50 ms (measured in the test);
  * the core uses the transport only through an interface, so it stays testable.
* HOLD: a driver, admin rights or a COM port library outside the repository would be needed.

### Phase 4: standalone app with the SD card page

Rename the standalone app to **Daisy Control Panel 0.5.0** (D3). Add the SD card page:
* status line, the buttons and texts of the 0.6.0 SD share tab in English, and the Live-loop
  confirmation;
* the eject explanation, "Return to …" and "Acknowledge interrupted access …".

The matrix page is unchanged. Instruments without a matrix (D4) open on the SD card page; the
matrix page says that this firmware has no matrix. Switching ports:

* the app saves the patch (D5);
* it releases the MIDI port, follows the kit to the COM port and back;
* after RETURN it finds the kit over MIDI again and sends the patch.

The VST3 gets the note from D2 and is otherwise unchanged.

* PASS:
  * `"Daisy Control Panel.exe" --selftest <dir>` runs, against the simulated device, a complete
    cycle for Nebulae MOD, Looper MOD, Nebulae and Looper: connect, SHARE (with CONFIRM),
    SHARED, eject, RETURN, patch restored, matrix live again;
  * it also runs one interruption case;
  * it writes screenshots of both pages;
  * the existing editor tests, the VST3 self-check (`ModOnDaisyPluginCheck`) and the browser e2e
    stay green;
  * the VST3 bundle name and plugin codes are unchanged (compared with the phase-0 build).
* HOLD: the D2 boundary can only be kept by changing the VST3 identity.

### Phase 5: package and store

Build the release configuration. Store EXE and VST3 in
`apps/daisy-control-panel/0.5.0+<commit7>/`:

* with `BUILD.txt`, `SHA256SUMS`, `STATUS.md` (`candidate`), and `COPYING.AGPL-3.0.txt`;
* following section 8 of `docs/FIRMWARE_BINARIES.md` from branch `claude/app-store-rules` if PR
  #105 is not merged yet.

Assemble the test package `build/dcp-sd-test/` **only from `apps/` and `firmware/`**:

* the four images listed in section 3;
* the app;
* `MANIFEST.json` with revision, versions and SHA-256;
* `SHA256SUMS`;
* `TEST_SHEET.md`.

The test sheet (results empty, everything NOT MEASURED) covers:

* J7 connection;
* matrix with an audible LFO route;
* SHARE/eject/RETURN on FAT32 and exFAT;
* CONFIRM with a Live loop;
* pulling the cable during SHARED;
* the patch surviving the share;
* plain Nebulae and Looper;
* the VST3 matrix in Studio One with the D2 note.

* PASS:
  * every checksum is verified against the store;
  * the packaged EXE passes `--selftest` from the package folder;
  * `python scripts/check-firmware.py` passes for the four images.
* HOLD: an image is missing from the store or does not match its `SHA256SUMS`.

### Phase 6: documentation and versions

Update the following:

* `editors/mod-on-daisy/README.md` and `docs/GUI.md`: the new name and the SD page;
* a short section in `diagnostics/docs/` saying that Daisy Diagnostic stays the diagnostics
  tool and that the SD share tab also exists in the Daisy Control Panel;
* the `components/registry.json` / manifest entries for the new panel version;
* a CHANGELOG entry;
* `docs/dcp/SD_PAGE_LOG.md` with every command and result.

* PASS:
  * the docs match the behaviour;
  * the manifest names the actual composition;
  * no German-only new text.
* HOLD: none expected.

### Phase 7: independent review, PR, handover

A fresh subagent (D7) reviews against the code and `SD_SHARE_PORT.md`, without your
conclusions:

* the state machine and card-safety rules;
* the thread and blocking rules;
* the error paths.

Fix every P1/P2 finding, add a regression test for each, and run phases 2, 4 and 5 again. Then
push `claude/dcp-sd-page` and open the PR against `main`. The PR text gives:

* the dependency on #102;
* validation with commands and results;
* "Hardware NOT MEASURED";
* the path of the test package.

Do not merge.

* PASS:
  * no open P1/P2 finding;
  * all tests from phases 0–5 green again;
  * the PR is open;
  * the package is ready and `apps/` is filled.
* Final report to the owner (in German in chat, the repo texts in English):
  * where the app and the package are;
  * how to start the test;
  * what is not measured;
  * any remaining HOLD points.

## 6. Out of scope

The following are not part of this work:

* the SD page in the VST3;
* PortHub or the MIDI bridge;
* kit keys to Studio One;
* automation (T4);
* changes to firmware, protocols or golden hashes;
* new mod sources;
* changes to Daisy Diagnostic beyond the doc note;
* removing the launcher from the Nebulae MOD package (PR #102 belongs to Codex);
* a release.
