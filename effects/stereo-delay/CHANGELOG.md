# Changelog Stereo Delay

## 0.1.0 (2026-10-27), Phase 4

Erster Kern und erste Firmware `delay-fx`. Hardware NICHT GEMESSEN.

* **Kern** nach 4.8: Ping-Pong und Stereo L/R, Zeit als Abstand der Echos. Frei
  10 ms – 2 s oder Sync 1/32 – 1/1 mit punktiert und triolisch, voller Bereich bis 12 s
  (D12). Lineare Interpolation, Lesegrenze Leitungslänge minus Rand (genau 12 s).
  Gleiten mit 5 % Rate, Überblenden in 20 ms ab 10 % Sprung. Feedback bis 95 % mit
  Soft-Clip, Dämpfung 1–20 kHz. Wet-only, im gemeinsamen Test `make test-fx`.
* **Host** (`host.h`): Einspeisung 10 ms, Nachhall mit Ende-Erkennung (−90 dBFS, 500 ms),
  Grenze 20 s mit 1 s Ausblenden, Wiedereinschalten ohne Löschen, Dry/Wet.
* **Firmware `delay-fx`:** Stereo-Eingang, Bedienung nach README. Tempo aus VR5 oder aus
  MIDI-Clock (USB, DIN), MIDI-CC 102–109, SQ-Parameter, Bootloader.
* **Golden-Hash** (FNV-1a über 3 s beider Modi, feste Eingabe, 132 BPM): `0xD11BFF18`.

Festlegungen, die der Auftrag offenließ:

* Dry/Wet mischt `dry · (1 − mix) + wet · mix`.
* Ein Moduswechsel gilt sofort, ohne Überblenden.
* SW18 der eigenen Firmware wählt Modus und Sync in drei Stellungen. Stereo L/R frei geht
  nur per CC oder Panel.
