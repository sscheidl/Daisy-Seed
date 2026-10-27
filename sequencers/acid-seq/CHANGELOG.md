# Changelog Acid Seq

## 0.1.0 (2026-10-27), Phase 6

Erstes Produkt aus Step Seq 0.1.0, Acid 303 0.1.0, Drive 0.1.0 und Stereo Delay 0.1.0.
Hardware NICHT GEMESSEN, kein Release.

* **Signalweg** `include/acid_seq/chain.h` nach 4.10: Sequencer → Stimme → Drive
  (Überblendung 10 ms) → Delay (Einspeisung, Nachhall, Dry/Wet) → Soft-Limiter.
* **Stimmenbesitz** zwischen Sequencer und MIDI-Stimmen-Kanal (R16), CC-Plan für
  Stimme, Drive und Delay auf dem Stimmen-Kanal.
* **Tempo-Übergabe** vom Sequencer an das Delay.
* **Firmware** mit allen vier Komponenten im SQ-Protokoll, CV/Gate und MIDI wie Step Seq,
  `delay_memory` im SDRAM.
* **Golden-Hash** (FNV-1a über 4 s beider Kanäle, festes Pattern, 120 BPM, alle Effekte an):
  `0xBC3D9C4E`. Die Golden-Hashes der Komponenten bleiben unverändert.

Festlegungen, die der Auftrag offenließ:

* Soft-Limiter: unter 0,8 linear, darüber `0,8 + 0,2 · u / (1 + u)` mit
  `u = (|x| − 0,8) / 0,2`.
* Nach dem Ende des Delay-Nachhalls blendet Dry/Wet auf 0 %. Das Produkt ist dann exakt
  trocken.
* Die Callback-Diagnose kommt aus `step_seq/diagnostics.h`. Die Registerkomponente
  `callback-diagnostics` ist der Mod-on-Daisy-Adapter und wird nicht verwendet.
