# Working in this repository (Codex, Claude Code)

Firmware for the Electro-Smith Seed3 Desktop Kit plus host tools. Start with
[README.md](README.md), [HANDOVER.md](HANDOVER.md) and the project's own README.

## GitHub Actions budget policy (mandatory)

The GitHub Free Actions quota is shared across private repositories. Preserve it.

* **Automatic**: `.github/workflows/quick.yml` runs short Linux host tests on
  code PRs and pushes to `main` (15-minute timeout, superseded runs cancelled).
  Only relevant instrument/module host tests run; docs-only changes do not trigger CI.
* **Artifacts**: self-test screenshots/logs are retained for 3 days, firmware
  and native app binaries for 14 days; published Releases are separate.
* **Manual firmware**: `Actions -> Firmware -> Run workflow`, selecting one
  project (default: `physical-modeling-mod`). The expensive `all` choice is
  explicitly opt-in. No full ARM matrix on push/PR.
* **Manual editor/diagnostics**: `Actions -> Editor` and
  `Actions -> Hardware diagnostics` are dispatched only when native app or
  firmware artifacts are needed.
* **Manual release**: `Actions -> Release -> Run workflow` executes the complete
  Toy Piano host, sanitizer, thread, ARM-build and release-guard tests before
  creating a draft by default. Never launch releases simply because a version
  header changed. Publication and hardware acceptance are the human's decision.
* Run tests for the changed component locally before pushing. Run the full
  project/sanitizer suites when preparing a release or specifically investigating
  a fault, not after every cosmetic or documentation edit.
* Do not add broad automatic triggers, expensive runners or parallel build
  matrices without the human's explicit approval. Budget-exhausted CI is not
  evidence of a software failure.

## Commands for targeted local validation

| Area | Commands |
| --- | --- |
| One instrument | `make PROJECT=<id> -j4`, `make PROJECT=<id> test`, `test-sanitize`, `test-thread` (`make list` shows the IDs) |
| All instruments | `make all-projects`, `make test-projects`, `make test-release` |
| Mod on Daisy module | `make -C modules/mod-on-daisy test test-sanitize test-thread`; browser editor e2e: `make -C modules/mod-on-daisy test-editor` (Playwright) |
| Editors | `make -C editors/mod-on-daisy test test-sanitize`; JUCE app via CMake, then `"Mod on Daisy Editor" --selftest out` |
| Hardware diagnostics | `make -C diagnostics/firmware test test-sanitize`, `bash diagnostics/scripts/build-firmware.sh`, `python -m pytest diagnostics/tests/python` |
| Images | `python scripts/check-firmware.py <bin>`; hardware-test package: `python scripts/hardware_test_package.py` |

Automatic CI intentionally uses only `quick.yml`. Full firmware, native editor and diagnostics builds are manual; run the relevant validations locally before pushing.

## Rules

* **Hardware is the human's.** Never flash, install a bootloader (`program-boot`) or
  run `program-dfu` unless the human has put the kit into bootloader mode and asked
  for it. Listening tests, keys, pots, cables and MIDI keyboards are done by the human;
  the agent runs the command-line steps and records what the human reports.
* **Report only what was observed.** Mark results `GEMESSEN` (with date) or
  `NICHT GEMESSEN`. Never fill in expected values as results.
* **Releases**: changing `instruments/toy-piano/src/version.h` does not
  start a release. Only the human manually dispatches the release workflow;
  publish a draft only after hardware acceptance.
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
