# Mod on Daisy — Architekturentscheidungen v0.1 und technische Einwände

**Stand:** 23. Oktober 2026 · **Phase:** 1 abgeschlossen, Phase 2 als Software umgesetzt

Dieses Dokument hält die Entscheidungen des Projektinhabers fest. Darauf folgen die
technischen Einwände, die sich bei der Prüfung gegen den tatsächlichen Code des
Repositories ergeben haben, sowie die daraus vorgeschlagenen Änderungen. Die
Spezifikation selbst steht in:

| Dokument | Inhalt |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Modularchitektur, Datenstrukturen, Verarbeitung, globale und stimmenbezogene Modulation, Instrumentenschnittstelle, Speicher- und CPU-Abschätzung |
| [PROTOCOL.md](PROTOCOL.md) | MIDI-Protokoll v1 (Live-Ebene und SysEx-Konfiguration) |
| [STORAGE.md](STORAGE.md) | QSPI-Aufteilung, Lösch- und Schreibverfahren, Datenintegrität (nur Spezifikation) |
| [TEST_PLAN_PHASE2.md](TEST_PLAN_PHASE2.md) | Host-Tests, Messfirmware, Messszenarien und Abnahme für Phase 2 |
| [VALIDATION.md](VALIDATION.md) | Ergebnisse von Phase 2 (Host gemessen, Hardware ausstehend) |
| [../../../editors/mod-on-daisy/docs/GUI.md](../../../editors/mod-on-daisy/docs/GUI.md) | Raster-GUI und Editor-Architektur (JUCE) |

**Evidenzklassen:** `BELEGT` = im Repository oder im gepinnten libDaisy nachgelesen;
`SCHÄTZUNG` = gerechnet, nicht gemessen; `VORSCHLAG` = noch zu bestätigen;
`OFFEN` = erst durch Hardwaretest zu klären.

## 1. Entscheidungen v0.1

| Nr. | Thema | Entscheidung |
| --- | --- | --- |
| D1 | Struktur | Modul unter `modules/mod-on-daisy/`, Editor unter `editors/mod-on-daisy/`. Vorhandene Instrumente bleiben vorerst unverändert. |
| D2 | Matrix | Grafische **8×8-Rastermatrix** nach dem Vorbild von MatrixBrute/MiniFreak. Keine Zuweisung über Menüs. **Höchstens 8 aktive Verbindungen** (vorläufiger Richtwert). Tiefe **−100 … +100** pro Kreuzungspunkt. |
| D3 | Quellen | 1 LFO 1 · 2 LFO 2 · 3 Random/S&H · 4 CV 1 · 5 CV 2 · 6 Modwheel (CC1) · 7 Channel AT · 8 Poly AT. Modwheel, Channel AT und Poly AT sind Pflicht. Poly AT wird stimmenbezogen verarbeitet. |
| D4 | Ziele | 8 Spalten, instrumentspezifisch. Nicht unterstützte Ziele und Verbindungen werden ausgegraut. Die Engine bleibt universell. |
| D5 | Rate | **Keine Audio-Rate in v0.1**, keine Infrastruktur dafür auf Vorrat. Control-Rate 1 kHz. Später allenfalls Audio-Rate für die CV-Eingänge, nach eigener Prüfung. |
| D6 | Editor | JUCE, zunächst Standalone für Windows. VST3 für Studio One soll später möglich bleiben. Kommunikation über USB-MIDI. |
| D7 | Pilot | Toy Piano für technische Integrationstests, danach Rings (Physical Modeling) mit Poly AT, später Nebulae. |
| D8 | Speicherung | Langfristig QSPI. Zuerst Aufteilung, Verfahren und Integrität spezifizieren; **noch keine Flash-Schreibfunktionen**. |
| D9 | Protokoll | SysEx-ID `0x7D` vorläufig. Eigene Projektkennung, Version, Geräte-ID, zuverlässige Übertragung der Matrix. libDaisy-Puffergrenzen beachten. |
| D10 | Performance | Erste Messung in Phase 2: inaktive Matrix, wenige Verbindungen, Maximum. Callback-Zeiten, Spitzen und Speicher nachvollziehbar dokumentieren. |
| D11 | Nächster Schritt | Nur Phase 1: Architektur und Spezifikation. |

Gegenüber dem ersten Konzept vom 22.10. entfallen damit: 32 Slots, Via-Modulation,
Hüllkurven als Quellen, frei zuweisbare CCs, der Audio-Rate-Pfad, Transaktionen über
viele Einzelbefehle, die selbstbeschreibenden Quellen und das Triple-Buffering
eines großen Programms. Das Konzept wird dadurch deutlich schlanker (siehe § 3).

**Ergänzungen zu D3 (24.10.2026, Entscheidung Betreiber):**

* **Aftertouch-Anzeige:** „Normalerweise ist entweder Channel AT oder Poly AT aktiv. Der
  Editor soll die empfangenen Daten automatisch unter ‚Aftertouch‘ anzeigen. Unter der
  Haube bleiben die Wege getrennt.“ Die Editoren zeigen deshalb eine gemeinsame Anzeige
  „Aftertouch“ mit dem Zusatz „Kanal“ bzw. „Poly · Note“. Sie folgt der zuletzt
  empfangenen Nachrichtenart, die das Gerät im erweiterten Monitor meldet (PROTOCOL
  § 3.6). Die Zeilen 7 (Channel AT) und 8 (Poly AT) bleiben in der Matrix, in Patches
  und im Audio-Weg unverändert getrennt und bearbeitbar; ein Punkt markiert die gerade
  angezeigte Zeile. Firmware ohne diese Meldung bekommt keine erratene Automatik,
  sondern den Hinweis „Kanal/Poly getrennt“.
* **LFO-Quermodulation:** LFO 1 und LFO 2 können sich gegenseitig in der Rate modulieren
  (Parameter `fm`, ±2 Oktaven, nur frei laufend). Das ist ein Quellparameter, keine
  neue Zeile oder Spalte; die 8×8-Matrix bleibt unverändert (PROTOCOL § 3.6).

## 2. Technische Einwände

Kein Einwand stellt eine Entscheidung grundsätzlich in Frage. Mehrere betreffen
jedoch Annahmen, die im vorhandenen Code nicht erfüllt sind und vor der Integration
bekannt sein sollten.

### E1 — Poly AT bei Rings verlangt einen Eingriff in die Torus-DSP (`BELEGT`)

`torus::Part::Process()` erhält **einen einzigen `Patch`** (Structure, Brightness,
Damping, Position) für alle Stimmen. `RenderModalVoice`, `RenderFMVoice` und
`RenderStringVoice` setzen daraus für jede Stimme dieselben Werte
(`instruments/physical-modeling/torus/dsp/part.cpp`). Unabhängige Stimmenmodulation
ist deshalb ohne Änderung der Torus-Schicht **nicht möglich**.

* **Aufwand**: klein. `Part::Process` bekommt ein Array mit 4 Stimmen-Patches und
  einen Pitch-Offset pro Stimme. Die Resonatoren berechnen ihre Filter ohnehin bei
  jedem Aufruf neu (`Resonator::Process` → `ComputeFilters()`). Unterschiedliche Werte
  pro Stimme kosten daher **keine zusätzliche Rechenzeit** (`SCHÄTZUNG`).
* **Folge**: Das ist eine Änderung an Upstream-DSP. Sie gehört in Phase 5 und muss in
  `torus/UPSTREAM.md` dokumentiert werden.
* **Sinnvolle Poly-AT-Ziele bei Rings**: Pitch, Structure, Brightness, Damping und
  Position. **Excitation** wird beim Anschlag übernommen (`velocity * excitation` pro
  Slot); Druck entsteht aber erst nach dem Anschlag, Poly AT wirkt dort also nicht.
  Reverb und Dry/Wet sind global (siehe E2).

### E2 — Poly AT auf globalen Zielen braucht eine Regel

Ein globales Ziel wie Reverb, Dry/Wet oder alle Ziele von Nebulae hat keine Stimme,
deren Druck es übernehmen könnte. `VORSCHLAG`: Auf globalen Zielen gilt der
**Druck der zuletzt angeschlagenen, noch gehaltenen Note** (Last-Note). Das entspricht
der Notenpriorität von Nebulae (`Notes::Current()`) und macht Poly AT auch bei
monophonen Instrumenten nutzbar. Die Alternative wäre, diese Zellen auszugrauen.
→ **Entscheidung nötig** (§ 4, Frage 1).

### E3 — MIDI 1.0 adressiert Noten, nicht Stimmen

Poly AT kommt als *(Kanal, Note, Druck)*. Die Zuordnung zur Stimme kennt nur das
Instrument. Die Engine führt deshalb eine **Drucktabelle pro Note**, und das
Instrument fragt für jede Stimme den Druck ihrer Note ab (ARCHITECTURE § 5).
Daraus folgen zwei Grenzen, die sich nicht vermeiden lassen:

* Zwei Stimmen auf derselben Note teilen sich den Druck. Das kommt vor: Bei Toy Piano
  liegt C5 auf zwei Tasten, und Rings verteilt wiederholte Unisono-Noten auf
  verschiedene Stimmen.
* v0.1 ist omni: Gleiche Notennummern auf verschiedenen Kanälen teilen sich die Tabelle.
  Echte Unabhängigkeit gleicher Noten gäbe es nur mit MPE (später möglich).

### E4 — Toy Piano hat keine dauerhaft modulierbaren Parameter (`BELEGT`)

Toy Piano besitzt nur **Attack und Decay**, und beide werden beim Anschlag übernommen
(`ToyPiano::Trigger/NoteOn(…, attack, decay)`). Außer Note On verarbeitet es keine
MIDI-Nachrichten. Für einen aussagekräftigen Integrationstest braucht es mindestens
2–3 neue kontinuierliche Ziele, zum Beispiel **Level pro Stimme** (Tremolo, Poly AT),
**Pitch pro Stimme** (Vibrato) und **Harmonic** (2. Oberton). Das ändert Toy Piano
v0.2, eine vom Betreiber abgenommene Release-Version.

`VORSCHLAG`:

* Die Messungen in Phase 2 laufen mit einer **eigenen Messfirmware** im Modul. Sie
  ändert kein Instrument (TEST_PLAN § 3).
* Die Toy-Piano-Integration wird ein eigener Schritt als Version v0.3. Ohne aktive
  Verbindung klingt sie bitgenau wie v0.2, was ein Test nachweist.
* Toy Piano bleibt ein guter Poly-AT-Prüfstand: 16 MIDI-Stimmen mit Notenzuordnung
  (`MidiVoiceNote()`) und Voice-Stealing.

### E5 — Die Tiefenauflösung ±100 reicht für Pitch nur mit zielbezogenem Bereich

Bei Rings bildet `Frequency` den normierten Bereich 0…1 auf **48 Halbtöne** ab
(`Transpose()` in `config.h`). Bezöge sich ±100 auf diesen vollen Bereich, wäre ein
Tiefenschritt **0,48 Halbtöne**. Für Vibrato ist das zu grob.

`VORSCHLAG`: Jedes Ziel legt einen **Modulationsumfang** (`span`) fest, also den
Wert, dem Tiefe 100 entspricht. Für Rings-Pitch wären das ±12 Halbtöne, ein Schritt
damit 0,12 Halbtöne, für Cutoff-artige Ziele der volle Bereich. Der Editor zeigt den
Umfang an, die Rasterwerte bleiben −100…+100. Kommt man damit nicht aus, wäre eine
quadratische Tiefenkurve die nächste Stufe; sie ist für v0.1 nicht vorgesehen.

### E6 — Die Grenze von 8 aktiven Verbindungen ist keine CPU-Frage

Auch alle 64 Zellen aktiv kosten geschätzt nur etwa 0,1 % CPU bei 1 kHz (ARCHITECTURE
§ 8). Die Grenze ist als **Bedien- und Klangrichtwert** sinnvoll, nicht als technische
Notwendigkeit. `VORSCHLAG`: Patch, Protokoll und Speicherformat bilden immer **das
volle 8×8-Raster** ab, und die Grenze ist eine einzige Konstante (`kMaxActive = 8`) in
der Validierung. Sie lässt sich später ohne Formatänderung anheben. Die Messung in
Phase 2 prüft zusätzlich 64 aktive Zellen als Lasttest.

### E7 — Die effektive Control-Rate hängt von der Blockgröße ab (`BELEGT`)

Die Engine rechnet mit der tatsächlichen Sampleanzahl pro Tick, die LFO-Frequenzen
stimmen deshalb immer. Ein Instrument liest seine Parameter aber nur einmal pro
eigenem Block. Bei Toy Piano, Looper und Physical Modeling (Block 48) sind das
1 kHz, bei Ambient Reverb (Block 32) 1,5 kHz, bei **Nebulae (Block 256) nur
187,5 Hz**. Ein 20-Hz-LFO hätte dort etwa 9 Stufen pro Periode. `VORSCHLAG`:
LFO-Bereich in v0.1 **0,02–20 Hz**. Nebulae misst und bewertet das gesondert, wenn
es integriert wird.

### E8 — CV-Eingänge: Abtastung, Rauschen und Kalibrierung (`BELEGT` / `OFFEN`)

* `AnalogControl::Process()` für CV läuft nur in `UpdateAllControls()`, also einmal pro
  Audio-Block. Die Bandbreite ist dadurch auf die halbe Blockrate begrenzt. Das stützt
  D5: Audio-Rate über CV bräuchte einen eigenen ADC-Pfad.
* Offene CV-Buchsen und ADC-Rauschen erzeugen ohne Gegenmaßnahme ständiges
  **Parameterzittern**. Daher eine Totzone um 0 und eine Glättung von etwa 2 ms
  (ARCHITECTURE § 4.3).
* `OFFEN`: Spannungsbereich, Nullpunkt und Pegel der offenen Desktop-Buchsen. Sie
  werden in Phase 2 gemessen und als **Gerätekalibrierung** gespeichert, getrennt vom
  Patch (STORAGE § 3).

### E9 — MIDI-Grenzen der gepinnten libDaisy (`BELEGT`)

* **SysEx-Empfang**: Puffer 128 Byte. Längere Nachrichten werden abgeschnitten und
  trotzdem als vollständig gemeldet. → Rahmen ≤ 120 Byte mit Längenfeld und Prüfsumme.
  Das ganze 8×8-Raster passt in **einen** Rahmen (99 Byte, PROTOCOL § 3.3).
* **SysEx-Senden**: `MidiToUsb` trennt vor `F7`. Laut Code-Lesung nicht
  spezifikationskonform, `OFFEN` unter Windows. Das wird als **erster Hardwaretest in
  Phase 2** geprüft. Falls nötig, folgt ein eigenes USB-Framing im Adapter, ohne
  Änderung am libDaisy-Submodul.
* **Ereignis-FIFO**: 256 Einträge, bei voller FIFO werden neue Ereignisse verworfen
  (`FIFO::PushBack`). Ein Poly-AT-Strom kann leicht über 1 000 Nachrichten pro Sekunde
  erreichen. Der Main-Loop muss deshalb jede Iteration vollständig leeren und darf nie
  lange blockieren. Das betrifft vor allem einen späteren QSPI-Löschvorgang mit bis zu
  400 ms, in denen MIDI verloren gehen kann (STORAGE § 5).

### E10 — Windows: Editor und Studio One teilen sich den MIDI-Port nicht ohne Weiteres

Klassische Windows-MIDI-Ports (WinMM) sind **nur von einer Anwendung gleichzeitig**
nutzbar. Hat Studio One „Daisy Seed External“ geöffnet, kann der Standalone-Editor den
Port nicht öffnen, und umgekehrt. Die Toy-Piano-Dokumentation beschreibt bereits
konkurrierende Anwendungen. Mögliche Wege:

* den Port in Studio One beim Editieren deaktivieren;
* Windows MIDI Services mit Mehrfachzugriff, falls auf dem System verfügbar
  (`OFFEN`, prüfen);
* später der VST3-Weg, bei dem das Plugin den Port statt Studio One hält.

Das gehört als Test an den Anfang von Phase 3, ist aber schon jetzt bekannt.

### E11 — QSPI-Speicherung: Bootloader, Cache und Blockierung (`BELEGT` / `OFFEN`)

* Der Daisy-Bootloader lässt die **ersten vier 64-KiB-Sektoren unberührt** und
  reserviert sie für künftige Funktionen. Programme liegen ab `0x90040000` und werden
  in **64-KiB-Blöcken** gelöscht. BOOT_SRAM-Programme sind höchstens 480 KiB groß
  (`libDaisy/doc/md/_a7_Getting-Started-Daisy-Bootloader.md`, `STM32H750IB_sram.lds`).
* libDaisys `PersistentStorage` löscht und schreibt ohne CRC und ohne Doppelablage.
  Bei Stromausfall kann der Inhalt verloren gehen. → eigenes Verfahren (STORAGE § 4).
* libDaisy richtet für `0x90000000` **keine MPU-Region** ein. Nach der
  ARMv7-M-Standardkarte ist der Bereich cachebar. Nach Lösch- oder Schreibvorgängen
  muss der D-Cache des Bereichs invalidiert werden, sonst liest man veraltete Daten.
  `OFFEN`: Die STM32H7-Problematik spekulativer Zugriffe auf QSPI im indirekten Modus
  ist vor der Implementierung zu prüfen.
* Ein Sektor-Löschvorgang dauert laut libDaisy-Konstante **bis zu 400 ms**
  (`IS25LP064A_SECTOR_ERASE_MAX_TIME`). Audio läuft weiter, weil der Code bei
  BOOT_SRAM aus dem SRAM ausgeführt wird. Der Main-Loop und damit der USB-Dienst steht
  in dieser Zeit.

### E12 — DTCM-Belegung (`BELEGT`)

Bei BOOT_SRAM liegen `.data` und `.bss` standardmäßig im **DTCM** (128 KiB). Toy Piano
belegt bereits 88,6 %, es bleiben etwa 14,9 KiB, auch für den Stack. Der Zustand der
Engine ist mit ≤ 1 KiB im Audiopfad klein genug (ARCHITECTURE § 8.1). Er wird trotzdem
pro Instrument im ELF-Check mitgeführt.

## 3. Daraus folgende Architekturänderungen

| Nr. | Änderung gegenüber dem Konzept vom 22.10. | Grund |
| --- | --- | --- |
| A1 | Festes Raster `depth[8][8]` + Aktiv-Maske statt Slot-Liste | D2, E6 |
| A2 | Feste Quellenliste ohne Quellen-Beschreibungsprotokoll; Ziele bleiben selbstbeschreibend | D3, D4 |
| A3 | Zwei Modulationswege: globale Offsets und ein **stimmenbezogener Poly-AT-Weg** mit Drucktabelle pro Note | D3, E1–E3 |
| A4 | `span` pro Ziel | E5 |
| A5 | Kein Audio-Rate-Code, kein Via, keine Hüllkurven, keine Polaritätsoptionen | D5, „schlank“ |
| A6 | Ganzes Raster in einem SysEx-Rahmen; jede Antwort bestätigt mit der CRC-32 des Patches | D9, E9 |
| A7 | Eigenes, stromausfallsicheres QSPI-Verfahren statt `PersistentStorage`; Gerätekalibrierung getrennt vom Patch | D8, E8, E11 |
| A8 | Messfirmware `modules/mod-on-daisy/bench/` in Phase 2; Toy-Piano-Integration als eigener Schritt v0.3 | D1, D10, E4 |

## 4. Nachtrag: Entscheidungen vom 23.10.2026

Die offenen Punkte aus der ersten Fassung dieses Dokuments sind entschieden:

| Nr. | Thema | Entscheidung | Umsetzung |
| --- | --- | --- | --- |
| N1 | Poly AT auf globalen Zielen | **Druck der zuletzt angeschlagenen, gehaltenen Note** (Last-Note) | `LiveState`, `AllowedCells`; nur `latched`-Ziele bleiben für Poly AT gesperrt |
| N2 | Bedeutung von Tiefe 100 | **pro Ziel festgelegt** (`span`), weil jeder Parameter eine andere musikalische Bedeutung hat | `TargetInfo::span`, `span_text`; Editor zeigt den Umfang im Spaltenkopf |
| N3 | Toy Piano | darf neue Ziele bekommen. Es ist **Entwicklungsumgebung**, kein eigentliches Instrument; hier wird ausprobiert | Toy Piano v0.3 als nächster Integrationsschritt (ARCHITECTURE § 6.3) |
| N4 | LFO-Parameter | Wellenformen **Sinus, Dreieck, Sägezahn steigend und fallend, Rechteck, Sample & Hold**; **Synchronisation schaltbar**: frei laufend, MIDI-Clock oder einer der Gate-Eingänge | Parameter Rate, Form, Sync, Gate, Teilung, Retrigger (ARCHITECTURE § 3.3, § 4.2) |
| N5 | Random/S&H | gleiche Sync-Möglichkeiten wie die LFOs (frei, MIDI-Clock, Gate); Modus Stufe oder geglättet | Ergänzung von mir für ein einheitliches Bedienkonzept, mit wenig Aufwand, weil dieselbe Clock-Logik genutzt wird. Lässt sich wieder entfernen |
| N6 | Messfirmware drei Ebenen tief | `common/desktop.mk` setzt `DAISY_ROOT ?= ../..` statt `:=` (rückwärtskompatibel) | umgesetzt; alle Instrumente bauen unverändert |

Damit ist Phase 2 freigegeben und als Software umgesetzt. Der Stand steht in
[VALIDATION.md](VALIDATION.md).

**Präzisierungen meinerseits** (ohne Einfluss auf die Entscheidungen, bei Bedarf änderbar):

* **Teilungen bei MIDI-Clock**: 4 Takte bis 1/32 einschließlich Triolen 1/4T, 1/8T,
  1/16T.
* **Teilungen bei Gate**: 1 Zyklus pro 4, pro 2 oder pro 1 Impuls; 2 oder 4 Zyklen pro
  Impuls. Zwischen den Impulsen interpoliert die gemessene Periode.
* **MIDI Start** setzt synchronisierte LFOs auf den Taktanfang.
* **Clock-Stopp**: Bleibt der Takt aus, halten synchronisierte LFOs an, statt frei
  weiterzulaufen.
* **Retrigger** wirkt nur bei frei laufenden LFOs.
