# Changelog Acid 303

## 0.1.0 (2026-10-27), Phase 2

Erste Stimme und erste Firmware `acid-303`. Hardware NICHT GEMESSEN.

* **Stimme:** DaisySP PolyBLEP Säge/Rechteck in den LadderFilter (LP24, 100 % Resonanz =
  K 4,4, Selbstoszillation). Filterhüllkurve mit bis zu 4 Oktaven Env Mod, Accent
  (bis +6 dB, +2 Oktaven, Decay ≤ 200 ms), konstante Slide-Zeit, 20 ms Parameterglättung,
  DC-Blocker.
* **Notenstapel** mit Quelle und Kennung (Sequencer, MIDI): die letzte Note gewinnt,
  Note-Off und Panic je Quelle (R16).
* **Firmware:** Tasten chromatisch ab C2, VR1–VR8 für die Stimme, SW17 Accent,
  SW18 Slide-Modus, MIDI omni über USB und DIN mit CC-Plan, SQ-Parameter über USB,
  Bootloader nach Stummschalten.
* **Golden-Hashes** (FNV-1a über 2 s der Test-Phrase): Säge `0xB05F5A1F`, Rechteck
  `0x355159C8`.
* **303-Fixture** `tests/fixtures/acid-seq/acid303_phrase.f32` (Stimme 0.1.0) für die
  Auto-Level-Abnahme in Phase 3.

Festlegungen aus dem Auftrag, die in 4.6 noch offen waren:

* MIDI-Anschlag über 100 spielt Accent (der Sequencer sendet 127 für Accent, 100 sonst).
* Die eigene Firmware hört omni. Im Produkt gilt der Stimmen-Kanal.
* **Klick-Kriterium:** Bezugswert ist der eingeschwungene Wert der lauteren der beiden
  Einstellungen. Bei einem Sprung nach leise wäre der Wert nach dem Sprung kleiner als
  die normale Steigung davor.

Korrekturen nach der Codex-Fehlersuche (2026-10-27, `docs/acid-seq/CODEX_FEHLERSUCHE.md`):

* **F01 volle Queue:** Ist die Befehls-Queue voll, gehen Note-Off, All-Off und Mute über
  einen eigenen Weg (`include/acid303/critical.h`), den der Callback direkt nach der
  Queue abarbeitet. Sie gehen nie verloren. Verworfene Note-Ons und CCs zählt
  `midi_queue_overflow`.
* **F10 Revision:** SQ-Parameter tragen ihre Revision bis zur Übernahme im Callback.
  Das Gerät meldet danach `applied` gleich `accepted`.
* **F12 Slide-Modus:** SW18 setzt den Slide-Modus beim Einschalten und wenn er bewegt
  wird. Ein Wert vom Panel oder per CC bleibt bis dahin bestehen.
* **F14 CI-Auswahl:** Änderungen am gemeinsamen Step-Seq-Unterbau (Bedienung,
  SQ-Gerät, Queues, Diagnose, Host-Fakes) wählen auch die Tests von Acid 303, Drive und
  Delay.

Die Golden-Hashes `0xB05F5A1F` und `0x355159C8` bleiben unverändert.
