# Looper v0.5.0: exFAT, shared writable SD driver, SD management from Windows - PLAN

Status: plan of 2026-11-03, nothing implemented. Hardware behaviour of every step is
**NICHT GEMESSEN** until the human runs it.

## Starting point (`main` @ `53927d2`, Looper v0.4.0)

| Area | State | Hardware |
| --- | --- | --- |
| File system | libDaisy FatFs **R0.12c** (`USE_FATFS = 1`, `FatFSInterface`, libDaisy `SD_Driver`), **FAT32 only**, no exFAT | NICHT GEMESSEN (HARDWARE_TEST section 7 unchecked) |
| Card use | **writes**: `looper/loop.wav` persistence (32-bit float, via `loop.tmp` + rename), delete on erase, `mkdir`; reads: recall at boot, import from `looper/loops/` | NICHT GEMESSEN |
| Abstraction | all card access through `looper::Volume` (`src/loop_store.h`); only `src/fatfs_volume.cpp` touches FatFs; host tests use `tests/fakes/memory_volume.h` | - |
| Transfer | 32 KB `transfer` buffer in SDRAM, libDaisy BSP multi-block DMA, 30 s waits inside libDaisy's `SD_write` | NICHT GEMESSEN |
| USB | libDaisy `MidiUsbHandler` (CC control, bootloader SysEx) | - |
| Variants | `Looper-v0.4.0` and `LooperMod-v0.4.0` (MOD=1); golden test requires identical sound | golden hash pinned |

`common/sd` (from Nebulae 0.2.0) is read-only by construction (`FF_FS_READONLY 1`), so
the Looper cannot use it as is: it must write.

## Goal

1. **Writable shared driver** in `common/sd`, FAT32 + exFAT, R0.16 with patches 1+2, one
   FatFs per image. Looper switches to it; behaviour of loop persistence and import is
   unchanged, exFAT cards work.
2. **SD management from Windows** for the Looper, built from the same parts as Nebulae
   (card reader, boot intent, DDP responder, control panel profile).
3. No change to the sound: golden tests of Looper and LooperMod unchanged.

## Decisions (proposed)

| ID | Decision |
| --- | --- |
| L1 | `common/sd` gets a **second, writable configuration** next to the read-only one: `common/sd/rw/ffconf.h` (`FF_FS_READONLY 0`, `FF_FS_EXFAT 1`, LFN, `FF_USE_MKFS 0`, no format, no `f_setlabel`, `FF_FS_NORTC 1`) and a disk layer `sd_rw.c` with `disk_write`. `sd.mk` selects it with `SD_WRITABLE = 1`. Read-only images (Nebulae) stay exactly as they are; the symbol check keeps banning writes there. |
| L2 | `disk_write` uses IDMA through an isolated, cache-line aligned buffer, waits for **programming completion** (CMD13 transfer state) before it returns, has a bounded timeout and the hardware-tested RCC-reset abort. Multi-block writes (up to the 32 KB buffer = 64 blocks) are needed for speed: single-block writes would make an 87 s loop (33 MB) take minutes. Multi-block IDMA writes are **new** compared with the hardware-tested card reader (single block) and need a board measurement. |
| L3 | The symbol policy for writable images allows only what writing a file needs: block writes and the FatFs file/dir API; formatting (`f_mkfs`, `f_fdisk`), erase (CMD32/33/38), lock (CMD42), `f_setlabel` stay banned. `CTRL_SYNC` really syncs (waits for transfer state). |
| L4 | Looper replaces `FatFSInterface`/libDaisy `SD_Driver` with the shared driver; `Volume` and `LoopStore` stay. The 30 s waits inside libDaisy's driver disappear (bounded per transfer, card check and USB service between transfers). |
| L5 | SD management reuses the Nebulae pieces, moved from `instruments/granular/src` to a shared component (e.g. `common/sdshare/`): boot intent, DDP responder (`Admin`), the management run loop (card reader wiring, LEDs, S4 escape). App name, USB product string/bcdDevice/serial suffix and the restore payload are parameters. Nebulae's image must stay functionally identical (its tests prove it; the BIN may change by code layout only). |
| L6 | Control SysEx for the Looper: same frame as Nebulae with its own prefix `F0 7D 4C 50` ("LP"), STATUS and SHARE. SHARE answers BUSY while recording, resampling, importing or **saving**. Before restarting, the firmware **finishes the pending `loop.wav` save** (or refuses), so the current loop survives the restart through the normal recall; only undo is lost. CONFIRM_REQUIRED only when the save fails (e.g. card full). |
| L7 | Control panel: the Nebulae profile becomes a generic "SD management" profile for apps listed by name (`NebulaeOnDaisy`, `Looper`), each with its MIDI prefix and texts. |
| L8 | Version: Looper **0.5.0** (both images), control panel 0.6.0. Release only by the human. |

## Steps (each its own commit; one PR)

1. **Writable driver** (`common/sd`): `rw/ffconf.h`, `sd_rw.c` (`disk_write`, real
   `CTRL_SYNC`, `GET_SECTOR_COUNT`), `sd.mk` switch, symbol policy for writable images.
   Host tests on FAT32 and exFAT images: create/overwrite/rename/delete/mkdir, files across
   cluster boundaries, odd write sizes, full card, write errors and timeouts at every
   point, card removal during a write, abort and remount; patch-2 corruption corpus also
   with writes; the read-only build and its tests unchanged.
2. **Power-loss robustness of the Looper's save** on real images: interrupt the write
   sequence (`loop.tmp` data, `f_close`, `f_unlink`, `f_rename`) at every block, then
   remount and `Recall`: either the old or the new loop, never a broken one; FAT32 and exFAT.
3. **Looper on the shared driver** (`fatfs_volume.cpp` rewritten, `USE_FATFS = 0`),
   production callback test on real FatFs images (as Nebulae's `test_callback`), golden
   tests of both variants unchanged, ARM build of both, `sd-check`, ELF/memory check.
4. **Shared SD management component** extracted from Nebulae; Nebulae tests unchanged
   and green.
5. **Looper SD management**: control SysEx (L6), PREPARE_SHARE (finish save, fade, notes
   off, stop audio, unmount, release SD, intent, restart), management personality,
   restore after return (recall `loop.wav`, rescan `loops/`), S4 escape, LEDs.
6. **Control panel 0.6.0**: generic profile (L7), Looper texts, simulator, tests.
7. Docs (README, HARDWARE_TEST German steps for exFAT, Windows share, power-loss checks
   on a test card), VALIDATION, release report, CI run for the EXE.

## Risks

| ID | Risk | Mitigation |
| --- | --- | --- |
| R1 | Multi-block IDMA write timing/abort never measured on the board | bounded waits, RCC abort, write-then-verify option in a test build, hardware step with timing |
| R2 | Power loss during a save corrupts the FAT/exFAT structure | `loop.tmp` + rename kept; host interruption tests at every block; exFAT has no 8.3 names but rename is atomic in FatFs' directory update order - verified by the tests, not assumed |
| R3 | Writes block the main loop longer than today | per-transfer bound, USB service between transfers, save stays chunked (32 KB per service) |
| R4 | Boot intent survival through the bootloader (same as Nebulae R1) | fallback is performance, panel reports it |
| R5 | Moving the Nebulae SD-management code changes Nebulae | Nebulae host/sanitizer/e2e tests unchanged; ELF checks |
| R6 | Two FatFs configurations confused in one image | `sd.mk` allows exactly one; compile-time asserts in each disk layer; symbol check per image |

## Open questions for the human

* Scope: both goals in one PR, or first the writable driver + exFAT (steps 1-3) and the
  Windows management later?
* Is "finish the pending save, then restart" (L6) the wanted behaviour for SHARE?
