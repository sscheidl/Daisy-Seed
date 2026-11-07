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
