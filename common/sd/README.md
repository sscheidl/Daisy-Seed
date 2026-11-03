# Shared SD driver (read-only, or writable)

microSD access for Seed3 Desktop firmware: SDMMC1 through libDaisy's BSP/HAL and
the vendored **FatFs R0.16** with patch-1 and patch-2 ([version](fatfs/VERSION.md)),
built read-only with **exFAT** and long file names. Design and decisions:
[PLAN.md](PLAN.md).

**Status:** steps 1, 3, 4 and 5 of the plan. Nebulae on Daisy 0.2.0 links the driver
(performance personality) and the card reader in [`reader/`](reader/) (SD management
personality). Host tests, sanitizers, a Cortex-M7 compile and the ELF symbol policy pass.
The driver's behaviour on the board is **NICHT GEMESSEN**; the card reader in
`reader/` is unchanged from the hardware-tested Diagnostics 0.3.2 (GEMESSEN 2026-10-31,
Diagnostics image rebuilt byte-identical after the move).

## Files

| File | Purpose |
| --- | --- |
| `ro_sd.h`, `ro_sd.c` | Disk layer (`disk_status`/`disk_initialize`/`disk_read`/`disk_ioctl`) and the small API below |
| `ffconf.h` | FatFs configuration: upstream file with five values changed (listed at its top) |
| `fatfs/` | Unmodified FatFs sources plus the two official patches |
| `sd.mk` | Build fragment for firmware projects |
| `Makefile`, `tests/` | Host tests, image generator, ARM compile check |
| `../../scripts/check-sd-symbols.py` | Symbol policy for objects and images |
| `reader/` | USB mass-storage card reader (exclusive ownership, BOT/SCSI, IDMA blocks, RCC-reset abort, CDC+MSC composite); moved unchanged from `diagnostics/firmware/src`, USB identity strings are `READER_USB_*` macros |

## Writable configuration (Looper 0.5.0)

`SD_WRITABLE = 1` (before including `sd.mk`) puts `rw/ffconf.h` ahead of `ffconf.h`: the
same FatFs R0.16 and the same disk layer `ro_sd.c`, with `FF_FS_READONLY 0`. Only the
file API needed to save files is compiled in (`f_write`, `f_unlink`, `f_rename`, `f_mkdir`,
`f_sync`, `f_truncate`); formatting, labels, chmod/expand and string functions stay off.

* `disk_write` copies into the bounce buffer, cleans the cache, starts a multi-block IDMA
  write (`BSP_SD_WriteBlocks_DMA`) and returns only after the card is back in the transfer
  state (programming completed), each step bounded by `write_timeout_ms` (default 1 s).
  `CTRL_SYNC` waits for the same state. Every fault calls the `abort` hook and sets
  `STA_NOINIT` (remount needed).
* `make sd-check` then allows exactly the write path (`SD_WRITE_ALLOW` in `sd.mk`); erase,
  lock, polling/IT writes and libDaisy's FatFs stay banned.
* `write_timeout_ms` exists only in the writable build, so read-only images (Nebulae) are
  byte-identical to their builds before this configuration existed.
* Host tests (`tests/test_rw_sd.cpp`): file operations on FAT32 and exFAT, chunking,
  programming waits, sync, every fault, removal mid-transfer, full card, and 300 rounds of
  random power cuts in which untouched files always survive.

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
* After the firmware build, `make sd-check` applies the symbol policy to the ELF.
  Images that must write blocks (the Diagnostics card reader) pass the allowed names
  in `SD_CHECK_ALLOW`.

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
* `arm-check`: Cortex-M7 compile with libDaisy's HAL headers, no write/erase/lock
  symbol, required read symbols present, and an injected `HAL_SD_WriteBlocks` call
  is detected.
