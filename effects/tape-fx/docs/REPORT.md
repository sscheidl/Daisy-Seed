# Tape FX 0.1.0 – Technischer Bericht

Portierung der drumlogue-Master-Effekte **TapeVibe** (Kern) und **MixForge**
(optionale Stufen) aus `sscheidl/drumlogue_fx_units` @ `0db9c37` auf das Seed3
Desktop Kit. Stand 26.10.2026. Alle Zahlen in diesem Bericht stammen aus Hosttests
oder ARM-Builds; **am Seed3 wurde nichts gemessen** (Kennzeichnung NICHT GEMESSEN).

## 1. Architektur

| Datei | Inhalt |
| --- | --- |
| `src/tape_fx.h` | `seed::fx::TapeFx`: Stereo-DSP, nur Wet, unter `common/audio/fx_contract.h` (Prepare / SetParameters / Process / Reset) |
| `src/tape_mixer.h` | `seed::fx::TapeMixer`: Host-Seite – TapeVibe-Dry/Wet, Bypass-Gleitweg, Ausgangsschutz |
| `src/tape_panel.h` | `seed::fx::TapePanel`: Testbedienung (8 Potis, 2 Schalter), boardunabhängig |
| `src/version.h` | Komponentenversion `TAPE_FX_VERSION` und Firmwareversion `TAPE_FX_FIRMWARE_VERSION` |
| `src/main.cpp` | Standalone-Firmware `tape-fx` |
| `instruments/toy-piano/src/tape_host.h` | Toy Piano → Tape FX für `toy-piano-tape` |

`TapeFx` kennt weder Korg-SDK noch libDaisy, Bedienung oder Instrument. Der Host
stellt den Speicher (`TapeFx::Storage`, 16 384 Byte) bereit und entscheidet damit
über die RAM-Region. Dry/Wet, Bypass und Ausgangsschutz liegen gemäß FX-Vertrag beim
Host; `TapeMixer` ist dafür die wiederverwendbare Umsetzung (später auch für Stardust).

Signalweg pro Kanal (Reihenfolge wie TapeVibe, MixForge-Stufen nur wenn aktiviert):

```text
[Kompressor (MixForge, stereo-gekoppelt)] → Head Bump → Drive/Sättigung mit
dynamischer Bandkompression [Kurve: TapeVibe | MixForge TAPE/HARD/FOLD] →
Wow/Flutter (gemeinsame Modulation L/R) → Dropouts → Tone/Age → Hiss →
Post-Gain → [Bitcrusher (MixForge)] → Wet
Host: dry + (wet − dry) · mix, ±1,2 begrenzt (TapeVibe) → weiches Knie > 0,95
```

Keine Verdopplung: MixForges SOFT-Kurve ist mathematisch die TapeVibe-Kurve und wird
nicht ein zweites Mal angeboten. Die anderen MixForge-Kurven **ersetzen** die Kurve
in der TapeVibe-Drive-Stufe (gleiche Drive-Verstärkung und Bandkompression), es
gibt also nie zwei Sättiger in Serie. Der MixForge-Kompressor ist eine getrennte,
standardmäßig ausgeschaltete Stufe vor dem Band (MixForge-Reihenfolge Kompressor →
Sättigung); TapeVibes eingebaute Bandkompression bleibt unverändert.

## 2. Klangtreue (Hosttest 9)

Die Originalheader liegen unverändert in `tests/reference/` (SHA-256 in
`UPSTREAM.md`). Gemessen am 26.10.2026 (`make PROJECT=tape-fx test`, g++ 13, x86-64):

* **Referenzpfad bitgleich zu TapeVibe**: 7 Parametersätze (Init-Werte, alle
  Extremwerte, alle Hiss-/Dropout-Charaktere, Mix 0/40/65/100 %) × 20 s × 2 Kanäle,
  48er-Blöcke, deterministisches Stereo-Programm. Jede Abweichung bricht den Test ab.
* **Optimierter Pfad (Firmware)**: größte Abweichung 7,75·10⁻⁷ (−115,7 dB unter dem
  Ausgangs-RMS). Ursache: Polynom statt `sinf()`, siehe Abschnitt 5.
* **MixForge-Stufen bitgleich**: Kurven SOFT/HARD/TAPE/FOLD über 40 001 Stützstellen,
  Pre-/De-Emphasis, Kompressor (4 Schwellen × 3 Ratios, Attack/Release), Bitcrusher
  (4 Bittiefen × 4 Drive-Werte) Sample für Sample gegen die privaten MixForge-Funktionen.

Hinweis zur Plattform: Bitgleichheit gilt bei gleichem Compiler und gleicher
Float-Auswertung. Auf dem Cortex-M7 kann GCC Multiplikation und Addition zu FMA
zusammenziehen; dann weichen beide Implementierungen gleichermaßen in der letzten
Stelle vom x86-Ergebnis ab. Der drumlogue (Cortex-A7) war dieser Vergleich nie bitgleich.

## 3. Abweichungen gegenüber den Korg-Algorithmen

| Punkt | drumlogue | Daisy | Klangwirkung |
| --- | --- | --- | --- |
| Parameterauflösung | ganze Prozent, sprunghaft pro Block | Potis als ganze Prozent mit Hysterese (`TapePanel`), dazu 15-ms-Gleitweg in `TapeFx` | Glatt statt Stufen; nach dem Einschwingen exakt der Prozentwert (bitgleich) |
| Dry/Wet | im Effekt | im Host (`TapeMixer`), gleiche Formel und ±1,2-Clamp, Mix gleitet 10 ms | Identisch bei ruhendem Regler |
| Bypass | nicht vorhanden | Mix gleitet auf exakt 0 → Ausgang = Eingang | neu |
| Ausgangsschutz | ±1,2-Clamp | ±1,2-Clamp, danach weiches Knie oberhalb 0,95 (wie Ambient-Reverb-Firmware) | Nur bei Pegeln > −0,45 dBFS |
| Sinus der Modulatoren | `sinf()` | Polynom auf **identischer** Float-Phasenfolge | ≤ 7,75·10⁻⁷ Ausgangsdifferenz |
| Kompressor (MixForge) | `log10f`/`powf` je Sample | unterhalb des Knies Kurzschluss auf Gain 1 | bitgleich (Test) |
| Bitcrusher-Haltezeit | an DRIVE gekoppelt (`1 + drive/13`) | eigener Parameter `crush_stride` (1–8) | Testwert 3; mit `1 + drive/13` exakt MixForge |
| Delay-Speicher | 2 getrennte Arrays | ein L/R-verschachteltes Array (gleiche 16 KiB), Maske statt Modulo | bitgleich |
| Eingänge | 2 oder 4 Kanäle verschachtelt | getrennte Kanalzeiger (FX-Vertrag) | – |
| NaN/Inf | ungeschützt | nach jedem Block Zustandsprüfung, bei NaN/Inf Zustand löschen; Host ersetzt nicht endliche Samples durch 0 | nur im Fehlerfall |

Nicht übernommen: MixForges DJ-Filter (nicht beauftragt), DrumShuffler, Delays,
Reverb. Kein verstärkter Wobble (siehe Abschnitt 7).

## 4. Messbare Eigenschaften (Hosttests, 26.10.2026)

**Wow/Flutter** (Verzögerungsmodulation; Tonhöhenabweichung = Tiefe · 2πf / 48 kHz):

| Einstellung | Frequenz nominal / gemessen | Tiefe (Samples) | Tonhöhe |
| --- | --- | --- | --- |
| WOW 100 %, AGE 0 % | 0,250 / 0,2504 Hz | ±0,600 | ±0,03 Cent |
| WOW 100 %, AGE 40 % | 0,470 / 0,4699 Hz | ±2,400 | ±0,26 Cent |
| WOW 100 %, AGE 100 % | 0,800 / 0,8003 Hz | ±5,100 | ±0,92 Cent |
| FLUTTER 100 %, AGE 0 % | 6,0 + 9,0 Hz | 0,175 + 0,075 | ≤ ±0,39 Cent |
| FLUTTER 100 %, AGE 40 % | 7,6 + 11,0 Hz | 0,567 + 0,243 | ≤ ±1,58 Cent |
| FLUTTER 100 %, AGE 100 % | 10,0 + 14,0 Hz | 1,155 + 0,495 | ≤ ±4,18 Cent |

Die kleine Frequenzabweichung (+0,16 % bei 0,25 Hz) stammt aus TapeVibes
Float-Phasenakkumulator und ist in beiden Pfaden identisch erhalten.

**Drive** (250-Hz-Sinus, −6 dBFS, Wow/Flutter/Hiss/Dropout aus, Age 40 %, Tone 50 %):
THD TapeVibe-Kurve 1,3 / 9,4 / 17,9 / 24,3 / 28,4 % bei DRIVE 0/25/50/75/100 %,
Grundwelle −2,6 bis +4,3 dB. MixForge TAPE 1,5–25,4 %, HARD 0,4–33,4 %, FOLD bis
1135 % (Faltung, Grundwelle bis −26 dB).

**Hiss** (HISS 100 %, AGE 100 %, Stille am Eingang): Studio −61,5 dBFS, Tape
−52,1 dBFS, Cassette −46,8 dBFS RMS, unkorreliert zwischen L und R.

**Dropouts** (DROPOUT 100 %, AGE 40 %, je 60 s): Studio 1699 Ereignisse (erwartet
1741), mittlere Länge 50,7 Samples (≈ 52); Tape 2765 (2823), 129,1 (≈ 130);
Cassette 4006 (4037), 240,8 (≈ 242). Folgen sind deterministisch (Startwert
`0x12345678` wie TapeVibe) und nach `Reset()` reproduzierbar.

**Parameterwechsel**: 1600 zufällige Parametersätze einschließlich NaN/Inf und
Werten außerhalb des Bereichs bleiben endlich; die Lesestelle bewegt sich höchstens
0,127 Samples je 8 Samples (Age-Sprung 0 → 100 % verschiebt die Grundverzögerung um
8 Samples; der Gleitweg begrenzt das auf einen kurzen Tonhöhenverlauf statt eines
Klicks). Ein DRIVE-Sprung 0 → 100 % erzeugt keine größere Sample-Differenz als der
eingeschwungene Zustand (0,0265 gegenüber 0,0276).

## 5. CPU

**Auf dem Seed3: NICHT GEMESSEN.** Messwege sind eingebaut:
`tape-fx` zählt Zyklen je Callback (DWT, `maximum_cycles`/`overruns`, LED S16 bei
Überlauf), `toy-piano-tape` liefert `python modules/mod-on-daisy/tools/mod_probe.py stats`.

Host-Benchmark (`make -C effects/tape-fx bench`, Intel Xeon 2,8 GHz, nur relative
Kosten, gemessen 26.10.2026):

| Konfiguration | ns/Stereo-Frame |
| --- | --- |
| TapeVibe-Init, Referenz `sinf` | 45,2 |
| TapeVibe-Init, optimiert | 45,1 |
| Hiss und Dropouts aus | 35,6 |
| Dauerhaftes Gleiten (Age/Drive) | 60,4 |
| + MixForge TAPE-Kurve | 51,4 |
| + Kompressor über Schwelle / unter Knie | 88,6 / 51,3 |
| + Bitcrusher | 50,9 |
| alle MixForge-Stufen | 127,4 |

Auf x86 ist glibcs `sinf` sehr schnell; der Unterschied Referenz/optimiert ist dort
nicht sichtbar. Statische ARM-Analyse des Builds (xPack GCC 13.2.1, `-O3`): Der
Referenzpfad ruft im Sample-Loop dreimal `sinf` (newlib, mit Bereichsreduktion) auf,
der Firmwarepfad keinmal; dazu kommen `log10f` und `powf` nur im Kompressor über dem
Knie. Je Sample bleiben im Standardpfad etwa 7 `VDIV.F32` (je 14 Takte):
2 × Sättigung, 2 × Bandkompression, bis zu 3 × Zufallszahl (`/16777215.0f`).
**Schätzung, NICHT GEMESSEN:** einige hundert Takte je Stereo-Sample, also wenige
Prozent der 480-MHz-Rechenzeit. Teuerste Stellen in dieser Reihenfolge:
MixForge-Kompressor über dem Knie (`log10f`+`powf`), Divisionen in Sättigung und
Bandkompression, Zufallszahlen für Hiss/Dropouts, Wow/Flutter (jetzt 3 × 12 Flops).

## 6. Optimierungen und ihre Prüfung

| Untersucht | Ergebnis | Prüfung |
| --- | --- | --- |
| `sinf` der drei Modulatoren | **übernommen**: Polynom 11. Grades nach Quadrantenfaltung (Fehler < 6·10⁻⁸ vor Rundung) auf TapeVibes eigener Phase | Test 9: ≤ 7,75·10⁻⁷; Test 4: Verzögerungsverlauf nach 10 min ≤ 3,8·10⁻⁶ Samples |
| Rotierender Quadraturoszillator | **verworfen**: läuft mit exakter Nennfrequenz, TapeVibes Float-Phase aber leicht daneben (z. B. +0,16 % bei 0,25 Hz); nach Sekunden entsteht Phasenversatz | erster Lauf von Test 9 (nicht eingecheckt): −46 dB (Init-Werte) bis −21 dB Ausgangsdifferenz |
| Kontrollrate mit Interpolation | nicht nötig: das Polynom ist billiger als Interpolationslogik und exakt | – |
| Blockweise Koeffizienten | **übernommen**: abgeleitete Größen (Drive-Gain, Post-Gain mit Division, Filter-Alphas, Tiefen, Raten, Dropout-Wahrscheinlichkeit) einmal je Block; nur während eines Gleitwegs je Sample | bitgleich (Test 9, 8) |
| Soft-Saturation | unverändert (eine Division); eine Näherung würde den Klang ändern | – |
| MixForge-Kompressor | **übernommen**: Kurzschluss unter dem Knie ohne `log10f`/`powf`, mit Sicherheitsabstand 10⁻⁴ | bitgleich (Test 9). **Verworfen**: Gain-Rechner nur jedes 4. Sample (Abweichung bis 2,6·10⁻³, messbar) |
| Delay-Lines | **übernommen**: L/R verschachtelt (eine Cache-Zeile für beide Kanäle), Maske statt `%`/Schleifen | bitgleich |
| Zufallszahl `/16777215.0f` | nicht umgesetzt: Kehrwert-Multiplikation ändert gelegentlich das letzte Bit und damit Dropout-Entscheidungen | – |

Der Referenzpfad bleibt per `SetReferenceMode(true)` für Tests erhalten.

## 7. Speicher

| Build (ARM, xPack GCC 13.2.1-1.1) | BIN | DTCM (Reserve) | SDRAM | SHA-256 BIN |
| --- | --- | --- | --- | --- |
| `tape-fx` 0.1.0 | 125 264 B | 105 816 B (25 256 B) | 16 384 B | `49472f686ce2a47eca17c86f605ce7a1fb1429a96d628d0d0b5defbb23364382` |
| `toy-piano-tape` 0.1.0 | 173 956 B | 119 584 B (11 488 B) | 16 708 B | `a730cb2afe9a1bb70304774752cf5e0d934cd2ff4e00003515f67794c3c62be6` |
| zum Vergleich `toy-piano` 0.4 | 160 532 B | 119 424 B | 0 B | `639dcacf…` (unverändert) |

Die Hashes gelten für den Stand dieses Berichts; maßgeblich ist das Manifest des
jeweiligen Builds. DTCM-Reserve ist statisch, keine Stack-Hochwassermessung.

* Im BOOT_SRAM-Layout landet jede statische Variable ohne Sektionsattribut im
  **DTCM** (128 KiB, hier zu 81–91 % belegt). Die 16 KiB Delay-Lines werden deshalb
  ausdrücklich mit `DSY_SDRAM_BSS` ins SDRAM gelegt (cachebar laut libDaisy-MPU).
  `scripts/check-tape-elf.py` prüft das im ELF über die Symboltabelle.
* `TapeFx` selbst (Zustand, 268 B) bleibt in der Standalone-Firmware im DTCM;
  in `toy-piano-tape` liegt der ganze Host (16 708 B) im SDRAM wie beim Ambient-Produkt.
* Code: `TapeFx::Run` optimiert 4,2 KiB, Referenzpfad 3,7 KiB (in der Firmware nicht
  aufgerufen, aber gelinkt).
* `RAM_D2` (256 KiB) ist in keiner Firmware belegt, hat im libDaisy-Linkerskript
  aber keine Sektion; nutzbar nur mit eigenem Linkerskript (nicht Teil dieses Auftrags).

**Für Stardust (Looper)**: SDRAM ist zu 99,68 % belegt (66 891 456 von 67 108 864 B,
frei ≈ 212 KiB); DTCM 115 976 B (Reserve ≈ 15 KiB). 16 KiB Delay-Speicher passen in
den freien SDRAM-Rest. Alternativ kann der Host den Speicher kleiner wählen: die
größte tatsächlich gelesene Verzögerung ist 26 Samples (10 + 8 + 5,1 + 1,65 + 1), ein
Puffer von 32 Frames (256 B) würde genügen. Das ist eine Folgeänderung mit eigenem
Bitgleichheitsnachweis; die 2 × 2048 von TapeVibe bleiben wie beauftragt.

## 8. Offene Punkte

* **Hörtest, CPU-Last, Pegel, Schalterrichtung am Gerät: NICHT GEMESSEN.**
* Wow ist in TapeVibe sehr dezent: höchstens ±0,92 Cent bei 0,8 Hz (AGE 100 %).
  Deutlich hörbar ist vor allem Flutter (bis ±4,2 Cent bei 10–14 Hz). Ein stärkerer
  Wobble wäre eine bewusste Klangänderung und ist erst nach einem Klangvergleich zu
  entscheiden; er ist nicht enthalten.
* Spätere Stardust-Integration: `TapeFx` + `TapeMixer` + Storage im SDRAM-Rest; kein
  Code dieses Auftrags berührt `instruments/looper/`.
