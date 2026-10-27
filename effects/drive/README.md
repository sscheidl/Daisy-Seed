# Drive

Sättigung mit den Kurven Soft, Hard, Tape und Fold aus Tape FX (gemeinsam in
[`common/dsp/saturation.h`](../../common/dsp/saturation.h), Herkunft MixForge), Tone und
automatischer Pegelanpassung um den Drive. Kern nach FX-Vertrag 1.0 (wet-only), eigene
Firmware `drive-fx` für die Stereo-Line-Eingänge.

| Stand | |
| --- | --- |
| Version | Kern und Firmware 0.1.0 (`src/version.h`) |
| Software | Host-, Sanitizer- und ARM-Prüfungen bestanden (siehe CHANGELOG) |
| Hardware | NICHT GEMESSEN, [Hardwaretest](docs/HARDWARE_TEST.md) |

## Bedienung (Firmware `drive-fx`)

| Element | Funktion |
| --- | --- |
| VR1 | Drive 0–100 % (bis +30 dB in die Kurve) |
| VR2 | Tone: Tiefpass 1–20 kHz |
| VR3 | Level −24 … +12 dB |
| VR4 | Ausgleich 0–100 % (Auto-Level) |
| VR5 | Zielversatz −3 … 0 dB |
| S1–S4 | Kurve Soft, Hard, Tape, Fold (LED D1–D4) |
| S5 | Auto-Level an/aus (LED D5) |
| SW17 | Drive an/aus, 10 ms überblendet (LED D8). Aus = exakt das trockene Signal |
| S9 + S16 (5 s) | Bootloader |

Eingang: Stereo. Ein Mono-Signal im linken Eingang speist beide Kanäle. MIDI-CC 20–27
(omni, USB und DIN) und SQ-Parameter über USB nach dem
[Parametervertrag](../../sequencers/step-seq/docs/PARAMETERS.md). Potis und SW17 wirken,
wenn sie bewegt werden. Bis dahin gilt der zuletzt per CC oder Panel gesetzte Wert.
D16 zeigt eine Überschreitung der Callback-Frist an.

## Auto-Level

Gemessen nur um den Drive, zwischen Eingang und Ausgang von Kurve und Tone, vor Level.
Delay, Dry/Wet und Limiter liegen außerhalb.

1. **Feste Kompensation** je Kurve über den Pegel in die Kurve, also Eingangs-RMS des
   letzten 300-ms-Fensters plus Drive-Verstärkung. Die Tabelle
   [`include/drive/autolevel_table.h`](include/drive/autolevel_table.h) entsteht aus den
   Referenz-Fixtures von Phase 0 (`make -C effects/drive autolevel-table`, Median über die
   Fixtures).
2. **Langsame Nachregelung** aus 300-ms-RMS-Fenstern, begrenzt auf ±3 dB, gehalten unter
   −60 dBFS.
3. **Zielpegel** = Eingangs-RMS + Zielversatz. Der Ausgleich skaliert die Korrektur linear
   in dB (0 % = keine).

Abnahme im Host-Test: Ausgangs-RMS innerhalb ±1 dB um den Zielpegel für alle Kurven,
Drive 0–100 %, Zielversatz 0 und −1,5 dB, mit den Phase-0-Fixtures und der 303-Fixture
(größte Abweichung 0,41 dB). **Grenze:** Fold auf Rechtecksignalen. Die flachen Dächer
falten auf einen nahezu konstanten Wert, das Ergebnis wird bis zu 17 dB leiser, aber nie
lauter als das Ziel.

Kein Oversampling (D5). Aliasing von Hard und Fold wird im Hörtest bewertet.

## Bauen und testen

```bash
make PROJECT=drive-fx -j4                 # build/DriveFx-v0.1.0.bin
make PROJECT=drive-fx test                # Kern und Firmware auf Fakes
make PROJECT=drive-fx test-sanitize
make test-fx                              # gemeinsamer FX-Vertragstest (mit Drive)
python3 scripts/check-firmware.py effects/drive/build/DriveFx-v0.1.0.bin
python3 scripts/check-elf-placement.py effects/drive/build/DriveFx-v0.1.0.elf
```

`include/drive/drive.h` enthält den Kern, `host.h` die 10-ms-Überblendung des Hosts und
`params.h` die Parameter. Im Audiopfad gibt es keine libm-Aufrufe
(`common/dsp/fastmath.h`).
