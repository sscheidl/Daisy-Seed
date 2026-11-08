# Review brief for Codex: Daisy Control Panel 0.5.0, SD card page (PR #106)

Written 2026-11-08 by Claude for an independent review by Codex. The owner decides whether
and when it runs (work order D7). Read-only review: report findings, do not change code,
firmware, protocols or golden hashes, do not flash the kit, do not dispatch CI workflows.

## 1. What to review

Branch `claude/dcp-sd-page` (PR [#106](https://github.com/sscheidl/Daisy-Seed/pull/106),
base `codex/nebulae-mod-sd` of PR #102). The standalone JUCE app of `editors/mod-on-daisy`
became **Daisy Control Panel 0.5.0** with two pages: the unchanged modulation matrix and a new
**SD card** page. The SD card page is a C++ port of the "SD share" tab of Daisy Diagnostic
0.6.0 (`diagnostics/windows/daisy_diag/nebulae.py`, `looper.py`, `session.py`,
`protocol.py`, simulators, GUI part of `gui/main_window.py`). One app now holds the kit's J7
port in both USB modes: USB-MIDI for the matrix and the instrument's STATUS/SHARE SysEx, then
USB CDC (DDP v1) for the card reader, then USB-MIDI again.

Documents to read first, in this order:

1. `docs/dcp/SD_PAGE_WORK_ORDER.md`: the task, the owner decisions D1–D7 (D2 standalone only,
   D4 instruments without a matrix, D5 patch autosave and restore, D6 no firmware or protocol
   change).
2. `docs/dcp/SD_SHARE_PORT.md`: the specification the port claims to follow. Section 4 lists
   every transition T1–T34, section 5 every timeout, section 10 the card-safety rules (marked
   **CS**), section 11 the mapping from every Python test to its C++ test.
3. `docs/dcp/SD_PAGE_LOG.md`: every command that was run, with date and exit code, and the
   first independent review (phase 7: 0 P1, 5 P2 fixed, 9 P3).
4. Protocols: `instruments/granular/docs/CONTROL_PROTOCOL.md`,
   `instruments/looper/docs/SD_MANAGEMENT.md`, `diagnostics/protocol/PROTOCOL.md` (READER),
   `diagnostics/docs/NEBULAE_PROFILE.md`, `diagnostics/docs/LOOPER_PROFILE.md`.

Code (all under `editors/mod-on-daisy/`):

| File | Role | Review weight |
| --- | --- | --- |
| `src/core/SdShareProtocol.h` | NB/LP SysEx codecs, profiles, texts | low: byte vectors come from the Python reference (`tests/sd_share_vectors.txt`) |
| `src/core/Ddp.h` | DDP framing, parsers | low |
| `src/core/DdpSession.h` | CDC session, READER guards, timeouts | medium: compare with `session.py` |
| `src/core/SdShare.h` | the workflow state machine | medium: compare with `nebulae.py`; already reviewed once |
| `src/core/SdShareSim.h` | simulated Nebulae and Looper, both personalities | medium: a wrong simulator makes every test above it worthless |
| `src/core/SerialTransport.h` | I/O thread, bounded queues, bounded `Close()` | **high** |
| `src/ui/SerialPortWin.h` | Win32 overlapped `ReadFile`/`WriteFile`, `CancelIoEx`, SetupAPI enumeration by VID/PID | **high: never ran against a real COM port** |
| `src/ui/EditorComponent.h/.cpp` | integration: the shared MIDI port (`SdLink`, `SdMidiPorts`, `SdPortCandidates`, `OpenSdLink`), `OnSdPhase`, D4 auto-switch, D5 `SaveBeforeShare`/`RestoreAfterShare`, `Busy()` | **high: most of the first review's findings were here** |
| `src/ui/SdSharePanel.h` | the page: rows, buttons, texts (section 6 of the spec) | low |
| `src/app/Main.cpp` | `--selftest` cycles (`SdCycle`, `SdLooperSaving`, `SdInterruption`, `SdRestoreNotToWrongDevice`, `SdNoInstrument`, `SdPluginNote`) | medium: does the self-test prove what the log claims? |
| `src/plugin/Plugin.cpp` | VST3 gets `EditorComponent(false, false)`: no SD page, no COM code (D2) | low |
| `tests/test_sd_share.cpp`, `tests/test_serial_transport.cpp`, `tests/sd_vectors.py` | the core tests | medium: look for checks that cannot fail |
| `scripts/dcp_sd_test_package.py`, `docs/dcp/SD_PAGE_TEST_SHEET.md` | store, package, hardware test sheet | low |

## 2. Questions to answer

Card safety first. A wrong state can destroy the owner's samples or loops.

1. **Ownership**: is there any path on which the app sends `READER a=SHARE`, `READER a=RETURN`,
   `NEB a=PERFORM` or `LOOPER a=PERFORM` without the user action the Python reference requires?
   Check in particular `share_after_restart_` (set only by ACCEPTED, consumed once, cleared by
   R1 and the restart timeout), `perform_after_return_`, and what `EditorComponent::Disconnect()`,
   `ConnectTo()`, `ReleasePort()` and the destructor do to the CDC session.
2. **Truth of "ejected"**: can the page show EJECTED or enable **Return** on anything but the
   firmware's `READER state=EJECTED busy=0`? Can a timed-out READER command, a lost CDC link or
   a reconnect ever look like a clean state?
3. **Interruption**: after INTERRUPTED or ERROR, is RETURN possible only through the explicit
   acknowledge, and is SHARE never repeated automatically?
4. **Shared MIDI port**: the matrix session, the SQ session, the bootloader request and the SD
   workflow use one JUCE port. Can the workflow lose its link while a SHARE is in flight, miss the
   ACCEPTED reply, or re-open a port in a loop? Look at `SdLink::Poll` (delivered messages count
   even if the port vanished in the same poll), `SdPortCandidates()` while `Following()`, the
   re-entrancy `sd_.Tick()` → `TryOpen()` → `OpenSdLink()` → `ConnectTo()` → `Disconnect()`
   (guarded by `opening_sd_link_`), and the port-release rule in `Advance()`.
5. **D5 restore**: `SaveBeforeShare()` writes the patch and keeps it; `RestoreAfterShare()` sends
   it only when `sd_reconnected_` is set and the instrument key matches, and the restart timeout
   or a user `ConnectTo()` abandons it. Is there still a path that sends a stale patch, sends it
   twice, or sends it to another kit? Is the autosave written where the work order says
   (`%LOCALAPPDATA%\Daisy Control Panel\autosave\`)?
6. **Win32 transport**: `Win32SerialBackend::Read/Write` with `OVERLAPPED`, `WaitSlices`,
   `CancelIoEx`; `ThreadedSerialLink::Close()` (drain ≤ 15 ms, cancel, wait ≤ 30 ms, detach).
   Are the OVERLAPPED structures and events valid for the whole pending I/O after a detach
   (the backend lives in the shared state owned by the thread)? Is `GetOverlappedResult(…, TRUE)`
   safe after `CancelIoEx`? Can `Cancel()` race with a `ReadFile` that is issued after the
   `cancelled_` check? Is `ListComPorts()` correct for a composite device
   (`USB\VID_0483&PID_5740&MI_00\…`, the `PortName` value under `SetupDiOpenDevRegKey`)? Does
   `CreateFileA("\\\\.\\COMxx")` handle COM10 and above? Is DTR set (the firmware sends only
   with DTR)?
7. **Looper 0.6.1**: `main` b7517b5 ("make the SHARE decision final") says the protocol is
   unchanged; the port was written from the 0.6.0 reference. Check `instruments/looper/src`
   on `main` against `SdShare.h`: does a SAVING answer followed by ACCEPTED, or an ACCEPTED that
   the firmware re-decides under its hold, ever leave the workflow in a wrong phase?
8. **Threads**: everything except the I/O thread and JUCE's MIDI callback runs on the message
   thread with a 10 ms timer. Can any call there block without bound? `MidiPort.h` queues SysEx
   on the MIDI thread; `SdLink::Push` bounds its queue at 256 frames.
9. **Error paths**: malformed DDP (huge numbers, empty tokens, 400-byte bodies, bad CRC), a
   port that opens but never answers, a kit that vanishes during the handshake, a SHARE reply
   with a foreign token, `uint32_t` time wrap (`Elapsed()` uses `int32_t` differences; the
   clock is ms since boot and may start anywhere).
10. **Tests**: name any check that cannot fail or does not test what its name says, and any
    row of SD_SHARE_PORT.md section 11 whose C++ test does not cover the Python case.

## 3. How to run things (Windows, see `docs/dcp/SD_PAGE_LOG.md` for the exact environment)

```bash
make -C editors/mod-on-daisy test
```

```bash
make -C editors/mod-on-daisy test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -Wshadow -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
```

The JUCE build and self-test (the EXE of the PR is also stored in
`apps/daisy-control-panel/0.5.0+530b195/` of the main checkout and in the package
`build/dcp-sd-test/` of the worktree `Daisy-Seed-worktrees/dcp-sd-page`):

```bash
cmake -S editors/mod-on-daisy -B build/editor -G "Visual Studio 18 2026" -A x64 -DMOD_EDITOR_JUCE_DIR=<main checkout>/.tools/juce-9.0.4/src
```

```bash
cmake --build build/editor --config Release --parallel
```

```bash
"build/editor/ModOnDaisyEditor_artefacts/Release/Daisy Control Panel.exe" --selftest out
```

`MOD_ON_DAISY_NO_AUTOCONNECT=1` keeps the VST3 plugin check away from a connected kit. Notes
for this machine: `python3` is a Windows Store alias (`make` recipes with `python3` need a shim
or `python`), `make test-sanitize` fails on the space in the path (run the `HOST_FLAGS` form
above), and inside a worktree with junctioned submodules `git status` fails (the registry test
needs an exported tree). Do not run the app and Daisy Diagnostic against the kit at the same
time.

## 4. What to deliver

A findings list with severity P1 (card safety, data loss, crash, deadlock), P2 (wrong
behaviour or spec deviation), P3 (minor), each with file and line, what is wrong, why (which
rule of SD_SHARE_PORT.md or which Python behaviour), and a concrete fix. Put it in
`docs/dcp/CODEX_REVIEW_SD_PAGE.md` on this branch or as a PR comment, mark unsure items as
such, and end with the count per severity. Do not fix anything in this pass; the owner decides
who fixes what. Hardware stays NOT MEASURED until the owner reports.
