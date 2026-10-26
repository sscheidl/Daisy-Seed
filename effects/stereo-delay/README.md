# Stereo Delay

Ping-Pong (Grundeinstellung) und Stereo L/R mit Clock-Sync bis zur punktierten ganzen
Note bei 30 BPM (12 s je Leitung im SDRAM), Feedback bis 95 %, Dry/Wet und Dämpfung.
Kern nach FX-Vertrag 1.0 (wet-only), eigene Firmware `delay-fx` für die Stereo-Eingänge.

| Stand | |
| --- | --- |
| Version | Kern und Firmware 0.1.0 (`src/version.h`) |
| Software | Host-, Sanitizer- und ARM-Prüfungen bestanden (siehe CHANGELOG) |
| Hardware | NICHT GEMESSEN, [Hardwaretest](docs/HARDWARE_TEST.md) |

## Verhalten

* **Delay-Zeit** ist der Abstand zweier aufeinanderfolgender Echos. Bei Ping-Pong ist das
  der Abstand von Seitenwechsel zu Seitenwechsel, bei Stereo L/R der Abstand je Kanal.
* **Ping-Pong:** (L + R) / 2 geht in die linke Leitung, die Rückkopplung wechselt die
  Seite. Echos: links nach T, rechts nach 2T, links nach 3T usw.
* **Stereo L/R:** zwei getrennte Leitungen mit eigener Rückkopplung.
* **Zeit:** frei 10 ms – 2 s, oder Sync mit Notenwerten 1/32 bis 1/1, auch punktiert und
  triolisch, aus dem Tempo des Hosts. Bis 12 s wird nicht geklemmt. Darüber klemmt das
  Delay auf 12 s und meldet es (LED D3).
* **Zeitänderungen** gleiten mit höchstens 5 % Tonhöhenänderung. Ab 10 % Sprung wird über
  zwei Lesezeiger in 20 ms überblendet.
* **Feedback** 0–95 %, ein Soft-Clip in der Schleife hält es begrenzt. **Dämpfung:**
  Tiefpass erster Ordnung in der Rückkopplung, 20 kHz bis 1 kHz.
* **Ausschalten** (Host, `host.h`): Zuerst wird die Einspeisung über 10 ms ausgeblendet,
  der Nachhall läuft weiter. Er endet, wenn der Wet-Pegel 500 ms unter −90 dBFS liegt,
  spätestens 20 s nach dem Ausschalten mit 1 s Ausblenden. Wird während des Nachhalls
  wieder eingeschaltet, blendet die Einspeisung ein, die Leitungen bleiben.

## Bedienung (Firmware `delay-fx`)

| Element | Funktion |
| --- | --- |
| VR1 | freie Zeit 10 ms – 2 s (logarithmisch) |
| VR2 / VR3 | Feedback 0–95 % / Dry/Wet |
| VR4 | Dämpfung 1–20 kHz (logarithmisch) |
| VR5 | Tempo 30–300 BPM (ohne externe Clock) |
| VR6 | Notenwert (18 Stufen: 1/32 … 1/1, je normal, triolisch, punktiert) |
| SW17 | Delay an/aus (mit Nachhall), LED D8 |
| SW18 | oben Ping-Pong frei, Mitte Ping-Pong Sync, unten Stereo L/R Sync |
| S9 + S16 (5 s) | Bootloader |

LEDs: D1 Sync, D2 Stereo L/R, D3 geklemmt, D4 externes Tempo, D16 Callback-Frist
überschritten. **MIDI-Clock** über USB oder DIN setzt das Tempo (Mittel über 24 Clocks),
nach 2 s ohne Clock gilt wieder VR5. MIDI-CC 102–109 (omni) und SQ-Parameter über USB.
Potis und Schalter wirken, wenn sie bewegt werden.

## Bauen und testen

```bash
make PROJECT=delay-fx -j4                 # build/DelayFx-v0.1.0.bin
make PROJECT=delay-fx test                # Kern und Firmware auf Fakes
make PROJECT=delay-fx test-sanitize
make test-fx                              # gemeinsamer FX-Vertragstest (mit Stereo Delay)
python3 scripts/check-firmware.py effects/stereo-delay/build/DelayFx-v0.1.0.bin
python3 scripts/check-elf-placement.py effects/stereo-delay/build/DelayFx-v0.1.0.elf --sdram delay_memory:4608000
```

Speicher: 2 Leitungen × (12 s × 48 000 + 64 Samples Rand) als Float = 4 608 512 Byte im
SDRAM (`DSY_SDRAM_BSS`), vom Host gestellt und vor dem Audiostart gelöscht.
