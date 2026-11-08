# SD page work log (Daisy Control Panel 0.5.0)

Every command of [SD_PAGE_WORK_ORDER.md](SD_PAGE_WORK_ORDER.md) with date, exit code and
result. Only what was observed is written here; hardware results are NOT MEASURED until the
owner reports them.

Environment (Windows 11, main checkout `Daisy-Seed`, worktree
`Daisy-Seed-worktrees/dcp-sd-page`, branch `claude/dcp-sd-page` from
`origin/codex/nebulae-mod-sd` c312b79): llvm-mingw 20261006 (clang 23.1.3) as `HOST_CXX`, xPack
GNU Make 4.4.1, CMake 4.4.2 with Visual Studio 18 2026 Build Tools (MSVC 14.51), JUCE 9.0.4 from
`.tools/juce-9.0.4/src`, Python 3.12.10 with PySide6 6.8.3 and pytest 9.0.3, Node 24.22.1 with
Playwright through a wrapper that launches the installed Edge (`PLAYWRIGHT_MODULE`). `libDaisy`,
`DaisySP` and `.tools` are junctions to the main checkout. `MOD_ON_DAISY_NO_AUTOCONNECT=1` for
every GUI run.

Environment notes (not failures): `python3` is a shim to `python` (the Windows Store alias
breaks `make`); `make test-sanitize` fails on Windows because the recipe calls `$(MAKE)` with
the unquoted path `D:/Eigene Dateien/...`, so the equivalent `make test HOST_FLAGS="<sanitizer
flags>"` was run; the browser e2e needs the `mod_sim.exe` path with its extension.

## Phase 0: worktree and baseline (2026-11-07/08)

| # | Command (in the worktree) | Date | Exit | Result |
| --- | --- | --- | --- | --- |
| 0.1 | `git worktree add -b claude/dcp-sd-page ../Daisy-Seed-worktrees/dcp-sd-page origin/codex/nebulae-mod-sd` | 2026-11-07 23:5x | 0 | HEAD c312b79 |
| 0.2 | junctions `libDaisy`, `DaisySP`, `.tools` → main checkout (PowerShell `New-Item -ItemType Junction`) | 2026-11-07 | 0 | 32 entries in both `libDaisy` listings |
| 0.3 | `git cherry-pick 10f906f` (docs/dcp from branch `dcp`) | 2026-11-07 | 0 | 39addc0 |
| 0.4 | `git add docs/dcp/SD_PAGE_WORK_ORDER.md && git commit` | 2026-11-07 | 0 | d885227 |
| 0.5 | `make -C editors/mod-on-daisy test` | 2026-11-07 23:57 | 0 | all PASS (core, SQ, Streichwurst, Ambient, browser SQ codec/client, contracts) |
| 0.6 | `make -C editors/mod-on-daisy test HOST_FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -Wshadow -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"` (= `test-sanitize`) | 2026-11-08 00:00 | 0 | all PASS, no sanitizer report |
| 0.7 | `cmake -S editors/mod-on-daisy -B build/editor -G "Visual Studio 18 2026" -A x64 -DMOD_EDITOR_JUCE_DIR=<wt>/.tools/juce-9.0.4/src` | 2026-11-07 23:57 | 0 | configured in 51 s |
| 0.8 | `cmake --build build/editor --config Release --parallel` | 2026-11-07 23:57–00:01 | 0 | `Mod on Daisy Editor.exe` (7,552,512 B, SHA-256 `e22b6916…98be`), `Mod on Daisy.vst3` (SHA-256 of the DLL `bfe75619…acf1`), `ModOnDaisyPluginCheck.exe`, `mod_editor_core_tests.exe` |
| 0.9 | `"Mod on Daisy Editor.exe" --selftest .work/baseline/selftest` | 2026-11-08 00:04 | 0 | PASS (toy-piano, physical-modeling, corrections, offline, plugin, Acid Seq, Streichwurst; screenshots written) |
| 0.10 | `ModOnDaisyPluginCheck.exe "<VST3>" editors/mod-on-daisy/web/tests/legacy-reverb.modpatch` | 2026-11-08 00:04 | 0 | PASS (load, pass-through, state round trip, editor window) |
| 0.11 | `clang++ … modules/mod-on-daisy/tools/mod_sim.cpp -o build/host/mod_sim` then `E2E_ONLY=nebulae node editors/mod-on-daisy/web/tests/e2e.cjs modules/mod-on-daisy/build/host/mod_sim.exe build/host` | 2026-11-08 00:00 | 0 | PASS nebulae (columns, all cells, LFO FM, route confirmed, unmodulated bases) |
| 0.12 | `python -m pytest diagnostics/tests/python -q` | 2026-11-07 23:57–23:59 | 0 | 335 passed in 126.7 s |

VST3 identity recorded for the phase-4 comparison (`.work/baseline/moduleinfo-0.4.0.json`):
name "Mod on Daisy", vendor "Daisy-Seed", processor CID `ABCDEF019182FAEB447365644D6F4461`,
controller CID `ABCDEF011234ABCD447365644D6F4461`, categories Fx/Tools, bundle `Mod on Daisy.vst3`.

Phase 0 PASS: every baseline check green; no HOLD.

## Phase 1: specification (2026-11-08)

| # | Item | Result |
| --- | --- | --- |
| 1.1 | [SD_SHARE_PORT.md](SD_SHARE_PORT.md) written: states, events, transitions (Nebulae and Looper side by side), every message with bytes, every timeout, user texts and button rules, D5 patch save/restore, test mapping | done |
| 1.2 | Every Python test of `test_nebulae.py`, `test_looper.py`, `test_gui_nebulae.py`, `test_gui_looper.py` mapped or justified as GUI-only (section 11) | done |
| 1.3 | Every rule of the profile documents assigned to a state (section 10); Python versus documents compared (section 9) | no contradiction, no HOLD |

## Phase 2: C++ core `SdShare` with simulator (2026-11-08)

New in `editors/mod-on-daisy/src/core/`: `SdShareProtocol.h` (NB/LP SysEx codecs, profiles,
texts), `Ddp.h` (CRC-16 framing, receiver, HELLO/READER/NEB/LOOPER/ST), `DdpSession.h` (CDC
session with the Python guards), `SdShare.h` (`SdShareWorkflow`, the state machine of
SD_SHARE_PORT.md § 4), `SdShareSim.h` (both instruments, both personalities, eject, bus reset,
I/O error, R1, restart that never completes, hung firmware). Tests: `tests/test_sd_share.cpp`
(27 groups, SD_SHARE_PORT.md § 11), vectors `tests/sd_share_vectors.txt` written by
`tests/sd_vectors.py` from the Python reference and checked by `--check`.

| # | Command (in `editors/mod-on-daisy`) | Date | Exit | Result |
| --- | --- | --- | --- | --- |
| 2.1 | `python tests/sd_vectors.py` then `--check` | 2026-11-08 00:2x | 0 | 54 lines, matches the reference |
| 2.2 | `clang++ -std=c++17 -O2 -Wall -Wextra -Werror -Wshadow -pthread … tests/test_sd_share.cpp` and run | 2026-11-08 | 0 | all 27 groups PASS (first run: 7 checks failed in hand-written test vectors and a timeout expectation; fixed in the test, not the core) |
| 2.3 | the same with `-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all` | 2026-11-08 | 0 | PASS (first run: a use-after-free in the test's own fake port; fixed) |
| 2.4 | `arm-none-eabi-g++ -mcpu=cortex-m7 -std=c++17 -O2 -Wall -Wextra -Werror -Wshadow -fsyntax-only tests/test_sd_share.cpp` (GCC warning check, no g++ here) | 2026-11-08 | 0 | clean |
| 2.5 | `make -C editors/mod-on-daisy test` (now with `test_sd_share` and the vector check) | 2026-11-08 00:3x | 0 | all PASS |

Byte cross-check: STATUS, SHARE (confirm 0/1, three tokens), CONFIRM/SAVING/… replies, ERROR,
`READER a=RETURN`, `NEB a=PERFORM`, `LOOPER a=PERFORM`, HELLO, PING, BYE: identical bytes in
Python and C++. Every timeout (STATUS 3 s, probe 1 s, SHARE 2 s, restart 20 s, save retry
0.5 s / 180 s, DDP handshake 1 s, backoff 5 s, reader command 20 s, RX 1.5 s / 5 s) is driven
by the fake clock in the tests. Phase 2 PASS, no HOLD.

## Phase 3: Windows serial transport and discovery (2026-11-08)

`src/core/SerialTransport.h`: `ThreadedSerialLink` (I/O thread, bounded 64 KiB queues,
`Close()` cancels and joins) and `ThreadedSerialPorts` around a platform `SerialBackend`.
`src/ui/SerialPortWin.h`: `Win32SerialBackend` (overlapped `ReadFile`/`WriteFile`,
`CancelIoEx`, DTR on) and `ListComPorts()` through SetupAPI with VID/PID from the device
instance id; Windows SDK only, no driver, no admin rights. The MIDI side stays the existing
JUCE port, now shared with the workflow (phase 4).

| # | Command | Date | Exit | Result |
| --- | --- | --- | --- | --- |
| 3.1 | `clang++ … tests/test_serial_transport.cpp` and run | 2026-11-08 | 0 | PASS: open/HELLO over the thread, handshake timeout, port vanishes, reconnect asks STATUS only, GUI-thread calls with a backend blocking 200 ms: open 0.1 ms, write 0.0 ms, read 0.0 ms, close 0.4–15.7 ms, destroy 0.0 ms (all < 50 ms), bounded send queue |
| 3.2 | the same under ASan/UBSan | 2026-11-08 | 0 | PASS |
| 3.3 | `clang++ -fsyntax-only` of `src/ui/SerialPortWin.h` (llvm-mingw Windows headers) | 2026-11-08 | 0 | clean (after adding `<cstdlib>`) |

Phase 3 PASS, no HOLD. Real COM-port behaviour is NOT MEASURED (no kit in this session).

## Phase 4: Daisy Control Panel 0.5.0 with the SD card page (2026-11-08)

Changes: `editors/mod-on-daisy/src/ui/SdSharePanel.h` (the page, § 6 of SD_SHARE_PORT.md),
`EditorComponent.h/.cpp` (page switch Matrix / SD card, the workflow on the shared MIDI port,
D4 auto-switch and "has no modulation matrix" notice, D5 autosave and restore, CDC session on
the Win32 serial ports in the standalone app only), `app/Main.cpp` (name "Daisy Control
Panel", about line, self-test cycles), `plugin/Plugin.cpp` (D2 note, no COM port code),
`core/SimInstruments.h` (Nebulae MOD and Looper MOD column tables), `CMakeLists.txt`
(project 0.5.0, product name, `MOD_EDITOR_SD_PAGE`, setupapi), `.github/workflows/editor.yml`
(new EXE name; manual workflow, not dispatched).

| # | Command | Date | Exit | Result |
| --- | --- | --- | --- | --- |
| 4.1 | `cmake … -B build/editor` (stale `*_resources.rc` deleted first) and `cmake --build build/editor --config Release --parallel` | 2026-11-08 00:36–00:39 | 0 | `Daisy Control Panel.exe` (7,782,912 B), `Mod on Daisy.vst3`, `ModOnDaisyPluginCheck.exe`, three test EXEs. First attempt: the VST3 failed to link SetupAPI symbols; the Win32 serial code is now compiled only with `MOD_EDITOR_SD_PAGE` (standalone), the plugin never touches COM ports (D2) |
| 4.2 | `"Daisy Control Panel.exe" --selftest .work/phase4/selftest` | 2026-11-08 00:39 | 1 | first run: 62 checks failed. Cause found with a debug line: the shared-port link reported "connection lost" for the SHARE ACK that arrives in the same poll as the kit's restart (the simulated port and the Python link deliver pending replies first), and a port opened by the user was released before the workflow attached. Fixed in `EditorComponent` (link poll semantics, `ScanNow()`, workflow ticks before the port-release rule) |
| 4.3 | the same, second run | 2026-11-08 00:44 | 0 | 19 PASS: all previous suites plus SD page idle (Toy Piano), Nebulae MOD cycle with CONFIRM/cancel/confirm, Looper MOD cycle, plain Nebulae and plain Looper (D4: auto-switch, "has no modulation matrix"), Looper SAVING, interruption (no/yes), D2 plugin note; screenshots `dcp-*.png` written; then once more after putting the page on top of the matrix (z-order) |
| 4.4 | `ModOnDaisyPluginCheck.exe "<VST3>" …legacy-reverb.modpatch` | 2026-11-08 00:46 | 0 | PASS |
| 4.5 | `diff` of `moduleinfo.json` against the phase-0 build without the version lines | 2026-11-08 00:46 | 0 | identity unchanged: name "Mod on Daisy", vendor "Daisy-Seed", both CIDs, Fx/Tools, bundle `Mod on Daisy.vst3`; version 0.5.0 |
| 4.6 | `ctest -C Release` in `build/editor` (MSVC builds of the three host tests) | 2026-11-08 00:46 | 0 | 3/3 passed |
| 4.7 | `make -C editors/mod-on-daisy test` | 2026-11-08 00:46 | 0 | all PASS |
| 4.8 | the same with the sanitizer flags | 2026-11-08 00:46–00:47 | 0 | all PASS |
| 4.9 | `E2E_ONLY=nebulae node …/e2e.cjs …/mod_sim.exe` | 2026-11-08 00:47 | 0 | PASS |

Phase 4 PASS, no HOLD. The D2 boundary needed no change of the VST3 identity.

## Phase 5: package and store (2026-11-08)

`scripts/dcp_sd_test_package.py` (new): `--store` copies the Release build into
`apps/daisy-control-panel/<version>+<commit7>/` of the main checkout with `BUILD.txt`,
`SHA256SUMS` (every file, also inside the `.vst3`), `STATUS.md` (`candidate`) and
`COPYING.AGPL-3.0.txt` (docs/FIRMWARE_BINARIES.md § 8, PR #105); `--package` assembles
`build/dcp-sd-test/` only from `apps/` and `firmware/`, verifies every store checksum, runs
`check-firmware.py` on the four images, copies the test sheet, writes `MANIFEST.json` and
`SHA256SUMS`; `--selftest` runs the packaged EXE from the package folder.

| # | Command | Date | Exit | Result |
| --- | --- | --- | --- | --- |
| 5.1 | `cmake --build build/editor --config Release --parallel` from the committed phase-4 state 5f6280d (clean tree) | 2026-11-08 00:49–00:51 | 0 | `Daisy Control Panel.exe` 7,783,424 B, SHA-256 `56c39565…9276`; VST3 DLL `b58f21ac…3b08` |
| 5.2 | `python scripts/dcp_sd_test_package.py --store` | 2026-11-08 00:53 | 0 | `apps/daisy-control-panel/0.5.0+5f6280d/` written (EXE, `Mod on Daisy.vst3`, licence, BUILD.txt, SHA256SUMS, STATUS.md `candidate`) |
| 5.3 | `python scripts/dcp_sd_test_package.py --package --selftest --check-root <origin/main 6e390c7 export>` | 2026-11-08 00:55 | 0 | `build/dcp-sd-test/` with `apps/` (EXE, VST3, licence, BUILD.txt), `firmware/` (the four images), `MANIFEST.json`, `SHA256SUMS`, `TEST_SHEET.md`, `README-Daisy-Control-Panel.md`; every checksum verified against the store; packaged EXE `--selftest` exit 0 with 19 PASS lines; `check-firmware.py` PASS for all four images |

Checker detail: `scripts/check-firmware.py` compares an image name with the version header
of its own source tree. This branch (base `codex/nebulae-mod-sd`) still carries
`LOOPER_VERSION "0.6.0"`, the stored Looper images are 0.6.1 from `main` (b7517b5, "make the
SHARE decision final", protocol unchanged). The two Looper images were therefore checked with
the checker and instrument sources exported from `origin/main` 6e390c7 (`git archive`), the two
Nebulae images with this branch's checker (main's has no `NebulaeMod` case yet; PR #102 adds
it). `MANIFEST.json` records the checker per image. After the rebase onto `main` the branch's
own checker covers all four.

Package image hashes (identical to the store): NebulaeMod `02e52f88…a727`, NebulaeOnDaisy
`cbc4c376…81ff`, LooperMod `25543ed0…9484`, Looper `82068473…9939`.

Phase 5 PASS, no HOLD.

## Phase 6: documentation and versions (2026-11-08)

| # | Item | Result |
| --- | --- | --- |
| 6.1 | `editors/mod-on-daisy/README.md`: new title and "Daisy Control Panel 0.5.0" section, EXE name, simulators, tests, self-test scope | done |
| 6.2 | `editors/mod-on-daisy/docs/GUI.md`: English note on the two pages at the top (the German concept text stays) | done |
| 6.3 | `editors/mod-on-daisy/CHANGELOG.md` (new): 0.5.0 entry | done |
| 6.4 | `diagnostics/docs/INSTALLATION_AND_USAGE.md`, `NEBULAE_PROFILE.md`, `LOOPER_PROFILE.md`: Daisy Diagnostic stays the diagnostics tool; the SD share tab also exists as the SD card page; one program at a time | done |
| 6.5 | `components/registry.json`: panel `daisy-control-panel` (cmake version 0.5.0) listed by `granular-mod`, `looper-mod`, `granular`, `looper`; `components/README.md` note | `python scripts/component_manifest.py --validate` PASS (37 components, 27 products); `tests/test_components.py` and `test_firmware_checker.py` OK (12 + 2 tests) in an exported copy of the tree, because `git status` fails inside this junctioned worktree (environment, see phase 0) |
| 6.6 | `instruments/granular/docs/MODULATION.md`: SD sharing section points to the one-app flow | done |
| 6.7 | `docs/dcp/SD_SHARE_PORT.md`: § 7 probing and link semantics as implemented, § 9 Looper 0.6.1 row | done |
| 6.8 | `web/version.js` | unchanged (browser not touched) |

Phase 6 PASS: no German-only new text; the manifest names the actual composition.

## Phase 7: independent review, fixes, PR (2026-11-08)

A fresh subagent (D7) reviewed the C++ against the code, SD_SHARE_PORT.md and the Python
reference, without this session's conclusions. Result: 0 P1, 5 P2, 9 P3 (three marked unsure).
The state machine and the DDP session were found to be a faithful port; no path sends SHARE,
RETURN or PERFORM without the user action the Python requires.

| # | Severity | Finding | Fix | Regression test |
| --- | --- | --- | --- | --- |
| 1 | P2 | D5 restore could go to another device (every Daisy port offered while following; flag never cleared after T22/T33 or a manual connect) | `EditorComponent`: the saved patch carries the instrument key and is sent only on a connection the workflow opened to the same key; a user `ConnectTo()` and a restart timeout abandon it with a log line naming the file; while following, only the port the share started on is offered unless it is gone | self-test `SdRestoreNotToWrongDevice` |
| 2 | P2 | `next_scan_ = 0` with a clock that is ms since boot: no COM scan once uptime ≥ 24.8 days | `DdpSession` and `SdShareWorkflow` arm the scan on their first `Tick()`; the simulator's `busy_until_` starts at boot time | `TestClockWrap` (clock at 2^31 and across 2^32) |
| 3 | P2 | "Disconnect" button reconnected instead when only the SD workflow held the port (plain Nebulae/Looper) | one `Busy()` for label and handler; `ConnectTo()` clears `user_disconnected_` | self-test `SdCycle` no-matrix cases click the button |
| 4 | P2 | Win32 `Write()` had no cancel guard; `Close()` could block for the driver's write timeout; the "< 50 ms" test only proved the cooperative fake | `Win32SerialBackend`: `Read`/`Write` return at once after `Cancel()`, wait in 20 ms slices and cancel their overlapped I/O; `ThreadedSerialLink`: shared state owned with the thread, `Close()` drains a queued BYE for ≤ 15 ms, cancels, waits ≤ 30 ms and detaches a thread a driver still holds | `TestCloseWithUncooperativeBackend`, `TestByeIsSent`; `TestNeverBlocksMessageThread` kept |
| 5 | P2 | `TestWfBusyReaderBlocksReturn` could not fail | simulator `reader_busy` emits `busy=1`; the test refuses RETURN after EJECTED while busy and allows it afterwards | the rewritten test |
| 6 | P3 | `volatile bool cancelled_` | `std::atomic<bool>` | — |
| 7 | P3 | unused condition variable | removed; the cv now signals drain and finish | — |
| 8 | P3 | comment "keep the newest" while the oldest were kept | the receive buffer drops the oldest bytes | — |
| 9 | P3 | BYE could be discarded at close | drained before stop (see 4) | `TestByeIsSent` |
| 10 | P3 (unsure) | CDC bytes received together with the failure are dropped | kept (matches the Python `SerialLink`); noted in SD_SHARE_PORT.md § 7 | — |
| 11 | P3 (unsure) | a port that refuses the first STATUS send was retried every second | backed off 5 s like a silent port | `TestProbeBackoff` |
| 12 | P3 | test gaps (T3, T13–T15), an unchecked error text, table names that did not match the C++ | `TestProbeBackoff`, `TestStaleAndErrorReplies`, the refusal check, SD_SHARE_PORT.md § 11 renamed to the real test functions, § 11.6 added | those tests |
| 13 | P3 (unsure) | SetupAPI enumeration and `CreateFile` on the message thread | kept (as the Python panel); documented in § 7 | — |
| 14 | P3 | duplicate log line on return | removed in the page callback | — |

While fixing, two simulator bugs surfaced and were fixed: the main loop reset the SYS/READER
period with every ST, so the periodic `READER` line was never sent on its own, and
`busy_until_ = 0` blocked the simulated firmware with a clock just below the 32-bit wrap.
The simulator gained `share_reply_delay_ms` for the pending-action test.

| # | Command | Date | Exit | Result |
| --- | --- | --- | --- | --- |
| 7.1 | `clang++ … tests/test_sd_share.cpp`, `tests/test_serial_transport.cpp` and run, normal and ASan/UBSan | 2026-11-08 01:1x–01:2x | 0 | PASS after the fixes above (first runs showed the simulator bugs and two tests that read the error text after the next STATUS had cleared it, as the Python panel does) |
| 7.2 | `cmake --build build/editor --config Release --parallel` | 2026-11-08 01:22 and 01:25 | 0 | rebuilt |
| 7.3 | `"Daisy Control Panel.exe" --selftest .work/phase7/selftest` | 2026-11-08 01:26 | 0 | 20 PASS lines (new: restore guard; Disconnect button in the no-matrix cycles); the first run failed one check that read the single-line log after it had been replaced; the check was narrowed to the flag |
| 7.4 | `ModOnDaisyPluginCheck.exe` on the VST3; `ctest -C Release`; `moduleinfo.json` diff | 2026-11-08 01:24–01:26 | 0 | PASS; 3/3; identity unchanged |
| 7.5 | `test_serial_transport` three times while the MSVC build ran in parallel | 2026-11-08 01:25 | 0 | close 15.7–40.4 ms (bound 50), uncooperative backend 47.6–76.5 ms (bound 95), BYE 16–64 ms; one earlier run during the full parallel build exceeded a bound with the former 30 ms drain wait, hence 15 ms |
| 7.6 | `make -C editors/mod-on-daisy test`, the sanitizer run, `E2E_ONLY=nebulae` e2e | 2026-11-08 01:27–01:28 | 0 / 0 / 0 | all PASS |
| 7.7 | `python -m pytest diagnostics/tests/python -q` | 2026-11-08 01:02 | 0 | 335 passed (Python untouched) |
| 7.8 | `cmake --build build/editor --config Release --parallel` from the final commit 530b195 (clean tree), then `scripts/dcp_sd_test_package.py --store --package --selftest --check-root <origin/main export>` | 2026-11-08 01:29 | 0 | `apps/daisy-control-panel/0.5.0+530b195/` stored (`candidate`); `build/dcp-sd-test/` rebuilt from the stores, every checksum verified, the four images PASS `check-firmware.py`, the packaged EXE self-test exit 0 with 20 PASS lines |
| 7.9 | retention: `apps/daisy-control-panel/0.5.0+5f6280d/` (the phase-5 candidate, never handed out) is superseded and deleted once 5f6280d is on GitHub (after the push) | 2026-11-08 | | listed in the PR |

Phase 7 PASS: no open P1/P2 finding; all tests of phases 0–5 green again; the package is ready
and `apps/` is filled; the PR is opened against `main` (depends on #102, not merged).

## Hardware probe (MEASURED 2026-11-08, owner-requested, after the PR was opened)

The owner put the kit into the Daisy bootloader and asked for flashing and own tests. A new
console tool `editors/mod-on-daisy/tests/hw_probe.cpp` (CMake target `DcpHardwareProbe`,
Windows only, not in CI) drives the same core classes as the app (`MidiPort`, `SdShareWorkflow`,
`DdpSession` on `SerialPortWin.h`, the Mod on Daisy `Session`) through a scripted cycle: STATUS,
matrix edit, SHARE, wait for the CDC session and the Windows drive, list the drive root
(read-only, nothing written), eject through the Windows shell verb, RETURN, STATUS again, matrix
reconnect and patch upload as `RestoreAfterShare()` does. Logs: `.work/hw/hw-probe-*.log`.
Flashing: `dfu-util -a 0 -s 0x90040000:leave -D <image> -d ,0483:df11` (`.tools/dfu-util-0.11`),
bootloader entry between images through the app's own request (`DcpHardwareProbe boot`).

| # | Image (SHA-256 prefix) | Date / time | Result |
| --- | --- | --- | --- |
| H1 | NebulaeMod-v0.2.1 (`02e52f88`) | 16:54 flash, 16:57 status | STATUS 0.16 s: Nebulae 0.2.1, card, bank 0/0 (card has no NEBULAE folder), peak 6 %; matrix connected 0.16 s, columns Start … Window |
| H2 | NebulaeMod-v0.2.1, cycle 1 | 16:57 | PASS: SHARE accepted 0.01 s; CDC COM16 after 2.69 s; SHARED; drive G: 2.8 s after the click; root "System Volume Information"; eject verb; EJECTED 0.78 s later; RETURN → PERFORM; USB-MIDI back 3.1 s; matrix reconnected, patch CRC BE2E4801 restored |
| H3 | NebulaeMod-v0.2.1, cycle 2 | 16:58 | PASS, same sequence; drive 1.7 s, MIDI back 3.1 s |
| H4 | LooperMod-v0.6.1 (`25543ed0`) | 16:59 boot + flash, 17:00 cycle | PASS: Looper 0.6.1, loop=0; matrix columns Speed … Mix; CDC COM17 2.0 s; drive 1.9 s; root now "looper" (created by the firmware); EJECTED; MIDI back 3.1 s; patch restored |
| H5 | Looper-v0.6.1 (`82068473`), `--no-matrix` | 17:00–17:01 | PASS: drive 1.9 s, EJECTED, MIDI back 3.1 s |
| H6 | NebulaeOnDaisy-v0.2.1 (`cbc4c376`), `--no-matrix` | 17:01 | PASS: drive 1.9 s, EJECTED, MIDI back 3.1 s |
| H7 | NebulaeMod-v0.2.1 flashed back, status | 17:01–17:02 | PASS: STATUS and matrix as H1; the kit is left in this state |

Every bootloader request was acknowledged within 0.01 s and the DFU device appeared. Not
measured: the GUI itself on hardware, listening, CONFIRM with a Live loop (none recorded),
Looper SAVING (no unsaved loop), the pulled cable, exFAT (card format not checked), the VST3.

## HOLD 2026-11-08 (owner): Codex review of PR #106 at 4b65faf

`docs/dcp/CODEX_REVIEW_SD_PAGE.md` (committed in 07fbdda): 5 P1, 7 P2, 3 P3. Nothing was
pushed after 4b65faf until the owner's instruction "Fix all" (below). The hardware probe tool
and its CMake target were committed with the report.

## Codex review fixes (2026-11-08, owner: "Fix all")

All 15 findings were addressed; `docs/dcp/SD_SHARE_PORT.md` § 7, § 9, § 10 and § 11.7 record
the rules and the tests. Commits: 07fbdda (probe tool, report), fcc6110 (core and transport),
2e4d9a0 (app, UI, self-test), the docs commit after this section.

| # | Prio | Finding (short) | Fix | Test |
| --- | --- | --- | --- | --- |
| 1 | P1 | an accepted SHARE could expose another kit's card (bare continuation flag) | the continuation is bound to the accepting instrument's HELLO app, expires with the restart window, dies with the CDC session and another instrument's HELLO | `TestShareIntentBoundAndExpiring` |
| 2 | P1 | a failed RETURN could leave PERFORM armed for a later LOCAL | PERFORM only after `a=RETURN state=LOCAL busy=0` on the same connection generation; timeout, ERR, loss, cancel invalidate; "Returning the card to the firmware …" meanwhile | `TestReturnContinuationInvalidated` |
| 3 | P1 | the acknowledge dialog could release a later interruption | bound to the interruption episode and generation; a stale answer sends nothing and logs a warning | self-test `SdAckEpisode` |
| 4 | P1 | D5 could restore to another kit with the same product key | only the port the share started on, by JUCE device identifier; no fallback to every Daisy port | `SdPortCandidates()`; `SdRestoreNotToWrongDevice`; two kits with one key NOT covered by a test (one identifier per simulator port) |
| 5 | P1 | SHARE before the matrix was read; stale snapshot under the confirmation | Share disabled with "(reading the matrix …)" while detection or the first read is pending; `RequestSdShare` refuses; the confirmation re-saves | self-test `SdShareGate` |
| 6 | P2 | a SHARE reply could be discarded with the vanished port | received frames are routed to the workflow before the port is dropped | code path; the simulator bypasses enumeration: NOT covered by a test |
| 7 | P2 | a kit power-cycled out of management stayed undiscoverable | MIDI STATUS probing in Management after CDC loss; a valid STATUS returns to Performance ("back on USB-MIDI") | `TestRecoveryAfterCdcLoss` |
| 8 | P2 | another DDP device could starve discovery | `DdpSession` accepts only the instrument app with `sdmsc` and its capability; others are backed off | `TestForeignDdpDeviceSkipped` |
| 9 | P2 | enumeration, open, MIDI send and the autosave write on the message thread | `ThreadedSerialPorts` lists in the background and opens on the link's thread; `MidiPort` sender thread with a bounded queue; autosave on a JUCE worker | `TestOpenOnWorker`, `TestNeverBlocksMessageThread`; MIDI/autosave threads are JUCE code without a host test; real driver latency NOT MEASURED |
| 10 | P2 | a failed COM configuration looked like a silent device | `GetCommState`/`SetCommState`/`SetCommTimeouts`/`PurgeComm` failures close the handle and report the call with the Win32 error | no injected-failure test (needs a real handle); the real open path passed the probe cycles below |
| 11 | P2 | duplicate READER fields took the first value | a duplicated field rejects the line and clears the cached reader state (the Python parser takes the last value) | `TestDuplicateReaderFields` |
| 12 | P2 | the self-test could open a real CDC port | `DisableHardwareSerial()` before the first test; `!HardwareSerialActive()` asserted first and last | self-test |
| 13 | P3 | the BYE drain waited for queue removal, not the write | write completion counters; `kDrainMs` 60, `kJoinMs` 30; the fake refuses writes after the cancel | `TestByeIsSent` |
| 14 | P3 | expired backoff entries came back after half a clock cycle | pruned on every scan while the port is absent; `ScanNow()` is a request for the next tick | `TestBackoffPruneAcrossHalfCycle` |
| 15 | P3 | § 11 overstated coverage | rows narrowed; missing scenarios added at the claimed layer | `SdNoCardAndLongTexts`, `SdLooperFailedSave`, `TestReaderTimeoutAfterShared`, `TestFrameParserLimits`, `TestClockWrap` extended |

Found while fixing: the simulated HELLO advertised the capability of the configured profile
instead of the app it claimed; `SdSharePanel` laid its texts out for at most three lines
(message, error and warning now take the wrapped lines they need, caps 5/4/3; the hint is hidden
when no room is left); `AllVisibleInside()` names the failing control.

| # | Command | Date | Exit | Result |
| --- | --- | --- | --- | --- |
| C.1 | `make -C editors/mod-on-daisy test` (core, SD share, vectors, serial transport, SQ, browser codec) | 2026-11-08 17:2x | 0 | all PASS |
| C.2 | the same with `-fsanitize=address,undefined` (`HOST_FLAGS` of `test-sanitize`) | 2026-11-08 17:40 | 0 | all PASS, no sanitizer report |
| C.3 | `cmake --build build/editor --config Release` (all targets) | 2026-11-08 17:33, app-only rebuilds 17:36–17:39 | 0 | rebuilt; one earlier build failed on a macro used before its definition (fixed) |
| C.4 | `"Daisy Control Panel.exe" --selftest build/selftest-codex` | 2026-11-08 17:39 | 0 | 24 PASS lines (new: share gate, acknowledge episode, no card + long texts, Looper failed save); the first runs with the stricter layout check reported empty labels and the three-line caps, see above |
| C.5 | `ctest -C Release` in build/editor | 2026-11-08 17:34 | 0 | 3/3 |
| C.6 | `ModOnDaisyPluginCheck.exe` on the VST3 (`MOD_ON_DAISY_NO_AUTOCONNECT=1`); plugin block diff against origin/main | 2026-11-08 17:40 and after the final build | 0 | PASS; identity unchanged (only the standalone's product name, bundle id and the project version differ) |
| C.7 | `DcpHardwareProbe status`, then `cycle` twice (kit on NebulaeMod 0.2.1, card inserted) | 2026-11-08 17:41 | 0 | **MEASURED**: STATUS 0.15 s, matrix 0.15 s; SHARE answered 0.01 s; CDC COM16 after 2.70/2.71 s; READER SHARED; drive letter 2.8 s after the click; Return disabled while Windows owns the card; eject verb → EJECTED after 0.64/0.71 s; RETURN → PERFORM 0.03 s; USB-MIDI back 3.11/3.12 s; matrix reconnected 0.24 s; patch CRC BE2E4801 restored in both cycles (logs `.work/hw/hw-probe-20261108-1741*.log`) |
| C.8 | final build from the code commit 2e4d9a0, `scripts/dcp_sd_test_package.py --store --package --selftest --check-root <origin/main export>` | 2026-11-08 17:4x | 0 | first run from the tree with the uncommitted docs of this section: store named `-dirty`, package rebuilt from the stores, every checksum verified, the four images PASS `check-firmware.py`, the packaged EXE self-test exit 0 with 24 PASS lines; the dirty store was deleted and the store repeated from the clean tree after this docs commit (C.10) |
| C.9 | retention: `apps/daisy-control-panel/0.5.0+530b195/` (the phase-7 candidate, never handed out) is superseded by `the clean store named in C.10` and deleted after the push | 2026-11-08 | | |
| C.10 | `scripts/dcp_sd_test_package.py --store --package --selftest --check-root <origin/main export>` from the clean tree at the docs commit 3daf6f4 (binaries unchanged since 2e4d9a0) | 2026-11-08 17:46 | 0 | `apps/daisy-control-panel/0.5.0+3daf6f4/` stored (`candidate`); `build/dcp-sd-test/` rebuilt from the stores, every checksum verified, the four images PASS `check-firmware.py`, the packaged EXE self-test exit 0 with 24 PASS lines |
| C.11 | owner: "flash and test", one image is enough. Kit already in the Daisy bootloader (the `boot` request found no USB-MIDI, dfu-util found `0483:df11`); `dfu-util -a 0 -s 0x90040000:leave -D firmware/Looper-v0.6.1.bin` from the package (sha256 82068473…); then `DcpHardwareProbe cycle --no-matrix` with the card inserted | 2026-11-08 19:11 | 0 | **MEASURED** on the plain Looper 0.6.1 (Looper product key, no matrix, share gate without a matrix read): STATUS 0.17 s; SHARE answered 0.01 s; CDC COM17 `LooperOnDaisy 0.6.1` after 2.86 s; READER SHARED 0.05 s; drive G: 2.9 s after the click, root `looper`; Return disabled while Windows owns the card; eject verb → EJECTED after 0.85 s; RETURN → PERFORM 0.02 s; USB-MIDI back 3.10 s; "Back in performance. The loop was reloaded from looper/loop.wav." (log `.work/hw/hw-probe-20261108-191114.log`). The kit stays on Looper 0.6.1 in performance |

Not re-run (components untouched by the fixes): the browser editor e2e and the diagnostics
`pytest`; both passed in phase 7. Not measured: the GUI itself on hardware, CONFIRM with a Live
loop, Looper SAVING, the pulled cable, a second kit, the VST3 in a DAW; NebulaeOnDaisy 0.2.1 and
LooperMod 0.6.1 were not re-flashed after the fixes (both passed the cycle with the phase-7 build and
the images are unchanged).
