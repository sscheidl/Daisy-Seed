# Acid Seq

Produkt aus [Step Seq](../step-seq/), [Acid 303](../../instruments/acid-303/),
[Drive](../../effects/drive/) und [Stereo Delay](../../effects/stereo-delay/).
Signalweg: Sequencer → Stimme → Drive → Delay → Host-Mixer → Soft-Limiter →
Ausgang. CV/Gate und MIDI laufen auch ohne angeschlossenen Audioausgang.

| Stand | |
| --- | --- |
| Version | 0.1.0 (`src/composed_version.h`), Komponenten mit eigenen Versionen |
| Phase | 6 umgesetzt, Host-Tests und ARM-Build grün |
| Hardware | NICHT GEMESSEN ([Hardwaretest](docs/HARDWARE_TEST.md)) |
| Release | keiner. Der Lieferweg für `acid-seq` ist noch offen (Arbeitsauftrag 4.10) |

Plan und Regeln: [Arbeitsauftrag Acid Seq](../../docs/acid-seq/ARBEITSAUFTRAG.md) 4.10.

## Bauen und testen

```bash
make PROJECT=acid-seq -j4
make -C sequencers/acid-seq test
make -C sequencers/acid-seq test-sanitize
python scripts/check-firmware.py sequencers/acid-seq/build/AcidSeq-v0.1.0.bin
python scripts/check-elf-placement.py sequencers/acid-seq/build/AcidSeq-v0.1.0.elf --sdram delay_memory:4608000
make PROJECT=acid-seq manifest
```

Image `sequencers/acid-seq/build/AcidSeq-v<version>.bin`, Kennung im Image
`ACID-SEQ-FW-VERSION=`, SQ-Produkt `acid-seq`.

## Bedienung am Kit

Wie [Step Seq](../step-seq/README.md) (Arbeitsauftrag 4.1): S1–S16 Steps, VR1–VR4 der
gewählte Step, VR5 Tempo, VR6 Swing, VR7 Länge, VR8 Transponierung, SW17 Start/Stop,
SW18 Modus. Die Stimme und die Effekte haben am Kit keine eigenen Regler. Sie werden über
das Panel (Editor, Seiten „Stimme“, „Drive“, „Delay“) oder per MIDI-CC eingestellt.

| Ausgang | Inhalt |
| --- | --- |
| Audio L/R und Kopfhörer | Stimme → Drive → Delay (Ping-Pong) → Soft-Limiter |
| CV OUT 1/2 | Tonhöhe und Gate des Sequencers, gesperrt bis zur Freigabe (H14, 4.5) |
| MIDI OUT (USB, DIN) | Noten des Sequencers auf dem Seq-Kanal (Standard 1), MIDI-Clock |

## Stimme, MIDI und Effekte

* **Stimmenbesitz (R16):** Sequencer und externe MIDI-Noten auf dem Stimmen-Kanal
  (Standard 2, Parameter `voice_channel`) teilen sich die monophone Stimme. Die zuletzt
  gespielte Note gilt. Jede Quelle beendet nur ihre eigenen Noten. Endet die klingende
  Note, übernimmt die zuletzt gehaltene Note der anderen Quelle.
* Ein Panic des Sequencers (Kanalwechsel, ausgeschalteter Ausgang) entfernt nur
  Sequencer- und Vorhör-Noten. USB-Verlust und ein Wechsel des Stimmen-Kanals beenden die
  MIDI-Noten. Vor dem Bootloader wird die Stimme ganz stumm geschaltet.
* Empfangene Noten werden nicht an die MIDI-Ausgänge weitergegeben.
* **CCs auf dem Stimmen-Kanal** (USB und DIN) nach [PARAMETERS.md](../step-seq/docs/PARAMETERS.md):
  Stimme CC 5, 7, 70, 71, 74–79, Drive CC 20–27, Delay CC 102–109.
  Anschlag über 100 spielt Accent.
* **Tempo-Übergabe:** Das Delay folgt im Sync-Modus dem Tempo des Sequencers. Das
  Tempo kommt von VR5 oder der externen Clock.
* **Host-Mixer:** Drive blendet beim Ein- und Ausschalten in 10 ms über. Das Delay hat
  Dry/Wet und einen Nachhall nach dem Ausschalten. Danach blendet das Produkt Dry/Wet
  auf 0 % und gibt die Stimme exakt trocken aus. Die Einzel-Firmware `delay-fx` behält
  dagegen `dry · (1 − mix)`.
* **Soft-Limiter** am Ausgang: unter 0,8 unverändert, darüber weich gegen ±1.

## Aufbau

* `include/acid_seq/chain.h`: Signalweg, gemeinsam für Firmware und End-to-End-Test.
* `src/main.cpp`: Firmware. Der Audio-Callback (1-ms-Blöcke) führt Bedienung,
  Sequencer, DAC und Audiokette. `main` führt MIDI, SQ, LEDs und Bootloader. Die
  Übergabe läuft über SPSC-Queues und einen Triple-Buffer.
* `delay_memory` (4,6 MB) liegt im SDRAM und wird vor dem Audiostart gelöscht.
* SQ-Komponenten in IDENTIFY-Reihenfolge: `step-seq`, `acid-303`, `drive`, `stereo-delay`.
