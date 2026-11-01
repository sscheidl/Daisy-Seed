# Looper v0.5 — SD management (Windows access to the card)

**Status:** implemented (step 1, path 2 of the plan below); host tests and both ARM builds
pass. Hardware behaviour is **NICHT GEMESSEN**: nothing in this document has run on the
kit yet. Checklist: [HARDWARE_TEST.md, section 10](HARDWARE_TEST.md#10-sd-management-windows-access-v05).

The looper keeps its microSD in the kit. v0.5 can hand that card to Windows over USB on
J7, the same way Nebulae on Daisy 0.2.0 does
([architecture](../../granular/docs/V0_2_ARCHITECTURE.md)), using the hardware-tested
card reader of [common/sd/reader](../../../common/sd/reader/) (Diagnostics 0.3.2,
GEMESSEN 2026-10-31). Use it to copy WAV files into `looper/loops/` or to back up
`looper/loop.wav` without taking the card out.

## Plan

| Path | Content | State |
| --- | --- | --- |
| 2 (this step) | Second boot personality with the shared card reader; loop persistence unchanged on libDaisy's FatFs R0.12c (FAT32) | **done in v0.5.0**, NICHT GEMESSEN |
| 1 (later, separate PR) | Writable variant of the shared driver in `common/sd` (FatFs R0.16 with the patch-2 CVE fixes, exFAT, long names) for `loop.wav`, with its own symbol policy and power-loss tests | open; needs a decision on `common/sd/PLAN.md` S1/S4, because that driver is read-only by design |

Until path 1 is done the card must be **FAT32**. SDXC cards (64 GB and larger) come as
exFAT and have to be reformatted to FAT32 for the looper; Windows itself can read and
write both through the card reader.

## Two boot personalities, one image

| Personality | USB on J7 | Card owner | Audio |
| --- | --- | --- | --- |
| PERFORMANCE (power-on, reset, any error) | USB-MIDI: CCs, bootloader request, control SysEx | the looper (libDaisy FatFs, `loop.wav`, `loops/`) | running |
| SD MANAGEMENT | USB CDC (DDP v1) + USB mass storage, product `Looper SD`, bcdDevice 4.00, serial suffix `L` | the card reader, Windows only after an explicit `READER a=SHARE` | stopped |

The switch is a software reset of the same image, never a USB re-initialisation at run
time (same decision as Nebulae D1):

1. The looper writes a checksummed one-shot **boot intent** (`src/boot_intent.h`, magic
   `LPR1`) to backup SRAM at `0x38800F00`, above libDaisy's `boot_info`
   (`src/memory.ld` asserts the gap).
2. `System::ResetToBootloader(DAISY_SKIP_TIMEOUT)` restarts through the existing Daisy
   bootloader without its 2 s wait. The bootloader is not changed.
3. `main()` takes (reads and clears) the record right after `hw.Init()`. Only a valid
   record starts SD management; anything else, a power cycle, or a crash in SD
   management starts the looper. Nebulae's record (magic `NEB1`) is rejected.

Whether the bootloader keeps `0x38800F00` untouched is hardware-pending, as for Nebulae
(risk R1). If it does not, the looper simply starts in performance and the control panel
reports "did not enter SD management". Nothing is shared in that case.

## Performance: control SysEx `F0 7D 4C 50`

The looper shares the non-commercial manufacturer ID `0x7D` with the bootloader entry
(`42 4C`), Mod on Daisy (`4D 44`) and Nebulae (`4E 42`); `4C 50` ("LP") is its own.
Implementation: `src/control_sysex.h` (header-only, host-tested). Requests are handled
in the main loop and answered at once.

```text
request  F0 7D 4C 50 <ver=01> <cmd> <payload...> F7
reply    F0 7D 4C 50 01 <cmd|0x40> <payload...> F7
```

### STATUS `01` → reply `41`, 13 bytes

| Byte | Meaning |
| --- | --- |
| 0–2 | firmware major, minor, patch (`src/version.h`) |
| 3 | personality, `0` = performance |
| 4 | flags: bit0 card present, bit1 loop in RAM, bit2 loop differs from `loop.wav`, bit3 saving, bit4 recording/overdub/resample (or pending), bit5 WAV import, bit6 audio deadline overrun latched |
| 5 | bit0: the last save failed, or the card was never usable |
| 6 | last card fault (`LoopStore::Fault`: 0 none, 1 card, 2 write, 3 busy, 4 no WAV files, 5 file rejected) |
| 7–9 | loop length in ms, 21 bits in 7-bit groups, saturating |
| 10–11 | audio overruns since power-on, 14 bit, saturating |
| 12 | callback peak in percent of the 48-frame deadline, saturating at 127 |

Bytes 10–12 are firmware counters (DWT), not bench measurements.

### SHARE `02` → reply `42`

Payload: `confirm` (0/1) and a 4-byte token chosen by the host; the reply echoes the
token after the result byte.

| Result | Meaning | Restart |
| --- | --- | --- |
| `00` ACCEPTED | `loop.wav` matches the loop (or confirm=1) | yes, after the reply |
| `01` CONFIRM_REQUIRED | the loop changed and cannot be saved (the last save failed); resend with confirm=1 to give the changes up | no |
| `02` BUSY | recording, overdub, resample or a WAV import runs | no |
| `03` NO_CARD | no card in the slot | no |
| `04` BAD_REQUEST | wrong length or confirm > 1 | no |
| `05` SAVING | the loop changed: the firmware writes `loop.wav` **now** (without the usual 2 s delay); ask again | no |

The restart reloads `loop.wav`, so a loop that only lives in RAM would be lost. Instead
of asking the user, the looper saves it first: the panel repeats SHARE every 0.5 s while
the answer is SAVING (at most 180 s; the write speed of the card is NICHT GEMESSEN; an
87 s loop is about 33 MB). Only when saving is impossible does the firmware answer
CONFIRM_REQUIRED.

On ACCEPTED the main loop runs PREPARE_SHARE: fade the output to exactly zero within one
48-frame block, stop audio, stop any running save (the temporary file is removed,
`loop.wav` stays as it is), close and unmount FatFs, reset SDMMC1 (RCC reset), all LEDs
off, write the boot intent, wait 20 ms for the SysEx reply, restart. While performing,
the fade code is never active: the golden hash is unchanged (`2A2DBCEC41A54F57`).

### Kept across the restart

| Kept | Lost / reset |
| --- | --- |
| the card, including `loop.wav` (saved before the share) | undo/redo history (as after a power cycle) |
| physical pot positions | loop mode, seam mode, reverse, play/pause: defaults |
| | MIDI takeover state of the knobs |

## SD management: DDP v1 over CDC + mass storage

`src/sd_admin.{h,cpp}` (libDaisy-free, host-tested) answers the control panel;
`src/sd_management.cpp` wires the card reader exactly like Nebulae's `hw_glue.cpp` and
the Diagnostics 0.3.2 image: SDMMC1 IDMA into one isolated, 32-byte-aligned 512-byte
AXI SRAM sector, polled programming completion, RCC-reset abort, CDC on interfaces 0/1,
MSC on interface 2. No audio and no FatFs run in this personality.

```text
HELLO proto=1 fw=0.5.0 app=LooperOnDaisy dev=seed3-desktop boot=BOOT_SRAM bl=v6_1 libdaisy=f715e48 caps=btn,pot,potraw,sw17,sw18,sd,boot,sdmsc,loopmgmt
```

HELLO, PING, GET, RATE, BYE, BOOT and READER follow
[diagnostics/protocol/PROTOCOL.md](../../../diagnostics/protocol/PROTOCOL.md). LED,
SDTEST, CVOUT, TONE and SCOPE answer `ERR c=<cmd> e=UNSUPPORTED`.

| Command (capability `loopmgmt`) | Response |
| --- | --- |
| `LOOPER a=STATUS` | `LOOPER mode=SDMGMT fw=0.5.0 card=1 ret=1 a=STATUS` (also sent once per second with SYS and READER) |
| `LOOPER a=PERFORM` | from LOCAL only: `LOOPER ... a=PERFORM`, then the restart into performance; otherwise `ERR c=LOOPER e=STATE` |

### Ownership rules (same as Nebulae)

* Windows gets the card only by an explicit `READER a=SHARE`.
* Back to performance (`LOOPER a=PERFORM`, `BOOT`, or the S4 key) only from LOCAL:
  after Windows ejected (EJECTED) and `READER a=RETURN`, or after an interruption was
  acknowledged with `READER a=RETURN`.
* BYE, a 3 s host timeout, a closed port or an unplugged cable never return the card.
* Without a PC: **S4 alone for 2 s** returns from LOCAL or EJECTED. INTERRUPTED and
  ERROR need the panel's acknowledgement or a power cycle.

### Panel LEDs in SD management

| LED | State |
| --- | --- |
| D1 steady | LOCAL (card with the looper, not shared) |
| D2 flashing / steady | STARTING / SHARED |
| D4 steady | EJECTED (safe to return) |
| D5 flashing 2 Hz | INTERRUPTED or ERROR (quarantined) |
| D16 slow blink | SD management personality |
| Seed LED fast blink | SD management personality |

## Control panel

The Windows control panel 0.6.0 (`diagnostics/windows`) drives the whole sequence in its
**SD share** tab: it finds the looper on USB-MIDI by its STATUS reply, shows loop length
and save state, waits while the loop is saved, opens the drive, explains the eject and
returns to the looper. [Panel documentation](../../../diagnostics/docs/LOOPER_PROFILE.md).

## Memory

| Item | Where | Size |
| --- | --- | --- |
| Card-reader IDMA sector | AXI SRAM, `.looper_sd` | 512 B |
| SD management responder | AXI SRAM, `.looper_axi` (placement new) | 4.6 KB |
| `LoopStore` (moved from DTCM in v0.5) | AXI SRAM, `.looper_axi` (placement new) | 5.0 KB |
| USB CDC/MSC buffers of `common/sd/reader` | DTCM `.bss` | about 2 KB |
| Boot intent | backup SRAM `0x38800F00` | 16 B |

DTCM left for the main stack (local builds, 2026-11-01): regular 18,016 B (v0.4.0:
15,096 B), MOD 15,448 B. `src/memory.ld` fails the link below 12 KiB. SDRAM use is
unchanged (two 87 s buffers).

## Tests

| Test | Covers |
| --- | --- |
| `tests/test_control.cpp` | SHARE decision for every state, STATUS layout and saturation, BAD_REQUEST/ERROR replies, foreign SysEx ignored; boot intent one-shot, every single-bit corruption, Nebulae's record, 10,000 random records |
| `tests/test_admin.cpp` | DDP session, HELLO/caps, READER ownership, LOOPER STATUS/PERFORM only from LOCAL, BOOT, interruption quarantine, S4 gesture, LEDs (same cases as Nebulae's admin test) |
| `tests/test_store.cpp` | `SaveSoon` skips the 2 s delay but waits for an idle engine and after a failed save; `Halt` stops a save and keeps `loop.wav` |
| `tests/test_callback.cpp` | production `main.cpp`, both builds: STATUS bytes, BUSY while overdubbing, SAVING then a save within 100 ms, NO_CARD, CONFIRM_REQUIRED after a failed save, recovery after the 10 s retry, malformed requests, ACCEPTED → PREPARE_SHARE (audio stopped, card released, boot intent, `DAISY_SKIP_TIMEOUT` reset, LEDs off), output exactly zero at the end of the fade block |
| `tests/test_golden.cpp` | regular and MOD output hash unchanged (`2A2DBCEC41A54F57`) |
| `diagnostics/tests/python/test_looper.py`, `test_gui_looper.py` | panel codec, profile probe (looper and Nebulae on one port), save-then-share, confirmation, cancel, timeouts, refusals, return; GUI |
