# Hardware-Hörtest Tape FX 0.1.0

Gilt für zwei Images:

| Image | Build | Zweck |
| --- | --- | --- |
| `TapeFx-v0.1.0.bin` | `make PROJECT=tape-fx -j4` | Stereo-Effekt für externe Quellen (Line-Eingang) |
| `ToyPianoTape-v0.1.0.bin` | `make PROJECT=toy-piano-tape -j4` | Toy Piano → Tape FX, Testinstrument |

Flashen nur durch dich, wie in [FLASHING.md](../../../docs/FLASHING.md). Vorher prüfen:

```bash
python3 scripts/check-firmware.py effects/tape-fx/build/TapeFx-v0.1.0.bin
python3 scripts/check-tape-elf.py effects/tape-fx/build/TapeFx-v0.1.0.elf
python3 scripts/check-firmware.py instruments/toy-piano/build/tape/ToyPianoTape-v0.1.0.bin
python3 scripts/check-tape-elf.py instruments/toy-piano/build/tape/ToyPianoTape-v0.1.0.elf
```

## Bedienung (beide Images gleich)

| Poti | Funktion | Startwert TapeVibe |
| --- | --- | --- |
| VR1 | Drive | 35 % |
| VR2 | Tone (links dunkel, Mitte neutral, rechts hell) | 50 % |
| VR3 | Age (skaliert Wow-/Flutter-Rate und -Tiefe, Head Bump, Hiss, Dropout-Häufigkeit) | 40 % |
| VR4 | Mix (Dry/Wet; links trocken, rechts nur Band) | 40 % |
| VR5 | Wow | 20 % |
| VR6 | Flutter | 15 % |
| VR7 | Hiss | 20 % |
| VR8 | Dropouts | 10 % |

Die Potis werden in ganze Prozent übersetzt wie auf dem drumlogue; Werte gleiten
ohne Sprünge (15 ms).

| Schalter | Stellung | Funktion |
| --- | --- | --- |
| SW17 (2 Stufen) | A (`Pressed() == false`) | Tape an |
| SW17 | B (`Pressed() == true`) | Bypass: Mix gleitet in 10 ms auf 0, Ausgang = Eingang |
| SW18 (3 Stufen) | oben | Hiss und Dropouts: Studio |
| SW18 | Mitte | Tape (TapeVibe-Standard) |
| SW18 | unten | Cassette |

Welche physische Stellung A/B ist, muss am Board bestätigt werden (NICHT GEMESSEN).

### Nur `tape-fx`: Tasten und LEDs

| Taste | Funktion | LED |
| --- | --- | --- |
| S1 | Drive-Kurve TapeVibe (Original) | leuchtet bei Auswahl |
| S2 | Drive-Kurve MixForge TAPE (Bias + Pre-/De-Emphasis) | ″ |
| S3 | Drive-Kurve MixForge HARD | ″ |
| S4 | Drive-Kurve MixForge FOLD | ″ |
| S5 | MixForge-Kompressor ein/aus (−18 dB, 4:1, 10/300 ms) | an = aktiv |
| S6 | Bitcrusher ein/aus (8 Bit, Haltezeit 3 Samples = 16 kHz) | an = aktiv |
| – | – | S8: Tape aktiv (aus bei Bypass) |
| – | – | S16: mindestens ein Callback-Überlauf seit dem Start |

Eingang: Stereo an IN L/R. Steckt rechts nichts, wird links auf beide Kanäle
gelegt. Blockgröße 48 Samples bei 48 kHz.

### Nur `toy-piano-tape`

Alle 16 Tasten spielen wie im Toy Piano, ihre LEDs zeigen gehaltene Tasten. USB-MIDI
an J7 spielt die MIDI-Stimmen, Mod on Daisy (Editor/VST3) meldet sich als
`toy-piano-tape 0.1.0` mit den fünf Toy-Piano-Spalten. Weil alle Potis zum Band
gehören, sind Attack (2 ms) und Decay (1,5 s) fest; die Matrix kann beide weiterhin
modulieren. Externe Eingänge werden ignoriert. CPU-Werte:

```bash
python modules/mod-on-daisy/tools/mod_probe.py stats
```

## Bootloader ohne BOOT/RESET

**S9 + S16** allein 5 s halten (im Toy Piano spielen beide zuerst ihre Note) oder
**Firmware-Update …** im Panel. Danach wartet der Bootloader ohne Zeitlimit.

## Ablauf

Mit niedrigem Abhörpegel beginnen. Ergebnisse mit Datum als GEMESSEN eintragen,
nicht Geprüftes bleibt NICHT GEMESSEN.

| # | Schritt | Erwartung (aus Hosttests, kein Messergebnis) | Ergebnis |
| --- | --- | --- | --- |
| 1 | Einschalten, SW17 Bypass | Signal unverändert (Toy Piano: trockenes Klavier) | NICHT GEMESSEN |
| 2 | SW17 Tape an, alle Potis auf Startwert (s. Tabelle) | leichte Färbung, leises Rauschen, seltene kurze Aussetzer | NICHT GEMESSEN |
| 3 | VR4 Mix ganz rechts, VR1 Drive 0 → 100 % | zunehmende Sättigung, Pegel ungefähr gleich (−3 bis +4 dB am Sinus) | NICHT GEMESSEN |
| 4 | VR3 Age 100 %, VR6 Flutter 100 %, VR5 Wow 0 | Flatterndes Tonhöhenzittern 10–14 Hz, bis ±4 Cent; im Toy Piano an gehaltenen Tönen am deutlichsten | NICHT GEMESSEN |
| 5 | VR5 Wow 100 %, VR6 Flutter 0 | sehr langsames Schwanken 0,8 Hz, höchstens ±0,9 Cent: **kaum hörbar ist erwartet** (TapeVibe-Original) | NICHT GEMESSEN |
| 6 | VR2 Tone 0 / 50 / 100 % | dunkel / neutral / heller | NICHT GEMESSEN |
| 7 | VR7 Hiss 100 %, SW18 oben/Mitte/unten | Rauschen leise → lauter (Host: −61,5 / −52,1 / −46,8 dBFS bei Age 100 %) | NICHT GEMESSEN |
| 8 | VR8 Dropouts 100 %, SW18 oben/Mitte/unten | kurze leichte → längere tiefe Aussetzer; Cassette teils einseitig | NICHT GEMESSEN |
| 9 | Alle Potis schnell drehen | keine Klicks oder Knackser, keine Aussetzer | NICHT GEMESSEN |
| 10 | SW17 während Spiel umschalten | weicher Übergang in 10 ms, kein Knacks | NICHT GEMESSEN |
| 11 | nur `tape-fx`: S1–S4, S5, S6 | Kurven hörbar verschieden, FOLD stark verzerrend; Kompressor glättet Pegel; Bitcrusher deutlich körnig | NICHT GEMESSEN |
| 12 | nur `tape-fx`: Mono-Quelle nur links | gleiches Signal auf beiden Ausgängen (Hiss/Dropouts dürfen L/R unterscheiden) | NICHT GEMESSEN |
| 13 | nur `toy-piano-tape`: alle 16 Tasten, schnelle Akkorde, MIDI-Akkorde | alle Töne, LEDs folgen den Tasten | NICHT GEMESSEN |
| 14 | CPU: `tape-fx` LED S16 nach 5 min Extremstellung (alles 100 %, S2, S5, S6); `toy-piano-tape` `mod_probe.py stats` | S16 bleibt aus; `load_max` deutlich unter 100 % | NICHT GEMESSEN |
| 15 | Bootloader S9+S16 5 s | alle 16 LEDs an, DFU-Gerät erscheint | NICHT GEMESSEN |

Bitte zusätzlich festhalten: Firmwarehash, Abhörkette, Quelle/Pegel und bei
CPU-Werten das Testszenario.
