# Modularität und Versionierung: Bestandsaufnahme Rings/Torus

**Geprüft am 25.10.2026.** Repository `sscheidl/Daisy-Seed`, Ausgangspunkt
`main` bei **`a0746637b968efb8bc21e63b126395b7bfe684ca`** (PR #35).
Die Prüfung betrifft Rings/Torus, Mod on Daisy, die Editoren und die gemeinsamen
Build-/Versionsgrenzen. Andere Instrumente und Ambient Reverb wurden hinsichtlich
ihrer Einbindung und Metadaten verglichen, nicht vollständig als DSP auditiert.
Diese Änderung enthält nur Dokumentation; Klang, Firmware und Protokoll bleiben
unverändert. Ergebnisse unten trennen Codebefunde, vorhandene Geräteprotokolle
und neue Host-/Build-Prüfungen.

## Ergebnis

**Die modulare Modulations-/Editor-Architektur existiert bereits und sollte
weiterverwendet werden.** Ein allgemeiner Baukasten für frei kombinierbare
Instrumente und Effekte ist dagegen noch nicht umgesetzt. Das entspricht der
[bestehenden Idee](MODULAR_KERNEL_IDEA.md) und [Issue #31](https://github.com/sscheidl/Daisy-Seed/issues/31).
Ein zweites Editor-Protokoll oder eine zweite Modulationsmatrix wäre unnötig.

| Bereich | Im Code bestätigt | Grenze |
| --- | --- | --- |
| Instrument-DSP | `physical_modeling::Synth` ist von `desktop_devkit::Hardware` getrennt und auf dem Host testbar | Seine öffentliche Schnittstelle ist projektspezifisch; kein gemeinsamer Instrument-Vertrag |
| Reverb in Torus | Ein globaler Wet-Reverb außerhalb der Stimmen; Ausklang bleibt beim Modellwechsel erhalten | `Synth` besitzt weiterhin direkt `torus::Reverb` und dessen Buffer; kein austauschbarer FX-Slot |
| Mod on Daisy | Eigenständiger C++17-Header-Kern, hardwarefreier Controller/Engine, separater Desktop-Adapter | 8 instrumentspezifische Zielspalten; kein allgemeines Parameterregister für Instrument plus FX |
| Zwei Anwendungen | Toy Piano und Physical Modeling verwenden denselben Mod-Kern | Instrumentenspezifische Einbindung in ihren `main.cpp`; noch kein gemeinsamer Firmware-Host |
| Firmwarevarianten | Reguläres Torus und `MOD=1` mit getrennten Build-Verzeichnissen und Images | Nur diese feste Option; kein implementiertes `INSTRUMENT=… FX=…` |
| Editoren | Browser, JUCE-Standalone und VST3 vorhanden; JUCE-App und Plugin teilen UI und Session | Control Panel für die Modulationsmatrix, noch kein vollständiges Instrument-/FX-Panel |
| Zustandsvertrag | IDENTIFY, Fähigkeiten, Zielbeschreibungen, Schema-Hash, Patch-CRC, Wiederholungen und Monitor | Keine Beschreibung aller eingebundenen Komponenten, keine DSP-Anwendungsrevision im ACK |
| Versionierung | Alle Quellen im Git; Instrumentheader, Mod-Version, Protokoll- und Dateiformatversion vorhanden | Unabhängige fachliche Versionen sind nicht durchgängig in UI, BIN und Release-Metadaten abgebildet |

Belege: [Synth](../instruments/physical-modeling/src/physical_modeling.h),
[Signalweg](../instruments/physical-modeling/src/physical_modeling.cpp),
[Mod-Kern](../modules/mod-on-daisy/include/mod_on_daisy/module.h),
[Desktop-Adapter](../modules/mod-on-daisy/adapters/mod_on_daisy_desktop.h),
[Torus-Einbindung](../instruments/physical-modeling/src/modulation.h),
[Buildvarianten](../instruments/physical-modeling/Makefile).

## Befunde für die Weiterentwicklung

### 1. Fachliche Versionskennung und Artefaktkennung laufen auseinander

**BESTÄTIGT:** `MakeModInfo()` meldet `PHYSICAL_MODELING_VERSION "-mod4"`.
`src/version.h`, der eingebettete Firmwaremarker und der Dateiname verwenden
weiterhin nur `0.1.0`: `PhysicalModelingMod-v0.1.0.bin`. Verschiedene mod-Stände
tragen damit denselben Dateinamen. Eine eindeutige Identifikation ist heute über
Git-Commit/SHA-256 und anschließend IDENTIFY möglich, aber nicht allein anhand
des Produktversionsnamens. Auch der Firmwarechecker prüft nur die Basisversion.

| Gegenstand | Aktueller Versionsstand / Quelle |
| --- | --- |
| Torus regulär | `0.1.0`, `instruments/physical-modeling/src/version.h` |
| Torus mit Mod | Gerätekennung `0.1.0-mod4`, Image/Marker `0.1.0` |
| Toy Piano | `0.3` |
| Ambient Reverb | `0.2` |
| Nebulae | `0.1.0` |
| Looper | `0.3.0` |
| Mod on Daisy | `0.1.0`, `types.h`; Erweiterungen über Fähigkeiten und Build-Flags |
| Mod-Protokoll | `1.0`, `protocol.h` |
| Binärer Mod-Patch | Format 1 bzw. 2; Dateihülle `.modpatch` Version 1 |
| JUCE-App und VST3 | Beide CMake-Projektversion `0.1.0` |
| Browser-Panel | Kein eigener Editor-Buildversionsmarker gefunden; Git versioniert den Stand |
| Torus-Reverb als eigene Komponente | Upstream-Provenienz vorhanden, keine unabhängige Effektversion |

**ENTSCHEIDUNGSVORSCHLAG:** Jede wiederverwendbare Komponente bekommt eine
stabile ID und eigene Version. Produktfirmware erhält zusätzlich eine eindeutige
Produktversion, Buildvariante und Git-Revision. Protokoll-, Parameter-Schema- und
Patchformatversion bleiben eigene Größen. Bestehende akzeptierte Images werden
dadurch nicht nachträglich verändert oder umbenannt.

### 2. Der FX-Kern ist abtrennbar, die Komposition fehlt noch

**BESTÄTIGT:** In `physical_modeling.h` liegen `part_`, `reverb_` und
`reverb_buffer_` in derselben `Synth`-Klasse. `Render()` führt unmittelbar die
Reverb-Setter, Wet-Verarbeitung, Dry/Wet-Mischung und Ausgangsschutz aus.
Das globale Reverb ist damit bereits von den Stimmen getrennt, aber noch fest
mit dieser Anwendung verbunden.

Ambient Reverb besitzt bereits hardwarefreie Klassen (`AmbientReverb`,
`WetPath`) und explizit übergebenen Speicher. Es ist ein guter zweiter FX-Pilot.
Eine gemeinsame Schnittstelle und eine Verbindung Torus → Ambient Reverb bzw.
Toy Piano → Ambient Reverb sind auf dem geprüften `main` nicht implementiert.
Der Torus-Audiocallback verwendet auch keinen externen Audioeingang als FX-Quelle.

**ENTSCHEIDUNGSVORSCHLAG:** Zunächst Komposition beim Bauen, mit kleinem
Instrument-Ausgang und FX-Stereo-Ein-/Ausgang, explizitem Speicher und definierter
Samplerate/Blockgröße. Hardware, MIDI und Editor bleiben außerhalb des DSP.
Dry/Wet-Mischung, Pegelschutz und Tail-Verhalten müssen einen eindeutigen Besitzer
bekommen, damit beim Kombinieren kein Dry-Signal doppelt gemischt wird. Ein
Laufzeit-Lader oder allgemeines Plugin-Framework ist dafür nicht nötig.

### 3. Das vorhandene Panel steuert die Matrix, nicht sämtliche Klangparameter

**BESTÄTIGT:** `TargetInfo` beschreibt Name, Modulationshub und Stimme/Anschlag.
`GET_TARGET` liefert keine vollständige Liste aller Instrument-/FX-Parameter
mit Datentyp, Einheit, Min/Max und Standardwert. Grundwerte kommen teilweise im
Monitor; direkte Klangparameter werden separat über die Instrument-CCs bedient.
Die aktuelle Ziel-ID ist die lokale Spaltennummer 0–7.

Das VST3 ist eine Gerätefernsteuerung mit unveränderter Audiodurchleitung.
Es enthält weder Torus- noch Ambient-Reverb-Audio-DSP. App und Plugin teilen
`EditorComponent` und Session. Der Browser implementiert dasselbe Mod-Protokoll
in JavaScript; gemeinsame Testvektoren prüfen die Übereinstimmung mit C++.

**BESTÄTIGT:** Der VST3-Songzustand enthält `.modpatch`-Text. Dieser speichert
Matrix, Quellen, Namen sowie Instrument-/Schema-/Spaltenherkunft, aber keinen
vollständigen Instrument- oder Reverb-Patch mit allen Grundparametern.

**ENTSCHEIDUNGSVORSCHLAG:** Bestehende Session und Transporte weiterverwenden.
Für vollständige Panels einen versionierten Parametervertrag ergänzen. Parameter
erhalten Komponentenzugehörigkeit und stabile IDs; die 8 Matrixspalten bleiben
eine begrenzte Auswahl von Modulationszielen. Instrument und FX nicht still in
dieselben acht Spalten umdeuten. Das neue Song-/Patchformat muss die ausgewählte
Zusammenstellung und deren Klangparameter ausdrücklich speichern.

### 4. Bestätigter Patch und tatsächlich übernommener DSP-Stand sind verschieden

**BESTÄTIGT:** `Controller::Commit()` validiert/kompiliert im Main-Kontext und
veröffentlicht ein Program über den Triple-Buffer. `Change()` bestätigt dessen
Patch-CRC. Die Audio-Engine holt es erst in ihrem nächsten `Tick()` ab. Das ACK
belegt den akzeptierten/veröffentlichten Patch; es trägt keine vom Audiopfad
bestätigte Programmgeneration oder Sample-Zeitmarke.

Der Monitor liefert echte Engine-Quellen/Offsets und teilweise Grundwerte.
Der Editor berechnet daraus den globalen Sollwert bzw. erreichbaren Bereich.
Das ist keine vollständige Telemetrie der geglätteten DSP-Parameter oder jeder
einzelnen Stimme. Diese Grenze ist im Editor-Code bereits beschrieben.

**ENTSCHEIDUNGSVORSCHLAG:** Für kombinierte Firmware Annahme und Anwendung
explizit unterscheiden. Bei Bedarf eine begrenzte, vom Audiopfad veröffentlichte
Revision verwenden. IDENTIFY um eine versionierte Komponentenliste erweitern;
Sitzungs-/Neustarterkennung für diesen erweiterten Vertrag festlegen. Aktuelle
7-Bit-Sequenznummern und CRC ersetzen keine solche Komponenten-/Boot-Kennung.
Die Protokollerweiterung muss Firmware, Python-Tool, beide Editoren und Tests
gemeinsam berücksichtigen; kein paralleles Reverb-Protokoll anfangen.

### 5. CI-Abhängigkeiten erfassen die Instrumenteinbindungen noch nicht

**BESTÄTIGT:** In `.github/workflows/quick.yml` werden bei einer Änderung an
`modules/mod-on-daisy/include/` die Modul- und Editor-Suites gewählt. `toy` und
`physical` werden dagegen nur durch ihre eigenen Verzeichnisse oder die
allgemeinen Shared-Pfade aktiviert. Eine Moduländerung kann somit die
Instrumentadapter/Golden-Tests überspringen.

**ENTSCHEIDUNGSVORSCHLAG:** Eine kleine explizite Abhängigkeitsliste für gezielte
Host-Tests pflegen. Änderungen am verwendeten Mod-Kern sollen beide Verbraucher
testen. Das erfordert keine teure automatische ARM-Matrix; die Budgetregeln
aus AGENTS.md bleiben bestehen.

**BESTÄTIGT:** CMake enthält einen VST3- und PluginCheck-Target. Der manuelle
Editor-Workflow baut CMake-Targets, führt aber `ModOnDaisyPluginCheck` nicht aus
und verpackt nur die Standalone-EXE. Ein eigener VST3-Download samt Prüfnachweis
fehlt dort noch. Vorhandene lokale Windows-Pluginprüfungen sind dokumentiert;
Studio-One-/Geräteabnahme P1–P5 ist dort noch `NICHT GEMESSEN`.

### 6. Dokumentation und Build-Portabilität benötigen einen Abgleich

**BESTÄTIGT:** Die Repository-README nennt VST3 noch als spätere Arbeit und
beschreibt einen eingebauten Simulator ohne die inzwischen eingeführte
Opt-in-Grenze. HANDOVER führt PR #33 noch als zu mergen und VST3 als geplant,
obwohl #33/#34 im geprüften main enthalten sind. Projektspezifische Quellen und
aktuelle Validierungsabschnitte sind hier genauer als die Einstiegstexte.

**GEMESSEN (Host):** Der unveränderte Modul-Testbefehl scheitert hier mit GCC
14.2 an `-Werror=mismatched-new-delete` im absichtlich ersetzten
Allokationszähler (`new` nutzt `malloc`, beide `delete` nutzen `free`). Mit nur
dieser Diagnose für den Testaufruf deaktiviert läuft die Suite vollständig.
Das ist eine lokale Test-Compiler-Kompatibilität, kein nachgewiesener
DSP-Laufzeitfehler. Im Audit wurden die Testquellen nicht geändert.

## Tatsächlich erneut ausgeführte Prüfungen

Neue Ergebnisse vom 25.10.2026, auf dem oben genannten Commit:

| Prüfung | Ergebnis |
| --- | --- |
| `make -C editors/mod-on-daisy test` | PASS: Firmware-Controller, Handshake, CRC, Ausfälle/Wiederverbindung, Dateikompatibilität, Modelle und Monitordaten |
| `make -C modules/mod-on-daisy test` | Standardaufruf scheitert mit GCC 14.2 an obiger Testdiagnose; erneuter Lauf mit zusätzlichem `-Wno-mismatched-new-delete` PASS |
| Mod-Suite dieses erneuten Laufs | Standard, max64, Erweiterungen, Fuzzing (1 Mio. Rahmen im Standardlauf), Threads, Python-Selbsttest, Browser-Protokoll gegen C++ PASS |
| `make PROJECT=physical-modeling test` mit unten erläuterten Dependency-Pfaden | PASS: DSP, Callback, Parser, Review-Regressionen, beide Golden-Varianten, Mod-Einbindung, CPU-Protokoll |
| Golden-Hash regulär und MOD, leere Matrix | Beide `37DE0BF9D93B81CE` |
| ARM-Cross-Build `PROJECT=physical-modeling-mod`, xPack 13.2.1-1.1 | PASS: 171.684-Byte-BIN |
| `check-firmware.py` und `check-physical-elf.py` | PASS: Vektoren, Marker, ARM hard-float, ELF/BIN-Identität und statische Speicherreserven |
| SHA-256 des neu gebauten MOD-BIN | `9517db27fa25cb3c967923304d15c081f025e942d34182f6153d2acb5b6f92e1` — entspricht dem dokumentierten mod4-Gerätestand |

Für den isolierten Audit-Worktree wurden die vorhandenen offiziellen
libDaisy-/DaisySP-Checkouts mit exakt den gepinnten Commits über
`LIBDAISY_DIR`/`DAISYSP_DIR` verwendet. Die Host-Include-Suche setzte die
Projekt-Fakes vor den absoluten libDaisy-Include-Pfad. Erste Versuche ohne
vollständige Dependency-Pfade bzw. mit falscher Fake-Include-Reihenfolge
scheiterten an fehlenden Make-/STM32-Headern; der korrigierte Gesamtlauf oben
bestand. Es gab keine Änderungen an Bibliotheken oder Firmwarequellen.

**Nicht erneut durchgeführt:** native JUCE-/VST3-Builds, Browser-GUI-E2E,
Sanitizer-Suites und sämtliche Geräteprüfungen. Bestehende Nachweise sind in
[VALIDATION](../modules/mod-on-daisy/docs/VALIDATION.md) verlinkt; dieser Audit
wertet sie nicht als neue Messungen. Es wurden keine Geräte verbunden/geflasht,
keine Releases erzeugt und keine kostspieligen GitHub-Workflows gestartet.

## Grenzen für kombinierte Firmware

Der neue ARM-Build belegt statisch **110.376 B DTCM** (20.696 B nominale Reserve),
**171.684 B SRAM-Linkerbelegung** und **197.932 B SDRAM**. Diese Buildwerte sind
keine Stack-Hochwassermessung und kein Laufzeitnachweis für eine FX-Kombination.

**DOKUMENTIERT, vorhandene Gerätemessung 22.10.2026:** Torus erreichte im
eingeschwungenen Zustand 56,71 % Callback-Spitze. Beim Spielen mit Modellwechseln
wurden dagegen bis **141,58 %** und 20 Fristüberschreitungen in 300.085
erfassten Callbacks gemessen. Die Spitzen sind als Einschränkung des jetzigen
Instruments akzeptiert, dürfen aber bei der Planung zusätzlicher FX nicht als
freie Reserve übergangen werden. Siehe
[Spiel-/Automationsmessung](../instruments/physical-modeling/docs/HARDWARE_PLAYING_2026-10-22.md).
Die vollständige Kombination aus aktuellem Mod4 und Ambient Reverb ist
**NICHT GEMESSEN**.

## Empfohlener nächster Ausbau

1. **Versionen und Abhängigkeiten festlegen:** stabile Komponenten-IDs,
   eigenständige Versionen für Instrument-/FX-Kerne und Control Panels,
   einheitliche Produktkennung in IDENTIFY, Marker und Artefaktnamen;
   gezielte CI-Verbrauchertests ergänzen. Golden-Tests und akzeptierte alte
   Images erhalten. MIT-DSP/Browser und AGPL-JUCE-Grenze beibehalten.
2. **Einen vorhandenen FX als Pilot herauslösen:** Torus-Reverb mit explizitem
   Speicher und Stereo-Prozessvertrag; das Verhalten innerhalb von Torus durch
   Golden-Tests absichern. Ambient Reverb erhält einen Adapter zu demselben
   Vertrag. Die Standalone-FX-Firmware bleibt ein eigener Hardware-Wrapper.
3. **Eine Kombination und ihre Panels ergänzen:** zunächst beim Bauen, mit
   versionierter Komponentenliste und Hardware-Mapping. Für den besprochenen
   Toy-Piano/Ambient-Test: VR1/2 Piano, VR3–8 Reverb, Schalterkombinationen als
   Modi, alle 16 Tasten als Klaviatur. Aktuell existieren fünf Ambient-Modi;
   der zusätzliche Shimmer-Modus ist eine getrennte DSP-Entwicklung.

Ein Firmware-Build soll künftig reproduzierbar angeben, welche Versionen von
Instrument, FX und Mod-Kern er enthält. Das Control Panel soll seine eigene
Version und die unterstützten Protokoll-/Schemas melden. Diese Metadaten und
ihre Tests sind ein kleiner erster Schritt; ein vollständiger Laufzeitkern
oder die Überarbeitung aller Instrumente gehört nicht zu dieser Prüfung.
