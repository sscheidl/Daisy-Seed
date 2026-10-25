# Toy Piano + Ambient Reverb v0.1.2

Eigenes Produkt `toy-piano-ambient`, aufbauend auf der modularen FX-Stufe in
PR #38. Das bisherige Toy Piano v0.3 und die reine Ambient-FX-Firmware bleiben
eigene Buildziele. Kein Gerät wurde verbunden oder geflasht. v0.1.2 ist v0.1.1 plus
der gemeinsame Bootloader-Einstieg (Integrationsbranch mit PR #40); Klang und
Bedienung sind sonst unverändert.

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
| B / unten | Shimmer, aufsteigende Oktaven im Hall-Ausklang |

A bedeutet `tog_sw17.Pressed() == false`, B bedeutet true. Die physische
Zuordnung ist am Board zu bestätigen. Beide Schalter werden als Kombination
15 ms stabil eingelesen. Stereo ist fest; externe Audioeingänge werden ignoriert.
Modulationsrate, Width und Pan bleiben auf festen Werten. Cloud X skaliert den
Vibratoanteil zwischen halber und voller Depth, da kein Sustain-Taster vorhanden
ist. Halo/Abyss X regelt den Oktavanteil, Scatter die Taps/Diffusion, Bloom die
Anschwellzeit. Shimmer X regelt den Anteil der aufwärts oktavierten Einspeisung.
Der späte Hall wird ebenfalls durch den gefilterten +12-Halbtöne-Pfad geführt,
sodass weitere Oktaven entstehen. Der Rücklauf skaliert mit der verbleibenden
Feedback-Reserve; es gibt keinen unbegrenzten Regenerationsregler. Das ist ein
eigener DSP-Modus, kein Slöer-Port. Modewechsel und Mix werden geglättet.
Trocken spielen: VR6 ganz links; alle sechs Schalterstellungen sind nun FX.

## Editor und Diagnose

Das bestehende HTML-/JUCE-/VST3-Panel erkennt das Produkt als
`toy-piano-ambient 0.1.2`. Alle acht Matrixspalten sind beschrieben: Pitch, Level,
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
python3 scripts/check-firmware.py instruments/toy-piano/build/ambient/ToyPianoAmbient-v0.1.2.bin
```

Am Host geprüft: alle 16 Tasten und LED-Zustände, exakter trockener Vergleich
mit dem ursprünglichen Piano-DSP, Transposition, Stereo-Hall nach dem Ende der
Piano-Hüllkurve, acht Editor-Ziele, MIDI-Rückstau ohne Verlust im Anwendungsqueue-
Test, begrenzte Main-/Audio-Batches und keine Allokation im Audiocallback.
Die Testuhr prüft CPU-Snapshot, Reset und Wraparound, keine reale CPU-Leistung.

ARM-Build v0.1.2 (Integrationsbranch, xPack GCC 13.2.1-1.1): 184220-Byte-BIN,
119584 B DTCM (11488 B statische Reserve), 1129624 B SDRAM. Die Reserve ist keine
Stack-Hochwassermessung. Firmware- und ELF-Prüfung bestehen. BIN-SHA-256:
`0ecfb4f9f44a7a8b51fafe1a7b1ec9d81cb1dce420b7ce46cbeecfc00f9753e0`.
Frühere v0.1.1 (Commit 07d39ad, unverändert, unabhängig neu gebaut und bytegleich):
184140 Byte, 119568 B DTCM, SHA-256
`3cd446fb4b0d17ecfffdcf5b7ec5a1b0a02a7f116cdab8a9aaa3d1de5da050ed`.

## Firmware-Update ohne BOOT/RESET

Wie in allen Firmwares dieses Repositorys ([Details](../../../common/bootloader/README.md)):

* **S9 + S16** (äußere Tasten der zweiten Reihe, hier C5 und C6) allein **5 s** halten.
  Beide Tasten spielen zuerst ihre Note; eine dritte Taste oder vorzeitiges Loslassen
  verhindert den Einstieg. Eine allein 5 s gehaltene C5-C6-Oktave löst ihn also aus.
* Oder **Firmware-Update …** im HTML-/JUCE-/VST3-Panel (SysEx über J7). Die Antwort
  des Kits bestätigt nur die angenommene Anfrage, noch nicht den DFU-Eintritt.
* Danach leuchten alle 16 LEDs (erwartet, NICHT GEMESSEN), und der Bootloader wartet
  ohne Zeitlimit; erst das DFU-Gerät am Rechner bestätigt ihn. Flashen wie in
  [FLASHING.md](../../../docs/FLASHING.md). RESET + BOOT bleibt der Rettungsweg.

**NICHT GEMESSEN am Gerät:** gemeinsame CPU-Last, Stack-Hochwasser, analoge
Pegel, Schalterrichtung und hörbare Übergänge. Zuerst mit niedrigem Abhörpegel
und VR6 links das Piano prüfen, dann Mix und Stretch langsam erhöhen. Danach
alle sechs FX, schnelle Anschläge und MIDI-Akkorde prüfen.
CPU-Werte samt Firmwarehash und Testszenario protokollieren. Ein vollständiges FX-Panel ist noch nicht implementiert.

## Shimmer-Prüfung (Host, 25.10.2026)

**GEMESSEN im synthetischen Test:** 48 kHz, 375-Hz-Sinus für 1 s, danach
3 s Stille; Projektion des linken Wet-Ausgangs zwischen 1 und 3 s, X = 1,
Decay 8 s, Modulation aus. +12-Halbtöne-Amplitude 67,0576 (X = 0: 0,238547),
+24-Halbtöne-Amplitude 4,02927 (Halo: 0,841954). Das sind unnormierte
Projektionssummen, keine dBFS oder Gerätepegel. Der Test prüft erzeugte Oktaven
und die Unterscheidung von Halo, keine klangliche Gleichwertigkeit mit einem Pedal.

90-s-Ausklang mit maximalem Decay/Depth/Rate/X: endlich, abklingend und ohne
Guard-Eingriff. Sechs Modi bei vier Host-Sampleraten, Stretch-Endpunkte im
Produktion-Callback und ASan/UBSan bestehen. Ein zusätzlicher Vorher/Nachher-
Vergleich gegen `44b08ca` liefert für die fünf bisherigen Modi bitgleiche
Float-Ausgaben (je 5 s bei 48 kHz, 1 s deterministisches Rauschen und 4 s Stille).
Der gemeinsame Kern ist jetzt `ambient-reverb 0.3`; der Adaptervertrag bleibt
unverändert. Die reine FX-Firmware bietet Shimmer auf S6 mit LED D6.
