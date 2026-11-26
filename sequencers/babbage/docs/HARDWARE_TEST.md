# Hardwaretest Acid Seq 0.1.0

**Stand:** 27. Oktober 2026 · Firmware `sequencers/acid-seq/build/AcidSeq-v0.1.0.bin`
(`make PROJECT=acid-seq`). Den Test führt der Benutzer am Kit durch. Alle Ergebnisse sind
**NICHT GEMESSEN**, bis ein Eintrag mit Datum steht.

> **Sicherheit:** CV OUT 1/2 erst nach der Diagnose H14 und der Freigabe an einen externen
> Synth anschließen ([Step-Seq-Hardwaretest](../../step-seq/docs/HARDWARE_TEST.md),
> Schritte 1 und 2). Kopfhörer und Lautsprecher leise beginnen: Bei Resonanz 100 % schwingt
> das Filter selbst, bei Feedback 95 % klingt das Delay lange nach.

## 0. Vorbereitung

* Bootloader-Modus und Flashen macht der Benutzer selbst.
* `python3 sequencers/step-seq/tools/sq_probe.py identify` meldet `acid-seq 0.1.0`.
* `sq_probe.py components` listet `step-seq`, `acid-303`, `drive` und `stereo-delay`.

## 1. Gesamttest

| Prüfung | Ergebnis |
| --- | --- |
| SW17 auf Run beim Einschalten: Sequencer startet, Audio an L/R und Kopfhörer | NICHT GEMESSEN |
| Drei Modi über SW18 (Vorwärts, Rückwärts, Zufall), hörbar und im Monitor | NICHT GEMESSEN |
| Start/Stop mit SW17: Stopp beendet die Note, kein hängender Ton | NICHT GEMESSEN |
| Accent und Slide aus dem Pattern klingen wie bei `acid-303` | NICHT GEMESSEN |
| Drive an/aus (Panel oder CC 20) ohne Knacken | NICHT GEMESSEN |
| Delay an/aus (CC 102): Nachhall läuft aus, danach trocken ohne Pegelsprung | NICHT GEMESSEN |
| Delay im Sync folgt VR5 und einer externen MIDI-Clock | NICHT GEMESSEN |
| Soft-Limiter: Resonanz 100 %, Drive 100 %, Feedback 95 %: Ausgang verzerrt weich, kein hartes Clipping | NICHT GEMESSEN |
| Ohne Audiokabel: CV/Gate (nach Freigabe) und MIDI laufen unverändert | NICHT GEMESSEN |
| CV/Gate, MIDI-Ausgang und Audio gleichzeitig, im Takt zueinander | NICHT GEMESSEN |

## 2. Stimme und MIDI (R16)

| Prüfung | Ergebnis |
| --- | --- |
| Keyboard auf Kanal 2 (DIN und USB) spielt die Stimme, Anschlag über 100 als Accent | NICHT GEMESSEN |
| Während der Sequencer läuft: die zuletzt gespielte Note gilt; Loslassen gibt an den Sequencer zurück | NICHT GEMESSEN |
| Gehaltene Keyboard-Note, Sequencer stoppen: die Keyboard-Note klingt weiter | NICHT GEMESSEN |
| Noten auf Kanal 1 spielen die Stimme nicht | NICHT GEMESSEN |
| CCs auf Kanal 2: Cutoff (74), Resonanz (71), Drive (22), Delay-Feedback (107) | NICHT GEMESSEN |
| Keyboard-Noten erscheinen nicht am MIDI-Ausgang | NICHT GEMESSEN |
| USB abziehen bei gehaltener DAW-Note: die Note endet | NICHT GEMESSEN |
| Bootloader (S9 + S16, 5 s): Stimme stumm, Gate low, All Notes Off vor dem Reset | NICHT GEMESSEN |

## 3. Panel

| Prüfung | Ergebnis |
| --- | --- |
| Editor erkennt `acid-seq` (SQ) und zeigt Sequencer, Stimme, Drive, Delay, MIDI | NICHT GEMESSEN |
| Änderungen im Panel wirken hörbar und bleiben nach „Vom Gerät lesen“ | NICHT GEMESSEN |
| `.acidpatch` speichern, laden und auf das Gerät übertragen | NICHT GEMESSEN |

## 4. Ressourcen-PASS im vollen Stressfall (Arbeitsauftrag 5, R15)

Stressfall: Sequencer bei 300 BPM, Resonanz 100 %, Accent und Slide im Pattern,
Drive 100 %, Delay mit Feedback 95 % und Sync 1/32, gleichzeitig Keyboard-Noten und CCs
über DIN, Panel-Verkehr und `sq_probe.py monitor`. Danach `sq_probe.py stats`:

| Wert | Grenze | Ergebnis |
| --- | --- | --- |
| `callback_overruns` | 0 | NICHT GEMESSEN |
| `callback_peak_cycles` | < 480 000 | NICHT GEMESSEN |
| `stack_free_bytes` | > 4096 | NICHT GEMESSEN |
| `queue_high_water` | < 64 | NICHT GEMESSEN |
| `note_on_dropped`, `midi_queue_overflow` | 0 | NICHT GEMESSEN |
| Langzeitlauf 1 h im Stressfall ohne Aussetzer | bestanden | NICHT GEMESSEN |

Statische Werte aus der ELF-Prüfung (GEMESSEN 2026-10-27 auf dem Build-Rechner, kein
Hardwarewert): DTCM 113 160 / 131 072 Byte, statische Reserve 17 912 Byte. SRAM
187 348 Byte. `delay_memory` 4 608 512 Byte im SDRAM bei `0xc0000000`. Image 193 292 Byte.
