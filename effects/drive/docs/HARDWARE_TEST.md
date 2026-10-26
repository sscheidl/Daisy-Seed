# Hardwaretest Drive FX 0.1.0

**Stand:** 27. Oktober 2026 · Firmware `effects/drive/build/DriveFx-v0.1.0.bin`
(`make PROJECT=drive-fx`). Den Test führt der Benutzer am Kit durch. Alle Ergebnisse sind
**NICHT GEMESSEN**, bis ein Eintrag mit Datum steht.

Quelle: Line-Signal in den Stereo-Eingang, am besten die Acid-303-Firmware eines zweiten
Kits oder ein Synth mit Säge/Rechteck. Kopfhörer leise beginnen.

## 1. Hörtest

| Prüfung | Ergebnis |
| --- | --- |
| Soft, Hard, Tape, Fold (S1–S4) klingen unterschiedlich, LEDs folgen | NICHT GEMESSEN |
| Auto-Level an (S5), Zielversatz −1,5 dB: Drive 0→100 % ändert die Lautheit kaum | NICHT GEMESSEN |
| Auto-Level aus: Drive macht deutlich lauter | NICHT GEMESSEN |
| Fold mit Rechteck: wird leiser (bekannte Grenze) | NICHT GEMESSEN |
| Aliasing bei Hard und Fold mit hohen Noten (D5: entscheidet über Oversampling) | NICHT GEMESSEN |
| SW17 an/aus ohne Knacken. Aus = trockenes Signal | NICHT GEMESSEN |
| Tone (VR2) dunkelt ab, Level (VR3) wirkt | NICHT GEMESSEN |
| Mono-Kabel nur links: beide Ausgänge | NICHT GEMESSEN |

## 2. MIDI und Panel

| Prüfung | Ergebnis |
| --- | --- |
| CC 20–27 über USB und DIN | NICHT GEMESSEN |
| `sq_probe.py identify` meldet `drive-fx`, `params drive` liest die Werte | NICHT GEMESSEN |

## 3. Ressourcen-PASS (Arbeitsauftrag 5, R15)

Stressfall: Drive 100 %, Fold, schnelle Poti-Fahrten, CCs über DIN, `sq_probe.py monitor`.
Danach `sq_probe.py stats`:

| Wert | Grenze | Ergebnis |
| --- | --- | --- |
| `callback_overruns` (LED D16) | 0 | NICHT GEMESSEN |
| `callback_peak_cycles` | < 480 000 | NICHT GEMESSEN |
| `queue_high_water` | < 32 | NICHT GEMESSEN |

Statische Werte aus der ELF-Prüfung (GEMESSEN 2026-10-27 auf dem Build-Rechner, kein
Hardwarewert): DTCM 107 184 / 131 072 Byte, statische Reserve 23 888 Byte, SRAM 138 316 Byte.
