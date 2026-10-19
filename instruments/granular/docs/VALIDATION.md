# Local validation - v0.1.0

**RESULT: LOCAL SOFTWARE PASS / HARDWARE PENDING.**

Started 2026-10-19, final checks 2026-10-20, Europe/Berlin. Scope is the user's
explicit choice of full **local** implementation and validation. Remote delivery
(push/PR/CI execution) is N/A for that selected scope; it is not represented as a
passed remote CI gate or a completed remote-delivery masterprompt. No hardware
was flashed, no bootloader changed, no sound auditioned and no release published.

Base: main `abc5a71`. Local branch `codex/nebulae-on-daisy-v0.1.0`.
Use `git rev-parse HEAD` for the final local source commit; upstream full pins and
license decisions are in [the audit](UPSTREAM_ANALYSIS.md).

## Artifact evidence

Executed from the actual Windows repository root:

```sh
make PROJECT=granular -j4
python scripts/check-firmware.py instruments/granular/build/NebulaeOnDaisy-v0.1.0.bin
python scripts/check-granular-elf.py instruments/granular/build/NebulaeOnDaisy-v0.1.0.elf
arm-none-eabi-size instruments/granular/build/NebulaeOnDaisy-v0.1.0.elf
```

xPack ARM GCC 13.2.1-1.1; GNU++17, Cortex-M7, hard float, -O3, official
BOOT_SRAM linker plus project reservations. Real BIN/ELF/MAP and SHA256SUMS exist
under `instruments/granular/build/`; build outputs are ignored, not source tracked.

The ELF checker was also run with `--host-test`: it feeds the actual linked
capacity (41,695,360 bytes) into the production WAV parser boundary tests, covering
the maximum accepted length and one extra frame for every rate/channel format.

* BIN: `NebulaeOnDaisy-v0.1.0.bin`, **162,956 bytes**.
* SHA256: `bc9ec2a4ae57cc3e4b52ee9e2406e89074da9f8c93bdbac186ddb4b0e6d839e8`.
* Stack vector `0x20020000`, reset `0x240006b5`; exactly one version marker.
* ELF: actual ARM machine, hard-float ABI, byte-identical load image, SRAM bounds,
  aligned isolated SD sector, 64 KiB bounded heap, DTCM stack floor and 8 MiB SDRAM
  reserve pass. [Memory details](MEMORY_BUDGET.md) include all four format limits.

## Stages and real evidence

| Stage | Result | Evidence |
| --- | --- | --- |
| 0 audit | PASS | Pinned original repository and bundled v2.1.2 instrument, exact official manual, port/version comparison, primary/secondary matrix and MIT/LGPL notices in UPSTREAM_ANALYSIS |
| 1 board/build | PASS | Actual root ARM build, official Hardware/Switch3/BOOT_SRAM, vector/version checker and ELF memory checks; 512 silently clamps in pinned driver, corrected to verified 256 |
| 2 stereo DSP | PASS | FFT roundtrip, seven original windows and 601 morphs, reference density values, BLEND endpoints/center, 18 stereo tone tests over 44.1/48 kHz, negative/positive speed and three pitches; freeze/reset, recording and parameter stress; real ARM link |
| 3 SD/PCM/RAM | PASS | Valid rate/channel matrix, exact capacity and one-frame overflow for all four formats, reordered/padded chunks, duplicates, header truncation, integer overflow and 10,000 corruptions; production FatFS adapter path/PCM decode and actual linked SD_Driver; no file read in callback |
| 4 UI/live | PASS | All 16 slots/keys, five actions, three switch values with stability, held-key release/suppression, SOURCE tap/hold/catch, all primary/secondary pot mappings, LEDs, stereo capture, full/early overdub and circular stop; poisoned-SDRAM construction |
| 5 MIDI | PASS | Actual callback/parser tests with running status and realtime interleave, Note On/Off/velocity zero, note/channel/local-C5 collisions, pitch bend, eight CCs, all-notes-off/reset/disconnect and overflow recovery; queue and producer/consumer load tests |
| 6 local integration | PASS | Root build/tests, ASan/UBSan, other-project ARM/host regression commands, release-guard tests, firmware/ELF/SHA checks, licenses and diff check; CI configuration extended |
| Remote CI/PR | N/A (local scope) | Workflow configured for BIN/ELF/MAP/SHA and Linux TSan, but no push or run ID/artifact URL exists |
| Target tests | PENDING-HARDWARE | CPU/stack high-water, stereo analog path, switch lever, SD, USB enumeration and sound require the short hardware plan |

## Executed tests and corrective checks

```sh
make PROJECT=granular test HOST_CXX=clang++
make PROJECT=granular test-sanitize HOST_CXX=clang++
make all-projects -j4
make test-projects HOST_CXX=clang++ HOST_PYTHON=python
python scripts/test-release-windows.py
```

Core suite: **2,610,935 assertions**, plus separately passing production callback,
SD-driver, one-million-message queue and 128-load concurrent bridge executables.
Sanitizers: address, undefined behavior and float-cast-overflow, recovery disabled.
All these local targets executed successfully. Host durations are deliberately
not used as Cortex-M7 performance figures.

Targeted corrections included the native block-size clamp, explicit clean SDRAM
construction, file-load ack release before a second load, manual circular stop,
duplicate C5 ownership, stale MIDI Note Ons after panic, original RampDown window,
and a bounded `_sbrk`. Boundary/fault regression cases cover those paths. The SD
driver test covers isolated bounce memory, cache calls, arbitrary destination
alignment, sector count limit, per-sector USB service, timeout/error/absence and
read-only writes. The FatFS peripheral is simulated for host tests; physical
microSD access is pending, while the real BSP/HAL/FatFS path is ARM-linked.

`test-thread` was attempted locally: LLVM-MinGW reports `unsupported option
'-fsanitize=thread' for target 'x86_64-w64-windows-gnu'`. Therefore local TSan is
**N/A (unsupported platform)**, not PASS. The same queue and bridge tests run
normally and under ASan/UBSan; Linux CI is configured to run TSan after a push.

Direct Windows release-guard invocation initially picked the System32 WSL shim
(no distribution installed), then MSYS truncated a literal newline test argument.
The checked-in Windows launcher explicitly uses Git Bash and preserves that
argument through a quoted environment hop. All **8 original tests** then pass;
the production guard and its fake paginated API remain unchanged.

Toy Piano, Physical Modeling and Reverb source/submodule diffs are empty.
`make all-projects` and their existing host tests pass. Shared changes are limited
to quoted recursive-make executable paths (Windows spaces), catalogue/build/CI
and firmware-checker integration. Release workflow is unchanged. Inherited ARM
startup FPU, nosys stdio and executable-SRAM RWX linker warnings are not treated
as evidence of hardware operation; project SD uses FatFS, not nosys stdio.

## Limits and handoff

This is a bounded adaptation, not a claim of original-device sample-identical
audio. README lists grain scheduling/FM/randomization, interpolation and bank
publication differences, encoder-to-pot substitutions and missing save-to-SD.
No extra effects are present. Ten grains per source channel include pending
delays and can drop new grains under overload. CPU headroom at 256 samples remains
unmeasured. The inherited LED-DMA and USB-transmit limitations are explicit.

Local artifacts and [hardware smoke steps](HARDWARE_TEST_PLAN.md) are ready for
the user. The missing hardware test is **not** a software HOLD. Remote CI and PR
delivery have not been performed because the user selected local work.
