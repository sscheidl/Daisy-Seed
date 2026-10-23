# Mod on Daisy — Validierung Phase 2

**Stand:** 23. Oktober 2026 · Modul v0.1.0, Messfirmware ModBench v0.1.0
**Umgebung:** Linux x86-64, GCC (Host), xPack ARM GCC 13.2.1-1.1, gepinnte libDaisy
`f715e48`. Hardwareergebnisse werden vom Betreiber auf dem Seed3 Desktop Kit unter
Windows ergänzt.

Kennzeichnung: `GEMESSEN` = in diesem Stand ausgeführt; `NICHT GEMESSEN` = steht aus.

## 1. Host-Tests (`GEMESSEN`)

`make -C modules/mod-on-daisy test`, `test-sanitize` (ASan + UBSan) und `test-thread`
(TSan): **alle grün**.

| Testgruppe (TEST_PLAN § 2) | Ergebnis |
| --- | --- |
| 2.1 Quellen | LFO-Phase bei Tick 12/32/48/64/256 nach 288 000 Samples **bitgleich**; 20 Hz → **12 000 Zyklen in 600 s**; Inkrementfehler < 0,05 % bei 0,02/1/20 Hz; Sinus-Näherung max. **0,109 %**; Retrigger; S&H 10 Hz → 9–11 Wechsel/s; Random reproduzierbar, geglättet stetig; Glättung 10 ms ± 5 % bei n = 48/256, exakte Konvergenz; CV-Totzone und Kalibrierung |
| 2.1 Synchronisation | MIDI-Clock 120 BPM: Periode 1000 ± 30 Samples erkannt; Start → Taktanfang; Clock-Stopp → LFO hält. Gate 5 Hz, Teilung 2/1: **89 LFO-Zyklen** und **44 Random-Schritte** in 9 s (erwartet 90/45, Abweichung durch den Messbeginn mitten in einer Periode) |
| 2.2 Patch | CRC-Prüfwert `CBF43926`; 1000 zufällige Patches Round-Trip; Validierung (Bereich, Grenze 8, gesperrte Zellen, Teilung passend zur Sync-Quelle, 7-Bit-Name); Patch-Testvektor `E2340CC6` in C++ und Python gleich |
| 2.3 Engine | leerer Patch bitgenau neutral für 100 001 Werte × 8 Ziele, auch nach Entfernen einer Verbindung; ±50 % heben sich exakt auf; Begrenzung nach der Summe; span; Program-Wechsel ohne Phasensprung; **0 Allokationen** in Tick/Apply/HandleMidi/Service über 1000 Durchläufe |
| 2.4 Stimmenweg | Drucktabelle, Last-Note mit Überlauf (17 Noten), Voice-Stealing (Gleiten 10 ms, `Reset`), gleiche Note in zwei Stimmen, Poly AT global → Last-Note |
| 2.5 Nebenläufigkeit | Triple-Buffer 200 000 Veröffentlichungen ohne gemischte oder ältere Werte; Main/Audio-Threads mit Noten, Druck, CC, Clock, SysEx und Monitor: **keine Meldung von TSan** |
| 2.6 Codec | alle Kommandos Round-Trip; `GET_MATRIX` = **100 Byte**, `SET_MATRIX` = **95 Byte**; jedes gekippte Bit eines Rahmens erkannt; durch libDaisy gekürzter 128-Byte-Rumpf → `NAK 2`; falsche Version → `NAK 6`; fremde SysEx → ans Instrument; **10⁶ Fuzz-Rahmen** ohne Absturz, Patch immer gültig |
| 2.7 Controller | Instrument-Key bis 24 Zeichen vollständig in `IDENTIFY` (`physical-modeling` = 17, Review-Befund); Wiederholung mit gleicher `seq` einmal ausgeführt, Lesekommandos immer frisch, lokale Änderung löscht den Wiederholungsspeicher; 9. aktive Zelle → `NAK 7`, CRC unverändert; Warteschlange 2, ≤ 1 Antwort/ms; Monitor ~20 Hz, endet nach 3 s; ohne USB-Host keine Warteschlangenstaus; Speicherkommandos → `NAK 9` |
| Variante `kMaxActive = 64` | dieselben Tests (ohne Grenzprüfung 8) grün; Szenario S6 mit 64 aktiven Zellen gültig |
| Szenarien | S0–S8 gültig für das Prüfinstrument; S4 nutzt jede Quelle genau einmal |

Zwei Fehler wurden vor dem Commit gefunden und behoben:

* Die Tests fanden einen um ein Byte zu kleinen Antwortpuffer für Zustandsänderungen;
  die CRC wurde abgeschnitten.
* Bei der Durchsicht fiel auf, dass der Wiederholungsspeicher auch Lesekommandos
  beantwortete. Ein neu gestarteter Client mit gleicher `seq` hätte veraltete Werte
  erhalten, etwa alte Statistiken.

## 1a. Browser-Editor gegen den echten Controller (`GEMESSEN`)

`make -C modules/mod-on-daisy test-editor`:
* Headless-Chromium (Playwright) mit simuliertem Web-MIDI-Port.
* Die Rahmen gehen an `tools/mod_sim`, also den Controller- und Engine-Code der
  Firmware auf dem PC.
* Geprüft mit den Spalten von Toy Piano und Physical Modeling sowie offline.
* Nach jedem Schritt stimmt die Patch-CRC von Editor und Gerät überein: Tastatur,
  Ziehen, Mausrad, Regler, Ein/Aus, Quellen, Grenze 8, Monitor, Datei
  speichern/laden.

Der Test fand zwei Fehler im Modul, die behoben sind:

* **Protokoll**: Die Antwort auf `MONITOR` (0x30 | 0x40 = 0x70) hatte denselben Code
  wie die Monitor-Datenrahmen. Ein Client konnte die Bestätigung nicht von den Daten
  unterscheiden; `mod_probe.py monitor` wäre in eine Zeitüberschreitung gelaufen. Die
  Monitor-Rahmen nutzen jetzt `0x7E`. Ein Test stellt sicher, dass kein Antwortcode
  mit ihnen kollidiert.
* **Sync-Teilung**: Beim Wechsel MIDI-Clock ↔ Gate blieb der Teilungsindex stehen. Aus
  MIDI 1/4 wurde so Gate 4/1. Jetzt beginnt jede Sync-Art mit ihrem Standard (1/4 bzw.
  1/1).

Der Browser-Codec (`editors/mod-on-daisy/web/mod_protocol.js`) liefert dieselben
Referenzwerte wie C++ und Python: CRC-Prüfwert, Patch-Testvektor `E2340CC6`,
Rahmengrößen 95/100 Byte. Das Gerät startet mit der Standard-Patch-CRC `61C264C4`.

## 1b. JUCE-Editor gegen den echten Controller (`GEMESSEN`)

* **Kern** (`make -C editors/mod-on-daisy test`, auch mit ASan/UBSan):
  * Der Kern des JUCE-Editors läuft gegen Controller und Engine der Firmware.
  * Geprüft: Verbinden, Spalten, gesperrte Zellen, Bearbeiten, Grenze 8 (auch für eine
    auf 0 gesetzte, eingeschaltete Zelle), Quellen, Name, Leeren, Datei-Upload mit
    Abbruch, Trennen ohne späte Wiederholungen, keine Antwort.
  * 100 Werte beim Ziehen gehen als 6 `SET_CELL` zum Gerät.
  * 400 zufällige Änderungen bei je 5 % verlorenen Anfragen, verlorenen und
    verfälschten Antworten (drei Startwerte): Editor und Gerät enden auf derselben
    Patch-CRC.
* **Fenster** (`--selftest`, Linux unter Xvfb): Tastatur, Ziehen, Mausrad, Regler,
  Doppelklick, Quellen, Grenze, Name und Dateien gegen den Simulator mit Toy-Piano- und
  Physical-Modeling-Spalten. Nach jedem Schritt stimmt die Patch-CRC überein.
* **Dateien**: Eine vom JUCE-Editor gespeicherte Datei ergibt im Browser-Editor dieselbe
  Patch-CRC (`506290C2`), eine Datei des Browser-Editors lädt im Kern unverändert.
* **Windows** (MSVC): Build, Kerntest und Selbsttest laufen in der CI
  (`.github/workflows/editor.yml`). Am echten Daisy unter Windows: `NICHT GEMESSEN`.

## 2. ARM-Build der Messfirmware (`GEMESSEN`)

| Variante | BIN | `mod::Module` (RAM) | `Engine::Tick` Code | `Controller::Execute` Code |
| --- | ---: | ---: | ---: | ---: |
| Block 48, kMaxActive 8 (`build/ModBench-v0.1.0.bin`) | 138 608 B | **1 936 B** | 3 406 B | 8 628 B |
| Block 256, kMaxActive 8 (`build/b256-max8/`) | 138 624 B | 1 936 B | 3 406 B | 8 628 B |
| Block 48, kMaxActive 64 (`build/b48-max64/`) | 143 448 B | 3 280 B | 3 332 B | 12 504 B |

* `scripts/check-firmware.py`: **PASS** (BOOT_SRAM-Vektoren, eingebettete Version).
* Stack `Engine::Tick` 40 B (`-fstack-usage`).
* DTCM gesamt (Standardvariante) 108 772 B von 131 072 B (83 %). Den größten Teil
  belegt libDaisy (MIDI-FIFO etwa 37 KB, USB); das Modul trägt 1,9 KB bei.
* Toy Piano baut mit dem geänderten `common/desktop.mk` unverändert
  (`check-firmware.py`: PASS, v0.2).

## 3. Hardware (`NICHT GEMESSEN`)

Anleitung: [bench/README.md](../bench/README.md). Ergebnisse hier eintragen.

### 3.1 Szenarien (TEST_PLAN § 3.2), Block 48

| Szenario | Matrix-Zyklen min/mittel/max | Callback-Last mittel/max | Überläufe | MIDI verloren | Main max (Dispatch/Service) |
| --- | --- | --- | --- | --- | --- |
| S0 Bypass | NICHT GEMESSEN | | | | |
| S1 inaktiv | NICHT GEMESSEN | | | | |
| S2 1 Verbindung | NICHT GEMESSEN | | | | |
| S3 4 Verbindungen | NICHT GEMESSEN | | | | |
| S4 8 Verbindungen | NICHT GEMESSEN | | | | |
| S5 S4 + 32 Stimmen | NICHT GEMESSEN | | | | |
| S6 64 Zellen (Variante max64) | NICHT GEMESSEN | | | | |
| S7 S4 + Editor-Last | NICHT GEMESSEN | | | | |
| S8 S4 + MIDI-Flut | NICHT GEMESSEN | | | | |

Dieselbe Tabelle wird für Block 256 (Variante `b256-max8`) erstellt.

### 3.2 Funktionstests (TEST_PLAN § 4)

| Nr. | Test | Ergebnis |
| --- | --- | --- |
| H1 | USB-Enumeration | NICHT GEMESSEN |
| H2 | SysEx empfangen (`mod_probe.py lengths`) | NICHT GEMESSEN |
| H3 | SysEx senden, bytegenau | NICHT GEMESSEN |
| H4 | Portkonflikt mit Studio One | NICHT GEMESSEN |
| H5 | CV-Nullpunkt, Rauschen, Skalierung (`mod_probe.py monitor`) | NICHT GEMESSEN |
| H6 | Live-MIDI (CC1, Channel AT, Poly AT) | NICHT GEMESSEN |
| H7 | Hörtest Zipper | NICHT GEMESSEN |
| H8 | MIDI-Clock- und Gate-Sync | NICHT GEMESSEN |
