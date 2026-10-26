# MIDI Acid 303

Gilt für die Firmware `acid-303` (omni) und für die Stimme im Produkt Acid Seq
(Stimmen-Kanal, Standard 2). Quelle: [Parametervertrag](../../../sequencers/step-seq/docs/PARAMETERS.md).

| CC | Parameter | Bereich | Abbildung |
| --- | --- | --- | --- |
| 5 | Slide-Zeit | 10–500 ms | linear |
| 7 | Lautstärke | 0–100 % | linear |
| 70 | Wellenform | Säge, Rechteck | 0–63 Säge, 64–127 Rechteck |
| 71 | Resonanz | 0–100 % | linear |
| 74 | Cutoff | 40 Hz – 12 kHz | logarithmisch |
| 75 | Decay | 30 ms – 3 s | logarithmisch |
| 76 | Env Mod | 0–100 % | linear |
| 77 | Accent | 0–100 % | linear |
| 78 | Tuning | ±12 Halbtöne | linear, gerundet |
| 79 | Fein | ±100 Cent | linear |
| 120, 123 | All Sound Off, All Notes Off | | beendet die MIDI-Noten |

* **Noten:** Note-On mit Anschlag 1–100 spielt normal, 101–127 mit Accent. Note-On mit
  Anschlag 0 gilt als Note-Off.
* **Slide bei MIDI-Noten** (Parameter `slide_mode`, an SW18 der Firmware): aus, Legato
  (überlappende Noten gleiten), immer.
* **Sequencer-Noten** gleiten nur bei Slide-Steps (Legato-Ereignis), unabhängig vom
  Slide-Modus.
