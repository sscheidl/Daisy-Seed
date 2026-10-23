# Shared read-only SD driver - PLANNED

**Status:** decisions recorded on 2026-10-23. Step 1 (shared driver, host tests,
symbol check) is implemented, see [README](README.md); steps 2-5 are open. Hardware
behaviour of the shared driver is **NICHT GEMESSEN**.

One microSD read path for every firmware in this repository, instead of separate
drivers per project. First users: the Diagnostics read test and Nebulae on Daisy
([sample banks](../../instruments/granular/docs/SAMPLE_BANKS.md)).

## Starting point

| Code | Purpose | Hardware status |
| --- | --- | --- |
| `diagnostics/firmware/src/main.cpp`, `RoRead`/`kReadOnlySd` | Mount, list root, unmount; 1 s per operation, 5 s per test; DMA straight into the `FATFS` window in SDRAM; reports `STA_PROTECT`; linked-symbol ban on all write/erase/lock paths | H8 *Passed* (operator test, results of 2026-10-17) |
| `instruments/granular/src/sd_driver.c` | Stays mounted, loads WAVs in steps; DMA into an isolated 512-byte AXI-SRAM sector, 20 ms per sector, card check and USB service between sectors | NICHT GEMESSEN |
| `diagnostics/firmware/src/{card_reader,msc_bot,reader_usb}.*` | USB mass storage on J7 (writes raw blocks for Windows), separate image | Software only; R1-R12 open |

Both FatFs users run libDaisy's FatFs **R0.12c** with `_FS_EXFAT 0`
(`libDaisy/src/sys/ffconf.h`), so cards must be FAT32.

## Decisions

| ID | Decision |
| --- | --- |
| S1 | One shared read-only driver in `common/sd/`. Diagnostics and Nebulae both use it; neither keeps its own FatFs disk driver. |
| S2 | Use the latest FatFs, **R0.16** with the official **patch-1** (`ff16p1.diff`) and **patch-2** (`ff16p2.diff`), vendored in `common/sd/fatfs/`. Patch-2 fixes three CVEs for "R0.16 and earlier" (malformed FAT/exFAT headers: file corruption, buffer overflow, divide by zero). Source: <https://elm-chan.org/fsw/ff/>, <https://elm-chan.org/fsw/ff/patches.html>. |
| S3 | **exFAT enabled**, so SDXC cards (64 GB and larger, delivered as exFAT) work without reformatting. FAT32 stays supported. |
| S4 | The libDaisy submodule stays unchanged, including its R0.12c for other code. Projects on the shared driver build with `USE_FATFS = 0` and do not use `daisy::FatFSInterface`, so only one FatFs is linked. |
| S5 | The USB card reader stays a separate component. The shared driver only defines card ownership (local FatFs **or** USB host, never both) so a reader can be added later. |
| S6 | Windows Explorer access for Nebulae is **deferred and optional**: at most a separate card-reader start mode (no audio/MIDI while shared), and only after Diagnostics R1-R12 have passed on hardware. |

## FatFs configuration (`common/sd/ffconf.h`)

* `FF_FS_READONLY 1`: write, create, rename, delete and format functions are not
  compiled at all. This is a stronger guarantee than today's `RES_WRPRT` stub.
* `FF_FS_EXFAT 1`, `FF_USE_LFN` with a static buffer, `FF_MAX_LFN 255` (also the
  workaround for CVE-2026-6688), `FF_LFN_UNICODE 0`, `FF_CODE_PAGE 850` as today.
* `FF_FS_MINIMIZE 0` for `f_opendir`/`f_readdir`/`f_lseek`. Callers must respect
  the documented `f_lseek` behaviour (CVE-2026-6686 note).
* `FF_FS_REENTRANT 0`: FatFs is used from one context (main loop) only.
* `FF_VOLUMES 1`, `FF_MULTI_PARTITION 0`, `FF_MIN_SS = FF_MAX_SS = 512`.
* Since R0.13 the option names carry the `FF_` prefix and `ccsbcs.c` is replaced by
  `ffunicode.c`; only the single-byte code page tables are needed.

## Driver behaviour

The disk layer implements FatFs' `disk_status`, `disk_initialize`, `disk_read`
and `disk_ioctl` directly. ST's `ff_gen_drv` and libDaisy's `SD_Driver` are not
used. It combines the parts of both existing drivers:

From Nebulae:

* SDMMC1 IDMA only into an isolated, 32-byte-aligned bounce buffer that the
  application provides in IDMA-reachable memory (AXI SRAM or SDRAM), with cache
  clean/invalidate; FatFs buffers may live anywhere (DTCM, SDRAM).
* Timeout per transfer, abort and `STA_NOINIT` on timeout, error or removal.
* Card-present check before every transfer (callback into the application).
* Service hook between transfers. Nebulae services USB MIDI there; Diagnostics
  passes an empty hook. The hook must not call FatFs.
* SDMMC1 interrupt priority 2; audio stays at 0.

From Diagnostics:

* `disk_status` always reports `STA_PROTECT`.
* Re-initialization on every mount, so a swapped card or a failed first attempt
  starts from scratch (`HAL_SD_DeInit`, then init).
* Optional overall time budget per operation (Diagnostics: 5 s per test).
* Waiting for the card's transfer state before each transfer.
* Linked-symbol policy, see below.

Configuration per application, set at init:

| Parameter | Diagnostics | Nebulae |
| --- | --- | --- |
| Transfer timeout | 1 s | 20 ms per sector |
| Overall budget | 5 s | none (bounded per main iteration by the caller) |
| Bounce buffer | 512 B | 512 B; larger (e.g. 4 KiB, multi-sector) only if a measurement shows a need |
| Service hook | none | `ServiceUsb()` |

The bus speed stays `SdmmcHandler::Speed::STANDARD`; libDaisy's `SdmmcHandler`
and BSP init remain in use. Load throughput at any setting is NICHT GEMESSEN.

## Build integration and symbol policy

* `common/sd/sd.mk`: adds the FatFs sources, the disk layer and the include path
  for the project's build. Diagnostics (own Makefile) and projects using
  `common/desktop.mk` both include it, before libDaisy's `core/Makefile`. It fails
  the build if `USE_FATFS = 1` or if it is included too late.
* A shared ELF check, derived from `FORBIDDEN_SYMBOLS` in
  `diagnostics/firmware/Makefile`, runs for every image that uses the driver:
  * forbidden: SDMMC write/erase/lock commands, `HAL_SD_Write*`, `HAL_SD_Erase`,
    `BSP_SD_Write*`, `BSP_SD_Erase`, libDaisy `SD_Driver`, `daisy::FatFSInterface`,
    every FatFs write API, R0.12c-only symbols;
  * required (non-vacuous check): the disk layer's read entry, `f_mount`,
    `f_readdir`, `SDMMC_CmdReadSingleBlock`.
* The Diagnostics card-reader image keeps its own, narrower policy (polled block
  writes allowed, FatFs mutation still forbidden).
* `f_open` can now leave the Diagnostics ban list, because a read-only FatFs
  build cannot open files for writing.

## Vendoring FatFs

* `common/sd/fatfs/`: unmodified `ff.c`, `ff.h`, `diskio.h`, `ffunicode.c` from
  `ff16.zip`, then patch-1 and patch-2 applied in that order.
* `common/sd/fatfs/VERSION.md`: version, download URLs, SHA256 of `ff16.zip`,
  `ff16p1.diff`, `ff16p2.diff` and of the resulting sources (recorded at the import
  on 2026-10-23). Updates only on purpose, with the reason in that file.
* FatFs license text in `common/sd/fatfs/LICENSE.txt` (BSD-style, any purpose).
* exFAT note: the specification is public since 2019; FatFs' documentation
  mentions possible licensing questions for commercial products. Not relevant for
  this private project; to be checked before any sale.

## Tests

Host:

* Disk layer against the fake BSP/HAL from `instruments/granular/tests/fakes`:
  bounce isolation, cache calls, unaligned destinations, timeout, error, removal,
  service hook per transfer, `STA_PROTECT`, re-init on mount.
* FatFs on in-memory card images: small FAT32 and exFAT images with long names,
  folders and files at cluster and sector boundaries.
* Malformed images for the patch-2 cases: corrupted boot sector, cluster size 0,
  FAT/exFAT header sizes and chain loops. Expected result: error code, no crash
  (run under ASan/UBSan).
* Symbol policy check on the ARM images, including a deliberately injected write
  call that must be detected (as Diagnostics does today).

Hardware (human; results recorded as GEMESSEN with date):

* Diagnostics: repeat H8 with the shared driver; new case: exFAT card larger than
  32 GB; card removal during the read test (still open from the H8 notes).
* Nebulae: step 3 of its hardware test plan on FAT32 and exFAT, then the bank tests.

## Steps

1. **Shared driver**: vendor FatFs R0.16 + patches, `ffconf.h`, disk layer, `sd.mk`,
   symbol check, host tests. No firmware behaviour changes yet. Done when host
   tests, sanitizers and the symbol check pass in CI.
2. **Diagnostics read test** on the shared driver. Same DDP messages and GUI;
   protected image keeps its guarantee. Done when CI is green and the human has
   repeated H8 and the exFAT case.
3. **Nebulae** on the shared driver; `src/sd_driver.c` and `USE_FATFS` removed,
   behaviour unchanged. Done when its tests pass and the hardware test step 3
   has been run.
4. **Sample banks and free names** for Nebulae per `SAMPLE_BANKS.md`.
5. *Later, optional*: card-reader start mode for Nebulae (S6).

Each step is its own branch and pull request. Toy Piano and Physical Modeling do
not use the SD card; their images and golden hashes stay unchanged.

## Open points

* Bounce buffer size for Nebulae: decide after a load-time measurement.
* Where the Diagnostics card-reader image gets its `FATFS` for the local read
  test: same shared driver, with ownership handed over per S5.
* Whether to raise the SD bus speed above `STANDARD`: only with hardware
  measurements, not part of this plan.
