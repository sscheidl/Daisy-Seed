# Changelog

The version is defined in [src/version.h](src/version.h).

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
