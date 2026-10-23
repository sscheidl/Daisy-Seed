# Mod on Daisy — Testplan Phase 2 (Prototyp und erste Messung)

**Stand:** 23. Oktober 2026 · **Status:** Plan. Phase 2 beginnt erst nach Freigabe der
Spezifikation und der offenen Punkte in [DECISIONS.md § 4](DECISIONS.md#4-offene-entscheidungen).

## 1. Umfang

| In Phase 2 | Nicht in Phase 2 |
| --- | --- |
| `include/mod_on_daisy/` (ARCHITECTURE § 1) | Änderungen an Toy Piano, Physical Modeling, Nebulae |
| Host-Tests mit ASan/UBSan/TSan, CI-Job | Flash-Schreibfunktionen (D8) |
| Messfirmware `bench/` für das Desktop-Kit | JUCE-Editor (Phase 3) |
| `tools/mod_probe.py`: kleines Kommandozeilenskript für SysEx-Tests und Messabläufe unter Windows | Audio-Rate (D5) |
| Messbericht `docs/VALIDATION.md` | VST3 |

Konventionen wie in den vorhandenen Projekten: `make test`, `make test-sanitize`,
`make test-thread` im Modulverzeichnis; Fakes statt Hardware; feste Startwerte für
Zufall.

## 2. Host-Tests

### 2.1 Quellen

| Test | Erwartung |
| --- | --- |
| LFO-Frequenz bei Tick-Größe 12, 32, 48, 64, 256 Samples, 0,02/1/20 Hz, 600 s simuliert | Phase nach gleicher Sampleanzahl **bitgleich** für alle Tick-Größen; Frequenzfehler < 0,05 % (Quantisierung des 32-Bit-Inkrements, bei 0,02 Hz ≈ 0,024 %) |
| LFO-Formen | Bereich −1…+1; Sinus-Fehler gegen `std::sin` < 0,1 %; Dreieck und Sägezahn an den Knicken stetig bzw. genau einmal pro Periode springend |
| Rate-Abbildung | `r = 0` → 0,02 Hz, `r = 16383` → 20 Hz, monoton |
| Retrigger | Ändert sich der Note-On-Zähler, beginnt die Phase bei 0; ohne Retrigger keine Änderung |
| Random Stufe | Wert bleibt zwischen zwei Takten konstant, liegt in [−1, 1], Folge bei festem Startwert reproduzierbar |
| Random geglättet | stetig: Änderung pro Tick ≤ 2 · n · f / fs |
| Glättung CV/MIDI | Sprungantwort mit Zeitkonstante 2 ms bzw. 10 ms ± 5 % bei n = 48 und n = 256; konvergiert exakt (Snap), keine Denormals |
| CV-Totzone | \|x\| < 0,02 → 0; stetig am Rand der Totzone; ±1 bleibt ±1 |

### 2.2 Patch, Validierung, CRC

| Test | Erwartung |
| --- | --- |
| Serialisierung | feste 104 Byte, Little Endian, Round-Trip identisch |
| CRC-32 | Prüfwert `CRC32("123456789") = 0xCBF43926`; Patch-CRC gleich auf x86-64-Host und ARM (Testvektor im Repository) |
| Validierung | Tiefe ±101 → abgelehnt; 9 aktive Zellen → abgelehnt; aktive, nicht erlaubte Zelle → abgelehnt; ausgeschaltete Zelle mit Tiefe → erlaubt; Name mit Byte > 127 → abgelehnt |
| Erlaubte Zellen | Ziel ohne Namen → ganze Spalte gesperrt; Poly AT auf `latched` → gesperrt; Poly AT global je nach Regel Last-Note oder gesperrt; Quelle in `unavailable_sources` → Zeile gesperrt |

### 2.3 Engine und Matrix

| Test | Erwartung |
| --- | --- |
| **Bitgenau neutral** | Leerer Patch: `Apply(t, base)` liefert für 10⁵ Werte `base ∈ [0, 1]` dasselbe Bitmuster wie `base`; `ApplyVoice(t, base, p)` ebenso für jedes p |
| Summe und Begrenzung | +50 % und −50 % derselben Quelle auf ein Ziel ergeben 0; Summe > 1 wird nach der Summe auf 1 begrenzt, nicht pro Verbindung |
| span | Tiefe 100 mit span 0,25 und Quelle 1 → Offset 0,25; Tiefe 1 → 0,0025 |
| Nur benutzte Quellen | `source_mask` enthält genau die Quellen aktiver Verbindungen (Zähler im Test-Build) |
| Program-Wechsel | LFO-Phase läuft beim Wechsel ohne Sprung weiter |
| Feste Grenzen | Laufzeit von `Tick()` unabhängig vom Inhalt (Operationszählung im Test-Build): höchstens 8 Quellen und `kMaxActive` Verbindungen |
| Keine Allokation | `operator new`/`malloc` im Test überschrieben und gezählt: 0 Aufrufe in `Tick`, `Apply`, `HandleMidi`, `Service` |

### 2.4 Stimmenweg und Noten

| Test | Erwartung |
| --- | --- |
| Drucktabelle | Note On setzt 0; Poly Pressure nur bei gehaltener Note; Note Off und Velocity 0 setzen 0; CC123 leert alles |
| Last-Note | A, B gedrückt → B; B los → A; 17 Noten → Liste läuft über, die älteste fällt heraus, kein Absturz |
| Voice-Stealing | Simulierte Stimme wechselt von Note 60 (Druck 100) auf 64 (Druck 0): `VoicePressure` gleitet in 10 ms auf 0; mit `Reset()` sofort |
| Gleiche Note zweimal | zwei Stimmen auf Note 72 bekommen denselben Druck |
| Poly AT global | Poly AT auf globalem Ziel nutzt den Last-Note-Druck, auf `per_voice`-Ziel nur `voice_gain` |

### 2.5 Nebenläufigkeit (ThreadSanitizer)

* **Triple-Buffer**: Ein Thread veröffentlicht 10⁶ Programs mit eingebetteter
  Prüfsumme, ein zweiter liest im Tick-Takt. Gefordert: keine Data Races, nie ein
  gemischtes Program, letzter Stand wird übernommen.
* **Atomare Rohwerte und Drucktabelle**: Main-Thread schreibt Noten- und
  Druckfolgen, Audio-Thread liest. Keine Data Races.

### 2.6 Protokoll-Codec

| Test | Erwartung |
| --- | --- |
| Round-Trip aller Kommandos | Kodieren → Dekodieren identisch; alle Rahmen ≤ 120 Byte; `GET_MATRIX` genau 100 Byte, `SET_MATRIX` 95 Byte |
| `packed`/`mask64` | Grenzwerte (−100, 0, +100, alle Bits) korrekt |
| Prüfsumme | jedes einzelne gekippte Bit im Rahmen → `NAK 1` oder `NAK 2` |
| **libDaisy-Kürzung** | Ein 130-Byte-Rahmen, wie ihn der Parser auf 128 Byte kürzt → `NAK 2`, Zustand unverändert |
| Fremde Rahmen | andere Hersteller-ID oder Projektkennung → `HandleMidi` gibt `false`; andere Geräte-ID → verbraucht, keine Antwort |
| Fuzzing | 10⁶ zufällige SysEx-Inhalte (Länge 0–140) unter ASan/UBSan: kein Absturz, kein undefiniertes Verhalten, Patch nur bei gültigen Rahmen verändert |

### 2.7 Controller

| Test | Erwartung |
| --- | --- |
| Wiederholung mit gleicher `seq` | gleiche Antwort, Änderung nur einmal ausgeführt |
| Grenze 8 | 9. `SET_CELL` aktiv → `NAK 7`, Patch-CRC unverändert |
| CRC-Bestätigung | Antwort-CRC = CRC des Editor-Modells (Testmodell mit gleichem `patch.h`) |
| Sendebegrenzung | 100 Anfragen in einem Aufruf → höchstens 1 Antwort pro `Service()` und ms |
| Monitor | endet 3 s nach der letzten `MONITOR`-Anfrage; Rate ≤ 25 Hz |
| Nicht implementiert | `STORE`, `RECALL`, `SET_POWER_ON` → `NAK 9` |

## 3. Messfirmware `bench/` (D10)

### 3.1 Aufbau

* **Ort und Bau**: `modules/mod-on-daisy/bench/`, eigenes Makefile mit
  `common/desktop.mk`.
  * `desktop.mk` setzt heute `DAISY_ROOT := ../..` für Projekte zwei Ebenen unter dem
    Repository. `bench/` liegt drei Ebenen tief. Vorgeschlagen ist eine kleine,
    rückwärtskompatible Änderung auf `DAISY_ROOT ?= ../..`. Die Alternative wäre ein
    eigenes Verzeichnis `modules/mod-on-daisy-bench/`.
  * Eintrag `mod-bench` in `projects.mk` und in der CI-Matrix, also Build, Firmware-Check
    und Artefakt wie bei den Instrumenten.
* **Inhalt**:
  * Desktop-Kit, 48 kHz, Block 48. Eine zweite Build-Variante nutzt Block 256.
  * Ein **Prüfinstrument**: Sinus-Oszillatorbank mit 4 oder 32 Stimmen und 8 Zielen
    (5 `per_voice`, 3 global). Es ruft `Apply`/`ApplyVoice` so auf, wie es ein echtes
    Instrument täte, und gibt einen hörbaren Ton aus.
  * Eine Variante mit `kMaxActive = 64` für den Lasttest.
* **Messgrößen**:

| Größe | Methode |
| --- | --- |
| Zyklen Matrix-Anteil (`Tick` + alle `Apply`/`ApplyVoice`) | DWT `CYCCNT` direkt davor und danach; Minimum, Mittel, Maximum |
| Callback-Last | `daisy::CpuLoadMeter` (wie Physical Modeling), Mittel und Spitze |
| Überläufe | Callback-Dauer > Blockbudget, gezählt |
| Main-Loop | Höchstdauer von `HandleMidi` + `Service`, getrennt von der Sendedauer `SendMessage` (bis zu 3 × 100 µs blockierend) |
| MIDI-Verluste | Lückenerkennung in einem nummerierten Teststrom aus `mod_probe.py`; libDaisy meldet Überläufe der FIFO nicht selbst |
| Speicher | `arm-none-eabi-size`, MAP-Datei, `nm -C --size-sort` gefiltert auf `mod::`; DTCM- und SRAM-Differenz gegen den Build S0 ohne Modul; `-fstack-usage` (`.su`) für `Tick`, `HandleMidi` und `Service` |

* **Steuerung und Auslesen**:
  * Das Szenario wird per `SET_SCENARIO` gewählt, die Ergebnisse kommen per
    `GET_STATS`. Damit prüft die Messfirmware gleichzeitig Empfang und Senden von SysEx.
  * Falls SysEx-Senden auf dem Rechner nicht ankommt, gibt es eine Rückfallebene über
    gewöhnliche CCs auf Kanal 16, nach dem Muster der Telemetrie von Physical Modeling.

### 3.2 Szenarien

| Nr. | Szenario | Zweck |
| --- | --- | --- |
| S0 | Prüfinstrument ohne Modul (eigener Build) | Basis |
| S1 | Modul eingebunden, **Matrix inaktiv** (leerer Patch) | Grundkosten |
| S2 | **1 Verbindung**: LFO 1 → globales Ziel | wenige Verbindungen |
| S3 | **4 Verbindungen**: LFO 1, LFO 2, Random, Modwheel | wenige Verbindungen |
| S4 | **8 Verbindungen**, alle 8 Quellen, davon Poly AT auf 3 Stimmenzielen, 4 Stimmen | **Maximum v0.1** |
| S5 | wie S4, 32 Stimmen | Stimmenweg wie Toy Piano |
| S6 | 64 aktive Zellen, 32 Stimmen (Variante `kMaxActive = 64`) | Lasttest über der Grenze (E6) |
| S7 | S4 + Editor-Last: `SET_CELL` 50/s und `MONITOR` 25 Hz | Main-Loop und Program-Wechsel |
| S8 | S4 + MIDI-Flut: Poly AT 16 Noten × 100 Hz, CC1 und Channel AT je 100 Hz | MIDI-Belastung (E9) |

Jedes Szenario läuft bei Block 48 und Block 256 jeweils **60 s nach `RESET_STATS`**,
dreimal. `mod_probe.py bench` führt die Folge automatisch aus und schreibt eine CSV-Datei.

### 3.3 Dokumentation der Ergebnisse

`docs/VALIDATION.md` nennt Firmware-Commit (SHA), Toolchain, Blockgröße und Szenario.
Darin stehen:

* je Szenario Matrix-Zyklen (min/mittel/max und µs), Callback-Last (mittel/max in %),
  Überläufe, Main-Loop-Höchstdauer, verworfene MIDI-Ereignisse;
* die Speichertabelle (DTCM, SRAM, Code, Stack pro Funktion);
* der **Vergleich mit der Abschätzung** in ARCHITECTURE § 8. Bei Abweichungen über
  Faktor 3 wird die Abschätzung korrigiert und begründet.

Gemessene Werte werden als `GEMESSEN` gekennzeichnet; Werte, die noch fehlen, als
`NICHT GEMESSEN`, wie in den vorhandenen Validierungsberichten.

### 3.4 Zielwerte (Abnahme)

| Größe | Zielwert |
| --- | --- |
| Matrix-Anteil S4, Maximum | ≤ 2 000 Zyklen (≈ 4,2 µs, 0,4 % des 1-ms-Blocks) |
| Matrix-Anteil S5, Maximum | ≤ 4 000 Zyklen |
| Matrix-Anteil S6, Maximum | ≤ 6 000 Zyklen (Information, keine Abnahme) |
| Überläufe | 0 in S1–S8 |
| Modul-RAM gesamt | ≤ 2 KiB |
| DTCM-Zuwachs gegenüber S0 | ≤ 2 KiB |
| Main-Loop pro `Service` ohne `SendMessage` | ≤ 50 µs |
| MIDI-Verluste in S8 | 0 |

Werden Zielwerte verfehlt, folgt keine Optimierung auf Verdacht. Zuerst wird die
Ursache im Messbericht benannt; danach wird entschieden.

## 4. Hardware-Funktionstests (Desktop-Kit, Windows)

| Nr. | Test | Erwartung / Ergebnis festhalten |
| --- | --- | --- |
| H1 | USB-Enumeration | „Daisy Seed External“ unter Windows sichtbar |
| H2 | SysEx empfangen | `IDENTIFY` aus `mod_probe.py` wird beantwortet; Rahmen 10–120 Byte angenommen; 130-Byte-Rahmen → `NAK 2` |
| H3 | **SysEx senden (E9)** | Unter Windows kommen Antworten der Längen 10–120 Byte (auch Längen um Vielfache von 3) **bytegenau** als `F0 … F7` an. Prüfung mit `mod_probe.py` und einem MIDI-Monitor. Ergebnis entscheidet über ein eigenes USB-Framing im Adapter |
| H4 | Portkonflikt (E10) | Studio One mit aktivem Port geöffnet → kann `mod_probe.py` den Port öffnen? Ergebnis geht in Phase 3 ein |
| H5 | CV-Eingänge (E8) | Werte bei offener Buchse, 0 V und, falls verfügbar, ±5 V: Nullpunkt, Rauschen (Standardabweichung), Skalierung → Totzone und Kalibrierung festlegen |
| H6 | Live-MIDI | CC1, Channel AT und Poly AT aus Keyboard oder Studio One sichtbar im Monitor; Poly AT pro Note korrekt zugeordnet |
| H7 | Hörtest | Prüfton mit LFO 0,02–20 Hz, Modwheel-Sweep und Poly AT ohne Zipper-Geräusche bei Block 48; Block 256 gesondert bewerten (E7) |

## 5. Abschluss von Phase 2

Phase 2 ist abgeschlossen, wenn:

1. alle Host-Tests in CI grün sind (Release, ASan/UBSan, TSan);
2. `mod-bench` in CI gebaut und geprüft wird;
3. `VALIDATION.md` die Szenarien S0–S8 und H1–H7 mit Werten oder begründetem
   `NICHT GEMESSEN` enthält;
4. die Ergebnisse zu H3 (SysEx-Senden) und H4 (Portkonflikt) vorliegen, weil sie den
   Editor in Phase 3 bestimmen.
