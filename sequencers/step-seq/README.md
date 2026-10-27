# Step Seq

16-Step-Sequencer (2 × 8) nach dem Vorbild KORG SQ-1 für das Seed3 Desktop Dev Kit:
CV/Gate, MIDI über USB (J7) und DIN, drei Abspielmodi an SW18, Start/Stop an SW17.
Eigene Firmware `step-seq` ohne Audio, Kern des Produkts [Acid Seq](../acid-seq/).

| Stand | |
| --- | --- |
| Version | Kern und Firmware 0.1.0 (`src/version.h`) |
| Software | Host-, Sanitizer- und ARM-Prüfungen bestanden (siehe CHANGELOG) |
| Hardware | NICHT GEMESSEN, [Hardwaretest](docs/HARDWARE_TEST.md) |

## Bedienung

| Element | Funktion |
| --- | --- |
| S1–S8, S9–S16 | Step A1–A8, B1–B8 wählen. Lang drücken (≥ 400 ms) schaltet das Gate beim Loslassen. Akkorde lösen kein Umschalten aus |
| LEDs | Gate an hell, aus dunkel. Lauflicht dunkelt bzw. hellt kurz auf. Gewählter Step blinkt schnell |
| VR1 / VR2 | Oktave (0–4) / Halbton (C–H) des gewählten Steps. Bei gestopptem Sequencer klingt die Note 150 ms |
| VR3 / VR4 | Gate-Länge 5–100 % / nichts, Accent, Slide, Accent + Slide |
| VR5 / VR6 | Tempo 30–300 BPM / Swing 50–75 % |
| VR7 / VR8 | Länge 1–16 / Transponierung ±12 |
| SW17 | Start/Stop. Beim Einschalten auf Run startet der Sequencer nach der Initialisierung |
| SW18 | oben Vorwärts, Mitte Rückwärts, unten Zufall |
| S9 + S16 (5 s) | Bootloader. Vorher gehen Gates und Noten aus |

Nach einem Step-Wechsel wirken VR1–VR4 erst, wenn sie sich um mehr als 2 % bewegt haben.

| Buchse | Belegung |
| --- | --- |
| CV OUT 1 | Tonhöhe 1V/Oct (0 V = C1), Slide als Glide |
| CV OUT 2 | Gate (Codes aus der Kalibrierung) |
| Gate IN 1 | Clock-Eingang (Quelle „Gate IN 1“ oder „Auto“) |
| Gate IN 2 | Reset auf die steigende Flanke, alternativ Run-Freigabe |
| CV IN 1 | Transponierung 1V/Oct (±24 Halbtöne) |
| CV IN 2 | aus, Gate-Länge oder Swing |

**CV ist nach jedem Start gesperrt.** Freigegeben wird erst nach dem Diagnosetest H14
und der Kalibrierung mit [`tools/sq_probe.py`](tools/sq_probe.py) (Hardwaretest Schritt 1–2).

MIDI: Noten auf Kanal 1 (einstellbar), Accent mit Anschlag 127, Slide als Legato.
MIDI-Clock (24 PPQN), Start, Stop und Continue gehen nur bei interner Clock oder
Gate IN 1 hinaus. Empfangene Clock wird nie zurückgesendet. USB und DIN senden
gleichzeitig, jeder Ausgang ist abschaltbar.

## Bauen und testen

```bash
make PROJECT=step-seq -j4                 # build/StepSeq-v0.1.0.bin
make PROJECT=step-seq test                # Kern, Firmware auf Fakes, Threads, Vertrag, Probe
make PROJECT=step-seq test-sanitize
make PROJECT=step-seq test-thread         # nur Linux (TSan)
python3 scripts/check-firmware.py sequencers/step-seq/build/StepSeq-v0.1.0.bin
python3 scripts/check-elf-placement.py sequencers/step-seq/build/StepSeq-v0.1.0.elf
make PROJECT=step-seq manifest
```

## Aufbau

* `include/step_seq/`: header-only Kern ohne libDaisy. Enthält Pattern und Abspielregeln
  (`pattern.h`), Zeitbasis, Transport und Clock-Quellen (`engine.h`), Bedienung
  (`controls.h`), CV-Kalibrierung, Glide und den DAC-Registeradapter (`cv.h`),
  MIDI-Ausgabe mit Notenverfolgung und Sendebudget (`midi_out.h`), SQ-Codec und
  Geräteseite (`sq_protocol.h`, `device.h`), Parameter (`params.h`, `param_table.h`
  aus `contracts/parameters.json`) sowie die Callback-Diagnose (`diagnostics.h`).
* `src/main.cpp`: Hardware-Adapter. Der Callback (1 kHz) besitzt Bedienung, Kern und DAC.
  `main` besitzt MIDI, SQ, LEDs und den Bootloader. Die Übergabe läuft nur über
  SPSC-Queues und Snapshots.
* **DAC (R01):** `hw.SetDacOut` nur einmal beim Start. Danach schreibt der Callback
  `DAC1->DHR12R1/DHR12R2` und `SWTRIGR`, ohne HAL und ohne Lock.

## Verträge

* [SQ-Protokoll](docs/PROTOCOL.md) (`F0 7D 53 51`), Referenz-Codec
  [`tools/sq_codec.py`](tools/sq_codec.py), Testvektoren
  [`contracts/sq_vectors.json`](contracts/sq_vectors.json)
* [Parametervertrag und CC-Plan](docs/PARAMETERS.md), maschinenlesbar in
  [`contracts/parameters.json`](contracts/parameters.json)
* [Transport- und Ereignisvertrag](docs/TRANSPORT.md) mit Sollfolgen in
  [`contracts/transport_vectors.json`](contracts/transport_vectors.json)

Plan und Regeln: [Arbeitsauftrag Acid Seq](../../docs/acid-seq/ARBEITSAUFTRAG.md).
