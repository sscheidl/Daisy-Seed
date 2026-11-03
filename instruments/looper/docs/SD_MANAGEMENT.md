# Looper 0.5.0: card and SD management

Status: implemented 2026-11-03, host-tested; hardware behaviour **NICHT GEMESSEN**
([plan](SD_V0_5_PLAN.md), [hardware steps](HARDWARE_TEST.md#7b-exfat-and-sd-card-sharing-v05)).

## Card driver

The Looper uses the shared SD driver ([common/sd](../../../common/sd/README.md)) in its
**writable** configuration: FatFs R0.16 with the official patches, **FAT32 and exFAT**,
long file names. libDaisy's FatFs R0.12c is no longer linked (`make sd-check`).

* Writes go through a 32 KB SDRAM bounce buffer as multi-block IDMA transfers. A write
  returns only after the card has finished programming (transfer state again); every
  transfer is bounded (1 s write, 250 ms read) and a fault resets SDMMC1 with the RCC
  abort proven in the Diagnostics card reader. libDaisy's 30 s waits are gone.
* Formatting, erase, card lock and labels are not in the image; the symbol check fails
  the build if they appear.
* `loop.wav` is still written as `loop.tmp` and renamed only when complete. A host
  campaign cut the power after every block of a save on FAT32 and exFAT: at power-up the
  Looper always recalled exactly the old or the new loop, and the next save worked.
* `looper/loops/` now also skips names starting with `.` (macOS `._*` files).

## SD management (Windows access with the card in the kit)

Same model as Nebulae on Daisy 0.2.0, built from the shared component
[common/sdshare](../../../common/sdshare/README.md): one image, two boot personalities,
switched by a controlled restart.

1. The Windows control panel (app 0.6.0, tab **SD share**) sends SHARE over USB-MIDI.
2. **The current loop is saved first.** If the card already holds it, the Looper restarts
   at once (ACCEPTED). Otherwise it answers PENDING, saves immediately (no 2 s wait) and
   restarts when the card is in sync. While it saves, recording or importing delays the
   restart; after 120 s it gives up. If the save fails (card full, write error) nothing
   restarts and the panel asks: **Share without saving** loses the unsaved change,
   `loop.wav` keeps the last saved loop.
3. Before the restart: audio fades to silence within one block, audio stops, the card is
   unmounted and the SD interface powered down. Undo history is lost (RAM).
4. SD management: USB CDC (DDP) + mass storage, product string "Looper SD" (bcdDevice
   4.00, serial suffix `L`). The panel shares the card; Windows sees a removable drive.
5. Windows eject, then **Return to Looper**: the Looper restarts into performance and
   recalls `loop.wav` (which may have been replaced or deleted in Windows).

Interruptions (USB reset, unplugging, card removal while shared) quarantine the card until
acknowledged in the panel. Without a PC, S4 held alone for 2 s returns from LOCAL or
EJECTED. A power cycle always starts the Looper normally.

### Control SysEx `F0 7D 4C 50 ...` ("LP")

Frame and SHARE as Nebulae ([CONTROL_PROTOCOL](../../granular/docs/CONTROL_PROTOCOL.md)),
results 0 ACCEPTED, 1 CONFIRM_REQUIRED (only after a failed save), 2 BUSY (recording,
resampling, fading, importing), 3 NO_CARD, 4 BAD_REQUEST, **5 PENDING** (saving first).

STATUS reply payload (12 bytes):

| Byte | Meaning |
| --- | --- |
| 0-2 | firmware version |
| 3 | personality, 0 = performance |
| 4 | bit0 card, 1 loop present, 2 engine busy, 3 saving, 4 importing, 5 card in sync, 6 overrun latched |
| 5 | bit0 share pending (saving before the restart), bit1 share failed |
| 6-7 | loop length in 0.1 s (14 bit) |
| 8 | last card fault (0 none, 1 card, 2 write, 3 busy, 4 no files, 5 file) |
| 9-10 | audio overruns (14 bit, firmware counter) |
| 11 | callback peak in % of the block deadline (firmware counter, not a bench measurement) |

### LEDs in SD management

D1 LOCAL, D2 flashing/steady STARTING/SHARED, D4 EJECTED, D5 flashing INTERRUPTED/ERROR,
D16 slow blink: SD management personality (common/sdshare).

## Unchanged

The sound: the golden test still gives `2A2DBCEC41A54F57` for the regular and the MOD
build. The fade only runs while sharing.
