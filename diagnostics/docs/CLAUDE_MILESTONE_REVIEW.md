# Prüfauftrag an Claude: vollständiger Meilenstein-Review der Diagnostic Suite

## Auftrag und Grenzen

Prüfe die **gesamte Daisy Seed3 Hardware Diagnostic Suite unabhängig** auf
Korrektheit, Fehlertoleranz und nachvollziehbare Nachweise. Sie soll ein belastbarer
Diagnosebaustein für die weitere Instrumentenentwicklung werden. Prüfe den gesamten
Ordner `diagnostics/`, nicht nur den letzten GUI-Diff. Frühere Reviews und grüne
Tests sind Hinweise, keine Freigabe des jetzigen Stands.

**Nur Review:** Keine Korrekturen, Versionsänderungen, Commits, Pushes, Merges,
Releases, Formatierungen oder Änderungen an Bibliotheken. Kein Flashen, kein
Bootloader-Einstieg, kein USB-Reset und keine Zugriffe auf die echte SD-Karte.
Die Hardware-Testreihe ist für heute beendet. GUI-Läufe ausdrücklich mit `--sim`;
keine neuen Hardwaretests ohne einen späteren konkreten Auftrag des Benutzers.
Bericht, Testlogs und eigene temporäre Buildausgaben dürfen angelegt werden.

## Verbindlicher Prüfstand

- Repository: `sscheidl/Daisy-Seed`.
- Reviewkandidat: [PR #72](https://github.com/sscheidl/Daisy-Seed/pull/72), Branch
  `codex/diagnostics-control-panel`, **Entwurf; nicht mergen**.
- Implementierungs-/Nachweisstand:
  **`09fb2e36caf85160971deb9f3f5fcb31c337c0c8`**. Spätere Auftrags-/Dokucommits
  sind separat zu benennen; bei weiteren Codeänderungen den Prüfstand neu festhalten.
- Hauptzweig vor dem Kandidaten: `87604e1773b8ca81e5d69c8e9b95b562b7d127f4`.
  PR #70 mit den SD-Korrekturen und PR #71 mit der Statusübersicht sind dort gemergt.
- Firmware **0.3.2**, zwei Varianten: geschützte Diagnose und schreibfähiger
  CardReader. GUI-Kandidat **0.3.3**; er ändert keine Firmware oder DDP-Version.
- Veröffentlichte Buildartefakte sind weiterhin 0.3.2, Quellstand `b4df351`:
  [erfolgreicher Build](https://github.com/sscheidl/Daisy-Seed/actions/runs/41245983087).
  Sie sind kein Nachweis für die neue GUI 0.3.3 und laufen am 14.11.2026 ab.

Lokaler Kandidat:

```text
D:\Eigene Dateien\Eigene Dokumente\Playground\Daisy-Seed\.work\diagnostics-reader-hardware
```

Lies zuerst `AGENTS.md`, das Root-README, `HANDOVER.md` und `diagnostics/README.md`.
Nutze möglichst einen **eigenen sauberen Worktree** am genannten Commit. Der
Hauptarbeitsbaum steht auf einem anderen Instrumentenbranch und enthält fremde
Arbeit. Kein Checkout, Reset oder Clean dort; auch vorhandene Worktrees und
`.tools` nicht verändern oder entfernen. Den neuen GUI-PR nicht in den Hauptzweig
übernehmen, um ihn zu prüfen.

Dokumentiere am Anfang Commit, Dirty-Status, Betriebssystem, Compiler, Python,
PySide6 und die tatsächlich benutzten Befehle. Bibliotheken und Toolchain bleiben
gepinnt; insbesondere libDaisy `f715e4875b2d28445d0c11a0a17f064a248f69d0` und
xPack ARM GCC 13.2.1-1.1 nicht aktualisieren.

## 1. DDP-Protokoll vollständig gegenprüfen

Vergleiche `protocol/PROTOCOL.md`, `protocol/ddp.*`, Firmwareformatierung sowie
`windows/daisy_diag/protocol.py` und die Vektoren jeweils gegeneinander.

- Rahmenanfang/-ende, Längen, CRC-Parameter und exakt erfasste Bytes; LF/CRLF,
  Fragmentierung, mehrere Rahmen, ungültige Prüfsumme und Resynchronisation.
- Rauschen, abgeschnittene/überlange Rahmen, eingebettete Startzeichen,
  ungültige Bytes, Grenzwerte und erneute gültige Rahmen nach Fehlern.
- Zahlen, Vorzeichen, Hexfelder, Listenlängen, Wertebereiche, überlange Felder,
  fehlende/zusätzliche/doppelte Felder und optionale Erweiterungen.
- HELLO/Fähigkeiten/Versionen, ST-Sequenz und Zeitüberlauf, SD-, READER- und
  CV/Gate-Felder: Welche Fehler werden verworfen, welche beenden die Sitzung?
- Neue `io/fault/he/lba/ms`-Gruppe vollständig oder abwesend; `fault=7`;
  ältere Firmware ohne die Gruppe und ohne CV/Gate-Erweiterung.
- Aussagekraft der gemeinsamen Golden-Vektoren: Sind C++ und Python unabhängig
  geprüft, oder bestätigen Tests nur dieselbe falsche Annahme?

## 2. Firmware: Nebenläufigkeit, Speicher und reale Zeitgrenzen

Prüfe `firmware/src/main.cpp`, `diag_app.*`, `diag_core.*` und die tatsächlich
aufgerufenen Stellen im **gepinnten** SDK.

- Callback/Main-Loop-Datenaustausch, Atomics, zusammengehörige Messwerte und
  Interruptmaskierung; verlorene Änderungen und inkonsistente Snapshots.
- USB-IRQ/Ringpuffer: Voll-/Leerzustände, Überlauf, Veröffentlichungsreihenfolge,
  Lebensdauer der Empfangsdaten und Resynchronisation nach verlorenen Bytes.
- Zwei Sendepuffer: Lebensdauer bis Übertragungsende, BUSY/Fehler, Queuewechsel,
  neue Sitzung und Reset. Ist `hUsbDeviceHS.dev_state` zusammen mit der
  Übermittlung wirksam geschützt? Welche Interrupts kann BASEPRI wirklich sperren?
- Audio/USB/SDMMC-Prioritäten auch nach BSP-Initialisierung und lokalem SDTEST;
  keine blockierenden Zugriffe, Locks oder Allokationen im Audiocallback.
- BOOT_SRAM, Stack/Vektoren, FatFs-Objekte und DMA-Puffer im richtigen Speicher;
  keine statischen Konstruktoren mit Zugriff auf noch uninitialisiertes SDRAM.
- Jede relevante Wartebedingung einschließlich HAL/LL-Unteraufrufen prüfen.
  Ein äußerer 1-s-Timeout begrenzt keinen blockierenden Unteraufruf. Gesondert
  StartRead/Write, CMD13, Init/DeInit, LED-DMA und Fehler-/Abbruchpfade betrachten.

### Kritischer Schwerpunkt: Reset-Abbruch seit 0.3.2

`reader_abort.h` ersetzt HAL_SD_Abort durch SDMMC1-RCC-Reset. Prüfe unabhängig:

- Stimmt die Register-/Resetsequenz für den tatsächlichen STM32H7? Werden DPSM,
  FIFO und IDMA zuverlässig stillgelegt, bevor der SDRAM-Puffer wiederverwendet wird?
- SDMMC-IRQ deaktivieren, Barrieren, Reset setzen/lösen, Pending-IRQ löschen,
  Context/Lock/State: Sind Reihenfolge und Voraussetzungen korrekt?
- Fertigstellung unmittelbar vor/bei der IRQ-Sperre, BUSY/READY/RESET,
  bereits gesetzte Fehlerbits und nicht initialisiertes Handle.
- Bleiben Audio/USB/SysTick wirklich frei? Gibt es globale IRQ-Nebenwirkungen?
- RESET statt READY: Werden neue Transfers bis vollständiger Initialisierung
  abgewiesen? Funktionieren explizites RETURN, geschützter SDTEST und SHARE danach?
- Alle drei Diagnosepfade: Reader-Fehler, Reader-Close und geschützter SD-Lesetest.
- Sind die CMSIS/RCC-Stubs realistisch genug? Sie sind **kein Siliziumbeweis**.
  Bewerte insbesondere Annahmen, die das Fake zwangsläufig wahr macht.

## 3. SD-Schreibschutz, Besitz und USB-MSC

Behandle die beiden Images getrennt. Lies auch `card_reader.*`, `msc_bot.*`,
`reader_usb.*`, `reader_ctlreq.c`, Makefile und die Build-/Symbolprüfungen.

- Kann der geschützte Build auf irgendeinem Pfad Dateisystem oder Sektoren
  verändern? FatFs-Flags, disk_write, BSP/HAL/SDMMC- und Erase/Lock-Pfade prüfen.
  `nm` ist ein Indiz, kein vollständiger Beweis der Laufzeitaufrufe.
- CardReader darf erst nach ausdrücklichem SHARE schreiben. Nie Windows-MSC
  und lokales FatFs gleichzeitig, auch bei spät eintreffenden Befehlen.
- Start unexportiert, Share-Fehler, PREVENT/ALLOW, SYNCHRONIZE CACHE,
  START STOP UNIT, vollständiges Kartenprogrammieren vor Erfolg/EJECTED.
- Entnahme/USB-Reset während DMA und Programmierung, beide Reihenfolgen von
  Fehler und Reset-Mailbox; ERROR/INTERRUPTED bleiben Quarantäne bis Bestätigung.
- USB-Generationen, veraltete Endpunktübermittlungen, BOT-Recovery, CBW/CSW,
  Residue, STALL/ClearHalt, Richtung/Länge, LBA/Anzahl und Ganzzahlüberläufe.
- Cache-Operationen, exklusive 32-Byte-Zeilen, Ausrichtung, DMA-Erreichbarkeit,
  keine Herausgabe partieller Lesedaten, keine verfrühte Schreibbestätigung.
- Folgeoperation nach Timeout/Reset: Fehlerdiagnose bleibt erhalten, aber alter
  Status verhindert oder verfälscht nicht einen erlaubten Neustart.

## 4. CV/Gate: gesamte Signalkette und Sicherheitsversprechen

- Offizielle Pin-/Kanalzuordnung und Polarität gegen die gepinnte Hardwareklasse
  und, wo nötig, Herstellerunterlagen prüfen. Keine fremde Daisy-Variante annehmen.
- ADC-Rohwert, invertierte bipolare Normierung, Skalierung, Endpunkte, Filterung,
  Abtastrate und Cache-/DMA-Kohärenz. Sind Rohwert und gefilterter Wert zeitgleich?
- Gate einmal pro 1-kHz-Tick: Pegel, steigende Flanke, gehaltenes High,
  modulo-256-Zähler, kurze Pulse, Startzustand und Abtastgrenzen.
- DAC-Kanal 1/2, FIX/RAMP/SAW, Werte-/Periodengrenzen, Rundung, Wrap,
  inverse zweite Ausgabe nur in den vorgesehenen Modi und Startphase.
- OFF bei Start, BYE, Timeout, Portwechsel, App-Schließen und Verbindungsabbruch.
  **Bekannt offen:** Main-Loop/SDTEST kann die CV-OUT-Aktualisierung und das
  Abschalten verzögern. Prüfe die tatsächliche Worst-Case-Situation und ob
  Dokumentation/GUI stärkeres Abschalten versprechen als die Firmware einhält.
  Bloße Bedienhinweise gelten nicht als Firmwarekorrektur.

### Neuer zwingender Prüfpunkt: SAW-Rücksprung

Lies `docs/HARDWARE_NATIVE_CV_2026-10-31.md`, die Zusammenfassung und das
verlustfrei komprimierte Original unter `docs/evidence/native-cv-*`.

- Echte Firmware 0.3.2, CV OUT 1 → CV IN 1, 2000-ms-Periode; RAMP und SAW je
  etwa 6 s. 878 gültige Rahmen, keine Transportfehler; abschließend OFF bestätigt.
- RAMP: Korrelation etwa 0,999401 und aufsteigende Mittendurchgänge alle 2000 ms.
- SAW: ebenfalls 2000-ms-Abstände, aber Korrelation 0,961593; Kriterium `>0.97`
  scheiterte. Rohstatus bleibt **completed=false / AssertionError()**.
- Bei Firmware-ms 2190006 und 2192006: gemeldeter DAC-Code 12, Eingang noch
  etwa 0,9906 bzw. 0,9905. Keine unabhängige Messung physischer Pulsform/Latenz.

Verfolge ST-Zeitstempel, DAC-Echo, tatsächliches SetDacOut, ADC-Pipeline und
AnalogControl-Filter bis zum gesendeten Rahmen. Unterscheide begründet zwischen
Firmwarefehler, zulässiger Mess-/Berichtsverzögerung und ungeeignetem Testkriterium.
Nicht nachträglich die Schwelle absenken, um einen grünen Test zu erzeugen.
Ohne belastbaren Nachweis bleibt die Einordnung offen.

## 5. Host-Sitzung und Simulator

Prüfe `session.py`, `simulator.py`, Ereignisse und ihre GUI-Nutzung.

- Zustandsautomat, Handshake-Wiederholungen, falsche Geräte, Protokollkonflikte,
  Sperrzeit gescheiterter Ports, faire weitere Suche, Lesen/Schreiben/Portverlust.
- PING/PONG, Empfangszeitüberwachung, SDTEST-Toleranz, Reader-Toleranz,
  verzögerte/veraltete Antworten und Wiederverbindung ohne implizites SHARE.
- Unbekannter Besitz nach Timeout darf nicht als erfolgreiche Rückgabe/Eject
  dargestellt werden. SD-Ergebnis bei unverändertem Besitz erhalten, bei echtem
  Wechsel invalidieren. Diagnoseänderung und Busy-Änderung trotzdem sichtbar.
- Simulator: Nachrichtentypen, Limits, Fehler, CV-/Gate- und Sitzungsverhalten
  gegen Firmware vergleichen. Seine Vereinfachungen müssen benannt sein;
  Simulation ist kein MSC-Laufwerk und kein Hardware-Nachweis.

## 6. Windows-GUI und neuer Control-Panel-Kandidat

Prüfe `windows/daisy_diag/gui/`, App-Einstieg und Paketierung, einschließlich
komplettem `main_window.py`, nicht nur sichtbaren Screenshots.

- Vier Reiter SD card / CV / Gate / Controls / USB, Werte live und korrekt
  zugeordnet, CV-/Gate-Rohwerte/Min/Max/Zähler, gemeldete DAC-Codes versus Sollwerte.
- Reiterwechsel, Widget-Neuaufbau, setChecked-/Signal-Rückkopplung und
  Verbindungswechsel dürfen keine unbeabsichtigten CV-, LED- oder SD-Befehle senden.
- Fehlende Fähigkeiten/alte Firmware, SHARED/EJECTED/INTERRUPTED/ERROR,
  bestätigungspflichtige Rückgabe und BOOT-Sperre bei Windows-Besitz.
- 1280/1560 Pixel, lange Zähler/Fehlertexte und Skalierung: keine horizontal
  abgeschnittenen Werte/Bedienelemente, sinnvolle Navigation und erreichbarer Bericht.
- JSON-Bericht: vollständige Fehlerfelder, fehlende Messung nicht als Null-Erfolg,
  Simulationskennzeichnung, App-/Firmwareversionen und Datei-/Abbruchfehler.
- Simulator sichtbar markiert; keine erfundenen Volt- oder elektrischen LED-Werte.
  Englische GUI-Texte müssen die tatsächliche Funktion verständlich beschreiben.

## 7. Tests unabhängig bewerten und lokal gegenprüfen

Suche fehlende Fehlerfälle, unrealistische Fakes und tautologische Erwartungen.
Zähle Assertions nicht als unabhängige Szenarien: In 4262 DMA-Prüfungen sind
4096 Bytevergleiche enthalten; 42 Abbruchadapter-Prüfungen laufen gegen Stubs.
Der Kandidat meldet 243 bestandene Python-/GUI-Tests — selbst nachprüfen.

Zielgerichtete Befehle aus dem Repository-Root, mit dokumentierter Toolchain:

```text
make -C diagnostics/firmware test
make -C diagnostics/firmware test-sanitize
python -m pytest diagnostics/tests/python -v
bash diagnostics/scripts/build-firmware.sh
```

Auf Windows je nach installierter Umgebung `HOST_CXX=clang++` und zusätzlich:

```text
diagnostics\scripts\run-host-tests-msvc.cmd
diagnostics\windows\build_exe.cmd
```

Gepackte EXE ausschließlich mit `--sim --selftest 5` prüfen. Für Offscreen
`QT_QPA_PLATFORM=offscreen` und `QT_QPA_FONTDIR=C:\Windows\Fonts` setzen.
Keine teuren GitHub-Workflows und keine fremden Instrumentensuites starten.
Fehlende Werkzeuge/Quota/Umgebungsfehler als solche dokumentieren, nicht als
Softwarefehler oder bestandenen Lauf. Eigenes Build-/Testprotokoll aufbewahren.

## 8. Hardware- und Dokumentationsnachweise auditieren

Lies TEST_REPORT, FIRMWARE, PROTOCOL, SD_CARD_READER, alle datierten Hardwareberichte,
PR70_REVIEW_FIXES, archivierten Originalreview und maschinenlesbare Nachweise.

- Hardwarevarianten und Firmwarestände strikt trennen: geschütztes 0.3.2-Image
  ist nicht geflasht; beide CV-/Gate-Grundtests stammen überwiegend von 0.3.1;
  native CV-Kurzprobe von 0.3.2; App-Kandidat 0.3.3 ist eine GUI-Änderung.
- Für 0.3.2 stimmen erwartete BIN-Hashes:
  geschützt `4dc2e93940ccd8e3b7d1929f610a2a6656c6ca51535613d1936569750968e836`,
  Reader `78c7f8536c5d798ec02675c18b3b2b0fc62e6257d53816915d10cf765e9c35c5`.
  Reproduzieren, nicht nur abschreiben.
- Physische Kartenentnahme beim Schreiben wurde einmal beobachtet, mit
  anschließend lebendigem CDC und explizitem Wiederanlauf. Kein Silizium-Trace,
  nicht jeder BUSY-/CMD12-/Timeout-Fall wurde physisch erzwungen.
- Der erste Abbruch-Probe-Rohstatus ist completed=false wegen eines
  Testskript-Rennens; die anschließende unabhängige 15-s-Probe ist ein eigener
  Nachweis. Nicht zu einem uneingeschränkten automatischen Pass zusammenziehen.
- Windows-Dirty-Bit/CHKDSK /F nach absichtlicher Schreibunterbrechung richtig
  einordnen: keine Garantie beliebiger Dateisystemkonsistenz oder Datenrettung.
- Tabellen mit JSON/Rohdaten abgleichen, komprimierten Native-CV-Trace entpacken
  und SHA-256 prüfen. Nicht vorhandene lokale Rohlogs als Nachweisgrenze benennen.
- Stehen implementiert, funktional gemessen, softwaregetestet, unkalibriert,
  noch offen und bekannte Einschränkung überall konsistent nebeneinander?

## Ergebnisformat und Meilensteinentscheidung

Bericht als eigene Datei, beispielsweise `CLAUDE_MILESTONE_REVIEW_RESULT.md`,
und kurze Zusammenfassung in der Antwort. Nichts ungefragt veröffentlichen.

1. Exakter Prüfstand und tatsächlich ausgeführte Befehle mit Exitcode, Datum,
   Compiler/Versionen, Testergebnis und Hashvergleich; fehlende Läufe kennzeichnen.
2. Befunde nach P0–P3: Datei und enge Zeilenstelle am geprüften Commit,
   konkrete Auslösung, verletzte Zusage/Invariante, Auswirkung, Beleg und
   minimaler Korrekturvorschlag. Quelle klar als Codeableitung, Softwaretest,
   fremder Hardwarebericht oder eigene Messung kennzeichnen.
3. Für jeden großen Prüfbereich geprüfte Punkte ohne Befund und tatsächliche
   Grenzen nennen. Keine erfundenen Fehler, keine bloß kosmetischen Merge-Blocker.
4. Die sechs früheren Reviewpunkte jeweils bestätigen oder erneut öffnen.
   SAW-Einordnung und CV-OUT-Abschalten während SDTEST separat entscheiden.
5. Abschließendes begründetes Urteil getrennt für Firmware/Protokoll,
   Host/GUI, Nachweise und Meilenstein: **bereit**, **bereit mit klaren
   Einschränkungen** oder **nicht bereit**. Welche Befunde sperren PR #72,
   welche sperren eine stabile Meilenstein-/Releaseaussage, welche bleiben
   begrenzte spätere Hardwareabnahme? Keine vollständige Hardwarefreigabe
   aus diesem reinen Software-/Nachweisreview ableiten.

Ziel ist eine unabhängige Entscheidung über den tragfähigen Funktionsumfang,
nicht die Bestätigung eines gewünschten grünen Ergebnisses.
