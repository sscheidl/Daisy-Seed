# Hardwaretest Step Seq 0.1.0

**Stand:** 27. Oktober 2026 · Firmware `sequencers/step-seq/build/StepSeq-v0.1.0.bin`
(`make PROJECT=step-seq`). Den Test führt der Benutzer am Kit durch. Der Agent trägt die
Rückmeldungen ein. Alle Ergebnisse sind **NICHT GEMESSEN**, bis ein Eintrag mit Datum
steht.

> **Sicherheit:** Vor Schritt 1 (H14) **kein Kabel von CV OUT 1/2 an einen externen
> Synth.** Die Spannung beim Einschalten und die Polarität sind unbekannt.

## 0. Vorbereitung

1. Firmware flashen (der Benutzer, siehe [FLASHING.md](../../../docs/FLASHING.md)). Updates
   ohne BOOT/RESET: S9 + S16 für 5 s.
2. `python3 scripts/check-firmware.py sequencers/step-seq/build/StepSeq-v0.1.0.bin` → PASS.
3. `pip install mido python-rtmidi`, dann `python3 sequencers/step-seq/tools/sq_probe.py identify`.
   Das Ergebnis enthält `product: step-seq`, die Version und die UID.

## 1. Diagnose H14 zuerst (Arbeitsauftrag 4.5)

Mit der Diagnose-Firmware (`diagnostics/`) bzw. einem Multimeter an CV OUT 1 und 2:

| Prüfung | Ergebnis |
| --- | --- |
| Spannung bei DAC-Code 0, 1024, 2048, 3072, 4095 (beide Ausgänge) | NICHT GEMESSEN |
| Polarität (steigt die Spannung mit dem Code?) | NICHT GEMESSEN |
| Spannung direkt nach dem Einschalten und nach Reset, vor der Initialisierung | NICHT GEMESSEN |
| Spannung mit Step Seq nach dem Start (Startcode 0, CV gesperrt) | NICHT GEMESSEN |
| Daraus Ruhe-/Gate-Low-Code und Gate-High-Code | NICHT GEMESSEN |

## 2. Kalibrieren und freigeben mit `sq_probe.py`

```bash
python3 sequencers/step-seq/tools/sq_probe.py cal-set 0 --offset <Code bei 0 V> --scale <Codes pro Volt> --gate-low <Code> --gate-high <Code>
python3 sequencers/step-seq/tools/sq_probe.py cal-set 1 --offset <Code bei 0 V> --scale <Codes pro Volt> --gate-low <Low-Code aus H14> --gate-high <High-Code>
python3 sequencers/step-seq/tools/sq_probe.py cal-save kit.json
python3 sequencers/step-seq/tools/sq_probe.py enable
```

* 0 V entspricht C1 (MIDI 24). Eine negative Skalierung beschreibt eine invertierende Stufe.
* `enable` ohne Kalibrierung beider Ausgänge wird abgelehnt (`NAK NOT_CALIBRATED`).
* Nach einem Neustart ist CV wieder gesperrt. `cal-load kit.json` sendet nur an dasselbe Kit (UID).

| Prüfung | Ergebnis |
| --- | --- |
| `cal-set`, `cal-get`, `enable`, `disable` antworten | NICHT GEMESSEN |
| `disable` bei offenem Gate: CV OUT 2 sofort auf Low | NICHT GEMESSEN |
| Nach Neustart gesperrt, `cal-load` an anderem Kit wird abgelehnt | NICHT GEMESSEN |

## 3. Bedienung

| Prüfung | Ergebnis |
| --- | --- |
| S1–S16 wählen den Step (LED blinkt schnell), langes Drücken ≥ 400 ms schaltet das Gate beim Loslassen | NICHT GEMESSEN |
| Zwei oder drei Tasten gleichzeitig oder versetzt: kein Gate-Umschalten, in jeder Loslass-Reihenfolge | NICHT GEMESSEN |
| VR1 Oktave (5 Stufen), VR2 Halbton (12), VR3 Gate-Länge, VR4 Accent/Slide (4): rasten ohne Flattern | NICHT GEMESSEN |
| Nach einem Step-Wechsel ändern VR1–VR4 erst nach einer Bewegung über 2 % | NICHT GEMESSEN |
| VR5 Tempo 30–300 BPM, VR6 Swing 50–75 %, VR7 Länge 1–16, VR8 Transponierung ±12 | NICHT GEMESSEN |
| Bei gestopptem Sequencer: VR1/VR2 spielen die Note 150 ms (CV/Gate und MIDI) | NICHT GEMESSEN |
| SW18 oben/Mitte/unten: Vorwärts, Rückwärts, Zufall | NICHT GEMESSEN |
| SW17 Start/Stop. Richtung „gedrückt = Run“ stimmt (sonst `kRunWhenPressed` in `src/main.cpp` umdrehen) | NICHT GEMESSEN |
| Einschalten mit SW17 auf Run: läuft nach der Initialisierung los | NICHT GEMESSEN |
| LEDs: Gate an hell, aus dunkel, Lauflicht dunkelt bzw. hellt kurz auf | NICHT GEMESSEN |
| S9 + S16 unter 5 s: nichts. Ab 5 s: alle LEDs an, Bootloader. Vorher Gates aus, Notes aus | NICHT GEMESSEN |

## 4. CV/Gate an einem externen Synth (erst nach Schritt 1 und 2)

| Prüfung | Ergebnis |
| --- | --- |
| Gate öffnet und schließt am Synth, Retrigger bei gleichen Noten hörbar | NICHT GEMESSEN |
| 1V/Oct über 3 Oktaven mit Stimmgerät (Abweichung in Cent je Oktave) | NICHT GEMESSEN |
| Slide gleitet 60 ms ohne neue Hüllkurve | NICHT GEMESSEN |
| CV IN 1 transponiert in Halbtönen, am Step-Anfang übernommen | NICHT GEMESSEN |

## 5. Clock und Reset

Clock-Quelle umstellen: `sq_probe.py set step-seq clock_source 1` (Gate IN 1), `2` (MIDI), `3` (Auto).

| Prüfung | Ergebnis |
| --- | --- |
| Gate IN 1, Pulsbreite 2 ms, 5 ms, 10 ms bis 120 Hz: kein verlorener Step | NICHT GEMESSEN |
| Kürzeste sicher erkannte Pulsbreite | NICHT GEMESSEN |
| Clock-Ausfall: nach 2 s Gate aus, nächster Puls spielt den nächsten Step | NICHT GEMESSEN |
| Gate IN 2 Reset: nächster Step ist der Startpunkt | NICHT GEMESSEN |
| Auto: Gate-Clock übernimmt, nach Ausfall läuft die interne Clock im Raster weiter | NICHT GEMESSEN |

## 6. MIDI

| Prüfung | Ergebnis |
| --- | --- |
| USB in der DAW: Noten auf Kanal 1, Accent mit Anschlag 127, Legato bei Slide | NICHT GEMESSEN |
| DIN an einem externen Gerät: Noten und Clock (DIN ist neu im Repo) | NICHT GEMESSEN |
| DIN-Eingang: MIDI-Clock als Quelle (Start, Stop, Continue) | NICHT GEMESSEN |
| Sync in beide Richtungen; empfangene Clock wird nicht zurückgesendet | NICHT GEMESSEN |
| USB abziehen: DIN-Noten und Gate aus. Wieder anstecken: zuerst All Notes Off | NICHT GEMESSEN |
| Schwankung der MIDI-Clock (Ein- und Ausgang) | NICHT GEMESSEN |

## 7. Ressourcen-PASS (Arbeitsauftrag 5, R15)

Stressfall: Sequencer läuft mit 300 BPM, MIDI-Clock-Eingang über DIN, Panel-Verkehr
(`sq_probe.py monitor`), Poti-Fahrten. Danach `sq_probe.py stats`:

| Wert | Grenze | Ergebnis |
| --- | --- | --- |
| `callback_overruns` | 0 | NICHT GEMESSEN |
| `callback_peak_cycles` | < 480 000 000 / 1000 = 480 000 | NICHT GEMESSEN |
| `stack_free_bytes` | > 4096 | NICHT GEMESSEN |
| `queue_high_water` | < 64 | NICHT GEMESSEN |
| `note_on_dropped`, `midi_queue_overflow` | 0 | NICHT GEMESSEN |

Statische Werte aus der ELF-Prüfung (`scripts/check-elf-placement.py`, GEMESSEN
2026-10-27 auf dem Build-Rechner, kein Hardwarewert): DTCM 110 600 / 131 072 Byte,
statische Reserve 20 472 Byte. SRAM 152 824 Byte, SDRAM 0.
