# Working in this repository (Codex, Claude Code)

Firmware for the Electro-Smith Seed3 Desktop Kit plus host tools. Start with
[README.md](README.md), [HANDOVER.md](HANDOVER.md) and the project's own README.

## Build and test before every push

| Area | Commands |
| --- | --- |
| One instrument | `make PROJECT=<id> -j4`, `make PROJECT=<id> test`, `test-sanitize`, `test-thread` (`make list` shows the IDs) |
| All instruments | `make all-projects`, `make test-projects`, `make test-release` |
| Mod on Daisy module | `make -C modules/mod-on-daisy test test-sanitize test-thread`; browser editor e2e: `make -C modules/mod-on-daisy test-editor` (Playwright) |
| Editors | `make -C editors/mod-on-daisy test test-sanitize`; JUCE app via CMake, then `"Mod on Daisy Editor" --selftest out` |
| Hardware diagnostics | `make -C diagnostics/firmware test test-sanitize`, `bash diagnostics/scripts/build-firmware.sh`, `python -m pytest diagnostics/tests/python` |
| Images | `python scripts/check-firmware.py <bin>`; hardware-test package: `python scripts/hardware_test_package.py` |

CI (`.github/workflows/build.yml`, `editor.yml`, `diagnostics.yml`) runs the same; keep it green.

## Rules

* **Hardware is the human's.** Never flash, install a bootloader (`program-boot`) or
  run `program-dfu` unless the human has put the kit into bootloader mode and asked
  for it. Listening tests, keys, pots, cables and MIDI keyboards are done by the human;
  the agent runs the command-line steps and records what the human reports.
* **Report only what was observed.** Mark results `GEMESSEN` (with date) or
  `NICHT GEMESSEN`. Never fill in expected values as results.
* **Releases**: merging a change of `instruments/toy-piano/src/version.h` to `main`
  creates a release *draft*. Do not publish releases; the human does after the
  hardware test.
* **Sound must not change by accident.** Golden tests (FNV-1a over the output) pin
  Toy Piano and Physical Modeling: with an empty matrix, Toy Piano v0.3 must match the
  v0.2 hash and the Physical Modeling `MOD=1` build the v0.1.0 hash. The Physical
  Modeling integration stays behind `PHYSICAL_MODELING_MOD` / `TORUS_VOICE_MODULATION`,
  so the regular image stays byte-identical. A golden hash changes only on purpose,
  with the reason in the changelog.
* **Real-time code**: no allocation, locks or blocking calls in the audio callback;
  Mod on Daisy runs at 1 kHz control rate. The protocol is `modules/mod-on-daisy/docs/PROTOCOL.md`
  (SysEx 0x7D); change it only together with firmware, `mod_probe.py`, both editors
  and their tests.
* **No flash writes** for Mod on Daisy storage until the human approves (D8,
  `STORAGE.md`).
* **Pinned versions**: libDaisy, DaisySP, xPack GCC 13.2.1-1.1 (`docs/BUILD.md`) and
  JUCE 9.0.4 (`editors/mod-on-daisy/CMakeLists.txt`). Do not bump them as a side effect.
* **Language**: code and code comments in English; user-facing documentation of the
  Mod on Daisy module, the editors and the hardware tests in German, as it is now.
* Work on a branch and merge via pull request; describe validation (commands, results,
  what is still untested on hardware) in the PR.
