# Changelog

## Unreleased

* **Firmware updates without BOOT/RESET** ([common/bootloader](../../common/bootloader/README.md)):
  holding S13+S14 alone for 3 s, or the USB-MIDI SysEx `F0 7D 42 4C 01 01 42 4F 4F 54 F7`
  (answered with `F0 7D 42 4C 01 41 F7`), lights all LEDs and restarts into the Daisy
  bootloader with `DAISY_INFINITE_TIMEOUT`. The control timer only runs the key timer; the raw USB-MIDI parser
  matches the SysEx in the USB interrupt.
* In KEYBOARD mode S13+S14 are G5+A5: a held dyad of exactly these two keys for 3 s
  enters update mode.

## 0.1.0 - 2026-10-19

* Initial stereo granular/phase-vocoder adaptation for official Seed3 Desktop,
  BOOT_SRAM, J7 USB MIDI, three UI modes and eight primary/secondary pots.
* Stereo capture, overdub/circular banks, PCM16 SD slots and bounded error handling.
* Host/production callback/SD driver/concurrency tests, sanitizers, ARM checks, CI.
* Review corrections: restore free playback after the last note; preserve SOURCE
  during overdub finalization; lock START/SIZE/SPEED during every normal recording.
* Hardware sound, timing and compatibility remain pending.
