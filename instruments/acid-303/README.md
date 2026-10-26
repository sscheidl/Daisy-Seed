# Acid 303

Monophone Stimme nach dem Vorbild TB-303 für das Seed3 Desktop Dev Kit: Säge oder
Rechteck (DaisySP PolyBLEP), DaisySP-`LadderFilter` (LP24, intern 4-fach überabgetastet)
mit Selbstoszillation (D4), Filterhüllkurve mit Env Mod und Decay, Accent und Slide.
Header-only-Kern für das Produkt [Acid Seq](../../sequencers/acid-seq/) und eigene
Firmware `acid-303` zum Testen und Spielen.

| Stand | |
| --- | --- |
| Version | Kern und Firmware 0.1.0 (`src/version.h`) |
| Software | Host-, Sanitizer- und ARM-Prüfungen bestanden (siehe CHANGELOG) |
| Hardware | NICHT GEMESSEN, [Hardwaretest](docs/HARDWARE_TEST.md) |

Der LadderFilter ist ein Moog-artiges Modell. Er nähert den 303-Klang an, bildet aber
kein Diode-Ladder nach. Ein Diode-Ladder bleibt eine spätere, eigens versionierte
Klangänderung mit neuem Golden-Hash.

## Bedienung (Firmware `acid-303`)

| Element | Funktion |
| --- | --- |
| S1–S16 | chromatisch ab C2 (MIDI 36–51) |
| VR1 / VR2 | Cutoff 40 Hz – 12 kHz (logarithmisch) / Resonanz (100 % schwingt selbst) |
| VR3 / VR4 | Env Mod / Decay 30 ms – 3 s (logarithmisch) |
| VR5 / VR6 | Accent-Stärke / Tuning ±12 Halbtöne (gerastet) |
| VR7 / VR8 | Lautstärke / Wellenform (links Säge, rechts Rechteck) |
| SW17 | Accent für die Tasten |
| SW18 | oben Slide aus, Mitte Slide bei gehaltener Taste (Legato), unten Slide immer |
| S9 + S16 (5 s) | Bootloader. Vorher wird die Stimme stummgeschaltet |

Potis wirken erst, wenn sie sich bewegen. Bis dahin gilt der zuletzt per MIDI-CC oder
Panel gesetzte Wert. Ausgang: mono auf beide Kanäle und den Kopfhörer. LEDs zeigen
gehaltene Tasten und die klingende Note.

MIDI über USB (J7) und DIN, in dieser Firmware omni: Noten (Anschlag über 100 = Accent),
CCs nach [MIDI.md](docs/MIDI.md), CC 120/123 beenden alle Noten. SQ-Parameter nur über USB.

## Bauen und testen

```bash
make PROJECT=acid-303 -j4                 # build/Acid303-v0.1.0.bin
make PROJECT=acid-303 test                # Stimme und Firmware auf Fakes
make PROJECT=acid-303 test-sanitize
make -C instruments/acid-303 bench        # relative Host-Kosten, keine Seed3-Messung
make -C instruments/acid-303 fixture      # 303-Fixture neu schreiben (nur absichtlich)
python3 scripts/check-firmware.py instruments/acid-303/build/Acid303-v0.1.0.bin
python3 scripts/check-elf-placement.py instruments/acid-303/build/Acid303-v0.1.0.elf
```

## Kern

`include/acid303/voice.h` enthält die Stimme, `params.h` die Parameter nach dem
[Vertrag](../../sequencers/step-seq/docs/PARAMETERS.md) und den CC-Plan.

* **Noten:** gemeinsamer Stapel für Sequencer und MIDI. Die zuletzt gespielte Note klingt,
  ein Note-Off wirkt nur auf die Note seiner Quelle und Kennung, Panic nur auf eine
  Quelle (R16). Endet die klingende Note, übernimmt die jüngste noch gehaltene legato.
* **Hüllkurven:** Amplitude 3 ms Attack, langsames Abfallen beim Halten, 8 ms Release.
  Filter auf 10 % in der Decay-Zeit. Accent: bis +6 dB, bis 2 Oktaven mehr Hüllkurve,
  Decay höchstens 200 ms.
* **Slide:** konstante Zeit (Standard 60 ms), ohne neue Hüllkurve.
* **Glättung:** Parameter 20 ms, Filterkoeffizienten alle 8 Samples. Am Ausgang liegt ein
  DC-Blocker (~4 Hz).
* **Determinismus:** keine libm-Aufrufe im Audiopfad (eigenes `Exp2`), damit die
  Golden-Hashes auf jedem Host gleich sind.
