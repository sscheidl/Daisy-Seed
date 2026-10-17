# Changelog

Die Version steht in [`src/version.h`](src/version.h).

## 0.2 — 2026-10-17

* **USB-MIDI** über J7 („Daisy Seed External“): Empfang von Note On (Noten 0–127, Velocity,
  16 freie Stimmen mit Stimmenklau) und Senden der Tasten S1–S16 auf Kanal 1.
  Siehe [docs/MIDI.md](docs/MIDI.md).
* Weicher Begrenzer, damit 32 Stimmen unter dem Ausgangspegel-Limit bleiben; zweite
  Harmonische oberhalb von 12 kHz abgeschaltet (kein Aliasing).
* Versionierung: Version nur in `src/version.h`, Dateiname und eingebettete Kennung
  folgen daraus, `scripts/check-firmware.py` prüft sie.
* Enthält die Korrektur gegen erneutes Auslösen nach Kontaktaussetzern bei gehaltener Taste.
  Ein Tastendruck sendet damit auch nur ein Note On.
* Neu geprüft: ThreadSanitizer (`make test-thread`, auch in der CI) für die Warteschlangen,
  Tests für MIDI, Begrenzer und Aliasing sowie USB-MIDI im Callback-Test.
* Am Kit mit Studio One getestet: Empfang (Studio One spielt das Board) und Senden (Tasten
  als MIDI in Studio One). Noch offen: Halbtöne/Velocity/16+ Noten gleichzeitig, Akkorde beim
  Senden, Betrieb ohne USB-Verbindung, CPU-Last (siehe [Prüfbericht](docs/VALIDATION.md)).

## 0.1 — 2026-10-16

* 16 unabhängige Toy-Piano-Stimmen (Sinus mit zweiter Harmonischer), One-Shot-AD-Hüllkurve
  mit VR1 (Attack 2–300 ms) und VR2 (Decay 50–3000 ms), LEDs D1–D16 zeigen die Tasten.
* Am Kit getestet: Tasten, Hüllkurve, Polyphonie, schnelles Spielen.
