# Nebulae on Daisy v0.2.0 - architecture

Status: design decisions recorded 2026-11-01 (gate G0), then implemented. Hardware
behaviour of everything new in v0.2.0 is **NICHT GEMESSEN**.

## Starting point (audited on `main` @ `d3f72b9`)

| Area | v0.1.1 state | Hardware status |
| --- | --- | --- |
| Nebulae firmware | `instruments/granular`, BOOT_SRAM, own read-only `src/sd_driver.c` on libDaisy FatFs R0.12c (`USE_FATFS=1`), fixed names `01.wav`..`16.wav`, USB-MIDI on J7 | not flashed by an agent; local clean build reproduces SHA256 `e30380f7...e50a` |
| Review findings | `docs/nebulae-review-notes` branch: `INDEPENDENT_REVIEW.md`, `CORRECTION_BRIEF.md` (not on `main`); open P2: F3 CPU peak, F6 trigger grain size, F7 KEYBOARD bootloader gesture | - |
| Shared SD driver | `common/sd`: FatFs R0.16 + patch-1/2, exFAT, LFN, read-only (`ro_sd.c`) | software only |
| Card reader | `diagnostics/firmware/src/{card_reader,msc_bot,reader_usb,reader_dma,reader_abort,reader_ctlreq}` in the separate `DaisyDiagnostic-CardReader` image, CDC + MSC composite | **GEMESSEN 2026-10-31** (0.3.2: FAT32, Windows create/edit/rename/delete, eject/remount hashes, one write-interruption) |
| Control panel | `diagnostics/windows` (PySide6, DDP v1 over CDC, `READER` commands) | 0.3.2 app used in the hardware test |

## Target

One application image `NebulaeOnDaisy-v0.2.0.bin` with two **boot personalities**:

| Personality | USB on J7 | SD owner | Audio |
| --- | --- | --- | --- |
| PERFORMANCE (default after power-on, reset, error) | class-compliant USB-MIDI (libDaisy `MidiUsbTransport`) | local FatFs R0.16, read-only | running |
| SD MANAGEMENT ("admin") | CDC (DDP v1) + MSC composite, reused from the card reader | MSC, only after explicit `READER a=SHARE` | stopped |

### Decision D1: switch by controlled restart, not by USB re-initialisation

libDaisy `f715e48` keeps class state in statics (`usbd_mode`, `MidiUsbTransport`
singleton, `UsbHandle` callbacks) and its `UsbHandle::DeInit` path is not used by
any tested firmware. Re-enumerating with a different class at run time would need
an untested `USBD_Stop`/`DeInit`/FIFO re-allocation sequence. The card reader's
composite is proven on hardware only when it is the first and only class after a
reset. Therefore the switch is a **software reset** inside the same image:

1. A validated **boot intent** record is written to a fixed address in backup SRAM
   (`0x38800F00`, far above libDaisy's 12-byte `boot_info` at `0x38800000`, which
   the linker script checks). It carries magic, version, target personality, the
   selected bank/slot and a checksum.
2. `System::ResetToBootloader(DAISY_SKIP_TIMEOUT)` restarts through the existing
   external bootloader without its 2 s DFU wait (older bootloaders simply wait).
3. `main()` reads and **clears** the record before anything else. Only a valid
   record selects SD management; anything else (power-on garbage, a cleared
   record, a power cycle, a crash in management mode) boots PERFORMANCE.

The bootloader itself is not changed. Whether it leaves `0x38800F00` untouched is
hardware-pending (risk R1); if it does not, the safe outcome is PERFORMANCE and the
control panel reports that the device did not come back in management mode.

### Decision D2: reuse the hardware-tested card reader unchanged

The card-reader sources move with `git mv` to `common/sd/reader/` (namespace `diag`
kept, content unchanged). Only the USB identity strings become compile-time macros
whose defaults equal the old literals. Proof: both Diagnostics images are rebuilt
and must stay **byte-identical** (G1 check). Nebulae compiles the same objects with
its own product string (`Nebulae on Daisy SD`), bcdDevice and serial suffix, so
Windows keeps a separate device instance.

### Decision D3: one FatFs, one SD stack per boot

Nebulae switches to `common/sd` (`USE_FATFS=0`, `sd.mk`), deleting `src/sd_driver.c`.
No libDaisy FatFs R0.12c, `FatFSInterface` or `SD_Driver` is linked (symbol check).
FatFs is built read-only (`FF_FS_READONLY 1`): no FatFs write path exists in the
image. Raw block writes exist only in the MSC path (`HAL_SD_WriteBlocks_DMA`, single
block, polled programming completion) and only run in SD management.
`ro_sd` gains an optional `abort` hook; Nebulae passes the hardware-tested
RCC-reset abort (`AbortReaderSd`) instead of `HAL_SD_Abort`.

### Decision D4: control channel

* PERFORMANCE: new USB-MIDI SysEx protocol `F0 7D 4E 42 ...` ("NB"), separate from
  the bootloader (`7D 42 4C`) and Mod on Daisy (`7D 4D 44`):
  [CONTROL_PROTOCOL.md](CONTROL_PROTOCOL.md). STATUS and SHARE with token echo,
  explicit confirmation when volatile Live audio exists.
* SD MANAGEMENT: DDP v1 over CDC with the existing `READER` commands, plus `NEB`
  (status / return to performance). The control panel identifies the device by
  `app=NebulaeOnDaisy` in HELLO.

### Decision D5: sample banks on the shared driver

`docs/SAMPLE_BANKS.md` is implemented as written: up to 16 banks x 16 WAVs under
`0:/NEBULAE/SAMPLES/`, natural sort, filters, one `f_readdir` per main iteration,
names held for the active bank only. SW17 switches slot/bank selection in SAMPLE.

## Ownership state machine (single owner of SDMMC1)

```text
            power-on / reset / any invalid intent
                          |
                          v
  +------------- PERFORMANCE (LOCAL, FatFs read-only) <----------------------+
  |  SysEx SHARE (+confirm if Live audio)                                    |
  v                                                                          |
PREPARE_SHARE: fade out, All Notes Off, stop audio, stop loader,           |
  unmount FatFs, HAL_SD_DeInit, write intent, ACK, reset                    |
  |                                                                          |
  v                                                                          |
ADMIN LOCAL (no FatFs; MSC answers NOT READY)                                |
  | READER a=SHARE (card present, BOT idle)                                  |
  v                                                                          |
SHARED (Windows reads/writes blocks) --bus reset/removal/I/O error--> INTERRUPTED/ERROR
  | Windows "Eject" (START STOP UNIT LoEj, after sync)                      |  quarantined: no
  v                                                                        |  reshare, no FatFs
EJECTED                                                                    |
  | READER a=RETURN (ack)  <-----------------------------------------------+  (explicit ack)
  v
ADMIN LOCAL --NEB a=PERFORM, or S4 held 2 s--> RETURNING: intent PERFORMANCE
  (bank/slot), reset --> PERFORMANCE: mount, scan banks, load slot
```

Required semantics map to the states: LOCAL/PERFORMANCE, PREPARE_SHARE, SHARED,
EJECTED, RETURNING, INTERRUPTED/ERROR. A closed GUI, BYE, a missing PING or an
unplugged cable never returns the card. While SHARED no FatFs object exists in the
running image state at all (management never mounts), so concurrent local access
is structurally impossible.

## Interfaces

| Interface | Owner | Notes |
| --- | --- | --- |
| `neb::BootIntent` (`src/boot_intent.h`) | main, before/after reset | pure, host-tested encode/decode |
| `neb::ControlSysEx` (`src/control_sysex.h`) | USB IRQ collects, main answers | host-tested |
| `neb::Admin` (`src/admin.{h,cpp}`) | management main loop | DDP v1 via `diagnostics/protocol/ddp.cpp` |
| `neb::BankScanner`, `neb::NaturalLess` (`src/banks.{h,cpp}`) | main loop | abstract `Directory`, FatFs adapter in `main.cpp` |
| `diag::CardReader`, `MscBot`, `ReaderDma`, `AbortReaderSd`, reader USB | management main loop / USB IRQ mailbox | `common/sd/reader/`, unchanged logic |

## Memory estimate (checked after the build in VALIDATION.md)

* AXI SRAM image: v0.1.1 163 KiB; + FatFs R0.16/exFAT (~25 KiB), CDC/MSC/DDP/admin
  (~20 KiB), banks (~4 KiB): about 215 KiB of 480 KiB.
* DTCM `.bss`: + two 16-entry name tables (16 x (256 + 13) B each, about 8.6 KiB),
  path buffer 532 B, FatFs objects; the 16 KiB stack floor stays a linker assert.
* SDRAM: unchanged layout (Engine 247,688 B, Live banks 2 x 8 MiB, file buffer up to
  the 8 MiB reserve). The PCM file capacity is re-derived from the ELF.
* Backup SRAM: 12 B `boot_info` + 32 B intent at `0x38800F00`.

## Risks

| ID | Risk | Mitigation |
| --- | --- | --- |
| R1 | External bootloader overwrites backup SRAM at `0x38800F00` | checksum; fallback is PERFORMANCE; panel reports failure; hardware test step |
| R2 | Windows does not issue START STOP UNIT (surprise removal policy) | firmware stays SHARED; panel explains "Safely remove"; no return until EJECTED or explicit interruption acknowledgement |
| R3 | Volatile Live loop lost by the restart | firmware refuses SHARE without `confirm=1` while a Live loop exists, BUSY while recording |
| R4 | Two FatFs or SD drivers linked | `USE_FATFS=0`, `check-sd-symbols.py` with explicit allow list for MSC writes |
| R5 | F3 CPU peak (six complex 2048 FFTs in one block) | two real forward FFTs packed into one complex FFT (same timing, no L/R hop offset); latched overrun LED; peak still NICHT GEMESSEN |
| R6 | Bank scan with many entries blocks USB MIDI | one `f_readdir` per main iteration; service hook between SD transfers |
| R7 | Names outside code page 850 cannot be opened | FAT32: open by 8.3 alias; exFAT: slot shows error 1 (documented) |
| R8 | Card-reader refactor changes the hardware-tested image | byte-identical Diagnostics images after the move |
| R9 | Control panel claims an eject that did not happen | eject state only from firmware `READER state=EJECTED`; timeouts mean "unknown" |
| R10 | Codec pop during restart | output faded to zero before `StopAudio`; audibility is hardware-pending |
