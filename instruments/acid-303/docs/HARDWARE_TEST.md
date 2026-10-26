# Hardwaretest Acid 303 0.1.0

**Stand:** 27. Oktober 2026 · Firmware `instruments/acid-303/build/Acid303-v0.1.0.bin`
(`make PROJECT=acid-303`). Den Test führt der Benutzer am Kit durch. Alle Ergebnisse sind
**NICHT GEMESSEN**, bis ein Eintrag mit Datum steht.

> Kopfhörer und Lautsprecher leise beginnen: Bei Resonanz 100 % schwingt das Filter selbst.

## 1. Klang (Hörtest)

| Prüfung | Ergebnis |
| --- | --- |
| Säge und Rechteck (VR8) klingen sauber, ohne Aliasing-Pfeifen in hohen Lagen | NICHT GEMESSEN |
| Cutoff (VR1) über den ganzen Bereich ohne Zippern | NICHT GEMESSEN |
| Resonanz (VR2) bis zur Selbstoszillation, stabil, kein Aufschaukeln | NICHT GEMESSEN |
| Env Mod und Decay (VR3, VR4) formen den typischen Acid-Sweep | NICHT GEMESSEN |
| Accent (SW17 an, VR5) lauter, heller, kürzer | NICHT GEMESSEN |
| Slide (SW18 Mitte/unten) gleitet ohne neue Hüllkurve | NICHT GEMESSEN |
| Tuning (VR6) rastet in Halbtönen | NICHT GEMESSEN |
| Kein Knacken bei schnellen Poti-Bewegungen | NICHT GEMESSEN |

## 2. MIDI

| Prüfung | Ergebnis |
| --- | --- |
| USB: Noten und CCs aus der DAW (CC-Tabelle in MIDI.md) | NICHT GEMESSEN |
| DIN: Noten und CCs von einem externen Keyboard | NICHT GEMESSEN |
| Anschlag über 100 spielt Accent | NICHT GEMESSEN |
| `sq_probe.py identify` meldet `acid-303`, `params acid-303` liest die Werte | NICHT GEMESSEN |
| USB abziehen bei gehaltener DAW-Note: die Note endet | NICHT GEMESSEN |

## 3. Ressourcen-PASS (Arbeitsauftrag 5, R15)

Stressfall: Resonanz 100 % (Selbstoszillation), Accent, schnelle Parameterfahrten über
VR1–VR4, gleichzeitig MIDI-Noten und CCs über DIN und `sq_probe.py monitor`.
Danach `sq_probe.py stats`:

| Wert | Grenze | Ergebnis |
| --- | --- | --- |
| `callback_overruns` | 0 | NICHT GEMESSEN |
| `callback_peak_cycles` | < 480 000 | NICHT GEMESSEN |
| `queue_high_water` | < 64 | NICHT GEMESSEN |
| CPU-Last der Stimme (LadderFilter 4-fach) | Reserve für Drive und Delay im Produkt | NICHT GEMESSEN |

Statische Werte aus der ELF-Prüfung (GEMESSEN 2026-10-27 auf dem Build-Rechner, kein
Hardwarewert): DTCM 107 816 / 131 072 Byte, statische Reserve 23 256 Byte, SRAM 158 596 Byte.
Host-Benchmark (`make bench`, relative Kosten): Stimme ≈ 1,33 × Oszillator + Ladder.
