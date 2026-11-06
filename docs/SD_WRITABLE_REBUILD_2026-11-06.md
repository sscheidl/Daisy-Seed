# Writable SD profile and Looper 0.6.0: rebuild of PR #94 on `main`

Date 2026-11-06. Branch `claude/sd-rw-rebuild` from `origin/main` @ `edea2ba`. Not merged,
not pushed, nothing flashed. Replaces the usable part of PR #94 (`claude/new-session-fqack4`,
head `0e44e28`), per [SD_WRITE_EXFAT_CLAUDE.md](SD_WRITE_EXFAT_CLAUDE.md) and part B of the
review order `docs/REVIEW_ORDER_SD_2026-11-06.md` on that branch.

All software results below are **MEASURED 2026-11-06** on Windows 11 with the repository's
xPack ARM GCC 13.2.1-1.1, xPack make 4.4.1 and llvm-mingw 20261006 (clang 23.1.3) as `g++`.
That toolchain rebuilt the three 0.5.1/0.2.1 images of clean `main` bit-identically to the
hashes in [SD_CARD_INTEGRATION_2026-11-03.md](SD_CARD_INTEGRATION_2026-11-03.md). Everything
on hardware is **NOT MEASURED**.

## What was taken over

| PR #94 | Here |
| --- | --- |
| `cedf68b`, `c5e5361` | Cherry-picked: `rw/ffconf.h`, `disk_write`, `CTRL_SYNC`, `SD_WRITABLE`, `tests/test_rw_sd.cpp` |
| `45a364d` | Adapted to `main`: `fat_volume.{h,cpp}`, `fatfs_volume.cpp`, `tests/test_card.cpp`, `fake_card.h`, `mkimage exfat-32k` |
| `9cbce3e`, `fd3582c`, `c7fa6ce` | Not taken: `common/sdshare`, `share_control.h`, the `LP` variant, panel 0.6.0. `main` has `sd_admin`, `sd_management.cpp`, `control_sysex.h`, `looper.py` |

Changes beyond the PR:

* `make sd-check` and `arm-check` inject writers into the real ELF or objects and require a
  rejection (`--probe` in `scripts/check-sd-symbols.py`, policies in `common/sd/sd_policy.mk`).
* `ro_sd_init` rejects a NULL `abort` in the writable build, because `HAL_SD_Abort` has
  unbounded waits. `fat_volume.cpp` refuses to compile against any other FatFs configuration.
* `common/sd` and the reader are versioned and registered, kind `storage`.
* [SD_PERSISTENCE.md](../instruments/looper/docs/SD_PERSISTENCE.md) with the failure
  boundaries, and `TestRenameFailureGap` in `instruments/looper/tests/test_store.cpp`.

## Checklist part B

### 1. Writable `common/sd` profile

| Item | Evidence |
| --- | --- |
| One FatFs and configuration per image | ELF symbols: `f_mount` defined once in all three images; `ff_convert`, `FATFS_LinkDriver`, `SD_Driver` absent. `sd.mk` selects one `ffconf.h` |
| Headers match the linked objects | `#error` in `ro_sd.c` and `fat_volume.cpp` (R0.16, `RO_SD_FFCONF`, profile, exFAT). `make test` diffs the two `ffconf.h`: only `FF_FS_READONLY` and comments differ |
| Nebulae stays read-only | `NebulaeOnDaisy-v0.2.1.bin` byte-identical to `main` (`cbc4c376…`); `f_write`, `disk_write` not defined |
| Its check rejects an injected writer | `sd-check` on the ELF: 12 injected writers rejected. ELF plus the real writable `ff.o`: rejected, exit 1. Allow list weakened by `f_write`: `injected writer not rejected: f_write` |
| Positive and negative checks, both profiles | `arm-check`: read-only 11, read-only with reader 12, writable 12 probes, required symbols present; writable objects fail the read-only policy. `sd-check` on Looper and Looper MOD: 12 rejected. Allow list weakened by `f_mkfs`: fails. The 0.5.1 Looper ELF fails the writable policy on libDaisy's glue |

### 2. `disk_write` and sync

| Item | Evidence |
| --- | --- |
| Bounded at every HAL wait | Audit in [common/sd/README.md](../common/sd/README.md#lower-hal-waits-on-the-write-path). Bounded on the write path, with two limits stated there (a CMD13 poll can overrun by 5 s; CMD12 in the interrupt relies on the hardware timeout). **Mount is not bounded in practice**: see open points |
| Programming completion | `WaitProgrammed` after each transfer and in `CTRL_SYNC`. Mutant without it: `test_rw_sd.cpp:130` fails |
| RCC-reset abort | Hook on every fault, `diag::AbortReaderSd` in `fatfs_volume.cpp`; required in the writable build (mutant: `test_rw_sd.cpp:230` fails). Faults in `test_rw_sd.cpp` |
| Cache-safe buffers | IDMA only on the 32-byte-aligned 32 KB `bounce` in SDRAM, cleaned before each write. `test_card.cpp` checks that writes come from `bounce` |
| Removal and reinitialisation | `test_rw_sd.cpp` (removal mid-transfer, 300 random power cuts), `test_card.cpp` |
| Coexistence with the reader | `sd_management.cpp` unchanged and runs without FatFs. `ReleaseCard` disassembled: `FatVolume::Unmount`, then the inlined RCC reset; no `sdshare` symbol in the ELF. Host tests fake `ReleaseCard`, so this order is checked by reading, not by a test |

### 3. Looper persistence on FAT32 and exFAT

| Item | Evidence |
| --- | --- |
| Boundaries documented, no atomic claim | [SD_PERSISTENCE.md](../instruments/looper/docs/SD_PERSISTENCE.md). The claim in `SD_V0_5_PLAN.md` is gone (line 81 here, it was line 77 on the PR branch) |
| Power-loss campaign | FAT32 143 cut points (136 old, 7 new), exFAT 137 (132 old, 5 new), never anything else, next save works every time; also under ASan/UBSan |
| Save-before-share, unusable card | `main.cpp`, `loop_store.{h,cpp}`, `sd_admin.{h,cpp}`, `control_sysex.h` have no diff against `main`. `test_admin`, `test_callback` pass in both builds |

### 4. Registry

`sd-driver` 0.2.0, `sd-fs-readonly` 0.1.0, `sd-fs-writable` 0.1.0, `sd-reader` 0.3.2 in
`components/registry.json`; `granular` lists the read-only profile, `looper` and `looper-mod`
the writable one, all three the driver and reader. `make components` logic and
`tests/test_components.py` (`test_sd_compositions`) pass.

### 5. Unchanged behaviour

| Item | Evidence |
| --- | --- |
| Golden, regular = MOD | `2A2DBCEC41A54F57` from both builds, `cmp` passes, also under sanitizers |
| Panel | `diagnostics/windows` has no diff. It never had a Looper "FAT32 only" string in code; the row was in `diagnostics/docs/LOOPER_PROFILE.md`, now corrected. So no panel version change |
| No protocol change | `control_sysex.h`, `sd_admin`, DDP unchanged; only the version bytes in STATUS read 0.6.0 |

## Validation

| Command | Result |
| --- | --- |
| `make PROJECT=looper test` | exit 0 |
| `make PROJECT=looper test-sanitize` | exit 0 |
| `make PROJECT=looper`, `make PROJECT=looper-mod` | exit 0 |
| `make PROJECT=granular`, `make PROJECT=granular test` | exit 0 |
| `make -C common/sd test test-sanitize arm-check` | exit 0 |
| `pytest diagnostics/tests/python` | 334 passed |
| `scripts/check-firmware.py` on all three images | PASS |
| `sd-check` on all three images | PASS |
| `tests/test_components.py`, `tests/test_firmware_checker.py` | OK |

**NOT MEASURED:** `make test-projects`, `make all-projects`, Nebulae `test-sanitize` and
`test-thread` (Nebulae's sources are unchanged, its image is byte-identical), the Diagnostics
firmware tests, CI, and everything on hardware.

## Images

| File | Bytes | SHA-256 |
| --- | --- | --- |
| `Looper-v0.6.0.bin` | 188896 | `cc42dbe1a0900c77146b557056d22e1fcb6a0395d15ddc54c7433defa649e269` |
| `LooperMod-v0.6.0.bin` | 222600 | `ce93489dcf6b2de963b6ab7786188829748a6c4de8db054bdb629a42e2f51286` |
| `NebulaeOnDaisy-v0.2.1.bin` | 183740 | `cbc4c37686be32eca39825459f95c676af7a7010c3c719faba1cf0def4e381ff` (unchanged) |

Local builds, not releases.

## Memory

DTCM left above `.bss` for the main stack, against the 12 KiB `memory.ld` asserts:

| Image | 0.5.1 | 0.6.0 |
| --- | --- | --- |
| Looper | 17.6 KiB | 15.9 KiB |
| Looper MOD | 15.1 KiB | 13.3 KiB |

The FatFs objects moved from SDRAM to `.bss`, which costs 1.8 KiB of DTCM. The largest FatFs
frames in the writable profile are `f_rename` 272 and `f_unlink` 240 bytes (`-fstack-usage`).
Actual stack use on the board is NOT MEASURED. SDRAM gains the 32 KB bounce buffer.

## Behaviour changes worth checking

* Waits: libDaisy's 30 s per transfer becomes 250 ms per read and 1 s per write, including
  programming. A slow card that used to pass could now fail with a write fault. NOT MEASURED.
* Multi-block IDMA writes are new on this board; the tested reader writes single blocks.
* Import skips names starting with `.` (macOS `._*`), from `45a364d`.

## Open points for the human

1. **Failed rename** (SD_PERSISTENCE.md): the retry cuts the only copy on the card. Documented
   and pinned by a test, not fixed. Finishing the pending rename before a new save would close
   it, but changes the store's behaviour.
2. **Cross-linked names** after a power loss between two directory sectors: reasoned from
   `f_rename`, not covered by any test.
3. **Mount waits**: `HAL_SD_Init` and `HAL_SD_ConfigWideBusOperation` can hold the main loop
   for practically unlimited time if the card goes away in those milliseconds. Same on 0.5.1
   and Nebulae; fixing it means patching the pinned libDaisy.
4. **Part A** of the review order (share handoff race in `main.cpp`) is untouched here.
5. PR #94 itself: close it or force-push this branch over it. The human's decision.

Hardware checklist: [HARDWARE_TEST.md, section 7 and 7a](../instruments/looper/docs/HARDWARE_TEST.md#7-microsd).
