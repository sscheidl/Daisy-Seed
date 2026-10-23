# Changelog

## 0.1.0 - 2026-10-19

* Initial stereo granular/phase-vocoder adaptation for official Seed3 Desktop,
  BOOT_SRAM, J7 USB MIDI, three UI modes and eight primary/secondary pots.
* Stereo capture, overdub/circular banks, PCM16 SD slots and bounded error handling.
* Host/production callback/SD driver/concurrency tests, sanitizers, ARM checks, CI.
* Review corrections: restore free playback after the last note; preserve SOURCE
  during overdub finalization; lock START/SIZE/SPEED during every normal recording.
* Hardware sound, timing and compatibility remain pending.
