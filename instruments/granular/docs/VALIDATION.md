# Local validation - v0.2.0

**RESULT: SOFTWARE PASS / HARDWARE PENDING.** Run 2026-11-01 (Linux x86_64 container,
g++ 13/clang, xPack GCC 13.2.1-1.1, libDaisy `f715e48`, DaisySP `2c72eaf`). Nothing was
flashed or listened to; every hardware item is NICHT GEMESSEN
([plan](HARDWARE_TEST_PLAN.md)).

Firmware source: commit `fbc0c47` on `claude/new-session-fqack4` (later commits change
documentation only; `src/` and `Makefile` are identical).

## Artifact

| File | Value |
| --- | --- |
| `build/NebulaeOnDaisy-v0.2.0.bin` | 183,652 B, SHA256 `3734dde5ee3d6ab462706f3c711c9ff107539959a7dfbb71d9a7663a4e5e2f19` |
| Reproducibility | identical SHA256 from a second clean build in a separate worktree |
| `.elf`, `.map`, `SHA256SUMS` | in `build/` (not tracked) |
| Marker | `NEBULAE-FW-VERSION=0.2.0` |

## Commands and exit codes

| Command | Exit | Result |
| --- | --- | --- |
| `make PROJECT=granular -j8` | 0 | BOOT_SRAM image; linker warnings (`_close` etc. not implemented, RWX segment) are identical in the v0.1.1 baseline |
| `make PROJECT=granular test` | 0 | `test_nebulae` 22,253,411 assertions (incl. SW17, panel LEDs, FILE per bank, F6, F3 A/B), `test_banks` 2,471, `test_control_sysex` 200,334, `test_admin` 57, `test_callback` 1,268,907 per image (FAT32 and exFAT), `test_queue`, `test_bridge`, e2e Windows/return on FAT32 and exFAT (4 files hash-verified each) |
| `make PROJECT=granular test-sanitize` | 0 | same suite with ASan/UBSan/float-cast-overflow, 22 PASS lines |
| `make PROJECT=granular test-thread` | 0 | TSan: queue, bridge, control SysEx mailbox (156,710 complete requests in the final run; the count varies by scheduling) |
| `python3 scripts/check-firmware.py build/NebulaeOnDaisy-v0.2.0.bin` | 0 | `PASS: v0.2.0, 183652 bytes, stack=0x20020000, reset=0x24002a81, BOOT_SRAM vectors` |
| `python3 scripts/check-granular-elf.py build/NebulaeOnDaisy-v0.2.0.elf --host-test` | 0 | hard-float, BIN = ELF, both IDMA buffers in AXI SRAM, 16 KiB stack floor, 8 MiB reserve, file capacity 41,695,328 B, WAV boundary tests at that capacity |
| `make -C instruments/granular sd-check` | 0 | 36 banned symbols absent (all FatFs writes, libDaisy FatFs/`SD_Driver`, erase/lock, PIO/IT writes), 6 required present; MSC single-block IDMA write allowed |
| `make -C common/sd test test-sanitize arm-check` | 0 | 148,869 checks incl. the new abort hook; injected write detected |
| `make -C diagnostics/firmware test` + both images | 0 | images byte-identical to `main` (`0564de97…f577`, `0be6f55c…4ea2`) after the reader move |
| `python -m pytest diagnostics/tests/python` | 0 | 309 passed (281 before + 28 Nebulae profile) |
| PyInstaller onefile (Linux smoke build) `--sim --selftest 3`, `--sim-nebulae --selftest 4` | 0 / 0 | packaging works; the Windows EXE is built by `Actions -> Hardware diagnostics` |
| `make all-projects` / `make test-projects` / `make test-release` | 0 / 0 / 0 | full repo regression incl. Toy Piano/Physical Modeling golden hashes |
| `python3 -m unittest tests/test_components.py`, `make components` | 0 | suite selection updated for the new dependencies |
| `git diff --check` | 0 | clean |

## What the host tests cover

* Ownership: SHARE only explicit; PERFORM/BOOT only from LOCAL; BYE/timeout/disconnect
  never return; INTERRUPTED/ERROR quarantined until RETURN; S4 gesture only from
  LOCAL/EJECTED; boot intent one-shot, every single-bit corruption and 200,000 random
  records rejected.
* Simulated round trip: writable FatFs R0.16 ("Windows") mounts the card only through
  SCSI over the production MscBot + CardReader, creates a bank, writes WAVs with
  partial/multi-sector writes, renames and deletes, ejects (PREVENT/ALLOW, SYNCHRONIZE
  CACHE, START STOP UNIT); then the production read-only stack rescans the banks and
  every PCM hash matches. Not a substitute for the hardware test.
* Production `main.cpp` on the real FatFs/ro_sd stack: startup scan and load, invalid
  WAV, bank view without load, LED dimming, KEYBOARD/SAMPLE S9+S16 without reset,
  NEBULAE gesture, MIDI regressions, STATUS/SHARE (busy, confirm, accepted),
  PREPARE_SHARE (ACK first, Note Off + CC123, audio stopped, SD released, intent,
  `DAISY_SKIP_TIMEOUT`, faded output), intent dispatch, card swap; the audio callback
  never reads the SD card.
* F3: packed forward FFT equals two separate forwards within 2e-5 relative; Mincer A/B
  on a stereo source: max |diff| 1.5e-4, error/signal -88.0 dB, identical hop timing.
* F6: 0.5 ms floor, 6x smoothed clock cap, fast/slow/saturated clocks.

## Open (hardware)

USB enumeration of both personalities on Windows, boot-intent survival through the
external bootloader (R1), SW17 lever direction, exFAT cards, transfer speed, CPU peak
and overruns, codec behaviour across the restart, listening comparison with v0.1.1.

---

# Local validation - v0.1.0

**RESULT: SOFTWARE PASS / HARDWARE PENDING.**

Started 2026-10-19; review corrections validated 2026-10-20, Europe/Berlin.
The original local PASS at `c71c67a` was incomplete: independent review exposed
a sticky post-note mute and an overdub SOURCE regression. Both were reproduced
before correction (4 failed assertions in 20 review checks), then corrected and
retested (20/20 passed). Audio-level regression tests now cover keyboard release,
both keyboard mode exits, MIDI release/velocity-zero, CC120/123, reset, disconnect,
AllOff, and multi-block overdub publication. Normal overdubs now also lock
START/SIZE/SPEED, as first recordings do.

Branch: `codex/nebulae-on-daisy-v0.1.0`, updated with main `9deb7dc`.
Nebulae sources/tests live in `instruments/granular/`; Stardust belongs to
`instruments/looper/`. The diff against main changes no Looper, Reverb, Toy Piano
or Physical Modeling source/docs. Shared changes integrate Nebulae with the
catalogue/build/checks. No board was flashed, bootloader changed or release made.
Upstream pins and license decisions are in [the audit](UPSTREAM_ANALYSIS.md).

[Draft PR #17](https://github.com/sscheidl/Daisy-Seed/pull/17) targets main.
[CI run 39485407516](https://github.com/sscheidl/Daisy-Seed/actions/runs/39485407516)
built source `ed14ea5fc7218f69485d849e182487588b638233` on Ubuntu 24.04.
Its Nebulae job passed GCC host tests, ASan/UBSan/float-cast-overflow, both Linux
ThreadSanitizer concurrency executables, ARM compilation and image/ELF/capacity
checks. All four matrix jobs, including the full build/test catalogue, passed.
The [downloaded artifact](https://github.com/sscheidl/Daisy-Seed/actions/runs/39485407516/artifacts/12292619536)
contains BIN/ELF/MAP/SHA256SUMS. Its BIN was checked locally and is byte-identical
to the Windows ARM build (SHA256 below). Initial Linux failures were four
misleading test loop layouts and a missing explicit `<cstring>` include; both
were corrected without disabling compiler warnings or tests.

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

* BIN: `NebulaeOnDaisy-v0.1.0.bin`, **162,540 bytes**.
* SHA256: `6f4acd2d19a608bb4b100878cfb6648d7e8a980359261edd3950a5691a113e0e`.
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
| Remote Nebulae CI/PR | PASS | Draft #17, run 39485407516; Linux GCC/sanitizers/TSan, ARM and downloaded artifact/hash verified |
| Target tests | PENDING-HARDWARE | CPU/stack high-water, stereo analog path, switch lever, SD, USB enumeration and sound require the short hardware plan |

## Executed tests and corrective checks

```sh
make PROJECT=granular test HOST_CXX=clang++
make PROJECT=granular test-sanitize HOST_CXX=clang++
make all-projects -j4
make test-projects HOST_CXX=clang++ HOST_PYTHON=python
python scripts/test-release-windows.py
```

Core suite: **5,852,467 assertions**, plus separately passing production callback,
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
normally and under ASan/UBSan; Linux CI executed both TSan tests successfully
in the run linked above.

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

External-clock trigger-only grain duration still uses the bounded internal
size law, without the original six-clock-period cap. Stereo FFT work remains
aligned on both channels: up to six 2048-point transforms in one callback. Engine
storage is in SDRAM. Do not infer deadline safety from host tests; measure DWT
peak/overrun counters on hardware before treating real-time margin as validated.

Local artifacts and [hardware smoke steps](HARDWARE_TEST_PLAN.md) are ready for
the user. The missing hardware test is **not** a software HOLD. The PR remains
a draft; merge, flash, bootloader update and release are separate actions.
