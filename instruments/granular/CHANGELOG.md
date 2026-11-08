# Changelog

## 0.2.1 diagnostic profile build - first hardware contact, 2026-11-08

Not a release. The first kit test (owner, 2026-11-08) showed the kit freezing as soon
as a sample plays ([OVERLOAD_DIAGNOSIS.md](docs/OVERLOAD_DIAGNOSIS.md)); hardware
numbers are **NOT MEASURED** until the owner runs the profile image.

* `make PROFILE=1` (also with `MOD=1`): DWT cycle splits of the callback (vocoder, grains,
  rest; window fill, FFTs, phase inside a hop), a budget guard that leaves the vocoder out
  for 8 blocks after an overrun so USB and LEDs stay alive, a boot benchmark of one
  `Mincer` hop from SDRAM and from AXI SRAM, and the PROFILE SysEx `03`
  ([protocol](docs/CONTROL_PROTOCOL.md)). `tools/nb_probe.py` reads STATUS and PROFILE
  from the PC, each probe in its own process with a timeout.
* Regular and MOD images stay byte-identical (`cbc4c376…`, `02e52f88…`): every hook
  compiles to nothing without `NEBULAE_PROFILE`. The profile build prints the regular
  golden hash while the guard does not trip.

## granular-mod on 0.2.1 - separate Mod on Daisy build, 2026-11-06

Hardware status: **NOT MEASURED**. Nebulae itself stays 0.2.1: the regular image is
byte-identical (SHA-256 `cbc4c376...`), and the MOD build sounds bit-identical with an
empty matrix.

* New product `granular-mod`: `build/mod/NebulaeMod-v0.2.1.bin`, marker
  `NEBULAE-MOD-VERSION=`, IDENTIFY key `nebulae`, eight global columns in CC20-27 order
  ([modulation](docs/MODULATION.md)).
* The matrix ticks at 1 kHz on the control timer, counted in milliseconds so merged timer
  interrupts cannot slow the LFOs; offsets reach the DSP once per 256-frame block
  (187.5 Hz) before the existing smoothing and laws.
* BL, NB and MD SysEx stay independent on the same J7 bytes. Sharing, restore and the SD
  management personality are unchanged. Patches are volatile; save and load them with the
  editor. No flash storage.
* Windows test package with one entry point for Modulation and SD card.

## 0.2.1 - SD handover regression fix, 2026-11-03

* Reject SHARE/RETURN while a performance or bootloader restart is pending; recheck local ownership and BOT idleness before the deferred reset.
* Add regressions for both restart types, blocked ACK transmission and ownership changes during ACK settling.
* Derive MIDI STATUS version limits from the version source rather than pinning the previous patch number.
* Preserve the existing FAT32/exFAT read-only and Windows raw-block paths. Hardware: **NOT MEASURED** for this version.

## 0.2.0 - software release candidate, not flashed, not released

Hardware status: **HARDWARE PENDING**. Release decision and publication are the human's.

* **SD management from Windows in the same image**: a USB-MIDI SysEx (`F0 7D 4E 42`,
  [protocol](docs/CONTROL_PROTOCOL.md)) restarts the kit through a validated one-shot
  boot intent into a CDC + mass-storage personality built from the hardware-tested
  Diagnostics card reader (now `common/sd/reader`). Return only after a Windows eject or
  an explicit acknowledgement of an interrupted access; power-on is always performance.
  Live loops are volatile: SHARE needs confirmation when one exists.
* **Sample banks** ([spec](docs/SAMPLE_BANKS.md)): free WAV names, up to 16 banks of 16,
  natural order, SW17 selects banks in SAMPLE mode, dimmed bank LEDs, FILE per bank.
* **FAT32 and exFAT** through the shared read-only FatFs R0.16 (patches 1+2); libDaisy's
  FatFs R0.12c and `src/sd_driver.c` are no longer linked. SD aborts use the card
  reader's RCC-reset abort.
* **F3 (CPU peak)**: both real forward FFTs of a hop are packed into one complex FFT
  (three to two complex 2048-point FFTs per channel and hop). Hop timing and the
  stereo phase relation are unchanged; host A/B error/signal -88 dB. No L/R hop offset.
  D16 in NEBULAE latches after an audio deadline overrun. CPU load is NICHT GEMESSEN.
* **F6**: in grain-trigger mode at minimum DENSITY the grain size is capped at 6x the
  portk-smoothed gate period (half-time 0.5 s, 0.5 ms floor), as the original
  instrument (lines 446-470).
* **F7**: the S9+S16 bootloader gesture is evaluated only in NEBULAE mode, so a held
  C5+C6 octave in KEYBOARD no longer restarts the kit; SysEx entry unchanged.
* Sound: the DSP only changes by the FFT packing rounding (above) and F6; no golden hash
  pins Nebulae.

## 0.1.1 - integration test image, not released

* **Firmware updates without BOOT/RESET** ([common/bootloader](../../common/bootloader/README.md)):
  holding S9+S16 alone for 5 s, or the USB-MIDI SysEx `F0 7D 42 4C 01 01 42 4F 4F 54 F7`
  (answered with `F0 7D 42 4C 01 41 F7`), lights all LEDs and restarts into the Daisy
  bootloader with `DAISY_INFINITE_TIMEOUT`. The control timer only runs the key timer; the raw USB-MIDI parser
  matches the SysEx in the USB interrupt.
* In KEYBOARD mode S9+S16 are C5+C6: a held octave of exactly these two keys for 5 s
  enters update mode.

## 0.1.0 - 2026-10-19

* Initial stereo granular/phase-vocoder adaptation for official Seed3 Desktop,
  BOOT_SRAM, J7 USB MIDI, three UI modes and eight primary/secondary pots.
* Stereo capture, overdub/circular banks, PCM16 SD slots and bounded error handling.
* Host/production callback/SD driver/concurrency tests, sanitizers, ARM checks, CI.
* Review corrections: restore free playback after the last note; preserve SOURCE
  during overdub finalization; lock START/SIZE/SPEED during every normal recording.
* Hardware sound, timing and compatibility remain pending.
