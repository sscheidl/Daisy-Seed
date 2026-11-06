# Shared SD driver (read-only or writable profile)

microSD access for Seed3 Desktop firmware: SDMMC1 through libDaisy's BSP/HAL and
the vendored **FatFs R0.16** with patch-1 and patch-2 ([version](fatfs/VERSION.md)),
built with **exFAT** and long file names, in one of two profiles: read-only (default) or
writable. Design and decisions: [PLAN.md](PLAN.md).

**Status:** steps 1, 3, 4 and 5 of the plan. Nebulae on Daisy 0.2.0 links the driver
(performance personality) and the card reader in [`reader/`](reader/) (SD management
personality). Host tests, sanitizers, a Cortex-M7 compile and the ELF symbol policy pass.
The driver's behaviour on the board is **NICHT GEMESSEN**; the card reader in
`reader/` is unchanged from the hardware-tested Diagnostics 0.3.2 (GEMESSEN 2026-10-31,
Diagnostics image rebuilt byte-identical after the move).
The Looper 0.6.0 links the writable profile for its own files and the card reader for its
SD management personality
([instruments/looper/docs/SD_MANAGEMENT.md](../../instruments/looper/docs/SD_MANAGEMENT.md)).
The writable path on the board is **NOT MEASURED**.

## Files

| File | Purpose |
| --- | --- |
| `ro_sd.h`, `ro_sd.c` | Disk layer (`disk_status`/`disk_initialize`/`disk_read`/`disk_ioctl`) and the small API below |
| `ffconf.h` | FatFs configuration of the read-only profile: upstream file with five values changed (listed at its top) |
| `rw/ffconf.h` | Writable profile: identical except `FF_FS_READONLY 0` (a test compares them) |
| `sd_policy.mk` | Symbol policy per profile: allow lists and injected-writer probes |
| `sd_version.h`, `reader/reader_version.h` | Version sources for `components/registry.json` |
| `fatfs/` | Unmodified FatFs sources plus the two official patches |
| `sd.mk` | Build fragment for firmware projects |
| `Makefile`, `tests/` | Host tests, image generator, ARM compile and symbol check |
| `../../scripts/check-sd-symbols.py` | Symbol policy for objects and images |
| `reader/` | USB mass-storage card reader (exclusive ownership, BOT/SCSI, IDMA blocks, RCC-reset abort, CDC+MSC composite); moved unchanged from `diagnostics/firmware/src`, USB identity strings are `READER_USB_*` macros |

## Writable profile (Looper 0.6.0)

`SD_WRITABLE = 1` (before including `sd.mk`) puts `rw/ffconf.h` ahead of `ffconf.h`: the
same FatFs R0.16 and the same disk layer `ro_sd.c`, with `FF_FS_READONLY 0`. Only the
file API needed to save files is compiled in (`f_write`, `f_unlink`, `f_rename`, `f_mkdir`,
`f_sync`, `f_truncate`); formatting, labels, chmod/expand and string functions stay off.
An image has exactly one profile: `sd.mk` selects one `ffconf.h`, and `ro_sd.c` refuses any
other configuration.

* `disk_write` copies into the bounce buffer, cleans the cache, starts a multi-block IDMA
  write (`BSP_SD_WriteBlocks_DMA`) and returns only after the card is back in the transfer
  state (programming completed), each step bounded by `write_timeout_ms` (default 1 s).
  `CTRL_SYNC` waits for the same state. Every fault calls the `abort` hook and sets
  `STA_NOINIT` (remount needed).
* The writable build requires the `abort` hook (`ro_sd_init` rejects NULL): `HAL_SD_Abort`
  has unbounded waits. Pass the RCC-reset abort of `reader/reader_abort.h`.
* `write_timeout_ms` exists only in the writable build, so read-only images (Nebulae) are
  byte-identical to their builds before this profile existed.
* The card reader is separate: it runs in another boot personality with no FatFs mounted.
  Before restarting into it, a writer unmounts and then calls `diag::AbortReaderSd`.

### Lower HAL waits on the write path

Checked in libDaisy's HAL (`stm32h7xx_hal_sd.c`, `stm32h7xx_ll_sdmmc.c`). The outer timeouts
of `ro_sd.c` only run between these calls:

| Wait | Where | Bound |
| --- | --- | --- |
| CMD24/25 response | `HAL_SD_WriteBlocks_DMA` | Hardware command timeout; software limit 5 s |
| CMD13 (programming) | `BSP_SD_GetCardState`, one call per poll | Same; so a poll can overrun `write_timeout_ms` by up to 5 s in the worst case |
| DMA completion | `disk_write` loop | `write_timeout_ms`, card-present check, then RCC-reset abort |
| CMD12 after a multi-block transfer | `HAL_SD_IRQHandler`, in the SDMMC interrupt | Hardware command timeout; the software limit overflows to about 70 s. Runs below audio priority, but blocks the main loop while it lasts |
| Abort | `reader_abort.h` | No wait: RCC reset, works without a card |

**Not bounded in practice, mount only:** `BSP_SD_Init` runs `HAL_SD_Init` and
`HAL_SD_ConfigWideBusOperation`, which poll CMD13 and read the SCR with `SDMMC_DATATIMEOUT`
(0xFFFFFFFF ms). A failed CMD13 reports state 0, so a card pulled in the milliseconds of an
init can keep the main loop in that wait; audio keeps running. This is unchanged from
libDaisy's own FatFs glue and from every read-only image, and it cannot be fixed without
patching the pinned libDaisy. The behaviour is NOT MEASURED.

## Use in a firmware project

```make
USE_FATFS = 0                       # libDaisy's FatFs R0.12c must not be linked
include ../../common/sd/sd.mk       # before common/desktop.mk or libDaisy core/Makefile
include ../../common/desktop.mk
```

```cpp
#include "ro_sd.h"
alignas(32) static uint8_t DSY_SDRAM_BSS bounce[512]; // or a section in AXI SRAM
static FATFS fs;                                     // may live anywhere

RoSdConfig c{};
c.bounce = bounce; c.bounce_bytes = sizeof bounce;
c.transfer_timeout_ms = 20; c.budget_ms = 0;          // per transfer / overall (0 = none)
c.card_present = CardPresent; c.service = ServiceUsb; // both optional
c.abort = MyAbort;                                   // optional; NULL = HAL_SD_Abort
ro_sd_init(&c);                                      // after SdmmcHandler::Init
if(ro_sd_mount(&fs) == FR_OK) { /* f_opendir, f_readdir, f_open(FA_READ), f_read ... */ }
```

* Call FatFs and this driver from the main loop only, never from the audio callback
  or an interrupt. The `service` hook must not call FatFs.
* The bounce buffer must be reachable by SDMMC1's IDMA: AXI SRAM or SDRAM, not DTCM
  (BOOT_SRAM `.bss` is DTCM). 32-byte aligned, a multiple of 512 bytes.
* `ro_sd_mount()` resets the card interface every time, so call it again after a
  card swap or an error. Errors, timeouts and removal set `STA_NOINIT`.
* Paths use volume `0:`, e.g. `0:/NEBULAE/SAMPLES`. exFAT has no 8.3 short names:
  open files by their long names (`FILINFO::fname`).
* `ffconf.h` aliases `_VOLUMES`/`_MAX_LFN` only so libDaisy's always-included
  `sys/fatfs.h`/`util/FileTable.h` declarations compile; their code is never linked.
* After the firmware build, `make sd-check` applies the symbol policy of the image's
  profile to the ELF, then injects each writer in `SD_CHECK_PROBES` into the same check and
  requires a rejection. Images that must write blocks pass the allowed names in
  `SD_CHECK_ALLOW`; the policies are in `sd_policy.mk` (`SD_WRITE_*`, `SD_READONLY_PROBES`,
  and `SD_READER_*` for a read-only image with the card reader).

## Tests

```bash
make -C common/sd test test-sanitize arm-check
```

On Windows add `HOST_CC=clang HOST_CXX=clang++ PYTHON=python` with LLVM-MinGW and
the xPack tools in `PATH`. The tests build FAT32 and exFAT images (with an MBR, like
real cards) using a writable host-only FatFs build, then check through the
production disk layer and read-only FatFs:

* directory listing, long and code-page names, case-insensitive lookup, file data
  with odd chunk sizes, unaligned destinations and seeks;
* bounce-buffer-only DMA, chunking, cache maintenance and the service hook per
  transfer, card absence and removal, error callback, failed start, missing
  completion and busy card (with timeout), overall budget, re-initialization;
* header values rejected by patch-2 (an exFAT cluster count below 256 is accepted
  by unpatched R0.16 and rejected with the patch), and 1,500 randomly corrupted
  images that must not crash (run under ASan/UBSan in `test-sanitize`);
* writable profile (`tests/test_rw_sd.cpp`): file operations on FAT32 and exFAT, chunking,
  programming waits, sync, every fault, removal mid-transfer, full card, and 300 rounds of
  random power cuts in which untouched files always survive;
* `arm-check`: Cortex-M7 compile with libDaisy's HAL headers of both profiles. Positive and
  negative symbol checks for the read-only, the read-only-with-reader and the writable
  policy: required symbols present, every injected writer (`f_mkfs`, polled
  `HAL_SD_WriteBlocks`, `f_write` in a read-only image, ...) rejected, and the writable
  objects fail the read-only policy.
