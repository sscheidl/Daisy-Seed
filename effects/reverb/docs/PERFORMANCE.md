# Ambient FX 0.4.0 — gemeinsamer Performance-Stand

Stand: 01.11.2026. **Software geprüft; CPU-Last und Klang am Gerät NICHT GEMESSEN.**
Die Entwicklung erfolgt zuerst am eigenständigen FX. Die anschließende Abnahme
von Toy Piano + Ambient und Rings/Torus + Ambient ist ein eigener Schritt.

## Zusammengeführte Entwicklung

Die Überlastmessungen und Speicherexperimente lagen auf
`claude/ambient-reverb-memory-experiments` (PR #68, `3f47547`). Ein weiterer Stand
auf `ccr-23103956-s5bxpy` (`d5c8c81`, 28.10.) enthielt Gate und 32-kHz-Limit bereits
als abschaltbare Experimente. Beide Historien sind in diesem Arbeitsstand mit dem
aktuellen `main` zusammengeführt; die historischen Messberichte bleiben erhalten.

Verbindlich sind die Komponenten in `effects/reverb/src/`: Kern **0.4.0**,
Wet-only-Adapter **0.2.0**. Standalone-FX, Toy Piano und Physical Modeling binden
diese Quellen ein. Es gibt keine eigenen Wet-Takt- oder Gate-Kopien im Instrument.
Die bisherigen Schalter `AMB_GATE`, `AMB_WET32`, `AMBIENT_PITCH_GATE` und
`AMBIENT_WET_MAX_HZ` werden mit einer Fehlermeldung abgelehnt. Eine alte
Experiment-Kommandozeile darf nicht unbemerkt ein anders gemeintes Image bauen.

Weitere FX-Arbeit beginnt auf aktuellem `main`; vor Änderungen in einem älteren
Feature-Branch wird dieser Stand übernommen. Alte Branches dokumentieren frühere
Stände und sind keine unabhängigen FX-Versionen. Die beiden Ambient-Arbeitsbranches
werden über denselben PR-Stand konsolidiert; keine parallele zweite Implementierung.

## Umsetzung und beabsichtigte Klangänderung

* Codec und Dry-Pfad bleiben bei 48 kHz. `WetPath` begrenzt den tatsächlichen
  Wet-Takt zentral auf **32–12 kHz**, mit exponentiellem Stretch-Regler.
* Der Kern wird mit **32 kHz** vorbereitet. Delay-Längen, Filter, LFOs und nominale
  Zeiten passen damit zur höchsten Wet-Rate; der Deckel verlangsamt keinen mit
  48 kHz vorbereiteten Kern. Stretch verlängert diese Zeiten um bis zu 32/12.
* Die 64-Tap-Anti-Alias-Filterbank folgt dem tatsächlichen Wet-/Codec-Verhältnis.
  Filterentwurf erfolgt vor Audiostart; Nachführung erfolgt am Kontrolltakt.
* Cloud, Bloom und Scatter rechnen keinen Pitchshifter, Abyss nur den unteren,
  Halo und Shimmer nur den oberen. Während eines Modus-Crossfades bleiben alle
  noch hörbaren Shifter aktiv. Wiederholte Parameterupdates starten sie nicht neu.
* Ein neu benötigter Shifter verwirft seine Historie logisch, setzt die kleinen
  Filterzustände zurück und blendet über ein Fenster ein. Kein großer Puffer wird
  im Audio-Callback gelöscht. Bei 32 kHz Wet-Takt dauert das Fenster etwa 85 ms,
  bei 12 kHz etwa 228 ms. Tank und globaler Hall-Ausklang werden nicht gelöscht.

Gegenüber 0.3.1 ändern sich Wet-Bandbreite, Stretch-Zuordnung und der Eintritt in
Pitch-Modi bewusst. Eine Klanggleichheit des gesamten FX wird nicht behauptet.
Die 32-kHz-Grenze ist unsere Entscheidung, keine verifizierte Slöer-Spezifikation.
Die festen Toy-Piano- und Torus-Goldens werden nicht angepasst.

Die Speicherexperimente `AMB_WA`, `AMB_COMPACT` und `AMB_FIR` bleiben optionale,
standardmäßig ausgeschaltete Diagnosevarianten. Sie sind keine Voraussetzung für
die neue FX-Policy und nicht als zusätzliche Verbesserung am Gerät belegt.

## Softwareprüfung

**GEMESSEN, Windows/Clang 20.1.7, 01.11.2026:**

* FX-Hostsuite, ASan/UBSan: sechs Modi, fünf LFO-Formen, vier Kernraten,
  Shimmer-Spektrum, 90-s-Tails, Hold/Sustain, Bypass, Mono und null Audioallokationen.
* Tatsächliche Kernaufrufe an den Stretch-Endpunkten und in der Mitte, an fünf
  Codec-Raten (32/32,768/44,1/48/96 kHz); Dry-Signal unverändert.
* Gate-Zähler pro Modus, auslaufende Shifter im Crossfade, wiederholte
  Parameterupdates, Neustart ohne Pufferlöschung und ohne alte Audiodaten.
* Wet-only-Adapter verwendet dieselbe Takt- und Gate-Policy. Der übernommene
  Moduswechseltest durchläuft alle Shifter-Kombinationen ohne Guard-Eingriff.
* Gemeinsame FX-Vertragstests sowie Toy-Piano-/Physical-Modeling-Verbrauchertests
  bestehen einschließlich ASan/UBSan. COMPACT/FIR stimmt im Toy-Piano-Vergleich
  über 2.304.000 Samples exakt mit dem Standard-Speicherpfad überein.

```sh
make -C effects/reverb test HOST_CXX=clang++
make -C effects/reverb test-sanitize HOST_CXX=clang++
make -C tests/fx test-sanitize CXX="clang++ -D_USE_MATH_DEFINES -ffp-contract=off"
make -C instruments/toy-piano AMBIENT=1 test HOST_CXX=clang++
make -C instruments/toy-piano AMBIENT=1 test-sanitize HOST_CXX=clang++
make -C instruments/physical-modeling MOD=1 COMPOSED=1 FX=ambient test HOST_CXX=clang++ HOST_PYTHON=python
make -C instruments/physical-modeling MOD=1 COMPOSED=1 FX=ambient test-sanitize HOST_CXX=clang++ HOST_PYTHON=python
python -m unittest discover -s tests -p test_components.py -v
```

`-ffp-contract=off` ist für den samplegenauen Torus-Referenztest unter diesem
Windows/Clang nötig; ohne diesen Schalter unterscheiden sich die beiden
Torus-Ausdruckskontexte durch FMA-Kontraktion. Die Firmware-Flags bleiben unverändert.
Linux/GCC-Goldens laufen zusätzlich in den vorhandenen gezielten Quick-Checks.

**GEMESSEN, ARM-Build, 01.11.2026:** xPack GCC 13.2.1-1.1 baut
`AmbientReverb-v0.4.0.bin`, 146.788 Bytes, SHA-256
`ff7e3ffcf0418a82b485153248283738a0dea4d43370a98418ed3723ca582758`.
Firmwaremarker, BOOT_SRAM-Vektoren und generische ELF-Platzierungsprüfung bestehen.
SDRAM: 1.114.112 Bytes; DTCM: 120.648 Bytes, statische Reserve 10.424 Bytes
(mindestens 8 KiB verlangt). Das ist keine Stack-Hochwassermessung.

```sh
make -C effects/reverb -j4
python scripts/check-firmware.py effects/reverb/build/AmbientReverb-v0.4.0.bin
python scripts/check-elf-placement.py effects/reverb/build/AmbientReverb-v0.4.0.elf --sdram memory:1114112
```

## Abnahmefolge am Gerät

1. Zuerst `reverb-fx` bauen und das Standalone-Image prüfen. Der Mensch übernimmt
   Flash und Hörtest. Die 48-kHz-Codec-Rate und 32 Frames pro Callback bleiben.
2. Alle sechs Modi, beide Stretch-Endpunkte und langsame/schnelle Fahrten prüfen;
   danach lange Tails, Sustain, Bypass und Wechsel zwischen Pitch-/Nicht-Pitch-Modi.
   `maximumCycles`, `overruns` und `budgetCycles` im Debugger protokollieren.
3. Erst nach FX-Abnahme Toy Piano + Ambient und Rings/Torus + Ambient einzeln
   versionieren, bauen, messen und hören. Bestehende Verbraucher-Hosttests sind
   Regressionstests und ersetzen diese Produktabnahme nicht.

Kein Flash, kein Bootloaderwechsel und kein Release ist Bestandteil der
Softwareprüfung. CPU-Gewinn, hörbare Übergänge, analoge Pegel und Stack-Hochwasser
dieser neuen Fassung bleiben **NICHT GEMESSEN**.
