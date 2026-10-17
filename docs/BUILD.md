# Reproducible project builds

Run commands from the repository root. `projects.mk` lists project IDs, paths and
implementation status. Each project owns its sources, tests and output directory;
`common/desktop.mk` provides the official board build. Dependencies are shared.

## Pinned dependencies

| Component | Commit / version |
| --- | --- |
| libDaisy | `f715e4875b2d28445d0c11a0a17f064a248f69d0` |
| DaisySP | `2c72eaf9eac5fc0dca1919d65d606da907832618` |
| Tested ARM toolchain | xPack GNU Arm Embedded GCC `13.2.1-1.1`, GCC `13.2.1 20231009` |
| Language / CPU | GNU++17, Cortex-M7, Thumb, FPv5-D16, hard float |
| Toy Piano | BOOT_SRAM, `-O3`, 48 kHz / 48 samples |
| Ambient Reverb | BOOT_SRAM, `-O3`, 48 kHz / 32 samples; 1,114,112 bytes of external SDRAM reserved |

This libDaisy version includes Desktop Dev Kit and Seed3 codec support. Older
DaisyExamples dependency pins are not automatically suitable. Fetch libDaisy's
own submodules recursively at their pinned commits. DaisySP-LGPL is not required.

The Linux x86_64 installer downloads xPack `v13.2.1-1.1`, verifies the published
SHA-256 below and installs only into `.tools/`, without sudo:

```text
1252a8cafe9237de27a765376697230368eec21db44dc3f1edeb8d838dabd530
```

For other platforms follow the [official setup](https://docs.daisy.audio/tutorials/cpp-dev-env/)
and provide `arm-none-eabi-gcc`, `g++`, `ar`, `objcopy` and `size` in PATH.

## Commands

```bash
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
bash scripts/install-toolchain.sh
export PATH="$PWD/.tools/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin:$PATH"
make list
make PROJECT=toy-piano -j4
make PROJECT=toy-piano test
make PROJECT=toy-piano test-sanitize
make PROJECT=toy-piano test-thread
make PROJECT=reverb-fx -j4
make PROJECT=reverb-fx test
make PROJECT=reverb-fx test-sanitize
make test-release
python3 scripts/check-firmware.py
python3 scripts/check-firmware.py effects/reverb/build/AmbientReverb-v0.1.bin
```

Toy Piano is the default when PROJECT is omitted. `make -C instruments/toy-piano`
builds the same firmware. Output is in `instruments/toy-piano/build/`; the former
root `build/` location is no longer used. Ambient Reverb builds into
`effects/reverb/build/`. Planned or unknown projects are rejected
rather than silently building a different instrument.

`make all-projects` and `make test-projects` process every implemented project in
`BUILDABLE_PROJECTS`. `make libs` explicitly rebuilds shared archives. After a
compiler/toolchain change, clean both the selected project and libraries:

```bash
make PROJECT=toy-piano clean
make -C libDaisy clean
make -C DaisySP clean
make PROJECT=toy-piano -j4
```

The official Makefiles build static archives and link only needed sections with
`--gc-sections`. Toy Piano uses DaisySP's `mtof()` with its own AD and sine-table DSP.
Generated files and toolchains are excluded from Git.

## Tests and CI

`make test` runs DSP tests and the production callback with fake GPIO/ADC and
libDaisy's unchanged DigitalControl debouncer. It does not replace hardware tests.
`make test-sanitize` runs ASan/UBSan with `-fno-sanitize-recover=all`;
`make PROJECT=toy-piano test-thread` runs ThreadSanitizer on the MIDI queue tests.
The reverb's host and sanitizer tests cover its DSP; its board controls and
callback timing still require a hardware check.
Shared release-preflight tests remain in root `tests/`; `make test-release` uses
a temporary Git repository and a fake API without creating releases.

The CI matrix builds Toy Piano and Ambient Reverb, deriving each BIN name from
its project version. It uploads BIN, ELF and SHA256SUMS for both.
Private-repository downloads require
an authorized GitHub login.

## Adding another implemented project

1. Add sources, tests and a project validation report. Set a unique TARGET and
   `.DEFAULT_GOAL := all`; include `../../common/desktop.mk` from its Makefile.
2. Only after a real ARM build, mark it buildable and add it to BUILDABLE_PROJECTS.
3. Add its path and artifact naming to CI; document budgets and manual hardware tests.

## Version and release

Toy Piano's sole version source is `instruments/toy-piano/src/version.h`.
It determines `instruments/toy-piano/build/ToyPiano-v<version>.bin`, the embedded
`TOYPIANO-FW-VERSION=<version>` marker and CI artifact name.
`make -s PROJECT=toy-piano version` prints it. The firmware checker verifies the
name, vector table and embedded version against that same header.

The Release workflow builds Toy Piano from main, runs tests, then creates
`v<version>` with BIN, ELF and SHA256SUMS. Notes come from
`instruments/toy-piano/CHANGELOG.md`. Existing tags and the complete paginated
release list, including drafts, are checked first. Existing versions or API errors
abort the workflow.

1. Update the project version and changelog for a new release.
2. After merge, a change to that version header triggers the workflow. It can also
   be started manually under Actions → Release → Run workflow on main.
3. By default it creates a **draft** (`publish=false`). Test its BIN on the device
   before publishing the draft. Publication creates the tag on the built commit.
   `publish=true` publishes immediately.

Only the release job requests `contents: write`; normal CI is read-only.
The existing v0.2 name is retained for Toy Piano. Other instruments need their
own version/tag conventions before their first release.
Ambient Reverb v0.1 is a CI artifact, not a published GitHub Release.
