# Changelog

The version is defined in [src/version.h](src/version.h).

## Unreleased

* **Firmware updates without BOOT/RESET** ([common/bootloader](../../common/bootloader/README.md)):
  holding S13+S14 alone for 3 s, or the USB-MIDI SysEx `F0 7D 42 4C 01 01 42 4F 4F 54 F7`
  (answered with `F0 7D 42 4C 01 41 F7`), lights all LEDs and restarts into the Daisy
  bootloader with `DAISY_INFINITE_TIMEOUT`. The audio callback only runs the key timer.
* Sound unchanged: the golden hash is the same before and after (compared on one
  compiler); `src/version.h` is not changed by this entry.

## 0.3 — 2026-10-23

* **Mod on Daisy modulation matrix** ([modules/mod-on-daisy](../../modules/mod-on-daisy/)).
  Toy Piano is its development environment, so it gets new modulation targets:
  * per voice: **Pitch** (±2 semitones at depth 100), **Level** (gain 0–2, ±100 %)
    and **Harmonic** (second-harmonic weight 0–0.5);
  * at the strike: **Attack** and **Decay**, latched like the pots;
  * columns 5–7 are left free.
* Sources: LFO 1/2, Random, CV 1/2, mod wheel (CC1), channel aftertouch and
  polyphonic aftertouch per MIDI voice. The grid is configured over USB-MIDI SysEx
  (`modules/mod-on-daisy/docs/PROTOCOL.md`), for now with `tools/mod_probe.py`
  until the editor exists.
* Level changes ramp over one block (1 ms). A silent voice takes its level at once.
  The second harmonic stays off above 12 kHz of the *modulated* pitch.
* **With an empty matrix the output is bit-identical to v0.2.** A new golden test
  hashes a 9 s performance of keys, pots and USB-MIDI and compares it with the hash
  recorded from v0.2 (`tests/test_golden.cpp`).
* CC1, channel and polyphonic aftertouch are now consumed by the matrix. Note On
  handling and key output are unchanged.
* DTCM use rises from 116,072 to 118,928 bytes (+2.8 KB for the matrix and 32 voice
  pressure smoothers), leaving about 12 KB for the stack.
* Hardware test pending: [HARDWARE_TEST_V0.3](docs/HARDWARE_TEST_V0.3.md).

## 0.2 — 2026-10-17

* **USB-MIDI** over J7 ("Daisy Seed External"): receive Note On for notes 0–127
  with velocity and 16 independently allocated MIDI voices, stealing the oldest
  voice when full. Transmit S1–S16 on channel 1. See [MIDI.md](docs/MIDI.md).
  Debounced key changes are bundled into USB messages, at most eight per transfer;
  there is no event queue that could drop Note Offs.
* Soft limiting keeps 32 active voices below the output ceiling. The second
  harmonic is disabled for fundamentals above 12 kHz to avoid aliasing.
* Versioning: `src/version.h` controls the output name and embedded version marker.
  The firmware checker validates both.
* Includes the fix preventing held-key retriggers after short contact dropouts;
  one physical press also sends only one Note On.
* Added MIDI, limiter and aliasing tests, plus ThreadSanitizer checks for queues.
* Reported hardware test with Studio One: incoming notes play the board and key
  presses reach the DAW. Still to test: semitones/velocity/16+ notes together,
  outgoing chords, operation without a USB host and measured CPU load.

## 0.1 — 2026-10-16

* 16 independent sine/second-harmonic voices, one-shot AD with VR1 attack
  (2–300 ms) and VR2 decay (50–3000 ms), LEDs D1–D16 following keys.
* Operator reports successful keys, envelopes, polyphony and fast playing.
