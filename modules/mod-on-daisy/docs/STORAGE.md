# Mod on Daisy — Storage in the QSPI flash (specification)

**As of:** 23 October 2026 · **Status:** specification only (D8). **No flash write
functions are implemented** until the layout and procedure are approved.
Until then the firmware answers `STORE`/`RECALL` with `NAK 9` (PROTOCOL § 3.4).

## 1. Starting point (`VERIFIED`)

| Fact | Source |
| --- | --- |
| For **all** Seed versions, including Seed3, libDaisy configures the chip **IS25LP064A** (8 MiB) in memory-mapped mode from `0x90000000` | `libDaisy/src/daisy_seed.cpp` `ConfigureQspi()`, `system.cpp` `QSPI_SIZE 0x800000` |
| With BOOT_SRAM the application initializes QSPI itself (`qspi.Init`, because the program does not run from QSPI) | `daisy_seed.cpp` |
| The bootloader leaves the **first four 64 KiB sectors** (`0x90000000–0x9003FFFF`) untouched and reserves them for future functions | `libDaisy/doc/md/_a7_Getting-Started-Daisy-Bootloader.md` |
| Programs start at `0x90040000`. When flashing, the bootloader erases in **64 KiB blocks**. Data behind the program must start on a 64 KiB boundary beyond the program code | ibid. |
| BOOT_SRAM programs are at most **480 KiB** in size (SRAM 512 KiB − 32 KiB for the bootloader) | ibid., `STM32H750IB_sram.lds` |
| Maximum erase times: 4 KiB sector **400 ms**, 64 KiB block **1000 ms** | `flash_IS25LP064A.h` |
| `QSPIHandle::Erase()` uses 64 KiB blocks when aligned and large enough, otherwise 4 KiB sectors; `EraseSector()` erases exactly 4 KiB | `qspi.cpp` |
| `PersistentStorage` erases and writes without CRC and without a double copy | `util/PersistentStorage.h` |
| libDaisy creates **no MPU region** for `0x90000000` | `sys/system.cpp` |

`OPEN`: whether every Seed3 actually carries an IS25LP064A is checked in phase 2 via the
JEDEC ID. That is a read command, not a write access.

## 2. Memory layout

```text
0x90000000 ┌───────────────────────────────┐
           │ bootloader reserve (256 KiB)  │  never use
0x90040000 ├───────────────────────────────┤
           │ firmware image (BOOT_SRAM)    │  ≤ 480 KiB → occupies at most up to 0x900BFFFF
0x900C0000 ├───────────────────────────────┤  (erased in 64 KiB blocks)
           │ free, ≈ 7.1 MiB               │  not used by Mod on Daisy
0x907E0000 ├───────────────────────────────┤
           │ MOD block P: 8 patch slots    │  64 KiB = 16 × 4 KiB sector (slot A/B each)
0x907F0000 ├───────────────────────────────┤
           │ MOD block S: settings         │  2 × 4 KiB (A/B) + 56 KiB reserve
0x90800000 └───────────────────────────────┘
```

| Area | Address | Content |
| --- | --- | --- |
| Slot k, sector A | `0x907E0000 + k · 0x2000` | patch record |
| Slot k, sector B | `0x907E1000 + k · 0x2000` | patch record |
| Settings A | `0x907F0000` | device ID, CV calibration, power-on slot |
| Settings B | `0x907F1000` | like A |
| Reserve | `0x907F2000–0x907FFFFF` | future extensions, e.g. more slots |

**Rationale**: the two topmost 64 KiB blocks are furthest from any image, lie on 64 KiB
boundaries and are never touched when a BOOT_SRAM firmware is flashed. The bootloader
reserve stays free.

**Safeguard at build time** (part of the later implementation): the ELF check script
aborts if

* the end of the image in the QSPI address space is ≥ `0x907E0000` (ruled out for
  BOOT_SRAM by the 480 KiB limit, relevant for future BOOT_QSPI images), or
* a `.qspiflash_text`/`.qspiflash_data` section is not empty. During an erase or write
  operation QSPI cannot be read; code or data there would freeze.

**Area shared by all instruments**: every firmware sees the same 8 slots. Each record
therefore carries the instrument key CRC and the schema hash. A foreign record is never
loaded (`RECALL` → status 3). `STORE` to a slot with a foreign record requires a
confirmation bit, which the editor sets only after asking. Before switching to another
instrument, the editor can back up all slots as files.

## 3. Records

Each 4 KiB sector contains **exactly one** record at the start of the sector. The rest
stays erased (`0xFF`).

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `'M','O','D','R'` |
| 4 | 1 | type: 1 = patch, 2 = settings |
| 5 | 1 | format version of the record (1) |
| 6 | 2 | payload length n |
| 8 | 4 | sequence number (u32, consecutive per slot) |
| 12 | 4 | CRC-32 of the instrument key (settings: 0) |
| 16 | 4 | schema hash (settings: 0) |
| 20 | 4 | CRC-32 of the payload |
| 24 | 4 | CRC-32 of bytes 0–23 (header) |
| 28 | n | payload: serialized `Patch` (109 B; format 2 with LFO cross-modulation 111 B, ARCHITECTURE § 3.3) or settings |
| 252 | 4 | **commit word** `0x434F4D54` ("COMT"), written last |

The record fits into one 256-byte program page; the commit word is at the end of this
page. **Settings (payload 16 B)**: `u8` device ID, `u8` power-on slot
(0xFF = default patch), `u16` reserved, `float` CV offset 1 and 2, `u32` reserved.

A record is **valid** only if **all** conditions hold:

* magic matches,
* header CRC matches,
* length ≤ 224,
* payload CRC matches,
* commit word matches,
* type and format version are known.

## 4. Procedure

### 4.1 Reading (at start-up and on `RECALL`)

1. Check both sectors of the slot.
2. The valid record with the **higher** sequence number applies. The comparison is
   overflow-safe: `int32_t(a − b) > 0`.
3. If both are invalid, the slot is empty.
4. For a patch additionally check: instrument key CRC and schema hash must match, and
   `Validate(patch, info)` must pass. Otherwise it is not loaded.

Reading uses the memory-mapped pointer (`qspi.GetData`), only in the main loop. After own
write operations the D-cache of the sector is invalidated first (§ 5).

### 4.2 Writing (`STORE`)

```text
Precondition: main loop, explicit command, patch validated.
1. Target sector = the sector of the slot that does NOT contain the currently valid record
   (if both are invalid: A). seq = highest valid sequence + 1 (otherwise 1).
2. EraseSector(target)                          ≤ 400 ms; erases only the outdated record
3. Invalidate the D-cache of the sector; check that the first 256 bytes = 0xFF.
4. Program header + payload (bytes 0 … 27+n), commit word NOT yet.
5. Invalidate the cache; read back and check the header CRC and payload CRC.
6. Program the commit word (one 4-byte programming, 1→0 transitions).
7. Invalidate the cache; check the record completely according to § 3.
8. Reply STORE|0x40 with status 0 and CRC; on an error in 2–7: NAK 10.
   The old record then remains valid.
```

**Power-loss safety**: the currently valid record is **never** erased or overwritten in
the same operation. If power fails in steps 2–6, the new record is incomplete (CRC or
commit missing) and is ignored; the old one stays valid. After step 6 the new record is
valid and has the higher sequence. At no point are both sectors invalid at the same
time, unless the slot was already empty before.

### 4.3 Wear

Every save erases one sector, alternating A and B. With the typical endurance of NOR
flash (≥ 100 000 erase cycles per sector; data sheet value `OPEN`, to be checked) this
gives ≥ 200 000 save operations per slot. Because saving happens only on an explicit
command (**no autosave**), this is not critical.

### 4.4 Power-on

1. Read the settings (A/B). Invalid → default: device ID 0, calibration 0, no power-on
   patch.
2. If a power-on slot is set and valid and matching according to § 4.1 → load it,
   otherwise the empty default patch (no active connection). The instrument then sounds
   as without Mod on Daisy.
3. The load state (loaded, empty, foreign, invalid) is recorded for `IDENTIFY` and the
   editor. Nothing is silently reinterpreted.

## 5. Run-time conditions and risks

| Topic | Specification |
| --- | --- |
| Context | main loop only, never in the audio IRQ |
| Audio during erasing | keeps running, because with BOOT_SRAM code and data live in SRAM/DTCM. The measurement firmware checks the overrun counter during `STORE` (acceptance) |
| QSPI during the operation | for erase/write, libDaisy switches from memory-mapped to indirect mode and back afterwards. During this time **nothing** may read the `0x90000000` area, not even an interrupt (ELF check § 2) |
| D-cache | no MPU region for QSPI; according to the ARMv7-M default map the area is cacheable (write-through). Therefore `SCB_InvalidateDCache_by_Addr` over the sector after erasing and after programming, before every read-back |
| Speculative accesses | `OPEN`: on the STM32H7, speculative read accesses to the QSPI address space in indirect mode can cause problems. Check before implementation; a possible remedy is an MPU region that locks the area during the operation |
| Blocked main loop | up to ~400 ms (maximum, typically less). USB MIDI is not collected during this time; the libDaisy FIFO (256 events) can overflow and **then discards new events**. LED service and USB send pause. → save only on command; the editor shows "Saving … do not send MIDI data" and waits up to 2 s for the reply |
| Firmware change | flashing via DFU, Web Programmer or SD erases only the blocks of the new image (`OPEN`, confirm in the hardware test: save a patch, flash another firmware, patch still present) |

## 6. Rejected alternatives

| Alternative | Reason |
| --- | --- |
| `daisy::PersistentStorage` | no CRC, erases the only record before writing (not power-loss safe), size bound to one structure |
| Bootloader reserve `0x90000000–0x9003FFFF` | reserved by the bootloader for future functions |
| Area directly behind the image | depends on the image size and shifts between instruments |
| Backup SRAM (4 KiB) | not persistent without a battery |
| Internal flash (128 KiB) | contains the bootloader |
| microSD | not used by every instrument and not always inserted; possible later as an additional backend behind the same interface |

## 7. Test plan for the later implementation

1. **Host, without hardware**: a NOR model enforces the erase granularity and allows only
   1→0 when programming. A power failure is injected at every single erase and program
   step. After every simulated restart **exactly the old or the new** record must be
   valid. In addition, bit flips in every field (detection by CRC) and sequence
   overflow.
2. **Hardware**:
   * 1000 × save and load;
   * cut power during `STORE` (20 attempts);
   * overrun counter and callback peaks during `STORE`;
   * flash another firmware via DFU; the slots must be preserved;
   * read the JEDEC ID.
3. Only then is `STORE` enabled in the protocol (IDENTIFY, bit 0).
