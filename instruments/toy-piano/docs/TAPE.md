# Toy Piano + Tape FX v0.1.0

Eigenes Testprodukt `toy-piano-tape`: Toy Piano → [Tape FX](../../../effects/tape-fx/)
(TapeVibe-Port) → Stereo-Ausgang. Das reguläre Toy Piano v0.4 und
`toy-piano-ambient` bleiben unverändert (ARM-Images nach dem Neubau bytegleich,
Golden-Test besteht). Kein Gerät wurde verbunden oder geflasht.

```bash
make PROJECT=toy-piano-tape test
make PROJECT=toy-piano-tape test-sanitize
make PROJECT=toy-piano-tape -j4
make PROJECT=toy-piano-tape manifest
python3 scripts/check-firmware.py instruments/toy-piano/build/tape/ToyPianoTape-v0.1.0.bin
python3 scripts/check-tape-elf.py instruments/toy-piano/build/tape/ToyPianoTape-v0.1.0.elf
```

## Bedienung

| Poti | Funktion | | Schalter | Funktion |
| --- | --- | --- | --- | --- |
| VR1 | Drive | | SW17 A | Tape an |
| VR2 | Tone | | SW17 B | Bypass (exakt trockenes Klavier) |
| VR3 | Age | | SW18 oben | Hiss/Dropouts Studio |
| VR4 | Mix (Dry/Wet) | | SW18 Mitte | Tape |
| VR5 | Wow | | SW18 unten | Cassette |
| VR6 | Flutter | | | |
| VR7 | Hiss | | | |
| VR8 | Dropouts | | | |

Gleiche Belegung wie die Standalone-Firmware `tape-fx` und wie die drumlogue-Seiten
von TapeVibe. Alle 16 Tasten bleiben Klaviertasten, ihre LEDs zeigen gehaltene
Tasten. Da alle Potis zum Band gehören, sind Attack 2 ms und Decay 1,5 s fest; über
die Mod-on-Daisy-Matrix bleiben beide modulierbar. Das Klavier ist mono; beide
Tape-Eingänge erhalten dasselbe Signal, Stereo entsteht nur durch Hiss und die
kanalweisen Dropout-Pegel. USB-MIDI spielt die 16 MIDI-Stimmen; Notenrückstau wird
wie im Ambient-Produkt begrenzt und nicht verworfen.

Mod on Daisy: IDENTIFY `toy-piano-tape 0.1.0`, die fünf Toy-Piano-Spalten (Pitch,
Level, Harmonic, Attack, Decay), erweiterter Monitor und LFO-Kreuzmodulation wie in
v0.4, Echoschutz, `GET_STATS` über den Callback-Diagnoseadapter:

```bash
python modules/mod-on-daisy/tools/mod_probe.py stats
```

Für Wow/Flutter-Tests: VR3 Age ganz rechts, dann VR6 Flutter (bis ±4,2 Cent bei
10–14 Hz, gut hörbar an gehaltenen Tönen) bzw. VR5 Wow (TapeVibe-Original: höchstens
±0,9 Cent bei 0,8 Hz, kaum hörbar). Drive bei VR4 Mix ganz rechts mit Akkorden
prüfen. Vollständiger Ablauf: [Hardware-Hörtest](../../../effects/tape-fx/docs/HARDWARE_TEST.md).

## Prüfungen (Host, 26.10.2026)

* Der Produktions-Callback ist 60 s lang bitgleich zur getrennt zusammengesetzten
  Kette aus Referenzklavier, `TapeFx` und `TapeMixer` (7 Tasten, Cassette, alle
  Potis außerhalb der Startwerte).
* Bypass liefert exakt das trockene Toy Piano; alle 16 Tasten und LED-Zustände.
* Wow ±5,1 Samples, Flutter und Drive (5,7 % nichtlinearer Anteil bei Drive 100 %
  an einem 16-Tasten-Cluster) über die Potis.
* SW18-Charakter, IDENTIFY, Echoschutz, Matrixziele, CPU-Statistik, MIDI-Noten,
  Bootloader S9+S16, keine Allokation im Audio-Callback. ASan/UBSan und TSan bestehen.

ARM (xPack GCC 13.2.1-1.1): 173 956-Byte-BIN, DTCM 119 584 B (statische Reserve
11 488 B, keine Stack-Hochwassermessung), SDRAM 16 708 B (Tape-Host mit 16 KiB
Delay-Lines). Firmware- und ELF-Prüfung bestehen.

**NICHT GEMESSEN am Gerät:** Klang, CPU-Last, Pegel, Schalterrichtung, Übergänge.
