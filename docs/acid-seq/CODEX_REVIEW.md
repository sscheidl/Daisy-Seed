# Codex Review: Arbeitsauftrag Acid Seq

VERDICT: GO MIT KORREKTUREN

Geprüft: `claude/acid-seq-work-order` @
`3b119bebb3b39c74d6523dfab4682fcb309d8f7f`, 26.10.2026.
Basisvergleich: `main` @ `1eb091c`. Gepinnte Submodule lokal bestätigt:
libDaisy `f715e487`, DaisySP `2c72eaf`.

Die Aufteilung in Sequencer, Stimme und Effekte ist tragfähig. Vor Phase 0 müssen
die Verträge und die Abhängigkeiten der Phasen berichtigt werden. Zwei Aussagen
sind bereits am vorhandenen Code beziehungsweise rechnerisch falsch: der
lock-freie DAC-Aufruf und der Speicherbedarf für den vollständigen Delay-Sync-Bereich.
Das ist kein grundsätzlicher Hardware-Nachweis gegen das Projekt.

SW17 = Start/Stop und SW18 = Modus werden als bestätigte Vorgabe übernommen.
Dem ergänzenden Vorschlag des Benutzers, beim Einschalten mit SW17 auf Run zu
starten, stimme ich zu: **interne Clock startet nach Initialisierung; externe
Clock wird scharfgeschaltet und wartet auf die für diese Quelle festgelegte
Startbedingung.** Das ersetzt noch keine Definition der Clock-Zustandsmaschine.

## Befunde

Die Zeilenangaben beziehen sich auf den geprüften Commit. P1 ist eine falsche
Annahme beziehungsweise ein Regelverstoß, P2 muss vor der genannten Phase
geschlossen werden, P3 ist ein Hinweis. Vorschläge sind keine gemessenen Ergebnisse.

| ID | Schwere (P1 blockierend / P2 vor der Phase beheben / P3 Hinweis) | Abschnitt | Befund | Beleg (Datei:Zeile) | Vorschlag |
| --- | --- | --- | --- | --- | --- |
| R01 | **P1 blockierend** | 3.2, DAC im Callback | `hw.SetDacOut` ist kein reiner Registerzugriff: `WriteValue` ruft für jeden Kanal auch `HAL_DAC_Start` auf, das `__HAL_LOCK` und `__HAL_UNLOCK` verwendet. Der HAL-Lock wartet zwar nicht, sondern liefert bei Belegung `HAL_BUSY`; der Wrapper ignoriert aber den Rückgabewert. Der Aufruf erfüllt die verbindliche Vorgabe „keine Sperren im Audio-Callback“ nicht. | `docs/acid-seq/ARBEITSAUFTRAG.md:99`; `libDaisy/src/per/dac.cpp:208`; `libDaisy/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dac.c:514`; `libDaisy/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_def.h:93` | Vor Phase 1 einen eigenen Hardwareadapter vorsehen: DAC einmal beim Start initialisieren/aktivieren; im Callback nur geprüfte, begrenzte Registerschreiboperationen einschließlich des benötigten Software-Triggers, ohne HAL-Start/Lock und mit genau einem Besitzer. Alternativ DAC-DMA mit definiertem Timing. Gepinnte Bibliothek unverändert lassen. Host-Adaptertest plus Geräteprüfung von Timing und Spannung verlangen. |
| R02 | **P1 blockierend** | 4.7, Phase 4 | Zwei Sekunden pro Kanal decken die zugesagten synchronen Notenwerte nicht ab. Bei 30 BPM dauert 1/1 bereits 8 s, punktiert 12 s. `2 × 96 000` Floats sind korrekt 768 000 B, aber nur für 2 s bei 48 kHz. Auch der Maximalzeitpunkt braucht bei interpolierter Ringpuffer-Lektüre eine festgelegte Randbehandlung. | `docs/acid-seq/ARBEITSAUFTRAG.md:262`, `:272`, `:455` | Vor Phase 0 entscheiden: Sync bis punktierter 1/1 bei 30 BPM mit mindestens 12 s je Kanal, also 4 608 000 B zuzüglich Randbedarf; oder zulässige Sync-Werte explizit begrenzen und im Panel kenntlich machen. Nicht still auf 2 s klemmen. Stereo-/Ping-Pong-Zeit als Abstand je Echo beziehungsweise je Kanal eindeutig definieren. |
| R03 | **P2 vor Phase 1** | 4.2–4.4, Clock und Transport | SW17 als Freigabe ist sinnvoll, die übrigen Zustandsübergänge fehlen. Unbestimmt sind Panel-Stop bei unverändertem Run-Schalter, Gate-Run nach MIDI Stop, Reset bei Stop, Continue-Position, Ausfall einer fest gewählten externen Clock sowie Wiederkehr nach Auto-Fallback. „Auto“ bestimmt weder Priorität zwischen Gate/USB/DIN noch die Quelle nach 2 s ohne Pulse. Ein Slide kann dabei ein Gate unbegrenzt offen halten. | `docs/acid-seq/ARBEITSAUFTRAG.md:155`, `:183`, `:185`, `:209` | Zustands-/Ereignistabelle vor Phase 1: SW17 Stop gewinnt immer; Gate-Run ist eine zusätzliche Freigabe, kein eigenständiger Startbefehl. Quellenwahl, Start/Continue/Reset, Timeout, Gate-Abschaltung, interne Übernahme und Rückkehr jeweils festlegen. Bei fest externer Quelle empfehle ich auf Ausfall Gate/Noten aus und weiter scharfgeschaltet warten; bei Auto einen ausdrücklich definierten, phasenbewussten Fallback. Gleichzeitige Quellen und Echo-Schleifen testen; empfangene MIDI-Clock nicht unkontrolliert wieder aussenden. |
| R04 | **P2 vor Phase 1** | 3.2, 4.1, 4.3, Timing | 1 kHz ist als Steuerraster plausibel, aber kein Nachweis für beliebige Clock-Pulse oder exakte kurze Gates. GateIn liest den momentanen GPIO; Pulse zwischen zwei Abtastungen gehen verloren. Bei 300 BPM ist ein Sechzehntel 50 ms, ein 5-%-Gate nur 2,5 ms; Swing verkürzt einzelne Schritte weiter. Prozent-Gates benötigen bei unregelmäßiger externer Clock außerdem eine Schätzung der noch unbekannten Step-Dauer. | `docs/acid-seq/ARBEITSAUFTRAG.md:96`, `:117`, `:120`, `:178`; `libDaisy/src/hid/gatein.cpp:13` | Mindestdauer für High und Low, zulässige Clock-Rate, Zeitquantisierung und Swing-Formel angeben. Swing 50 % als gleichmäßige Teilung eines Step-Paars erklären. Fractional-/Sample-Akkumulator statt wiederholt gerundeter Millisekunden verwenden; bei Bedarf Flankenerfassung mit Zeitstempel. Schätzung/Startwert externer Perioden und Reihenfolge bei gleichzeitigem Gate-Off/Step/Reset testen. 100-%-Gates ohne Slide brauchen eine definierte Retrigger-Lücke. |
| R05 | **P2 vor Phase 1** | 3.2, 4.4, MIDI-Ausgabe | Die feste Queue mit „Note-Off hat Vorrang“ garantiert noch keine Freigabe bei voller Queue. USB `SendMessage` meldet keinen Sendefehler; beim physisch getrennten USB kann dort kein Panic mehr ankommen. Die Angabe „3 × 100 µs“ gilt für den USB-Retrypfad, nicht für DIN: `MidiUartTransport::Tx` nutzt `PollTx` mit 10-ms-Timeout. Lange DIN-SysEx können so Clock-/Notenausgabe aus `main` verzögern. | `docs/acid-seq/ARBEITSAUFTRAG.md:46`, `:103`, `:212`; `libDaisy/src/hid/midi.h:107`, `:222`; `libDaisy/src/per/uart.cpp:1208`; `libDaisy/src/hid/usb_midi.cpp:130`; `docs/HARDWARE.md:64` | Getrennte Zustandsverfolgung pro Ausgang/Kanal, reservierter Freigabe-/Panic-Pfad und begrenzte Ausgabearbeit je Main-Durchlauf. Note-On erst zulassen, wenn seine Freigabe abgesichert ist; Wiederholungen derselben Note und Legato berücksichtigen. Stop, Kanalwechsel, Ausgang deaktivieren, Queue-Überlauf, USB-Verlust und Wiederanschluss testen. Beim USB-Verlust lokale Stimme/Gates und erreichbares DIN bereinigen; beim Wiederanschluss zuerst definiert synchronisieren. SysEx zunächst auf USB beschränken oder DIN-Fragmentierung und Bandbreitenbudget festlegen. |
| R06 | **P2 vor Phase 1** | 4.1–4.3, Pattern-Semantik | Die Folge bei Längenänderung ist nicht definiert, obwohl nur der Moduswechsel geregelt wird. Ebenso fehlen Random-Start/Reseed, Resetposition je Modus, Slide in einen ausgeschalteten Step und wiederholte Noten. „5 Oktaven“, Skalen einschließlich „linear“ und nachgeschaltete Transposition legen noch keinen eindeutigen MIDI-/CV-Notenbereich fest. Step-Vorhören kann mit einem folgenden Start oder einem weiteren Vorhörereignis kollidieren. | `docs/acid-seq/ARBEITSAUFTRAG.md:115`, `:121`, `:132`, `:147`, `:149`, `:185`, `:191`; Phase 1 `:375` | Regeln mit kurzen Sollfolgen ergänzen: Länge erst am Step-Rand übernehmen, Position außerhalb der neuen Länge definiert korrigieren; Zufall inklusive Seed/Nullseed, Reset und Wiederholungen beschreiben. Notenbasis, Skalenabbildung, Quantisierung von CV IN und Clamp-Bereich festlegen. Vorhören zeitlich besitzen/ablösen; Stop/Start darf keinen alten 150-ms-Note-Off auf eine neue Note anwenden. Tests mit Länge 1 und während einer gehaltenen Slide-Note verlangen. |
| R07 | **P2 vor Phase 1** | 4.1, Bootloader-Geste | Die Aussage „Kollision ausgeschlossen“ ist zu stark. S9 und S16 bleiben normale Step-Tasten; bei versetztem Drücken und anschließendem Loslassen kann die zweite Taste als langer Einzeldruck ausgewertet werden, wenn nur nach weiteren Tastendruck-Flanken gesucht wird. Der gemeinsame Bootloader erkennt die gehaltene Maske, unterdrückt jedoch keine Anwendungsaktion. | `docs/acid-seq/ARBEITSAUFTRAG.md:113`, `:136`; `common/bootloader/bootloader_entry.h:80`; `common/bootloader/README.md:37` | Für jede Taste merken, ob während ihrer gesamten Haltezeit irgendeine andere Taste gehalten wurde, einschließlich bereits gehaltener Tasten. Dann bis zum Loslassen beide Step-Aktionen unterdrücken. Gleichzeitiges/versetztes Drücken, Halten unter 5 s, dritte Taste, verschiedene Loslassreihenfolgen und Potibewegung während der Geste testen. Für den tatsächlichen Bootloader-Eintritt zuvor Gate/Noten definiert freigeben. |
| R08 | **P2 vor Phase 1** | 4.3, 4.8, CV-Kalibrierung | „Neutraler Code“ ist bei unbekannter Polarität keine nachgewiesene 0-V- oder Gate-Low-Stellung. Auch das Kalibrier-Bootstrapping ist offen: Die Editorseite kommt erst in Phase 5, die CV-Abnahme schon in Phase 1. Automatisches Wiederherstellen einer Kalibrierung darf nicht die Werte eines anderen Kits oder eine veraltete Aktivfreigabe übernehmen. | `docs/acid-seq/ARBEITSAUFTRAG.md:193`, `:295`, `:392`, `:476`; `diagnostics/firmware/src/diag_app.h:33` | Vor der Nutzung am externen Synth H14 samt Einschalt-/Resetverhalten durchführen und einen bekannten Gate-Low-/Ruhecode festhalten; „Ausgang gesperrt“ bedeutet bis dahin nur keine dynamische Ausgabe. In Phase 1 `sq_probe.py` mit validiertem Kalibrierungs- und Aktivierungsbefehl liefern. Gerätezuordnung, endliche/beschränkte Kalibrierdaten, Spannungsclamp, Low/High und Deaktivierung während Gate-High spezifizieren. Kalibrierdaten und Freigabe getrennt behandeln; Freigabe nach Neustart/Gerätewechsel nicht blind restaurieren. |
| R09 | **P2 vor Phase 0/1** | 4.8, D1, Phase 5 | Ein eigener SQ-Namensraum erhält MD v1, macht die bestehende Session aber nicht automatisch nutzbar. C++ `Session` verwendet direkt `mod::proto`, sendet dessen IDENTIFY und lädt danach die Matrix. Der Browser verwirft andere Köpfe. Das Audit empfiehlt einen Parametervertrag, fordert aber keinen zweiten Protokollkopf; an anderer Stelle empfiehlt es ausdrücklich, kein paralleles Effektprotokoll anzufangen. D1 sollte diese bewusste Abweichung und ihren Aufwand benennen. | `editors/mod-on-daisy/src/core/Session.h:19`, `:164`, `:196`, `:230`; `editors/mod-on-daisy/web/mod_protocol.js:11`, `:110`; `docs/MODULARITY_AUDIT_2026-10-25.md`, Abschnitte 3 und 4 | In Phase 0 den Parameter-/Komponentenvertrag festlegen, in Phase 1 SQ-Codecs, Probe und Tests. Transporte mitnutzen, aber expliziten MD-/SQ-Dispatcher und passende Sessionstrategie einplanen. Kein MD-Matrix-Pull für reine SQ-Geräte; keine SQ-spezifischen Sonderbefehle je Effekt. Schon in Phase 1 kleinen Editor-Core-Handshake-Test vorsehen, vollständige UI erst in Phase 5. MD v1 samt Echo-Schutz bleibt unverändert. |
| R10 | **P2 vor Phase 1** | 4.8, Protokoll-/Zustandsvertrag | Nachrichteninhalte sind genannt, aber entscheidende Übertragungsregeln fehlen: Maximalrahmen, Fragmentierung von Pattern/Deskriptoren, Fehler-/Retry-Verhalten, Antwort-Echos, Versions-/Schemaabgleich und atomare Übernahme. Der gepinnte Parser speichert nur 128 SysEx-Inhaltsbytes und schneidet darüber ab. Direkte Änderungen aus `main` am vom Callback gelesenen Pattern wären ein Datenrennen. | `docs/acid-seq/ARBEITSAUFTRAG.md:284`, `:291`, `:299`; `libDaisy/src/hid/MidiEvent.h:2`; `libDaisy/src/hid/midi_parser.cpp:167`; `libDaisy/src/hid/midi.h:246` | Begrenzte Rahmen mit Länge/Prüfsumme, Request-ID, Timeout, idempotenten Retries und Antwortbit definieren; Antworten nie beantworten. Pattern-/Parametertransaktionen über Queue oder Snapshot am definierten Step-/Blockrand anwenden, angenommene und angewandte Revision unterscheiden. Neustart/Quellenwechsel erkennen; kaputte, zu lange, doppelte, verlorene und mit Echtzeitbytes verschachtelte Nachrichten testen. Gemeinsame Vektoren auch für Python verlangen. |
| R11 | **P2 vor Phase 0** | 5, Unabhängigkeit und PASS | Phasen 2–4 sind nicht vollständig unabhängig. Die Auto-Level-Tabelle in Phase 3 verlangt 303-Referenzsignale aus Phase 2; alle benötigen stabile Parameter-IDs und Tempo-/Eventverträge. Die allgemeine Pflicht zu ARM-Image und Hardwareabnahme passt außerdem nicht zur ausdrücklich firmwarelosen Phase 0. | `docs/acid-seq/ARBEITSAUFTRAG.md:245`, `:315`, `:317`, `:329`, `:407` | Abhängigkeiten ausdrücklich festlegen: Phase 0 Verträge; Phase 1 Transport/Kern; danach Stimme, Drive-Grundkern und Delay parallel möglich, Auto-Level-Abnahme erst nach versionierter 303-Fixture. Alternative: vorher unabhängig definierte Referenz-Fixtures liefern. Phase 0 von Image-/Hardwarepflicht ausnehmen; für Phase 3a Vorher-Baselines vor dem Umbau erfassen. Die Hardwareprüfung bleibt beim Benutzer. |
| R12 | **P2 vor Phase 0** | 3.1, Phase 0, Register und Prüftools | Phase 0 soll Projekte `planned` lassen, zugleich aber jede Firmware als Registerprodukt eintragen. Der vorhandene Test fordert exakt `BUILDABLE_PROJECTS == products`; beides zusammen besteht nicht. Für die Einzeldatei `common/dsp/saturation.h` fehlt außerdem der konkrete Ort ihrer geforderten eigenen Version. Und `check-firmware.py` erkennt neue Imagepräfixe noch nicht: sie fallen in den Toy-Piano-Zweig und scheitern am Namen. | `docs/acid-seq/ARBEITSAUFTRAG.md:76`, `:318`, `:333`, `:335`; `tests/test_components.py:23`; `scripts/component_manifest.py:51`; `scripts/check-firmware.py:101` | Komponenten zunächst mit existierenden Versionsquellen registrieren, Produkte erst mit einem validierten Build; alternativ expliziten Planstatus samt angepasster Tests einführen. Versionsort von `saturation` nennen, z. B. eigenes Makro im Header. Die Erweiterung des Firmwarecheckers um alle neuen Produkte samt Positiv-/Negativtests als Lieferteil vor dem ersten Image aufnehmen. `sequencer` und `dsp` als kleine kontrollierte Kind-Erweiterung sind sinnvoll. |
| R13 | **P2 vor Phase 0** | Phase 0, gezielte CI | Die neuen Suites allein decken das zusammengesetzte Produkt nicht ab: `acid-seq` hat keine eigene genannte Suite/Verbraucherregel. Gemeinsame Sättigung muss auch `tape-fx`, `toy-piano-tape` und FX-Vertragstests erreichen. Neue ARM-Abhängigkeiten müssen außerdem die Submodulauswahl aktivieren, deren bestehende Liste fest kodiert ist. `test-thread` läuft gegenwärtig nicht automatisch als eigenständiger Schritt in `quick.yml`. | `docs/acid-seq/ARBEITSAUFTRAG.md:322`, `:343`; `scripts/select_test_suites.py:13`, `:17`, `:37`; `.github/workflows/quick.yml:63`; `tests/test_components.py:87` | Explizite Verbraucherregeln und Auswahltests für Sequencer, Stimme, Drive, Delay, Sättigung und Produkt ergänzen, einschließlich `submodules`. `test-fx` bei neuen Effektadaptern wählen; `acid-seq` bei Änderungen an einem seiner Kerne testen. Gezielte Linux-Threadprüfung lokal ausführen oder ausdrücklich budgetiert einplanen. Ein Linux-Job, 15-min-Limit, Abbruch überholter Läufe und Docs-Ausschlüsse bleiben erhalten. Keine ARM-Matrix; Freigabe neuer CI-Schritte bleibt beim Benutzer. |
| R14 | **P2 vor Phase 3/4** | 3.2, 4.6–4.7, FX und PASS | „Echo klingt aus, danach ist der Eingang stumm“ kehrt die nötige Spillover-Reihenfolge um: Solange neue Eingabe hineinläuft, kann der Tail nicht verlässlich ausklingen. Auto-Level misst „vor und nach der Kette“, obwohl Drive ein wiederverwendbarer Effekt sein soll. Sein Ziel „1,5 dB weniger als voller Ausgleich“ widerspricht außerdem dem PASS-Kriterium ±1 dB um unveränderten RMS, falls beide denselben Modus meinen. | `docs/acid-seq/ARBEITSAUFTRAG.md:248`, `:251`, `:271`, `:440`; `common/audio/fx_contract.h:26` | Host blendet Delay-Einspeisung beim Ausschalten zuerst aus und verarbeitet den Wet-Tail weiter; Ende, erneutes Einschalten und Schutz vor endlosem Tail definieren. Auto-Level lokal um den Drive messen, vor Delay/Dry-Wet/Limiter; Zielpegel und Messfenster je Auto-Level-Modus festlegen. Für 1,5-dB-Unterkompensation gegen dieses Ziel prüfen. Numerische Schwellwerte für Glättung, Klick-/DC-Grenze und endliche Ausgabe vor Implementierung festhalten. |
| R15 | **P2 vor Phase 1, 2 und 4** | Ressourcen, PASS und Produktzustand | Speicherprüfung erst in Phase 6 ist zu spät für die neue DIN-Nutzung und das Delay. Die allgemeine DTCM-Warnung ist berechtigt, aber `Hardware` enthält bereits den großen UART-Handler: DIN aktivieren bedeutet nicht automatisch eine zusätzliche 36-KiB-Queue. CPU-Reserve ist plausibel, jedoch unbewiesen; der LadderFilter arbeitet intern bereits vierfach oversampled. Grenzwerte für Callback-Spitzen, Stack und Queueauslastung fehlen. | `docs/acid-seq/ARBEITSAUFTRAG.md:503`; `libDaisy/src/desktop_devkit.h:32`; `libDaisy/src/hid/midi.h:255`; `DaisySP/Source/Filters/ladder.h:119`; `scripts/check-tape-elf.py:62` | Ab dem ersten jeweiligen Image DTCM-/SRAM-/SDRAM-Platzierung und statische Stackreserve per ELF prüfen; keine zweite UART-Instanz anlegen. Mit Endprodukt-Budget planen. Hardware-PASS verlangt ausdrücklich keine Callback-Fristüberschreitung im vereinbarten Stressfall, erfasste Spitzen/Stack-Hochwasser und begrenzte Queues. Das gilt mit MIDI-/Panelverkehr, Selbstoszillation, maximalem Feedback und Parameterwechseln. Keine CPU-Prozentwerte aus Hostzeiten ableiten. |
| R16 | **P2 vor Phase 5/6** | 4.8, VST3 und Produktintegration | Gespeichert werden ausdrücklich Patterns und Kalibrierung; für einen reproduzierbaren Song fehlen Stimme, Drive, Delay, Clock-/MIDI-Einstellungen und deren Schema-/Komponentenversionen. Automatisches Senden beim Verbinden braucht eine Konfliktregel gegenüber dem bereits laufenden Gerät. Außerdem konkurrieren im Produkt Sequencer und externe Noten auf Kanal 2 um dieselbe monophone Stimme. | `docs/acid-seq/ARBEITSAUFTRAG.md:295`, `:479`, `:497`; `editors/mod-on-daisy/src/plugin/Plugin.cpp:68`; `docs/MODULARITY_AUDIT_2026-10-25.md`, Abschnitt 3 | Vollständiges versioniertes Produkt-Patchformat definieren, einschließlich Offline-Restore und fehlender/inkompatibler Komponenten. Gerätebezogene Kalibrierung getrennt vom musikalischen Songzustand behandeln (R08). Für Wiederverbindung feste Lade-/Uploadregel und atomare Übernahme festlegen. Stimmenbesitz/Notenpriorität zwischen Sequencer und MIDI sowie deren Note-Off/Panic testen. Bestehende `.modpatch`-Dateien weiter lesen. |

## Entscheidungen D1–D9

| Nr. | Zustimmung ja/nein | Begründung |
| --- | --- | --- |
| D1 | **Ja, mit R09/R10** | `7D 53 51` ist ein sinnvoll abgrenzbarer Entwicklungsnamensraum, wenn MD unverändert bleiben soll. Er ersetzt keinen Parametervertrag und erfordert Firmware, Probe, C++-/JS-Sessionstrategie und Tests. Die Begründung „Editoren müssen nicht gemeinsam geändert werden“ gilt nur für das alte MD-Protokoll; SQ-Unterstützung müssen beide erhalten. Kein weiteres eigenes Protokoll für Drive oder Delay beginnen. |
| D2 | **Ja, mit R03** | Start/Stop auf SW17 und drei Modi auf SW18 bleiben wie bestätigt. Auch dem aktuellen Benutzervorschlag zum Einschalten stimme ich zu: intern sofort nach fertiger Initialisierung starten, extern zunächst scharfschalten. SW17 muss vor Audio-/MIDI-/Gate-Ausgabe stabil ausgewertet werden. Eine noch fehlende CV-Freigabe darf das interne Audio/MIDI nicht pauschal blockieren. |
| D3 | **Ja** | Drei Modi begrenzen die erste Version sinnvoll. Positions-/Längen-/Resetregeln fehlen trotzdem, siehe R06. Zusätzliche Modi erst mit ausdrücklicher Panel-/Schalterpriorität planen. |
| D4 | **Ja** | DaisySP stellt LP24 und dokumentierte Selbstoszillation bereit. Ein Moog-Ladder ist eine bewusste Klangannäherung; authentisches TB-303-Verhalten wird damit nicht nachgewiesen. Diode-Ladder später als absichtliche Klangänderung versionieren und neue Golden-Referenz begründen. |
| D5 | **Ja** | Hard/Fold zunächst ohne zusätzliches Oversampling entwickeln; Hardwarekosten und Aliasing/Hörtest dokumentieren. Vorher/Nachher-Goldens bei späterer Änderung ausdrücklich neu bewerten. Internes 4-fach-Oversampling des LadderFilters ist davon unabhängig und muss bereits im CPU-Budget stehen. |
| D6 | **Ja** | Toy Piano nicht für den Sequencer umbauen. Die vorhandene zusätzliche 20-ms-Glättung und 0,1-%-Hysterese ist in `toy_piano.cpp:13–24` nachweisbar. Eigener Test bei 1 kHz; diskrete Oktav-/Halbton-/Längenstufen brauchen zusätzlich ihre eigenen Schaltschwellen und Hysteresen. |
| D7 | **Ja, mit R02/R14** | Ping-Pong als Standardmodus eines Stereo-Delays ist eine gute gemeinsame Komponente. Stereo-L/R-Modus aber ausdrücklich zu Phase 4 einschließlich Parameter, Routing, Impulsantwort und Speicherbudget hinzufügen; bisher beschreibt 4.7 nur den Mono-Summen-Ping-Pong-Pfad. |
| D8 | **Ja** | MIT für eigenen Firmware-/DSP-Code und die bestehende AGPL-Grenze des JUCE-Editors beibehalten. Fremde Lizenz-/Copyrightvermerke bleiben erhalten, insbesondere beim Wiederverwenden von DaisySP oder Auslagern der Tape-Kurven. Ein MIT-Kopf allein ersetzt keine Herkunftsangabe. |
| D9 | **Ja** | Die Arbeitsnamen sind eindeutig genug. Projekt-ID, Komponente, Imagepräfix, Marker, Gerätekennung und Panel-Kompatibilität gemeinsam registrieren. Für den breiteren Stereo-Delay-Scope nach D7 kann die Komponenten-ID jetzt noch angepasst werden; vor Phase 1 festlegen. |

## Nachgeprüfte Repo-Fakten und ergänzende Hinweise

* **Verzeichnis und FX-Vertrag bestätigt:** `common/desktop.mk:5–6` setzt
  `DAISY_ROOT ?= ../..`; `sequencers/<id>` passt. FX-Vertrag 1.0 verlangt
  startup-only `Prepare`, begrenztes wet-only `Process`, vom Host gestellten
  Speicher und Mischungs-/Übergangs-/Ausgangsschutz im Host
  (`common/audio/fx_contract.h:26–29`). Der Auftrag trifft das grundsätzlich.
* **Register und CI-Aussagen bestätigt:** Der Validator akzeptiert aktuell nur
  die fünf genannten Kinds (`scripts/component_manifest.py:51`). Ein neuer
  `common/dsp/`-Pfad wählt aktuell tatsächlich alle Suites; `common/audio/`
  und `common/sd/` haben gesonderte Regeln. Die behauptete Erweiterung ist also
  nötig, aber einschließlich Verbraucher- und Negativtests umzusetzen.
* **Hardware-API bestätigt:** CV OUT wird über beide DAC-Kanäle bedient;
  `Hardware` bietet keine eigenen Gate-Ausgänge. SW18 wird in
  `UpdateAllControls` nicht entprellt (`desktop_devkit.cpp:95–111`);
  Nebulaes 20-ms-Zustandsmapping existiert (`instruments/granular/src/control.cpp:40–48`).
  `hw.midi` wird bei `Hardware::Init` bereits für USART1 initialisiert
  (`desktop_devkit.cpp:43–50`). Eine Nutzung zum DIN-Empfangen/Senden wurde in
  den produktiven Quellen unter `instruments`, `effects`, `modules`,
  `diagnostics`, `common` und `editors` nicht gefunden. Das ist ein
  Quellensuchbefund, kein DIN-Hardwaretest.
* **MIDI-Speicher differenziert:** Mit der gepinnten ARM-Toolchain und dem
  produktiven `-fshort-enums` ist `sizeof(daisy::MidiEvent) = 144 B`; die
  256 Queueelemente belegen **36 864 B = 36 KiB**, zuzüglich Verwaltungsdaten,
  Parser und Transport. Die nominelle FIFO-Kapazität ist nur 255 Elemente
  (`libDaisy/src/util/FIFO.h:168`). Der UART-DMA-RX-Standardbuffer ist dagegen
  nur 256 B in `DMA_BUFFER_MEM_SECTION` (`libDaisy/src/hid/midi.cpp:5–8`).
  Die ältere Angabe „zusätzlich 2 kB“ im Klassenkommentar ist kein belastbares
  Speicherbudget. Toy Pianos 88,6 % aus `docs/HARDWARE.md:73–75` sind eine
  vorhandene Dokumentationsangabe, hier nicht neu gelinkt oder gemessen.
* **SDRAM und CPU:** 768 000 B für die vorgeschlagenen zwei 2-s-Float-Leitungen
  passen grundsätzlich in die 64-MiB-SDRAM-Region des vorhandenen ELF-Prüfers
  (`scripts/check-tape-elf.py:16`). Auch die R02-Alternative mit 12 s pro Kanal
  liegt rechnerisch darunter. Tatsächliche Platzierung, Zugriffszeiten und
  Callbackreserve bleiben **NICHT GEMESSEN**. Monophone Stimme plus begrenzte
  Effekte und Sequencer ist ein plausibler Versuchsaufbau, keine CPU-Zusage.
* **Tape-Referenzen bestätigt, nicht neu ausgeführt:** Die vier Kurven liegen
  in `effects/tape-fx/src/tape_fx.h:51–64`. Tests vergleichen sie exakt mit
  MixForge (`effects/tape-fx/tests/test_tape_fx.cpp:158–168`); die TapeVibe-
  Referenzprüfung ist ebenfalls vorhanden. Phase 3a soll vor dem ersten Edit
  dieselben Compiler-/Flags-/Fixture-Baselines beider Verbraucher sichern und
  danach Tests, Ausgabehash und ARM-BIN-Vergleich wiederholen. Namespace-
  Kompatibilität über Wrapper kann bestehende Aufrufer erhalten. Diese
  Refaktorierung verlangt keine Änderung der Toy-Piano-/PM-Golden-Hashes oder
  des MD-Protokolls.
* **P3, Aussage zum Looper präzisieren:** „Looper enthält keine Sättigung“
  (`ARBEITSAUFTRAG.md:240`) ist wörtlich falsch. `SoftLimit` in
  `instruments/looper/src/looper_engine.cpp:35` wird in der Frippertronics-
  Rückkopplung verwendet (`:1008`), außerdem gibt es Clipping. Richtig wäre:
  kein eigener wiederverwendbarer Drive mit den hier ausgewählten vier Kurven.
  Am Looper muss für dieses Projekt nichts geändert werden.
* **P3, Release-Lieferweg offen:** Der vorhandene manuelle Release-Workflow ist
  auf Toy Piano festgelegt (`.github/workflows/release.yml:38`, `:56`). Phase 6
  darf ihn nicht als bereits verfügbaren Acid-Seq-Releaseweg voraussetzen.
  Produktselektiven manuellen Lieferweg später gesondert festlegen, ohne
  automatische Releases oder einen ungewollten Toy-Piano-Release auszulösen.
  Bootloader in jeder neuen Firmware und Verzicht auf Flashspeicherung sind
  im Auftrag richtig vorgesehen; die tatsächliche Einbindung ist erst mit
  Firmwarecode prüfbar.

## Tatsächlich ausgeführte Prüfungen

**GEMESSEN am 26.10.2026 (lokal, Windows; Software-/Buildprüfungen):**

| Kommando / Prüfung | Beobachtetes Ergebnis |
| --- | --- |
| `git diff --stat 1eb091c...3b119be` | Nur Arbeitsauftrag und Review-Brief, zusammen 644 neue Dokumentationszeilen. Kein Firmwarecode in diesem Branch-Diff. |
| `git submodule status` | libDaisy `f715e487`, DaisySP `2c72eaf`, keine abweichenden Submodulstände angezeigt. |
| `python scripts/component_manifest.py --validate` | PASS: 15 Komponenten, 13 Firmwareprodukte. |
| `python -m unittest discover -s tests -p test_components.py -v` | PASS: alle 7 Tests, einschließlich Register-/Produktabdeckung und Verbraucherwahl. Das prüft den bestehenden Stand, nicht die vorgeschlagene Erweiterung. |
| `python scripts/select_test_suites.py --base 1eb091c --head 3b119be` | Nur `components=true`; alle anderen Suites und `submodules=false`. |
| Direkter Aufruf von `select_suites` mit synthetischen Einzelpfaden | `common/dsp/saturation.h`: alle bestehenden Suites und Submodule; `common/audio/fx_contract.h`: toy/physical/tape/fx/components/submodules; `common/sd/ro_sd.c`: sd/components; Review-Dokument: nur components. |
| Temporärer ARM-Compile eines Größenprobes mit xPack GCC 13.2.1-1.1, `-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -fshort-enums`, anschließend `arm-none-eabi-nm -S` | Symbolgröße für ein `MidiEvent`: `0x90` = 144 B; für 256 Ereignisse `0x9000` = 36 864 B. Nur Objektdatei im temporären Verzeichnis, kein Firmwarelink und keine Geräteausführung. |
| Zeit-/Speicherrechnung mit Python | 30 BPM: 1/1 = 8 s, punktiert = 12 s. 300 BPM: 1/16 = 50 ms, 5-%-Gate = 2,5 ms. Stereo-Float-Speicher für 2 s = 768 000 B, für 12 s = 4 608 000 B, jeweils ohne Interpolationsrand. |

Keine Testsuite bestehender Produkte wurde allein wegen dieser Dokumentation
vollständig neu ausgeführt. Arbeitsauftrag und Code blieben unverändert;
dieser Bericht ist das einzige neue Review-Lieferteil.

## Nicht geprüft

* **NICHT GEMESSEN:** sämtliche Spannungen, Polaritäten, Gate-Schwellen,
  Einschalttransienten, 1V/Oct-Genauigkeit, DIN-Funktion, Clock-Jitter und
  Pulsverlust auf dem Kit; Audio, Aliasverhalten, Selbstoszillation und Auto-Level.
* **NICHT GEMESSEN:** Laufzeit-CPU, Deadline-Spitzen, Stack-Hochwasser und
  Speichernutzung neuer Firmwares. Es gibt auf dem geprüften Stand noch keine
  Acid-Seq-Implementierung, keinen vollständigen ARM-Link und kein BIN dafür.
* Nicht erneut ausgeführt: Tape-/Toy-Piano-/PM-Golden- und Referenztests,
  Sanitizer-/Thread-Suites, Browser-E2E, native JUCE-/VST3-Builds und DAW-Tests.
  Vorhandener Testcode wurde als Beleg gelesen, nicht als neuer PASS gewertet.
* Keine externe Schaltungsanalyse oder neue Prüfung von Herstellerunterlagen;
  elektrische Aussagen beruhen hier allein auf vorhandenen APIs und explizit
  noch ausstehenden Messungen.
* Keine Geräte geflasht, keine Bootloader-Kommandos ausgeführt, keine
  GitHub-Workflows oder Releases gestartet, keine Flashspeicherung eingerichtet.
