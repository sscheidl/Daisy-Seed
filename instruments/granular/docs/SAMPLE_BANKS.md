# Sample banks and free file names - PLANNED

**Status: specification only. Nothing here is implemented.** The current firmware
(v0.1.0) still loads the fixed names `0:/NEBULAE/SAMPLES/01.wav` ... `16.wav`; see
the [README](../README.md#sd-and-errors). This document records the agreed design
for a later version. Hardware behaviour is **NICHT GEMESSEN**.

## Goal

WAV files come from arbitrary sources and keep their own names. The firmware maps
them to the 16 slots by name, and subfolders become banks, so one card holds up
to 16 x 16 samples. The limits are deliberate: whatever exceeds them is ignored,
not reported as an error.

## Card layout

```text
0:/NEBULAE/SAMPLES/
    Field recording.wav     -> bank 1, slot 1   (loose files form bank 1)
    kalimba.WAV             -> bank 1, slot 2
    Drones/                 -> bank 2
        2 Low hum.wav       -> bank 2, slot 1
        10 Air.wav          -> bank 2, slot 2
        notes.txt              ignored (not .wav)
        alt/                   ignored (deeper subfolder)
    Voices/                 -> bank 3
    Empty/                     ignored (contains no .wav)
```

* The root folder stays `0:/NEBULAE/SAMPLES/`. WAVs anywhere else on the card are
  ignored.
* **Bank 1** is formed by the WAVs directly in `SAMPLES/`, if there are any. The
  subfolders follow in sorted order as the next banks.
* At most **16 banks** in total. Subfolders beyond that are ignored.
* At most **16 WAVs per bank**, assigned to slots 1-16 in sorted order. Further
  files are ignored.
* A folder without any `.wav` entry is not a bank and takes no bank number.
  If `SAMPLES/` has no loose WAVs, the first non-empty subfolder is bank 1.
* Only one folder level is read. Folders inside a bank folder are ignored.
* Cards that use the current names `01.wav` ... `16.wav` directly in `SAMPLES/`
  keep exactly their current slot assignment (bank 1, slots 1-16), as long as no
  gaps exist. With gaps, later files move up: a card with only `01.wav` and
  `03.wav` puts `03.wav` into slot 2.

## Filtering and sort order

The same rules apply to bank folders and to files:

* Skipped entries: names starting with `.` (including macOS `._*` files), entries
  with the FAT hidden or system attribute (e.g. `System Volume Information`).
* Files count only with the extension `.wav`, compared case-insensitively.
* Sorting is case-insensitive for ASCII letters. Other bytes compare by value in
  the configured FatFS code page 850.
* Digit runs compare by numeric value: `2 Low hum.wav` sorts before `10 Air.wav`.
  Equal values with a different number of leading zeros sort shorter first. A final
  byte-wise comparison makes the order deterministic.
* A WAV the parser rejects (e.g. 24-bit, 96 kHz, EXTENSIBLE) still occupies its
  slot and shows error code 2 when selected, as today. It is not skipped, so the
  user can see that a file was refused.

## Operation

Bank selection uses the reserved two-position switch **SW17** (`tog_sw17`, D19),
only in SAMPLE mode (SW18 center):

| SW17 | S1/D1-S16/D16 in SAMPLE mode |
| --- | --- |
| Slot position | Select slot 1-16 of the active bank (current behaviour) |
| Bank position | Select bank 1-16 |

* Which mechanical position means *slot* and which *bank* is decided during the
  hardware test, like the SW18 orientation. The default position should be *slot*.
* SW17 must be stable for 20 ms, like SW18. Keys held while SW17 changes are
  suppressed until they are released.
* In NEBULAE and KEYBOARD mode SW17 has no function.
* **Choosing a bank loads nothing.** The playing sample keeps playing until a slot
  is pressed in the slot position. Only one file fits in RAM, and an automatic load
  would cut the sound off unasked.
* Pressing the key of a missing bank or of the active bank does nothing.
* FILE in NEBULAE mode steps through the occupied slots of the **active** bank in
  the selected order (forward, backward, random). If the playing sample belongs to
  another bank, FILE starts at the first slot of the active bank in that order.
* MIDI CC and the other controls are unchanged.

## Startup, card change and persistence

* At power-on and on card insertion the firmware selects bank 1 and loads its
  first occupied slot, matching today's "first existing slot" behaviour.
* Removing the card clears the bank and slot tables. Live recording stays usable.
* The active bank is **not** stored. The card stays read-only and there are no
  flash writes.

## LEDs

* **Slot position:** as today. The active slot lights steadily, but only if the
  playing sample belongs to the active bank. Loading flashes at 5 Hz; errors pulse
  1-5 times per two seconds.
* **Bank position:** the active bank lights at full brightness, other existing
  banks dimmed, missing banks off. While a bank's file list is read, its LED
  flashes at 5 Hz. If its folder cannot be read, its LED pulses code 4.
* `Hardware::SetLed` already takes a brightness value (PCA9685 PWM). The current
  LED path only passes an on/off mask, so dimming needs a small extension there.
  LED updates remain in main, only on changes, at most 100/s.

## Implementation notes (design proposal)

* All directory work runs in main, never in the audio callback. Each main
  iteration handles at most one `f_readdir` entry, like the loader's one-chunk
  budget, so USB MIDI and LEDs keep being serviced.
* **Bank list** on mount: read `SAMPLES/` once. Loose WAVs mark bank 1 as present.
  For each candidate subfolder, check incrementally whether it contains a `.wav`;
  keep the first 16 non-empty folders in sorted order with a bounded insertion into
  a 16-entry table (larger names are dropped). Cost grows with the number of
  entries on the card; scan time is **NICHT GEMESSEN**.
* **File list** on bank change: read only the active bank's folder into a second
  16-entry table the same way. Only the 16 names of one bank are held at a time.
  Slot requests during a scan are kept (latest wins) and served afterwards, like
  the current `pending` request.
* Banks are built on the shared read-only SD driver ([plan](../../../common/sd/PLAN.md)):
  FatFs R0.16 with long file names, directory reads and exFAT, so FAT32 and exFAT
  cards both work. Nebulae's own `src/sd_driver.c` is replaced before this step.
* exFAT has no 8.3 short names (`FILINFO::altname` stays empty), so both tables
  keep the long names, used for sorting and for opening. A path buffer of about
  530 B holds `0:/NEBULAE/SAMPLES/<bank>/<file>` with 255-character names. Both
  tables need about 2 x 16 x 256 B = 8 KiB; DTCM has 53,972 B of static headroom
  ([memory](MEMORY_BUDGET.md)).
* `neb::File::Open(slot)` keeps its interface; `SdFile` resolves the slot through
  the active bank's table instead of formatting `%02u.wav`. The `occupied` mask
  passed to the engine becomes the occupied slots of the active bank.
* The sort and filter rules are a pure function without FatFS, so they get their
  own host tests. The FatFS fake gains `f_opendir`/`f_readdir`/`f_closedir`.

## Tests to add with the implementation

Host (`make PROJECT=granular test test-sanitize`):

* Sorting: case, numeric runs, leading zeros, equal names, code-page bytes.
* Filtering: non-WAV, `.`/`._*`, hidden/system, deeper folders, empty folders.
* Limits: 17+ files and 17+ folders keep exactly the first 16 in order.
* Bank 1 from loose files, and the case without loose files.
* Legacy card `01.wav` ... `16.wav` maps unchanged; gaps move later files up.
* SW17 debouncing, key suppression across the switch, bank key without load.
* FILE order inside the active bank and from a sample of another bank.
* Card removal during a bank scan and during loading; no FatFS call in the
  callback (existing `fake_sd::calls` check).

Hardware (human, add to [HARDWARE_TEST_PLAN](HARDWARE_TEST_PLAN.md) when implemented):

* SW17 orientation, bank LEDs including dimming, bank scan time with many files,
  long and non-ASCII names created on Windows and macOS, a card formatted by
  Windows and one by macOS, a FAT32 card and an exFAT card larger than 32 GB.

## Out of scope

* More than 16 banks or 16 files per bank, nested banks.
* Saving samples or the active bank (FILE+FREEZE stays reserved).
* Showing file names; the device has no display. The PC's sorted folder view is
  the reference for the slot order.
* Formats beyond PCM16 44.1/48 kHz mono/stereo.
* Windows Explorer access to the card (deferred, see the plan's decision S6).
