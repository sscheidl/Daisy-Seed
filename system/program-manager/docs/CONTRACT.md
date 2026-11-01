# Memory, start and commit contract of the Daisy Program Manager

**Status 2026-11-01, version 0.1.0.** Binding rules for firmware, linker, tools and
tests. Every statement is marked **[B]** proven (file/line or upstream source), **[P]**
practice on the kit (dated reports elsewhere in the repository) or **[A]** assumption
that only a hardware test confirms ([HARDWARE_TEST.md](HARDWARE_TEST.md)). Basis:
[concept v2](../../../docs/SD_RAM_BOOT_CONCEPT_V2.md), [review](../../../docs/SD_RAM_BOOT_REVIEW_CLAUDE.md),
[development order](../../../docs/SD_RAM_BOOT_CLAUDE_AUFTRAG.md) (all German).

## 1. Source sheet

| Subject | Source | Status |
| --- | --- | --- |
| Actual start-up code | `libDaisy/core/startup_stm32h750xx.c` (lines 1535–1558): for `BOOT_APP` no `SystemInit()`, SP not set by the handler | [B], libDaisy `f715e487` |
| BOOT_QSPI/BOOT_SRAM selection | `libDaisy/core/Makefile` 229–241 (`-DBOOT_APP`, linker) | [B] |
| Bootloader JUMP path | DaisyBootloader `shared/bootloader.cpp` `startup_process()`, tag v6.4.0 (read 2026-10-31): `InitBackupSram`, `version = v6_1`, on `JUMP` → `status = INVALID`; SRAM target: `SCB->VTOR = data`, `__set_MSP(*data)`, jump; QSPI target: `HAL_Init`, QSPI memory mapped, jump | [B] (source); that the installed binary matches this tag: [A] |
| Return/service | `libDaisy/src/sys/system.cpp` 301–353 (`ResetToBootloader`: `boot_info.status`, `RCC->CIER = 0`, `HAL_NVIC_SystemReset`); upstream: `SKIP_TIMEOUT` → 100 ms, `INF_TIMEOUT` → `UINT32_MAX` | [B] |
| Daily reset→SRAM start | all instruments are BOOT_SRAM (`common/desktop.mk:9`); diagnostics H1–H10 GEMESSEN 2026-10-17 | [P] |
| Cache functions | CMSIS 5.9 `cachel1_armv7.h` 358–377: `SCB_CleanDCache_by_Addr(addr, dsize)` walks 32-byte lines from `addr`, `op_size = dsize + (addr & 31)` → rounds to whole lines, `DSB/ISB` at the end | [B] |
| SDMMC1 IDMA targets | libDaisy `src/hid/fatfs.h:21-25` (AXI SRAM only, not DTCM); diagnostics practice: buffers in AXI/SDRAM | [B]/[P]; D2 not released |
| **RM0433** (system reset, SRAM retention, RSR flags, RAMECC, bus matrix, BOOT_ADD) | **not read**: the download from st.com fails in this environment (no browser download without approval, curl returns 0 bytes). No section numbers invented. | open – see section 11 |

## 2. Inherited hand-over state (bootloader → application)

The manager builds no jump trampoline of its own. On entry into the reset handler of a
started BOOT_SRAM application the state of the upstream JUMP path applies [B]: RCC in its
reset state (HSI), SysTick off, caches off, MPU off, CPACR (FPU) enabled by the
bootloader's start-up code, `SCB->VTOR = 0x24000000`, MSP = word 0 of the image, backup
SRAM accessible, `boot_info.version = v6_1`, `boot_info.status = INVALID`, QSPI **not**
memory mapped. This is identical to today's start of every application via DFU/QSPI copy;
only the source of the bytes (SD instead of the QSPI copy) and the way there (CPU copy
through the cache) are new.

For the manager itself (QSPI target) the bootloader additionally runs `HAL_Init()` and
configures QSPI memory mapped [B]; `DaisySeed::Init` therefore skips `qspi.Init`
(`daisy_seed.cpp:121`), and `QSPIHandle` refuses write/erase from QSPI execution
(`qspi.cpp:971-980`). The clock change in `System::Init` while executing in place is
supported upstream but **[A]** on the Seed3 (first probe test).

## 3. Memory plan of the manager

Linker: [`ld/program_manager_qspi.lds`](../ld/program_manager_qspi.lds). Check:
`tools/check_qspi_image.py` (ELF sections, VMA **and** LMA, against the regions;
`.qspiflash_*` empty, vectors, marker, BIN = QSPI extent).

| Region | Addresses (end exclusive) | Manager content | Measured in build 0.1.0 (manager / probe) |
| --- | --- | --- | --- |
| QSPI | `0x90040000`–`0x907E0000` | vectors, code, rodata, init arrays, `.data` load image | 156 744 B / 141 120 B |
| SRAM_TARGET | `0x24000000`–`0x24078000` | **nothing** (image target) | 0 B / 0 B, enforced by the checker |
| SRAM_HELPER | `0x24078000`–`0x24080000` | `.pm_axi_bounce`: 4 KiB SDMMC1 IDMA bounce, 32-byte aligned | 4 096 B / 0 B |
| DTCM | `0x20000000`–`0x20020000` | `.data`, `.bss` (FatFs objects, 16 KiB catalog text, diagnostics buffers), stack up to `_estack = 0x20020000`; linker ASSERT: ≥ 16 KiB stack reserve | 104 200 B / 101 360 B |
| RAM_D2_DMA | `0x30000000`–`0x30008000` | libDaisy DMA buffers (LED, ADC, audio, MIDI) | 17 096 B |
| RAM_D2 | `0x30008000`–`0x30048000` | heap (`end`); no `malloc` in own code | 0 B used |
| Backup SRAM | `0x38800000`–`0x38801000` | libDaisy's `boot_info` (12 B) | 12 B |
| SDRAM | `0xC0000000`–`0xC4000000` | unused (initialized by `DaisySeed::Init`) | 0 B |

Rules: no DMA target in DTCM (SDMMC1 cannot reach it [B]); D2 not as SD IDMA target
without RM0433 evidence; the bounce buffer owns its cache lines; the logical FatFs/catalog
buffer in DTCM is only filled by CPU `memcpy` from the bounce (`common/sd/ro_sd.c:102-109` [B]).

## 4. Image profile `axi-sram-480k-v1`

A `.dsy` payload is an unchanged BOOT_SRAM BIN. `tools/dsy_pack.py check` requires before
packaging (reason in parentheses):

* `.isr_vector` at `0x24000000`; every section with file content is loaded inside
  `0x24000000`–`0x24078000` (the target is exactly this range);
* `.data` lives in DTCM with its load address in the target, `.bss` and
  `.reserved_for_stack` in DTCM, `_estack = 0x20020000` (the start-up code copies `.data`
  from the target and zeroes `.bss` itself; MSP from word 0 must be in DTCM);
* no section in internal flash or QSPI, `.qspiflash_*` empty (an image expecting QSPI data
  would depend on the flashed manager);
* NOLOAD sections inside the target (`.axi_bss` of Toy Piano/Physical Modeling, `.neb_sd` of
  Nebulae) end below `0x24078000` **and** are named with `--noload`: the packager thereby
  confirms that the product initializes them itself. The loader zeroes the whole target
  before copying, but that replaces no product initialization of D2/SDRAM/ITCM areas;
* BIN = load extent, 8 B ≤ length ≤ 491 520 B; word 0 in `(0x20000000, 0x20020000]` and
  8-byte aligned, word 1 Thumb and inside the payload, ELF entry = reset vector;
* product id/version/marker from `components/registry.json` (`--project`).

The runtime loader independently checks the header (CRC, fields), length, SHA-256 and the
same vector conditions (`pm_image.cpp`, `pm_loader.cpp`).

## 5. Load contract (`pm_loader.cpp`)

Order per `Step()` (one chunk of 4 096 B card I/O per main-loop pass, overall budget 60 s,
card presence before every step):

1. **Opening**: open the file; the file size must lie in `160+8 … 160+491520`.
2. **Header**: read 160 B, parse (magic → CRC → fields); product id = catalog id; header hash
   = catalog hash; file size = 160 + payload length; then the **whole** 480 KiB target is
   zeroed and the SHA-256 started.
3. **Payload**: chunks directly into the target (CPU copy from the bounce), hash running; EOF
   before the length → `SHORT`, read error → `READ`; then one probe read: more bytes → `LONG`.
4. **Verify**: hash = header hash, check vectors, close the file.
5. **Finalize**: `Quiesce()` → `Clean(480 KiB)` → `Commit(0x24000000)`.

On every error `boot_info` stays untouched, the file is closed and the manager shows the
error (`PMSTATUS e=…`, LED blinking). Host tests cover all error paths, partial reads,
cancel, budget and the commit order (`tests/test_loader.cpp`, 77 checks).

## 6. Commit contract (`pm_main.cpp`, `DaisyBootCommit`, `SramTarget`)

| Step | Code | Reason |
| --- | --- | --- |
| Quiesce | `hw.seed.StopAudio()`; LEDs off, `UpdateLeds()` twice (waits for the last DMA transfer); DAC codes 0/0; `ro_sd_unmount()`, `HAL_SD_DeInit` | after this point no interrupt/DMA writes RAM; DAC code 0 is no electrical evidence (NICHT GEMESSEN) |
| Clean | `SCB_CleanDCache_by_Addr(0x24000000, 480 KiB)`, `__DSB(); __ISB();` | image and zeroing were written through the D-cache; the reset drops the cache. Clean (not invalidate) writes dirty lines back; CMSIS rounds to lines [B]. The stock bootloader copies with the D-cache **disabled** (`bootloader/main.cpp`: `SCB_DisableDCache()`), so this duty is new and the host test proves only the order, not the register effect |
| Commit | `__disable_irq()`; `boot_info.data = 0x24000000; DSB; boot_info.status = JUMP; DSB; RCC->CIER = 0; HAL_NVIC_SystemReset()` | same order as `ResetToBootloader()` [B]; target first, status last; **not** calling `ResetToBootloader()` (it would overwrite the status). Backup SRAM access is active since the bootloader (`InitBackupSram`) and non-cacheable via MPU (`system.cpp:548-556`) |

Residual risks (accepted, concept v2 §6): the bootloader checks neither hash nor reset
cause after the reset; `boot_info.data` is a free pointer there; only the manager writes
`JUMP`, applications never.

## 7. Return and service

* Return from an application: `common/bootloader/return_entry.h` (S1+S16 3 s, SysEx
  `F0 7D 50 4D 01 01 4F 50 45 4E F7`) → the application ends audio/MIDI/CV/storage itself →
  `manager_return::Enter()` → `ResetToBootloader(DAISY_SKIP_TIMEOUT)` → after 100 ms the
  bootloader starts the QSPI application (manager) through a second reset. Between the two
  a DFU device `0483:DF11` briefly appears on J7.
* Service/DFU: unchanged S9+S16 5 s, BL SysEx, DDP `BOOT` → `DAISY_INFINITE_TIMEOUT`. In the
  manager `BOOT` is refused with `ERR e=BUSY` while `LOADING`.
* Manager update = today's DFU path to `0x90040000` (`docs/FLASHING.md`); it runs from internal
  flash, never from the QSPI being written.

## 8. Operation and modes (`pm_manager.cpp`)

`SELECT` (keys 1–15 select, 16 opens diagnostics), `DIAG` (the diagnostics layer owns
keys/LEDs/CV/tone; S1+S16 3 s or `PMMODE m=SELECT` returns), `LOADING` (keys ignored),
`SERVICE`. A selection requires one complete press/release cycle of exactly one key; keys
held at start-up or at a mode change must be released first; chords and holds ≥ 3 s never
select. No program autostarts; the bootloader starts the manager after power-up (2 s
window). CV OUT and the test tone only in `DIAG` and only in a host session; on leaving
`DIAG` the DAC is set to 0/0 and the tone off.

## 9. Card rule

At start-up the unchanged bootloader flashes a root-directory file whose name contains
`.bin`/`.BIN` to `0x90040000`, replacing the manager (upstream `SearchBin` [B]).
Therefore: **no file with `.bin` in its name (any case) in the root directory**; programs
only as `.dsy` below `/DAISY/PROGRAMS/`. `tools/pm_card.py check` enforces that on the PC
before the card is inserted; `PMINFO rootbin=1` reports it afterwards (too late for the
current boot).

## 10. Limits of version 1

No MSC in the manager (the reader stays a separate image), no autostart, no watchdog
(HardFault = hang until a manual reset), no check after the reset, no flash write
protection, the 2 s DFU window remains, single-slot update (an aborted DFU → flash again;
ROM DFU on the module port as the last resort). JUCE/VST3 deferred.

## 11. Open evidence

| ID | Evidence | Status |
| --- | --- | --- |
| C1 | RM0433: system reset sources, SRAM retention across a software reset, RSR flags, RAMECC behaviour, SDMMC1 bus matrix, BOOT_ADD range | document not obtainable in this environment; provide or look up before the first SD→SRAM test |
| C2 | installed bootloader ≙ tag v6.4.0 (`SKIP_TIMEOUT`/`INF_TIMEOUT` behaviour) | NICHT GEMESSEN (B1–B14 in `docs/HARDWARE_TEST_BOOTLOADER.md`) |
| C3 | XIP start of the probe on the Seed3 incl. clock change, QSPI chip | NICHT GEMESSEN |
| C4 | cache clean + commit: first SD→SRAM image starts and identifies itself | NICHT GEMESSEN |
| C5 | electrical rest state of CV OUT across all transitions | NICHT GEMESSEN (H14/H15 open) |
