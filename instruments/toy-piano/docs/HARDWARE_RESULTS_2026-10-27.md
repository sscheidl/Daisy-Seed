# Hardwaremessung Toy Piano + Ambient: CPU-Überlast — 27./28.10.2026

**Historischer Bericht:** Seit 01.11.2026 werden beide damaligen Entwicklungsstände
in PR #68 zusammengeführt. Kern 0.4.0 / Adapter 0.2.0 verwenden Gate und 32–12 kHz
zentral im FX; die alten `AMB_GATE`-/`AMB_WET32`-Schalter entfallen. Die folgenden
Messungen und Prüfsummen gelten weiterhin ausschließlich für die damaligen Images.
FX-zuerst-Abnahme und aktueller Stand: [FX-Performance](../../../effects/reverb/docs/PERFORMANCE.md).

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

## Algorithmus-Experimente (28.10.2026)

Zwei weitere Build-Schalter, im Standard ebenfalls aus:

| Schalter | Wirkung |
| --- | --- |
| `AMB_GATE=1` | Rechnet nur die Oktav-Shifter, die der Modus hört: Cloud, Bloom und Scatter keinen, Abyss den unteren, Halo und Shimmer den oberen. Wird ein Shifter wieder hörbar, startet er wie nach `Prepare` mit leerem Puffer, ohne den Puffer im Audio-Thread zu löschen. |
| `AMB_WET32=1` | Wet-Takt höchstens 32 kHz. Der Kern wird mit 32 kHz vorbereitet, Hallzeiten und Delays bleiben also richtig; VR8 regelt 32–12 kHz. **Bewusste Klangänderung.** |

```sh
make -C instruments/toy-piano AMBIENT=1 AMB_GATE=1 AMB_WET32=1 -j4
```

**GEMESSEN (Host, 28.10.2026):**

- `effects/reverb/tests/test_variants.cpp` rendert alle sechs Modi ab `Prepare` ohne
  Moduswechsel (Stretch-Endpunkte und -Fahrten, Rauschstöße): mit `AMB_GATE`
  1 728 000 Samples, 0 Abweichungen. Gegenprobe: ein absichtlich falsch abgeschalteter
  unterer Shifter liefert 277 342 Abweichungen, ein zu kurzer Füllzähler 835 293.
- Moduswechsel durch alle Shifter-Kombinationen bleiben endlich und unter 1, ohne
  Guard-Eingriff. Beim Wechsel in einen Pitch-Modus ist die Ausgabe nicht bitgleich
  (der Shifter startet leer).
- Mit `AMB_WET32`: 32 000 Kernaufrufe pro Host-Sekunde bei Stretch 0, 12 000 bei
  Stretch 1.
- `make AMBIENT=1 test` mit und ohne die Schalter und `test-sanitize` mit
  `AMB_GATE=1 AMB_WET32=1`: PASS.

**GEMESSEN (ARM-Build, 28.10.2026, xPack GCC 13.2.1-1.1):** Das Standard-Image ist
weiterhin byte-identisch (`0ecfb4f9…`), ebenso `AmbientReverb-v0.3.1.bin`
(`3a19183d…`). Alle Images bestehen `scripts/check-firmware.py`.

| Image (Build-Verzeichnis `build/ambient-exp-…`) | Bytes | SHA256 |
| --- | ---: | --- |
| `w0-c0-f0-g1-r0` (nur Gate) | 185148 | `bf1bd425cd19126416912cb287c19e5307be6912612df27a1084bb5ab12fc494` |
| `w0-c0-f0-g1-r1` (Gate + 32 kHz) | 185212 | `c9113184646b0de89f81fc2ee104c6b568725843bdf0054a53afb5ebc890d8bb` |
| `w0-c1-f1-g1-r1` (Gate + 32 kHz + COMPACT + FIR, ohne WA) | 185236 | `43463379d0a8ecb5fd285fef17dc1ad63863e8daf11827f0288df50f15659571` |

Alle drei melden sich als `toy-piano-ambient 0.1.2`; das Image ist nur über den Hash
zu unterscheiden.

**Schätzung, NICHT GEMESSEN:** Aus dem linearen Lastmodell (16 % fest + 77 % ×
Wet-Rate / 48 kHz) und rund 20 % Kernanteil der Shifter ergeben sich bei VR8 links
etwa 57 % Ø und 74 % Spitze bei Akkorden für Gate + 32 kHz. Der Host-Benchmark
(x86, nicht repräsentativ) zeigt in Cloud rund −19 % für das Gate und −31 % für
beides zusammen.

## Nächste Schritte (offen)

1. Am Kit, VR8 ganz links, mit `tools/toy_ambient_probe.py` (ruhig, Akkorde) messen,
   in dieser Reihenfolge: nur Gate, Gate + 32 kHz, Gate + 32 kHz + COMPACT + FIR.
   Modus Cloud und Shimmer.
2. Hörtest: Artefakte bei VR8 links weg? Klingt 32 kHz im Hall dunkler? Knackt der
   Wechsel in Abyss, Halo oder Shimmer?
3. Danach entscheidet der Mensch, welche Schalter Standard werden; das wird dann eine
   neue Version mit Changelog-Eintrag.

## Nicht gemessen

- Einzel-Images `AMB_WA`, `AMB_COMPACT`, `AMB_FIR`.
- Dichte Akkorde (Abbruch durch USB-Sendefehler unter Last).
- Abschnittsweise Zyklen im Reverb-Kern (Profiling-Build).
- Standalone-Ambient-Reverb und Physical Modeling + Ambient.
- Alle Images mit `AMB_GATE` oder `AMB_WET32` am Kit (Last, Hörtest, Moduswechsel).
