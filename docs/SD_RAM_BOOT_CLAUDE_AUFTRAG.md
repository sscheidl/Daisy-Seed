# Entwicklungsauftrag an Claude Code: Daisy-Programmverwaltung

**Auftrag vom 31.10.2026 · lokale Entwicklung, Tests und Builds jetzt beauftragt.**

## Direkt übernehmbarer Einstieg

> Entwickle die Daisy-Programmverwaltungs-Firmware nach Entwurf E aus
> `docs/SD_RAM_BOOT_CONCEPT_V2.md`. Lies diesen Auftrag vollständig sowie
> AGENTS.md, README.md, HANDOVER.md, das Konzept v2 und
> `docs/SD_RAM_BOOT_REVIEW_CLAUDE.md`.
>
> Ziel ist ein vollständig implementierter und lokal geprüfter Firmwarekandidat:
> bestehender Bootloader unverändert, Verwaltung/Diagnose aus QSPI, Auswahl von
> SD-Programmen über die Desktop-Tasten oder ein Host-Control-Panel, Prüfung und
> Laden eines BOOT_SRAM-Images ins SRAM mit anschließendem Start. Liefere auch
> Paket-/Karten-/ELF-Prüfwerkzeuge, einen CDC-Client, ein einfaches HTML-Panel und
> gezielte Tests. Stelle keine Platzhalter als fertige Funktionen dar.
>
> Dieser neue Nutzerauftrag hebt die frühere Beschränkung auf Konzeptarbeit
> für lokale Implementierung, Tests und Builds auf. Die anderslautenden Aussagen
> in den historischen Konzept-/Planungsdateien beschreiben den früheren Stand.
> Arbeite die Softwarephasen selbstständig bis zu einem reviewbaren Ergebnis ab;
> bitte nicht nach jeder Phase erneut um Erlaubnis. Wenn Hardware fehlt, entwickle
> und prüfe softwareseitig weiter und markiere die Hardwarepunkte offen.
>
> Kein Flashen, kein `program-dfu`, kein `program-boot`, keine sonstigen
> Geräte-Schreibzugriffe oder Hardwaretests ohne die ausdrücklich angeforderte
> Mitwirkung des Menschen. Keine Releaseveröffentlichung und keine teuren
> Actions-Dispatches. Die Diagnose-Fixes werden in einem anderen Chat bearbeitet;
> koordiniere über deren tatsächlich verfügbaren Quell-/Prüfstand, statt dieselben
> Fehler parallel zu korrigieren.
>
> Fable 5.1 darf optional als Coding-Assistent ausprobiert werden, falls dieser
> unter diesem Namen in deiner Umgebung verfügbar ist. Der Versuch ist keine
> Voraussetzung und keine neue Projektabhängigkeit; Vorgaben dazu in Abschnitt 12.

## 1. Verbindliches Ergebnis und Umfang

Eine eigenständige Geräte-Firmware, Arbeitsname **Daisy Program Manager**:

1. Startet hinter dem vorhandenen Daisy-Bootloader als BOOT_QSPI-Anwendung am
   Standardslot `0x90040000` und wartet auf Auswahl.
2. Besitzt einen übersichtlich getrennten Kern für Bedienung, Programmkatalog,
   Read-only-SD, Paketprüfung, RAM-Laden, Bootdienst und Hostverbindung.
3. Belegt den SRAM-Zielbereich während Verwaltung/Laden nicht mit eigenen Daten,
   Stacks, Heaps oder fremden DMA-Puffern.
4. Zeigt bis zu 15 SD-Programme über Tasten/LEDs an; Platz 16 öffnet die residente
   Diagnose. Kein Instrumentautostart nach Einschalten oder Reconnect.
5. Prüft ein ausgewähltes Paket vollständig, lädt es ins AXI-SRAM und startet es
   über den vorhandenen BootInfo-/Resetweg. Normale Auswahl/Rückkehr benötigt
   keinen Flash-Schreibzugriff.
6. Funktioniert ohne SD weiterhin für Diagnose und Service; SD-Fehler führen in
   einen bedienbaren Zustand zurück, niemals in einen Sprung auf ein Teilimage.
7. Unterstützt die geordnete Rückkehr aus unseren Anwendungen in die Verwaltung.
8. Bietet Programmliste, Start, Status und Diagnose über CDC sowie einen CLI-Client
   und ein einfaches funktionierendes HTML-Panel an.

Die vollständige Oberfläche der vorhandenen Modulations-/Sequencereditoren wird
nicht neu geschrieben. JUCE Standalone und VST3 sind nachgelagerte Clients:
ihre spätere Anbindung über dieselbe Semantik dokumentieren und den vorhandenen
Code dafür anschlussfähig halten. In diesem Auftrag sind weder deren vollständige
Diagnoseoberfläche noch ein neuer serieller VST3-Transport Pflicht. Dies im
Abschluss klar benennen. Keine vermeintlich implementierten JUCE-/VST3-Funktionen
als Platzhalter anzeigen.

MSC bleibt ein separates Diagnose-/Reader-Image. Keine gleichzeitige Freigabe
der Karte an den PC im normalen Verwaltungsbetrieb. Kein DSP-Baukasten,
kein nahtloser Wechsel und kein Ausführen mehrerer BIN-Dateien gleichzeitig.

## 2. Arbeitsstand, Branch und vorhandene Diagnosearbeit

Die Konzeptdateien liegen derzeit im Worktree:

```text
C:/Users/ssche/.codex/worktrees/sd-ram-boot-concept/Daisy-Seed
```

Sie können dort uncommitted/untracked sein. Vor Branch-/Worktreeoperationen ihren
Inhalt sichern beziehungsweise gezielt übernehmen; keine fremden Arbeitsdateien
löschen oder mit einem Reset verwerfen. Konzept und Originalreview als Quellen
erhalten. Auf einem eigenen Branch arbeiten, Vorschlag
`codex/sd-program-manager`; Integration über PR, nicht direkt nach `main`.

Der Konzeptstand `3f475477...` ist eine historische Quellbasis. Vor Umsetzung den
aktuellen passenden Integrationsstand bestimmen, insbesondere die inzwischen
verfügbaren Diagnose-/SD-Korrekturen. Den gewählten Basiscommit und eventuelle
noch ungemergte Abhängigkeiten dokumentieren. Keine Diagnosebefunde allein aus
einem Versionsnamen für geschlossen erklären. Nicht blind den alten Konzeptstand
als Produktbasis benutzen oder parallele Diagnose-Fixes überschreiben.

Submodule fehlen im Konzept-Worktree; gepinnte Quellen sind im Hauptcheckout
vorhanden. Für echte Builds einen ordentlichen isolierten Checkout mit den am
gewählten Basiscommit gepinnten Submodulen verwenden. Kein `--remote`, kein
ungefragtes Aktualisieren von libDaisy, DaisySP, xPack GCC oder JUCE. Keine Builds
gegen zufällige Header aus einem anderen Branch. Vorhandene Toolchains dürfen
wiederverwendet werden, ohne fremde Buildoutputs zu beschädigen.

Unabhängige Teile wie Parser, Paketierung, Linkercheck und XIP-Verwaltung können
entwickelt werden, während die andere Diagnoseaufgabe noch läuft. Diagnose-/
SD-Integration an deren tatsächlichen Stand binden. Wenn eine Abhängigkeit am
Ende wirklich fehlt, den Kandidaten und verbleibenden Blocker exakt benennen;
keinen unfertigen oder bekannten fehlerhaften Pfad als fertige Integration melden.

## 3. Projektstruktur und Buildintegration

Vorschlag: `system/program-manager/` mit `src/`, `ld/`, `tests/`, README und
eigenem Makefile; hostseitige Werkzeuge sinnvoll unter `tools/` beziehungsweise
`scripts/` und ein kleines HTML-Panel beim Projekt. Passe die Struktur bei guten
Repositorygründen an, ohne unnötigen Umbau.

- Projekt-ID `program-manager` in der vorhandenen Projektauswahl ergänzen, sobald
  der Build wirklich funktioniert. Bisheriges Standardprojekt nicht ändern.
- Achtung: `common/desktop.mk` setzt derzeit ausdrücklich `APP_TYPE = BOOT_SRAM`.
  Nicht blind für die Verwaltung übernehmen. Projektlokales BOOT_QSPI-Makefile
  beziehungsweise eine eng begrenzte Lösung verwenden; vorhandene Images nicht
  durch eine globale Buildänderung versehentlich anders linken.
- Für Verwaltung eigenes Versions-/Produktmerkmal und Capability-Angaben führen.
  Hardwarestatus ausdrücklich experimentell/ungetestet bis zu realen Befunden.
- Zwei reproduzierbare Firmwareprofile liefern: **probe** (LED/CDC/Service ohne
  SD-Start) und **manager** (vollständige Version 1). Getrennte Buildverzeichnisse
  und eindeutige Dateinamen; keine veralteten Artefakte miteinander vermischen.
- Bestehendes `scripts/check-firmware.py` prüft BOOT_SRAM und darf für QSPI nicht
  still abgeschwächt werden. Einen passenden BOOT_QSPI-/Verwaltungscheck ergänzen.
- Keine gewöhnliche Build-/Testaktion darf ein angeschlossenes Gerät flashen
  oder automatisch einen Hostbefehl mit Neustart-/Ausgangswirkung senden.

## 4. Speicher- und Startvertrag zuerst implementierbar festlegen

Schreibe vor dem eigentlichen Loader einen knappen verbindlichen Vertrag mit
Quellen und prüfbaren Grenzen. Kritische Quellenlücken aus Claude §5.3 mit
passenden Primärquellen schließen; fehlende Fundstellen nicht durch Sicherheit
aus dem Gedächtnis ersetzen. Keine beiläufige Libraryänderung als Abkürzung.

### QSPI und RAM

- Vektoren/Code/Rodata/Init-Arrays der Verwaltung nach QSPI ab `0x90040000`;
  initialisierte Daten nach DTCM mit passender QSPI-Ladeadresse; `.bss`/Stack nach
  DTCM, Heap und Peripheriepuffer in geeignete separat geprüfte Bereiche.
- `0x24000000` bis exklusiv `0x24078000` ist das 480-KiB-Imageziel. Alle aktiven
  Verwaltungsintervalle einschließlich Bibliotheksglobals, Stack-/Heapgrenzen
  und DMA-Zielen müssen es freihalten. Den Linker und ELF-/Map-Prüfer dafür nutzen.
- Die oberen 32 KiB AXI sind ein Hilfsbereich. SDMMC1-IDMA-Bounce nur in geeignetem
  AXI oder nach Initialisierung im SDRAM, ausgerichtet auf 32 Byte mit eigenen
  Cachezeilen. Kein IDMA in DTCM oder ungeprüft in D2.
- Mit dem Bounce-Treiber können logische FatFs-/Katalogpuffer dagegen in DTCM
  liegen. Zwischen CPU-Kopierziel und tatsächlichem DMA-Ziel unterscheiden.
- Vorhandene obere QSPI-Mod-Reservierungen ab `0x907E0000` und die ersten
  256 KiB QSPI unberührt lassen. Verwaltungsimage/Löschumfang hostseitig begrenzen;
  der unveränderte DFU-Server erzwingt diesen Projektvertrag nicht.
- SDRAM nicht als über Reset erhaltenes Image-Staging verwenden.

### Laden und Commit

- Bestehenden Bootloader erhalten. Der tatsächlich kompilierte C-Startcode
  `startup_stm32h750xx.c` und der vorhandene JUMP-Pfad sind maßgeblich, nicht die
  auskommentierte Assemblerdatei.
- Den vollständigen geerbten Vertrag berücksichtigen: VTOR/MSP, Resetvektor,
  FPU/CPACR, BootInfo-Version und Backup-SRAM-Zugang. Kein eigenes direktes
  Anspringen von `main()` und kein unnötiger neuer Bootkern.
- JUMP während Laden/Fehler ungültig halten; Ziel kontrolliert vorbereiten,
  exakt begrenzte Payloadbytes lesen/kopieren, Hash und Vektoren prüfen.
- Einen nicht sektorgroßen letzten Dateiblock korrekt behandeln; Padding nicht
  versehentlich mitkopieren oder mit dem Payloadhash vermischen.
- Nach Peripherieabschluss das vorbereitete Ziel aus dem D-Cache ins physische
  RAM zurückschreiben, Cachebereich korrekt aufrunden, Barrieren setzen. Nicht
  bloß invalidieren. Kein Interrupt/DMA darf danach das Ziel verändern.
- Erst Ziel setzen, JUMP zuletzt, dann unmittelbar direkter Software-Reset.
  Nicht danach `ResetToBootloader()` rufen, weil dies den Status überschreibt.
- Optionaler zusätzlicher Backup-Block nur für Verwaltungsmetadaten; keine
  erfundene Prüffunktion des Stock-Bootloaders daraus ableiten.
- Keine neue Hash-/Resetursachenprüfung nach dem Reset versprechen: der Stock-
  Bootloader bietet sie nicht. Unterbrechungs- und Commitrisiken dokumentieren.

## 5. Read-only-SD, Paket und Kartenwerkzeuge

`common/sd` nach Prüfung des dann tatsächlichen Integrationsstands verwenden.
Genau eine FatFs-Version pro Firmware; Read-only-R0.16 mit vorhandenen Patches,
FAT32/exFAT nach diesem Treiber, kein gleichzeitiges libDaisy-FatFs-R0.12c.
Block- und Gesamtzeitbudgets einschließlich unterlagerter Fehler-/Abortpfade
belegen; keine unbegrenzten HAL-Aufrufe hinter scheinbar begrenzter Hauptschleife.

Formate konkret spezifizieren und implementieren:

```text
/DAISY/catalog.json
/DAISY/PROGRAMS/<product-id>/<image-id>.dsy
```

- `.dsy`: begrenzter versionierter Header mit fester Byteordnung, Hardware-/
  Profilkennung, Produkt-ID, Variante/Version, Payloadlänge und eindeutigem
  SHA-256-Hashbereich; ein zusammenhängender BOOT_SRAM-Payload, keine beliebigen
  Zielsegmente, Kompression oder Relokation.
- Katalog: bis 15 SD-Plätze plus residente Diagnose. Grenzen aus v2 als Basis;
  IDs, Generation, Pfade, Hashes, fehlende Dateien und inkompatible Images
  eindeutig behandeln. Keine freien Hostpfade oder Sprungadressen.
- Hostwerkzeug paketiert nur nach erfolgreicher ELF-/Map-/BIN-Prüfung. Zusätzliche
  QSPI-Ladesegmente, falsche Vektoren/MSP, zu große Images und ungeprüfte
  NOLOAD-Abhängigkeiten ablehnen. Nicht alle BINs pauschal kompatibel nennen.
- Kartenprüfer untersucht vor Einlegen auch das **Wurzelverzeichnis**. Kein
  Dateiname mit `.bin` in beliebiger Groß-/Kleinschreibung dort zulässig.
  Hintergrund: vorgeschalteter Stock-Bootloader kann vor der Verwaltung flashen.
  Eine Warnung der Verwaltung ist kein Schutz für den aktuellen Boot.
- Werkzeugausgaben zunächst in einen normalen lokalen Ausgabepfad erzeugen.
  Keine automatische Suche nach Wechseldatenträgern, kein Formatieren und kein
  Schreiben auf eine reale SD-Karte ohne konkrete Geräteanweisung des Menschen.
- Keine Logs, Auswahlzustände, Zähler oder Umbenennungen durch den normalen
  Firmware-Ladepfad auf SD/Flash schreiben. Anwendungsdaten bleiben getrennt.
- SHA-256-Implementierung mit bekannten Vektoren prüfen. Lizenz/Abhängigkeit
  bewusst wählen und dokumentieren; kein Hash als Herkunftssignatur ausgeben.

## 6. Bedienung und residente Diagnose

Modi mindestens `PROGRAM_SELECT`, `DIAGNOSTICS`, `LOADING`, Serviceanforderung.
Nur der Auswahlmodus interpretiert Einzeltasten als Programmwahl. Belegte Plätze
über LEDs anzeigen; Lade-/Fehlerzustände eindeutig darstellen. Nach Hardwareinit
alle bisherigen Tasten freigeben lassen; ein gültiger einzelner Press-/Release-
Zyklus wählt aus. Mehrere gedrückte Tasten dürfen keinen zufälligen Start auslösen.

Platz 16 aktiviert Diagnose ohne SD. Vorhandene bewährte portable Diagnosebausteine
wiederverwenden und als separate Schicht anbinden. Tasten-/LED-/Poti-/Schalter-,
SD- sowie vorhandene CV-/Gate-Funktionen erhalten, soweit der gewählte Basisstand
sie tatsächlich implementiert. Nicht implementierte Hardwarefunktionen ehrlich
kennzeichnen. Kein vollständiges Duplizieren der Diagnose in jedes Instrument.

Das Kit hat zwei CV-Ausgänge und keine dedizierten Gate-Ausgänge. Gate 1/2 sind
Eingänge. Keine Spannungswerte ohne Messung ableiten. CV-Testsignale nur nach
ausdrücklichem Diagnosebefehl in gültiger Session; Ende bei Sessionverlust,
Modus-/Programmwechsel und Service. Ruhezustand als Code dokumentieren,
elektrische Wirkung **NICHT GEMESSEN**, bis der Mensch sie berichtet.

Keine Diagnose-Tasteneingabe darf zugleich ein Programm laden. Keine SD-/USB-/
LED-Transfers, Hashberechnung, Allokation oder blockierenden Aufrufe im Audio-
Callback. Bedien-/Diagnosetakt entsprechend den bestehenden Regeln planen.

## 7. Gemeinsame Rückkehr aus Anwendungen

Einen kleinen wiederverwendbaren Dienst neben dem bestehenden Bootloader-Einstieg
entwickeln. Kein Ersetzen von S9+S16, BL-SysEx oder DDP BOOT; diese bleiben
`INF_TIMEOUT`/Service. Rückkehr zur Verwaltung benutzt `DAISY_SKIP_TIMEOUT`.

Neue Tastenkombination anhand der bestehenden Belegungen wählen und mit
Entprellung/Haltedauer/freigegebenen Tasten testen. Routineentscheidungen selbst
treffen und dokumentieren; nur bei einem echten unauflösbaren Bedienkonflikt
gezielt nachfragen. Host-Rückkehrbefehl in klar getrenntem Namensraum/Capability.

Vor Reset: Audio geordnet beenden, relevante MIDI-Noten abschließen, CV-Ruhezustand
herstellen und laufende Kartenschreibvorgänge sicher abschließen. Bei Speicher-
fehler nicht still Datenverlust und sofortigen Reset als Erfolg behandeln.

Alle aktuell implementierten eigenen Produkt-/Variantenpfade inventarisieren.
Rückkehrdienst nach Möglichkeit konsistent integrieren, auch reguläre und MOD-
Builds. Insbesondere ein Instrument, ein Effekt, Looper und CV-erzeugender
Sequencer müssen gezielt betrachtet werden. Keine Mod-on-Daisy-Abhängigkeit in
reguläre Images einführen. Betroffene Callback-/Host-/Golden-Tests laufen lassen.
Alte unveränderte BINs können bei bestandenem Imageprofil weiter paketierbar
sein; fehlende Rückkehrfunktion dann als Legacy-Einschränkung anzeigen.

## 8. CDC-Protokoll und erste Hostoberfläche

Bestehendes DDP-Framing/CRC und dessen Regeln für HELLO/caps als Basis nutzen;
Wireformat und Befehle mit Firmware/Host/Testvektoren zusammen spezifizieren.
Keine stille Umdeutung bestehender Befehle. Semantisch mindestens:

- Fähigkeiten und Produkt-/Buildkennung;
- Programmkatalog mit Generation und kompatiblen Einträgen;
- Startauftrag mit ID, erwartetem Hash, Generation und Request-ID;
- Lade-/Fehlerstatus, Diagnosemodus und Serviceanforderung.

Aufträge wiederholungssicher machen; doppelte oder veraltete Startbefehle dürfen
nicht nach Reconnect erneut wechseln. Paginierung/Framegrenzen und Timeouts
eindeutig. Ein ACK ist Annahme, kein Startnachweis. Produkt-/Versionsidentifikation
nicht mit tatsächlich gemessenem Imagehash gleichsetzen. Backup-Metadaten sind
ebenfalls keine unabhängige Runtime-Hashmessung.

CDC-CLI: zunächst rein lesende Verbindungs-/Status-/List-Funktionen, explizite
Start-/Diagnose-/Servicebefehle. Keine automatischen Schreib-/Neustartbefehle
bei Discovery. Tests mit Fake-Transport, kein unaufgeforderter Zugriff auf Kits.

Ein einfaches HTML-Control-Panel mit Verbindung, tatsächlicher Programmliste,
Startschaltfläche, Status/Fehlern und Diagnoseanzeige/-bedienung liefern.
Browserverfügbarkeit und Berechtigungen für den gewählten CDC-Zugang prüfen;
korrekte Hoststartanleitung und Simulator nur ausdrücklich als solchen anbieten.
Kein Demo-Erfolg anstelle einer implementierten Geräteverbindung.

USB-Wechsel CDC/MIDI und mögliches DFU-Zwischengerät behandeln. Geräteidentität
erneut prüfen, COM-/MIDI-Portnummer nicht als stabile ID benutzen. Ohne passende
ID bei mehreren Geräten explizite Auswahl statt unsicherer automatischer Zuordnung.
Beim Wiederverbinden/Songladen/Transportstart/Program-Change niemals automatisch
Firmware wechseln. Die spätere JUCE-/VST3-Steuerung läuft außerhalb des DAW-
Audio-Callbacks und braucht eine eindeutige Instanzzuständigkeit.

## 9. Softwarephasen – bis zum Kandidaten weiterarbeiten

| Phase | Liefergegenstand | Softwareabschluss |
| --- | --- | --- |
| 0 | Aktuelle Basis, Quellen-/Start-/Speichervertrag, Formate | Konkrete implementierbare Regeln; unverifizierte Hardwareannahmen getrennt. |
| 1 | Portable Parser, Paketierung, Karten-/ELF-Prüfer, Zustandsautomat | Positive und negative Tests, keine Gerätezugriffe. |
| 2 | BOOT_QSPI-probe mit eigenem Linker, CDC/LED/Service | ARM-Build, Vektoren, Speicher-/Symbolchecks und reproduzierbarer Imagehash. |
| 3 | Read-only-SD, Katalog und paketprüfender Manager | Fake-Karte/Transport, Fehler/Entnahme/Timeouts und ungültiger Commit getestet. |
| 4 | RAM-Startpfad und Rückkehrdienst | Produktionspfad mit Hostabstraktion durchgetestet, ARM-Kandidat, Integrationen/Goldens geprüft. |
| 5 | Residente Diagnose, CDC-CLI und HTML-Panel | Protokoll-/Session-/UI-Tests; keine vorgetäuschten Hardwarebefunde. |
| 6 | Integrationsprüfung und Übergabe | Vollständiger Softwarebericht, offene Hardwareliste, Artefakte und PR. |

Zwischenstände sinnvoll committen und prüfen; nicht wegen eines fehlenden Kits
nach Phase 2 aufhören. Hardwareabhängige Annahmen sichtbar halten und keine
gefährliche Abkürzung als getestete Funktion ausgeben. Eine neu entdeckte echte
Architekturunmöglichkeit mit konkreter Quelle erklären und gegebenenfalls einen
kleineren tragfähigen Gegenvorschlag machen. Kein grundlegender Wechsel auf
eigenen Bootloader/Flash-bei-jeder-Auswahl ohne Rücksprache.

## 10. Pflichtprüfungen

Tests müssen Fehlerverhalten und Grenzen prüfen, nicht bloß Implementierung
nachzeichnen. Mindestens:

- Parser: trunkierte Header, Überläufe, falsche Profile/Hashes, Grenzen,
  Traversal/unerlaubte Pfade, doppelte Plätze/IDs und veraltete Generationen.
- Loader: Teilreads, letzte Nutzbytes, Fehler/Entnahme/Timeout vor und nach
  Datenübernahme, Abbruch bleibt ohne JUMP, Reihenfolge von Peripherieabschluss,
  Cache-Clean, Barrieren und Commit. Produktionslogik mit instrumentierbaren
  Schnittstellen testen; Registereffekte nicht als durch Fake bewiesen ausgeben.
- Speicher: ELF-/Map-Grenzen einschließlich Stack/Heap, keine Kollision zum
  Ziel, korrektes QSPI-Limit und passende SD-IDMA-Puffer. Eigenständige Checks
  müssen eine bewusst ungültige Testdatei erkennen, nicht nur PASS ausgeben.
- SD: genau eine FatFs, Read-only-Symbolpolitik, tiefe Fehler-/Abortpfade und
  echte Gesamtbudgets; vorhandene Shared-SD-Tests gezielt wiederverwenden.
- Rückkehr: zusätzliche/ungehaltene Tasten, Zeitzählerüberlauf, Echo, unvollständige
  Frames, Doppelanforderung, Speicherabschluss/Fehler und bisheriger Servicepfad.
- Host/HTML: Frame-/Sessionfehler, Reconnect, doppelte Requests, falsches Gerät,
  Busy und Versions-/Capabilitygrenzen; keine automatischen Starts.
- Klang: bestehende Golden-Hashes bei allen betroffenen Klangpfaden unverändert.
  Geänderte Firmwarebytes durch den neuen Dienst nicht als Klangänderung deuten.

Normale Hosttests und ASan/UBSan für neue C/C++-Parser/-Zustandslogik ausführen.
ARM-Builds beider neuer Profile mit gepinnter Toolchain, betroffene Diagnose-/
Instrument-/Python-/Browserprüfungen gezielt ausführen. TSan nur bei tatsächlich
betroffener Thread-/Queue-Logik, keine pauschalen Vollprojekt-/Release-Suites.
Vorhandene CI-Budgetpolitik beachten: keine teuren Workflows dispatchen, keine
neuen breiten automatischen ARM-/OS-Matrizen. Erweiterung bestehender kurzer
Hosttests nur gezielt und im gleichen Budgetrahmen.

Ergebnisse nachvollziehbar mit Datum, vollständigen Befehlen, Exitcodes,
Quellcommit, relevanten Versionen und Artefakthashes sichern. Nur wirklich
ausgeführte Prüfungen als bestanden angeben. Hardwarepunkte bleiben
**NICHT GEMESSEN**. Keine neue Freigabe von Mod-Flashspeicherung und kein Release.

## 11. Fertige Übergabe und PR

Zum Abschluss liefern:

1. Implementierter Quellcode und Projektintegration, keine als fertig
   deklarierten TODO-/Demo-/Simulatorpfade.
2. Probe- und Manager-BIN/ELF/Map mit SHA-256 und eindeutigem Quellstand;
   reproduzierbarer lokaler Buildweg. Artefakte nach Repositoryregeln ablegen,
   nicht unnötig Binaries in Git aufnehmen.
3. Werkzeuge zum Prüfen/Paketieren geeigneter Firmware und Vorbereiten einer
   **lokalen** Kartenvorlage; keine fremden Samples oder Benutzerdateien bündeln.
4. CDC-CLI und einfaches HTML-Control-Panel, echte Protokollimplementierung und
   klar gekennzeichnete Simulation für Entwicklung/Tests.
5. Deutsche README mit Speicher-/Startvertrag, Kartenregel, Bedienung,
   Verwaltungsupdate versus SD-Programmupdate und manueller Wiederherstellung.
6. Datiertes Softwareprotokoll sowie kompakter Hardwarelaufzettel: erster
   XIP-Start, SD/RAM-Wechsel, Rückkehr, Ausgänge und Recovery jeweils zunächst
   **NICHT GEMESSEN**. Keine Spannung/Ladezeit als erwartet gemessen ausfüllen.
7. Liste auditierter Produkte/Varianten samt Imageprofil-/NOLOAD-/Rückkehrstatus,
   verbleibende Kompatibilitätsgrenzen und bewusst nachgelagerte JUCE-/VST3-Arbeit.
8. Reviewbarer PR mit Problem/Verhalten, Validierung, Grenzen und offenen
   Hardwarepunkten. Ohne ausdrücklichen Auftrag nicht mergen oder veröffentlichen.

Der Hardwarelaufzettel muss den ersten autorisierten Flashversuch mit genauem
Imagehash und bekanntem Wiederherstellungsweg ermöglichen. Keine Aufforderung,
blind einen Bootloader zu ersetzen. Ein beschädigtes Verwaltungsimage kann trotz
scheinbar gültiger Vektoren hängen; automatische DFU-Recovery nicht garantieren.

## 12. Optionaler Versuch mit Fable 5.1

Der Nutzer bezeichnet **Fable 5.1 als Coding-Assistent von Claude** und möchte
ihn bei Bedarf ausprobieren. Das ist die gewünschte Bezeichnung, keine hier
verifizierte Aussage über Produktverfügbarkeit oder Modellfunktionen.

Falls in deiner Umgebung verfügbar, einen begrenzten Versuch machen, etwa
negative Tests für den Paketparser oder ein unabhängiges Review der Speicher-/
Cache-Commit-Prüfungen. Nur selbst kontrollierte Änderungen übernehmen, dieselben
Tests ausführen und kurz festhalten, wofür der Assistent verwendet wurde und
welche Fehler/Korrekturen daraus entstanden. Ein einzelner Versuch ist kein
allgemeiner Modellbenchmark.

Keine Installation, kein Upgrade, kein zusätzlicher kostenpflichtiger Dienst
und keine neue Projektabhängigkeit allein dafür. Wenn nicht verfügbar oder
anders benannt: knapp berichten und die Entwicklung mit der vorhandenen
Umgebung fortsetzen. Nicht automatisch einen anderen Dienst als „Fable 5.1“
ausgeben. Keine Hardware-/Flashentscheidung an einen Coding-Assistenten delegieren.

## 13. Vorrang und Auftragsgrenze

Dieser Auftrag ist die aktuelle Nutzeranweisung für Claude zur Entwicklung.
Historische Sätze „keine Implementierung/Builds freigegeben“ aus Konzept v1/v2,
Review oder altem Arbeitsauftrag begrenzen die **nun beauftragte lokale** Arbeit
nicht mehr. AGENTS.md und die Hardware-/Release-/Budgetgrenzen gelten weiter.

In diesem Codex-Chat wird der Auftrag erstellt; die Firmwareimplementierung
selbst wird damit nicht als bereits ausgeführt behauptet. Der ursprüngliche
Claude-Review bleibt als eigenständiges Dokument unverändert.
