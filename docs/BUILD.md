# Reproducible build

## Pinned dependencies

| Component | Commit / version |
| --- | --- |
| libDaisy | `f715e4875b2d28445d0c11a0a17f064a248f69d0` |
| DaisySP | `2c72eaf9eac5fc0dca1919d65d606da907832618` |
| ARM toolchain tested | xPack GNU Arm Embedded GCC `13.2.1-1.1`, GCC `13.2.1 20231009` |
| Language / CPU | GNU++17, Cortex-M7, Thumb, FPv5-D16, hard float |
| Application | `APP_TYPE=BOOT_SRAM`, `-O3`, 48 kHz / 48 samples |

This libDaisy revision includes `desktop_devkit::Hardware` and Seed3 codec support.
Older revisions bundled with DaisyExamples are not necessarily compatible.
Initialize libDaisy's nested submodules at their pinned commits. DaisySP-LGPL is
not required by this project.

The installer downloads the Linux x86_64 archive from xPack release `v13.2.1-1.1`
and verifies its published SHA-256:

```text
1252a8cafe9237de27a765376697230368eec21db44dc3f1edeb8d838dabd530
```

It installs only into `.tools/`. For another platform, follow the
[official toolchain instructions](https://docs.daisy.audio/tutorials/cpp-dev-env/)
and place `arm-none-eabi-gcc`, `g++`, `ar`, `objcopy`, and `size` on PATH.
The recorded cloud build used the version listed above.

## Commands

```bash
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
bash scripts/install-toolchain.sh
export PATH="$PWD/.tools/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin:$PATH"
make -j4
make test
make test-sanitize
python3 scripts/check-firmware.py build/ToyPiano-v0.1.bin
```

`make libs` explicitly builds both libraries, for example after an intentional
dependency change. `make clean` removes only the application build. After changing
the toolchain or compiler flags, rebuild the libraries as well:

```bash
make clean
make -C libDaisy clean
make -C DaisySP clean
make -j4
```

The official Makefiles build static libraries; `--gc-sections` keeps only required
functions. The application uses DaisySP's `mtof()` and its own small, host-testable
AD envelope and sine-table implementation. No UI framework is required.

Generated files and toolchains are excluded from Git. GitHub Actions uploads
BIN, ELF, and a checksum after a successful build. Downloads from this private
repository require a GitHub account with access.

`make test` checks DSP and the actual callback with simulated inputs and libDaisy's
unmodified DigitalControl debounce code. `make test-sanitize` runs both tests with
AddressSanitizer and UndefinedBehaviorSanitizer. `-fno-sanitize-recover=all` turns
every detected error into a test failure. GPIO/ADC stubs do not replace hardware tests.
