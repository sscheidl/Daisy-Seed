# Toy Piano + Ambient Reverb v0.1.0

Eigenes Produkt `toy-piano-ambient`, aufbauend auf der modularen FX-Stufe in
PR #38. Das bisherige Toy Piano v0.3 und die reine Ambient-FX-Firmware bleiben
eigene Buildziele. Kein Gerät wurde verbunden oder geflasht.

| Poti | Funktion |
| --- | --- |
| VR1 | Piano-Transposition, kontinuierlich −12 bis +12 Halbtöne, Mitte = Originaltonhöhe |
| VR2 | Piano-Decay, 50–3000 ms, logarithmisch |
| VR3 | Hall-Decay, 1–45 s nominal, logarithmisch |
| VR4 | Filter, 400–14000 Hz |
| VR5 | Modulation Depth |
| VR6 | Dry/Wet Mix |
| VR7 | Modusspezifisches X |
| VR8 | Stretch, interner Wet-Takt 48 bis 12 kHz |

Der Piano-Attack ist physisch fest auf 2 ms gesetzt. Die Matrix kann ihn als
erweiterten Parameter weiterhin modulieren. Zeiten werden je Anschlag übernommen;
Transposition verändert auch klingende Stimmen. Extrem hohe MIDI-Noten oberhalb
Nyquist werden ausgeblendet. Gesendete Tastennoten bleiben untransponiert.
Alle 16 Taster bleiben Klaviertasten; LEDs folgen ausschließlich den Tasten.

| Zweistufenschalter / Dreistufenschalter | Auswahl |
| --- | --- |
| A / oben | Cloud |
| A / Mitte | Halo |
| A / unten | Scatter |
| B / oben | Abyss |
| B / Mitte | Bloom |
| B / unten | Dry, Platzhalter für den noch nicht entwickelten Shimmer-Modus |

A bedeutet `tog_sw17.Pressed() == false`, B bedeutet true. Die physische
Zuordnung ist am Board zu bestätigen. Beide Schalter werden als Kombination
15 ms stabil eingelesen. Stereo ist fest; externe Audioeingänge werden ignoriert.
Modulationsrate, Width und Pan bleiben auf festen Werten. Cloud X skaliert den
Vibratoanteil zwischen halber und voller Depth, da kein Sustain-Taster vorhanden
ist. Halo/Abyss X regelt den Oktavanteil, Scatter die Taps/Diffusion, Bloom die
Anschwellzeit. Modewechsel und Dry/Mix verwenden geglättete Übergänge.
Dry blendet Wet aus und stoppt den Send; der interne Ausklang bleibt erhalten
und kann beim schnellen Wiedereinschalten wieder hörbar werden.

## Editor und Diagnose

Das bestehende HTML-/JUCE-/VST3-Panel erkennt das Produkt als
`toy-piano-ambient 0.1.0`. Alle acht Matrixspalten sind beschrieben: Pitch, Level,
Harmonic, Attack, Decay, HallDecay, HallMix und HallX. Neue FX-Spalten sind global;
Poly AT auf Attack/Decay bleibt wegen der Anschlagübernahme gesperrt. Grundwerte
und die gewählte Schalterstellung werden über den bestehenden Monitor gemeldet.
Die Anzeige ist die gewählte FX-Konfiguration, kein Nachweis einer bereits
abgeschlossenen internen DSP-Überblendung. Kein neues Protokoll wurde eingeführt.

Der neue Callback-Diagnoseadapter ist separat versioniert. `GET_STATS` und
`RESET_STATS` nutzen den vorhandenen Vertrag:

```bash
python modules/mod-on-daisy/tools/mod_probe.py stats
```

`load_avg` ist der 1-Hz-geglättete libDaisy-Mittelwert, `load_max` die größte
Callback-Last; Werte über 100 % bleiben erhalten. `ticks` zählt Blöcke,
`overruns` Fristüberschreitungen. Die Zyklus-/Dispatch-Felder werden nicht
gemessen und bleiben null. Snapshot-Publikation erfolgt nach dem Messintervall.
Alle Meterzugriffe liegen im Audio-Thread; Main erhält zusammenhängende Snapshots
über den vorhandenen Triple-Buffer. Reset wird im nächsten Callback übernommen.
Die SysEx-Antwortschleifensperre ist hier eingeschaltet; v0.3 bleibt unverändert.

## Build und Abnahme

```bash
make PROJECT=toy-piano-ambient test
make PROJECT=toy-piano-ambient test-sanitize
make PROJECT=toy-piano-ambient -j4
make PROJECT=toy-piano-ambient manifest
python3 scripts/check-firmware.py instruments/toy-piano/build/ambient/ToyPianoAmbient-v0.1.0.bin
```

Am Host geprüft: alle 16 Tasten und LED-Zustände, exakter trockener Vergleich
mit dem ursprünglichen Piano-DSP, Transposition, Stereo-Hall nach dem Ende der
Piano-Hüllkurve, acht Editor-Ziele, MIDI-Rückstau ohne Verlust im Anwendungsqueue-
Test, begrenzte Main-/Audio-Batches und keine Allokation im Audiocallback.
Die Testuhr prüft CPU-Snapshot, Reset und Wraparound, keine reale CPU-Leistung.

ARM-Build: 183868-Byte-BIN, 119568 B DTCM (11504 B statische Reserve), 1129608 B
SDRAM. Die Reserve ist keine Stack-Hochwassermessung. Firmware- und ELF-Prüfung
bestehen. BIN-SHA-256:
`676c34f87c30c7175cb7ef3e31d37e048900c06c923c164a0a84e1431d0b2a1b`.

**NICHT GEMESSEN am Gerät:** gemeinsame CPU-Last, Stack-Hochwasser, analoge
Pegel, Schalterrichtung und hörbare Übergänge. Zuerst mit niedrigem Abhörpegel
und VR6 links das Piano prüfen, dann Mix und Stretch langsam erhöhen. Danach
alle fünf FX und den Dry-Zustand, schnelle Anschläge und MIDI-Akkorde prüfen.
CPU-Werte samt Firmwarehash und Testszenario protokollieren. Der sechste echte
Shimmer-Algorithmus und ein vollständiges FX-Panel sind noch nicht implementiert.
