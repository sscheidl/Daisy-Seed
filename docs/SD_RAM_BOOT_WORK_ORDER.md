# Arbeitsauftrag: Entwurf E schrittweise nachweisen

**31.10.2026 · vorbereiteter Auftrag, noch keine Implementierung.**

**Nachtrag:** Der Nutzer hat anschließend die Entwicklung durch Claude Code
beauftragt. Maßgeblich ist jetzt der
[Entwicklungsauftrag](SD_RAM_BOOT_CLAUDE_AUFTRAG.md), der lokale Codearbeit,
Tests und Builds erlaubt. Die folgenden Freigabeaussagen halten den früheren
Planungsstand fest. Geräteaktionen bleiben gesondert beim Menschen.

Grundlage: [Konzept v2](SD_RAM_BOOT_CONCEPT_V2.md) und
[Claude-Review](SD_RAM_BOOT_REVIEW_CLAUDE.md). Der nächste technische Meilenstein
ist eine **minimale, stabil aus QSPI laufende Verwaltung mit LED und CDC**.
Noch kein SD-Laden, keine vollständige Diagnose und kein JUCE-/VST3-Umbau in
diesem ersten Hardwarekandidaten.

Die bisherige Beschränkung auf Konzeptarbeit bleibt bestehen. Dieses Dokument
ordnet die späteren Arbeiten und ihre Abschlusskriterien; es startet sie nicht.
Flashen/Bootloaderzugang und physische Tests bleiben gemäß AGENTS.md beim Menschen.

## 1. Vorgeschlagener Umfang für Version 1

| Entscheidung | Vorschlag für die spätere Umsetzung |
| --- | --- |
| Architektur | E: vorhandener Bootloader, neue BOOT_QSPI-Verwaltung, BOOT_SRAM-Anwendungen auf SD. Kein eigener Bootkern. |
| Auswahl | Plätze 1–15 SD-Anwendungen, Platz 16 residente Diagnose. Als Arbeitsvorschlag behandeln, bis die Bedienung festgelegt ist. |
| Kartenleser | Separates Image; keine MSC-Integration in die erste Verwaltung. |
| Startverhalten | Verwaltung startet und wartet; kein Autostart des letzten Instruments. |
| Rückkehr | SKIP_TIMEOUT mit eigenem Auslöser; S9+S16/BL/DDP BOOT behalten ihren Servicesinn. Konkrete neue Tastenkombination vor Implementierung auf Konflikte prüfen. |
| Erstes Panel | Bestehendes CDC/DDP-Framing als Grundlage. Minimales Hostwerkzeug zuerst; HTML, JUCE und VST3 später mit derselben Semantik. |
| Update | Instrumente über SD-Pakete, Verwaltung über bisherigen DFU-Anwendungsweg; kein `program-boot` für E. |
| Unvermeidliche Grenze | Stock-SD-Autoflash und ungeschützter DFU-Slot bleiben bestehen. Vorherige Prüfung der Karte auf Root-Dateien mit `.bin` im Namen ist Bestandteil des Betriebsvertrags. |

Noch offene Architektur-/Bedienentscheidungen nicht mit einer bereits erteilten
Umsetzungsfreigabe verwechseln. Wenn die Stock-Grenzen nicht akzeptabel sind,
vor Codebeginn zu Entwurf B zurückkehren und den Mehraufwand neu planen.

## 2. Voraussetzungen aus der anderen Diagnoseaufgabe

Als Übergabe genügt ein konkreter integrierter Commit mit Prüfbericht. Hier
keine parallelen Diagnosekorrekturen anfangen und keinen Reviewbefund allein
aufgrund eines neuen Versionsnamens als geschlossen ansehen.

Erforderliche Inhalte der Übergabe:

- SD-Fehler- und Abbruchpfade einschließlich aufgerufener HAL-Funktionen sind
  nachvollziehbar begrenzt; passende Softwaretests und offene Hardwarefälle
  stehen ausdrücklich im Bericht.
- FAT32/exFAT beziehungsweise der tatsächlich integrierte SD-Treiberstand sind
  benannt; zukünftige Fähigkeiten nicht als vorhandene Integration ausgeben.
- CV-Eingänge, Gate-Eingänge und CV-Ausgänge haben einen klar dokumentierten
  Stand. Loopback ist keine unabhängige Spannungsmessung.
- Das Beenden von CV-Testsignalen bei Sessionende und vor Reset/Programmwechsel
  ist vorgesehen und passend geprüft. Ungeprüfte elektrische Übergänge bleiben
  **NICHT GEMESSEN**.
- Kartenbesitz und USB-/Kartenunterbrechungen haben eindeutige Zustände.

Die Diagnose-Übergabe ist Voraussetzung für die spätere Übernahme der Diagnose
und SD-Pfade. Ob ein rein lokaler QSPI-Minimalbuild vorher untersucht werden
soll, wäre eine gesonderte Aufhebung der derzeitigen Buildbeschränkung; dieses
Dokument nimmt sie nicht vorweg.

## 3. Phase 0 – Spezifikation und Quellen schließen

Diese Phase kann weiterhin ohne Firmwarecode oder Build vorbereitet werden.

| Ergebnis | Inhalt / Abschlusskriterium |
| --- | --- |
| Quellenblatt | Versionsgebundene Fundstellen für Startup, BootInfo/JUMP, Cache-Commit und die noch offenen RM0433-Fragen. Ungelesene Stellen sichtbar offen lassen; keine angenommenen Abschnittsnummern. |
| Speichervertrag | QSPI-Code, DTCM-Daten/Stack, geeignete D2-Puffer, SD-Bounce im AXI-Hilfsbereich oder SDRAM. Kein aktiver Verwaltungsbereich im 480-KiB-Ziel. Prüfbare LMA-/VMA-Intervalle, nicht nur Symbolnamen. |
| Paketvertrag | Binärer `.dsy`-Header mit eindeutigem Format/Byteordnung, Profil/Hardware-ID, Produkt-ID, exakter Payloadlänge und Hashbereich. Grenzen und Verhalten bei unbekannten Pflichtfeldern festlegen. |
| Katalogvertrag | IDs, Plätze, Generation und relative Pfade; vorhandene v2-Limits prüfen. Payloadhash und Gesamtpakethash auseinanderhalten. |
| Commitvertrag | Zielvorbereitung, begrenzte Reads, vollständige Prüfung, Peripherieabschluss, Cache-Clean und Barrieren, Ziel setzen, JUMP zuletzt, direkter Reset. Nicht anschließend `ResetToBootloader()` rufen. |
| Rückkehr-/Servicevertrag | Speicherabschluss, CV-Ruhezustand, Taste/Hostauslöser; Statuswerte nicht umdeuten. |
| Karten-/Recoveryvertrag | Root-Dateiprüfung am PC vor Einlegen; manuell erreichbarer Wiederherstellungsweg. Warnung aus der bereits laufenden Verwaltung ist kein Schutz vor dem vorherigen Autoflash. |

Vor einem späteren Flashkandidaten müssen Architekturgrenzen, Linker-/Startvertrag
und manueller Wiederherstellungsweg konkret reviewbar sein. Binäridentität des
installierten Bootloaders nicht aus dem Dateinamen des mitgelieferten Images
ableiten. Die ausgelieferte Datei und die tatsächlich installierte Firmware
getrennt erfassen.

## 4. Spätere Umsetzungsphasen und Abschlusskriterien

| Phase | Begrenzte Arbeit | Wann abgeschlossen? |
| --- | --- | --- |
| 1: Hostwerkzeuge | Paket-/Katalogprüfung, Kartenprüfung, ELF-/Map-Prüfer und portable Ladezustände; noch keine Geräteaktionen. | Fehlende/trunkierte Pakete, Überläufe, falsche Hashes/Profile/Vektoren, ungültige Pfade, doppelte Plätze, veraltete Aufträge und unpassende Speicherintervalle werden gezielt abgewiesen. |
| 2: QSPI-Minimalanwendung | Projektlokaler Linker; LED/CDC-Identifikation und Servicesprung. Kein SD-Loader, kein Synth, kein komplettes Panel. | Lokal: Größe/Map und Ladebereiche geprüft. Am Kit: stabile Starts und CDC, ausreichende Speicherreserven, passende Chip-/Taktkonfiguration und erreichbarer DFU-Service **GEMESSEN**. |
| 3: Read-only-SD | Karte montieren, Katalog lesen, Verfügbarkeit anzeigen, Paket vollständig prüfen; JUMP noch deaktiviert. | Ohne Karte nutzbar; FAT32/exFAT nach vereinbartem Profil; Kartenentnahme, Fehler und Gesamtbudgets geprüft. Geschlossene Dateien/ungültiger Startauftrag nach jedem Abbruch. |
| 4: Genau ein RAM-Programm | Erstes kleines bekanntes Image; Kopieren, Cache-Commit, Start und Rückkehr. Keine Programmbibliothek und keine neue DSP-Kette. | Reproduzierbarer Start/Reset/Rückkehr und korrekte Initialisierung; Fehler vor Commit springen nie. Hashnachweis, Produktidentifikation und Hörtest getrennt protokolliert. |
| 5: Diagnose und weitere Produkte | Diagnoseschicht, Tastenmenü, danach Instrument/FX, anschließend speichernder Looper und CV-erzeugender Sequencer. | Kein Diagnose-Tastentest startet ein Programm. Ausgänge/Schreibabschluss funktionieren über Übergänge; Golden-/Komponententests bleiben unverändert. |
| 6: Panels | Zunächst HTML/CDC, danach JUCE Standalone, dann VST3; Instrument-MIDI und Verwaltungs-CDC als getrennte Adapter. | Wiederverbindung und Geräteidentität funktionieren; ACK ist nicht Startnachweis; kein automatischer Wechsel durch Songladen, Reconnect oder MIDI-Program-Change; mehrere Instanzen koordiniert. |

Phase 1 und der lokale Teil von Phase 2 erfordern eine spätere ausdrückliche
Anweisung zur Code-/Buildarbeit. Hardwareinstallationen folgen erst, wenn der
Mensch das konkrete Image und den passenden Bootloaderzugang angefordert hat.
Kein Hardwaretest wird durch einen erfolgreichen Hosttest als bestanden markiert.

## 5. Erster späterer Hardwarekandidat: präziser Umfang

Der erste Kandidat soll nur die bislang nicht am Kit nachgewiesene XIP-Ausführung
und die Verwaltungsbasis klären. Noch keine Auswahl von 15 Images anbieten.

- Vektortabelle am Standardslot `0x90040000`; Daten/Stack außerhalb des
  reservierten SRAM-Ziels. Das spätere Paket muss auch die oberen Mod-Reserve-
  blöcke freihalten.
- Eine einfache LED-Zustandsanzeige, CDC-HELLO mit eigener Produkt-/Buildkennung,
  reine Statusabfrage und expliziter Wechsel in den vorhandenen DFU-Service.
- Keine ungefragten CV-Testsignale, keine QSPI-Schreibfunktionen und keine
  Änderung am installierten Bootloader. Elektrischen CV-Ruhezustand nicht
  allein aus „keine Testsignale erzeugen“ als bewiesen ableiten.
- Read-only-Chipidentifikation nur über eine für die laufende XIP-Konfiguration
  geprüfte Methode. QSPI nicht während der Codeausführung versuchsweise aus
  Memory-Mapped herauskonfigurieren. Wenn das nicht sicher möglich ist, die
  Identifikation getrennt untersuchen statt in den ersten Kandidaten zwingen.
- Konkretes Image, ELF/Map, SHA-256, Quellcommit und Rückweg dokumentieren,
  bevor ein menschlich freigegebener Flashversuch beginnt.

Alle neuen Hardwarepunkte stehen derzeit auf **NICHT GEMESSEN**. Mindestprotokoll:
Datum, Bootloaderstand soweit bekannt, Imagehash, tatsächliche Beobachtung,
USB-Verhalten, Neustart-/Serviceergebnis und offene Abweichungen. Noch keine
Zahlen für Ladezeit oder elektrische Spannungen vorgeben.

## 6. Begrenzung und Übergabe

Keine Library-/Toolchain-/JUCE-Upgrades, keine Änderung der MD-/SQ-/BL-Semantik,
keine QSPI-Modspeicherung, keine automatische CI-Matrix und keine Releaseaktion.
Jede spätere Phase auf Branch/PR mit gezielten lokalen Prüfungen abschließen.
Nicht alle Phasen in einen einzigen großen Implementierungsauftrag bündeln.

**Nächste Übergabe an einen implementierenden Chat:** Konzept v2 plus dieser
Auftrag, genauer integrierter Diagnosecommit, gewünschte einzelne Phase und die
dafür geltenden Grenzen. Eine reine Konzeptübergabe enthält keine Flashfreigabe.

Status bei Erstellung: Arbeitsauftrag dokumentiert; Phase 0 noch nicht vollständig
geschlossen; Phasen 1–6 nicht gestartet. Keine Testsuites, Builds oder
Hardwareaktionen für dieses Dokument ausgeführt.
