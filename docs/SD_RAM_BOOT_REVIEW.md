# Prüfauftrag an Claude Code: SD-/RAM-Bootkonzept

**Stand 31.10.2026 · ursprünglicher Prüfauftrag, Review inzwischen durchgeführt.**

Ergebnis: [Claude-Code-Review](SD_RAM_BOOT_REVIEW_CLAUDE.md), Urteil „tragfähig mit
Änderungen“. Die Verarbeitung der Befunde steht in
[Konzept v2](SD_RAM_BOOT_CONCEPT_V2.md). Der folgende Auftrag bleibt als
historische Aufgabenbeschreibung erhalten; er ist keine Implementierungsfreigabe.

Bitte [SD_RAM_BOOT_CONCEPT.md](SD_RAM_BOOT_CONCEPT.md) unabhängig und kritisch
prüfen. Ziel ist eine begründete Architekturentscheidung, ausdrücklich auch mit
einem besseren Gegenvorschlag. Nicht lediglich das Dokument zusammenfassen.

## Auftrag zum direkten Übernehmen

> Prüfe das Konzept `docs/SD_RAM_BOOT_CONCEPT.md` anhand des tatsächlichen
> Repositorycodes und geeigneter Primärquellen. Lies vorher AGENTS.md, README.md,
> HANDOVER.md und die betroffenen Projekt-READMEs. Es geht ausschließlich um
> Konzept und Review: keine Firmwareimplementierung, Builds, Testsuites,
> Flashvorgänge, Bootloaderinstallation, CI-Dispatches oder Abhängigkeitsupdates.
> Die vorhandene Diagnose-/SD-/CV-/Gate-Arbeit läuft unabhängig weiter.
>
> Bevorzugter Entwurf ist ein interner Bootkern ohne Dateisystem, eine feste
> QSPI-XIP-Verwaltung mit modularer Diagnose und das direkte Laden eines
> SD-Anwendungsimages ins freigehaltene AXI-SRAM. Prüfe diese Präferenz ebenso
> streng wie einen internen Bootloader mit eigenem SD-Pfad. Du darfst Entwurf B
> verwerfen. Frühere Chat-Aussagen sind keine bewiesenen Voraussetzungen.
>
> Erstelle `docs/SD_RAM_BOOT_REVIEW_CLAUDE.md` mit Urteil, nachprüfbaren Befunden,
> Korrekturen, Verbesserungen und gegebenenfalls einem vollständigen
> Gegenvorschlag. Das Review darf Dateien lesen und ausschließlich Review-/
> Konzeptdokumentation schreiben. Keine Implementierung und keine vorweggenommene
> Hardwarefreigabe. Ungeprüfte Werte nicht als Messergebnisse eintragen.

## 1. Zwingende technische Fragen

1. **Board und Speicher:** Sind STM32H750, QSPI, AXI-SRAM und Backup-SRAM für
   genau diesen Seed3-Stand korrekt eingeordnet? Welche Unterschiede zum
   öffentlich verfügbaren DaisyBootloader betreffen Seed3? Welche Größe/Reserve
   lässt sich ohne neuen Build überhaupt belegen?
2. **XIP-Verwaltung:** Kann sie mit der gepinnten libDaisy direkt aus QSPI laufen,
   ohne sich bei `SystemInit`, PLL-/QSPI-Konfiguration, Cache-/MPU-Änderungen oder
   `Hardware::Init()` die Ausführung zu entziehen? Welche minimalen lokalen
   Anpassungen wären nötig, ohne die gepinnten Libraries nebenbei zu ändern?
3. **Ladeziel:** Reichen DTCM/D2/oberste 32 KiB AXI für Verwaltungsdaten, Stacks,
   FatFs, USB und DMA? Alle Abhängigkeiten berücksichtigen. Der Standard-QSPI-
   Linker belegt das Ziel bereits mit `.data/.bss` und kann nicht unverändert
   benutzt werden. Vorschlag für einen konkreten, phasenweisen Speicherplan.
4. **Reset und früher Einstieg:** Was garantieren ST RM0433, die tatsächlichen
   Startup-Dateien und der Daisy-Vorgänger über AXI-SRAM beim Software-Reset?
   Welche Resetflags, ECC-Regeln, Cacheflushes, Barrieren, Stack-/VTOR-/MSP-/
   Interruptzustände sind erforderlich? Welche Initialisierung würde das
   geladene Image vor dem Sprung zerstören? Externe SDRAM-Retention darf nicht
   vorausgesetzt werden. Quellenlücken ausdrücklich benennen.
5. **Mailbox:** Ist die vorgeschlagene Reservierung konfliktfrei mit BootInfo,
   allen `.backup_sram`-Sektionen und Startcodes? Wie werden Teilwrites, alte
   Aufträge, Watchdog-/Power-Resets und Wiederholung behandelt? Welche alte
   `JUMP`-Kompatibilität wäre unzulässig oder nur eingeschränkt möglich?
6. **Imageprofil:** Kann ein bestehender BIN-Payload unverändert starten? Was
   müssen ELF-/Map-Prüfungen über LMA/VMA, Dateninitialwerte, AXI-NOLOAD-Puffer,
   ITCM, QSPI-Abhängigkeiten, Stack und Vektortabellen zusätzlich belegen?
   Konkrete Beispiele Toy Piano, Physical Modeling und Diagnose ansehen.
7. **SD:** Passt `common/sd` zu diesem Einsatz und zum tatsächlichen aktuellen
   Integrationsstand? FAT32/exFAT, einzige FatFs-Version, IDMA-Erreichbarkeit,
   Cache, Datenlimits, fehlerhafte Dateisysteme, Gesamtbudgets und Kartenentnahme
   prüfen. Die Diagnose-Makefile-Policy verbietet heute sogar `f_open`; wie
   würde eine spätere Read-only-Migration diese Regel begründet ersetzen?
8. **QSPI-Service:** Die heutige Anwendungsadresse wird zum Verwaltungsslot.
   Welche Flashwerkzeuge wären dadurch gefährlich inkompatibel? Ist ein
   unabhängiger begrenzter J7-Service im Bootkern realistisch? Wie erfolgt ein
   Verwaltungsupdate ohne Schreiben/Löschen während XIP vom gleichen Chip?
   Single-Slot-Stromausfall und erreichbaren ROM-Recoveryweg ehrlich bewerten.
9. **Ausgänge und Speicherabschluss:** Was benötigen Looper, Sequencer und
   Diagnose vor Reset? Welche CV-/Gate-Ausgangsfunktionen existieren wirklich,
   welche nur in Dokumenten? Keine Gleichsetzung DAC-Code 0 = 0 V. Konflikte
   mit S9+S16, gehaltenen Tasten, laufenden Tests und MSC-Besitz untersuchen.
10. **Control Panel:** Wie lässt sich die neue Verwaltung modular neben DDP,
    MD, SQ und BL aufbauen? CDC/Web Serial versus USB-MIDI, JUCE-Serialadapter,
    Betriebssystemunterschiede, USB-Neuanmeldung, mehrere Geräte/Plugininstanzen
    und DAW-Echtzeitregeln behandeln. Keine automatische Geräteumschaltung bei
    Song-Laden, MIDI-Program-Change oder Wiederverbinden. Keine bereits
    implementierten HTML-/JUCE-Diagnosepanels behaupten.

## 2. Alternativen verpflichtend vergleichen

Mindestens A und B aus dem Konzept unabhängig bewerten. Wenn eine kritische
Annahme nicht tragfähig ist, einen konkreten Gegenvorschlag liefern, nicht nur
„müsste man testen“ schreiben. Ein Gegenvorschlag beschreibt:

- welches Programm aus welchem Speicher ausgeführt wird;
- wer SD liest und wohin genau geladen/kopiert wird;
- welche Bereiche gleichzeitig aktiv sind und warum sie nicht kollidieren;
- ob ein Reset nötig ist und was dabei erhalten bleiben muss;
- welche Flash-Schreibvorgänge im Alltag beziehungsweise nur im Service anfallen;
- Diagnose ohne Karte, Ausgänge, Fehlerfälle und unabhängige Wiederherstellung;
- Änderungen an bestehenden Images, Protokollen und Panels;
- die ersten später erforderlichen Nachweise, ohne sie jetzt auszuführen.

Eine Lösung darf einfacher sein und Komfort verschieben. Wenn das Ziel „kein
Flashen beim normalen Wechsel“ aufgegeben wird, dies ausdrücklich als Abweichung
markieren. Eine SRAM-Verwaltung mit SDRAM-Staging benötigt einen vollständigen
Kopier-/Trampolinvertrag; „ins RAM laden und springen“ reicht nicht.

## 3. Gewünschtes Reviewformat

1. **Urteil:** tragfähig / tragfähig mit Änderungen / derzeit nicht tragfähig;
   getrennt davon Implementierungsfreigabe: weiterhin nicht erteilt.
2. **Befundtabelle:** ID, Schweregrad, Konzeptabschnitt, konkrete Datei/Zeile oder
   Primärquelle, Begründung, notwendige Korrektur, Verifikationsbedarf. Zwischen
   belegtem Fehler, Risiko und bloß offener Frage unterscheiden.
3. **Korrigierter Architekturvorschlag:** Speicherplan und Ablauf, auch wenn die
   Grundidee bestehen bleibt. Annahmen und belegte Fakten sichtbar trennen.
4. **Vergleich A/B und gegebenenfalls Gegenvorschlag** mit begründeter Empfehlung.
5. **Nächste Entscheidungen und Nachweise:** Was muss vor jedem Bootloaderbuild
   feststehen, was kann erst später an Hardware geklärt werden?

Die ursprüngliche Konzeptdatei zunächst erhalten, damit Befunde eindeutig
zugeordnet werden können. Redaktionelle Verbesserungen und Architekturkorrekturen
im Review auflisten; bei Bedarf ein separates überarbeitetes Konzept v2 erstellen.
Keinen leeren oder simulierten Ergebnisbericht als unabhängiges Review ablegen.

## 4. Einstiegspunkte im Repository

- `common/desktop.mk`, `common/bootloader/`, `docs/FLASHING.md`
- `libDaisy/core/STM32H750IB_{sram,qspi}.lds`, `startup_stm32h750xx.s`
- `libDaisy/src/sys/system.{h,cpp}`, `system_stm32h7xx.c`
- `libDaisy/src/daisy_seed.cpp`, `desktop_devkit.{h,cpp}`
- `instruments/toy-piano/ld/toy_piano_axi.lds`
- `instruments/physical-modeling/ld/physical_modeling_axi.lds`
- `diagnostics/README.md`, `firmware/Makefile`, `firmware/src/`,
  `docs/FIRMWARE.md`, `docs/SD_CARD_READER_REVIEW.md`, `protocol/PROTOCOL.md`
- `common/sd/{README.md,PLAN.md,ro_sd.c,ffconf.h,sd.mk}`
- `scripts/check-firmware.py`, `scripts/check-sd-symbols.py`
- `editors/mod-on-daisy/README.md`, `modules/mod-on-daisy/docs/PROTOCOL.md`
- `sequencers/step-seq/docs/PROTOCOL.md`, `components/README.md`

Submodule nicht automatisch aktualisieren oder initialisieren. Falls im
Dokumentations-Worktree nicht ausgecheckt, vorhandene gepinnte Quellen im
Hauptcheckout lesend nutzen oder den Reviewstand ausdrücklich einschränken.
Im Konzept genannte Quellrevision gegen den eigenen Reviewstand vergleichen und
Abweichungen protokollieren. Aktuell existierende unbeteiligte Arbeitsdateien
anderer Aufgaben nicht ändern.
