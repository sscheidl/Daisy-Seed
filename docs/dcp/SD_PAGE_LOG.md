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
