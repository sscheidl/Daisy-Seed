# Hardwaremessung Toy Piano + Ambient: CPU-Überlast — 27./28.10.2026

**CPU-ÜBERLAST GEMESSEN (27./28.10.2026) — URSACHE: AMBIENT-REVERB-KERN BEI VOLLER WET-RATE.**

Der Mensch hat alle Images geflasht und gespielt. Auf seinen Wunsch hat der Agent das
Kit über USB-MIDI (J7) angesteuert und die Callback-Statistik der Firmware gelesen
(`GET_STATS`/`RESET_STATS`, siehe [AMBIENT.md](AMBIENT.md)). Der Agent hat nicht geflasht.

## Symptom (vom Menschen berichtet)

Mit VR8 (Stretch) ganz links, also voller Wet-Rate von 48 kHz, klingt das Toy Piano
leicht verzerrt, mit digitalen Artefakten wie von einem Bitcrusher. Mit niedrigerer
Wet-Rate tritt das nicht auf. Schnelle Akkorde verstärken es.

## Werkzeuge

- [`tools/toy_ambient_probe.py`](../tools/toy_ambient_probe.py): spielt Noten über
  USB-MIDI (ruhig: 4 Noten alle 2 s; Akkorde: 8 Noten alle 0,5 s; dicht: 8 Noten alle
  0,1 s, je 15 s) und liest danach die Statistik.
- [`tools/live_load.py`](../tools/live_load.py): Last einmal pro Sekunde, ohne Noten.
- Rohdaten: [`data/2026-10-27/`](data/2026-10-27/).

VR8 ist ein Hardware-Regler und lässt sich nicht per MIDI setzen. Last = Durchschnitt
und Spitze in Prozent der 1-ms-Frist (48 Samples); „zu spät“ = Blöcke über der Frist.

## Referenz-Image (GEMESSEN 27.10.2026)

Toy Piano Ambient v0.1.2 von `main`, SHA256 `0ecfb4f9f44a7a8b51fafe1a7b1ec9d81cb1dce420b7ce46cbeecfc00f9753e0`.

| Situation (VR8 links) | Ø | Spitze | zu spät |
| --- | ---: | ---: | ---: |
| seit dem Einschalten (~4,5 min, gemischt) | 93,39 % | 108,64 % | 9502 / 271 321 |
| ruhig | 92,53 % | 115,89 % | 1843 / 16 208 (11 %) |
| Akkorde | 92,87 % | 117,22 % | 10 226 / 16 018 (64 %) |
| dicht | — | — | abgebrochen: Windows lehnte das Senden über USB-MIDI ab |
| Leerlauf ohne Noten, 10 s | 93,34 % | 108,69 % | 391 / 10 009 (4 %) |

Zwei weitere Läufe „VR8 rechts“ zeigten dieselben Werte wie VR8 links (92,6 % Ø
ruhig; 95,5 % Ø Akkorde), einer davon ohne geöffneten Editor. Der anschließende
Live-Test zeigt, dass VR8 sehr wohl wirkt; bei diesen Läufen stand VR8 daher offenbar
nicht ganz rechts. Sie sind nicht verwertbar. Der Mod-on-Daisy-Editor ist nicht die
Ursache der Last.

### Live-Test: VR8 während der Messung gedreht (ohne Noten)

| VR8 | Ø | Spitze | zu spät pro Sekunde |
| --- | ---: | ---: | ---: |
| ganz links (48 kHz) | 92–96 % | 102–109 % | 12–92 von ~1000 |
| ganz rechts (12 kHz) | 34,5–35,5 % | 39–40 % | 0 |

Beim Drehen folgt die Last stetig dem Regler, in beide Richtungen.

**Abgeleitet (Schätzung, NICHT GEMESSEN):** Der Reverb-Kern läuft mit Wet-Rate, alles
andere mit 48 kHz. Aus (93 − 35) / 0,75 ergeben sich rund 77 Prozentpunkte für den
Kern bei voller Rate, also etwa 7400 Zyklen pro Kernaufruf. Der feste Rest
(FIR, Piano, Steuerung) liegt bei rund 16 %. Das Toy Piano selbst ist leicht.

### Host-Simulation (GEMESSEN am Host, 27.10.2026)

Toy Piano + Ambient mit Akkorden, Stretch 0 gegen 1: Der Reverb braucht bei voller Rate
etwa 2,4-mal so viel Rechenzeit. Ausgangsspitzen 0,1–0,28; der Ausgangsbegrenzer (0,9)
greift nie. Pegel oder Begrenzer sind also nicht die Ursache der Artefakte.

## Speicher-Experimente

Branch `claude/ambient-reverb-memory-experiments`. Drei Build-Schalter, im Standard
aus; das Standard-Image ist byte-identisch zur Referenz (`0ecfb4f9…`):

| Schalter | Wirkung |
| --- | --- |
| `AMB_WA=1` | SDRAM nach `hw.Init()` als write-back **mit** write-allocate |
| `AMB_COMPACT=1` | Delay-Puffer für 48 kHz bemessen; Allpass- und Pitch-Puffer (224 KB) im AXI-SRAM hinter dem Image (`ld/toy_piano_axi.lds`) |
| `AMB_FIR=1` | Anti-Alias-FIR ohne Ringindex, gleiche Summationsreihenfolge |

```sh
make -C instruments/toy-piano AMBIENT=1 AMB_WA=1 AMB_COMPACT=1 AMB_FIR=1 -j4
```

**GEMESSEN (Host, 27.10.2026):** `tests/test_ambient_experiments.cpp` rendert alle sechs
Modi, Stretch-Endpunkte und -Fahrten, dichte Akkorde und maximale Modulationstiefe:
2 304 000 Samples, 0 Abweichungen, für jeden Schalter einzeln, alle zusammen und auch
mit FMA-Kontraktion. Der Test läuft in `make AMBIENT=1 test`.

| Image | SHA256 (Anfang) |
| --- | --- |
| nur `AMB_WA` | `b7c3c61b…` |
| nur `AMB_COMPACT` | `dccd430b…` |
| nur `AMB_FIR` | `d9d2c3fd…` |
| alle drei | `f8a21bec…` |

### Ergebnis „alle drei“ (GEMESSEN 28.10.2026, VR8 links)

| Situation | Referenz | alle drei |
| --- | --- | --- |
| Leerlauf | 93 % Ø, 105 % Spitze, ~4 % zu spät | 85,5–87 % Ø, 94–96,7 % Spitze, 0 zu spät |
| ruhig | 92,53 % Ø, 1843 zu spät (11 %) | 90,30 % Ø, 103,68 % Spitze, 173 / 16 208 (1 %) |
| Akkorde | 92,87 % Ø, 64 % zu spät | 100,07 % Ø, 112,36 % Spitze, 11 712 / 19 395 (60 %) |
| dicht | — | abgebrochen (126 MIDI-Sendefehler, dann SysEx-Sendefehler) |

Der Mensch berichtet: etwas besser, aber nicht verschwunden; schnelle Akkorde
klingen schlechter als vorher. Kurz nach diesem Flash fand das Messskript noch
keinen Daisy-MIDI-Port; das Kit spielte dabei normal, kurz darauf war der Port da.

**Bewertung:** Speicher-Tuning allein bringt nur rund 7 Punkte. Vermutung, NICHT
GEMESSEN: write-allocate verschlechtert die Akkordphase, weil jeder Schreibzugriff
auf eine neue Delay-Zeile erst eine Cache-Zeile aus dem SDRAM lädt. Die einzelnen
Images (nur WA, nur COMPACT, nur FIR) wurden nicht gemessen.

## Nächste Schritte (offen)

1. `AMB_COMPACT=1 AMB_FIR=1` **ohne** `AMB_WA` messen, um die WA-Vermutung zu prüfen.
2. Algorithmus sparsamer machen, Entscheidung des Menschen ausstehend:
   - **Ungenutzte Pitchshifter abschalten:** Cloud, Bloom und Scatter brauchen keinen,
     Abyss nur den unteren, Halo und Shimmer nur den oberen. Rund 20 % der
     Kern-Instruktionen; im eingeschwungenen Modus bitgleich, beim Wechsel in einen
     Pitch-Modus startet der Shifter mit leerem Puffer (Einblenden über ~85 ms).
   - **Wet-Takt auf höchstens 32 kHz begrenzen** (Takt eines echten FV-1), Stretch dann
     32–12 kHz: Kernkosten ×2/3. Bewusste Klangänderung: weniger Höhen im Hall
     oberhalb von etwa 14 kHz bei maximaler Rate.
   - Geschätzt zusammen 50–60 % Last bei voller Rate. NICHT GEMESSEN.
3. Danach Hörtest und erneute Messung mit `tools/toy_ambient_probe.py`.

## Nicht gemessen

- Einzel-Images `AMB_WA`, `AMB_COMPACT`, `AMB_FIR`.
- Dichte Akkorde (Abbruch durch USB-Sendefehler unter Last).
- Abschnittsweise Zyklen im Reverb-Kern (Profiling-Build).
- Standalone-Ambient-Reverb und Physical Modeling + Ambient.
