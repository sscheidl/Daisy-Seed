# Verbindliche modulare Entwicklung für Daisy

**Nutzerentscheidung 01.11.2026:** Das modulare Konzept gilt generell für alle
Daisy-Entwicklungen in diesem Repository. Instrumente und FX sollen getrennt
weiterentwickelt und anschließend wieder in Firmwareprodukte eingebunden werden
können. Ein kompatibles FX-Modul muss austauschbar sein, beispielsweise Reverb
gegen Delay, ohne den Instrument-DSP umzuschreiben.

Diese Regel konkretisiert die vorhandenen
[modularen Komponenten](MODULAR_COMPONENTS.md) und die
[Baukastenidee](MODULAR_KERNEL_IDEA.md). Sie ist für neue Entwicklungen und
wesentliche Erweiterungen verbindlich. Die vorhandenen stabilen Produkte werden
bei gezielten Umbauten schrittweise angepasst; keine unbeauftragte breite
Migration oder Klangänderung. Historische Berichte bleiben als solche erhalten.

## 1. Bausteine und Firmwareprodukt

* **Instrument:** eigene Klangerzeugung, Noten-/Stimmenzustände und Hüllkurven.
  Keine fest eingebaute, nur intern erreichbare FX-Implementierung.
* **FX:** eigene Zustände, Parameter und Speicherverträge. Ein FX kann aus
  mehreren separat aufrufbaren Stufen bestehen, wenn das Routing es erfordert.
  Keine Abhängigkeit von der konkreten Instrumentimplementierung.
* **MIDI-/Control-Engine:** wiederverwendbare Transport-, Queue-, Dispatch- und
  Parameteranbindung. Bestehende Bausteine gezielt nutzen oder erweitern;
  Instrument und FX erhalten typisierte Events/Parameter statt eigene USBstacks.
* **Modulation:** vorhandener Mod-on-Daisy-Kern als separater Baustein. Kein
  neuer duplizierter Matrixkern je Instrument oder FX.
* **Control App:** gemeinsame bestehende Oberfläche mit Komponenten-/Parameter-
  Adaptern. App, DSP und Hardwarebedienung bleiben getrennt. Das Panel ist kein
  in der Daisy-Firmware enthaltener Audio-DSP.
* **Firmwareprodukt/Host:** benennt und verbindet die Komponenten, besitzt
  Speicher, Audiograph, Board-/Bedienmapping, Routing, Mischungen, Ausgangsschutz
  und gemeinsame Dienste. Die Produktzusammenstellung ist sichtbar und versioniert.

Gemeinsam genutzter Code hat eine eigene Quelle. Derselbe Algorithmus wird
nicht zum Weiterentwickeln in mehrere Firmwareverzeichnisse kopiert.
Eine einzelne Produkt-Makefile oder `main.cpp` darf die Komponenten verbinden;
dadurch werden die DSP-Kerne nicht zu einem einzigen untrennbaren Modul.

## 2. Austauschbare FX und Audiograph

Ein FX-Platz besitzt einen dokumentierten Port-/Parameter-/Speichervertrag.
Kompatible FX werden durch die Produktkonfiguration oder einen kleinen
Hostadapter ausgewählt. Reverb gegen Delay auszutauschen verändert diesen
Zusammenbau und seine Parameterbeschreibung; der Instrumentkern bleibt gleich.
Ein Bypass-/Durchgangspfad erlaubt isolierte Tests der Quelle.

Die bestehende [FX-Schnittstelle 1.0](../common/audio/fx_contract.h) ist die
Grundlage für Stereo-Wet-Stufen: `Prepare(Config, Storage&)`, `SetParameters`,
`Process(StereoBlock)`, vom Host bereitgestellter Speicher und vom Host
verantwortete Dry/Wet-Mischung. Bestehende Verträge nicht still verändern.
Zusätzliche Ports, Kontrollsignale oder Mehrbusstufen werden ausdrücklich
beschrieben und versioniert. So bleiben beispielsweise getrennte String-/Solo-
Wege möglich, ohne ihre FX vorzeitig auf die Gesamtmischung zu verschieben.

Vor dem Austausch prüfen:

* Kanal-/Buslayout, Format, Samplerate, Blockgrenzen, In-place-/Aliasregeln;
* Speicherbedarf, Alignment, Platzierung, Stack und maximale Rechenlast;
* Gain, Dry/Wet, Latenz, Tails, Bypass und Reset-/Wechselverhalten;
* Parameter-, Status- und Patchschema sowie anwendbare Modulationsziele.

Inkompatible Komponenten werden abgewiesen oder über einen ausdrücklich
getesteten Adapter verbunden. Kein automatischer Mix-/Gainwechsel als
unbemerkter Nebeneffekt eines FX-Austauschs.

**Erste garantierte Entwicklungsstufe:** Zusammensetzung beim Bauen mit
expliziter Auswahl im Produkt. Eine neue FX-Wahl darf nicht bloß ein neues
kopiertes Synthesizerprojekt erzeugen. Ein allgemeiner Laufzeitwechsel oder
dynamischer Loader ist erst implementiert, wenn Zustandsübergabe, Vorreservierung,
Ausklang, Pegel, Speicher und Echtzeitlast tatsächlich abgesichert sind.
Die Richtlinie behauptet nicht, dass ein solcher Loader bereits existiert.

## 3. Module unabhängig weiterentwickeln und wieder einbinden

Jedes Modul besitzt stabile Komponenten-ID, eigene Versionsquelle, öffentliche
Schnittstelle, README, Herkunft/Lizenz und gezielte Hosttests. Interne Änderungen
können unabhängig erfolgen, solange der öffentliche Vertrag eingehalten wird.
Inkompatible Audio-, Parameter-, Patch- oder Speicheränderungen werden als
Vertrags-/Schemaänderung versioniert und mit einer Anpassungsstrategie dokumentiert.

Jedes gebaute Produktmanifest nennt die konkrete Produktversion und die
eingebundenen Komponenten-/Vertragsversionen, Gitrevision sowie passende
Control Panels. Keine zweite manuell gepflegte Kopie der Modulversion.
Gitstände machen die Auswahl reproduzierbar und einen Rückwechsel möglich.

Ein separat weiterentwickeltes Modul wird in dieser Reihenfolge reintegriert:

1. Eigene Modul-/Vertragstests mit definierten Eingaben bestehen lassen.
2. Versions-/Abhängigkeitsauswahl und gegebenenfalls Hostadapter aktualisieren.
3. Betroffene Verbraucher und deren Audio-/Ereignis-/Parametergrenzen testen.
4. Klangänderungen begründen, Goldenwerte gezielt aktualisieren und
   Patchkompatibilität prüfen; unveränderte Varianten erhalten ihre Referenzen.
5. Zusammengesetzte Firmware tatsächlich bauen und Ressourcen aus dem Build
   dokumentieren. Gesamte CPU-Last anschließend auf Hardware messen.

Ein bestandener Modultest allein bestätigt noch keine funktionierende
Gesamtfirmware. Austauschbarkeit bedeutet nicht, dass jede Kombination
innerhalb jedes Speicher-/CPUbudgets möglich ist.

## 4. Gemeinsamer Parameter- und Appvertrag

Kitbedienung, MIDI, Modulation und Control App greifen über dasselbe
Parameterregister auf die aktiven Module zu. Komponenten-/Parameter-ID,
Typ/Einheit, Range/Default/Enum, Rate/Glättung, Gültigkeit und Anwendungsrevision
sind eindeutig. Die App erkennt Komponenten und bedient deren Parameter über
den gemeinsamen Vertrag; spezielle Seiten können diesen ergänzen.

Neue Module erweitern die bestehende Mod-on-Daisy-App an ihren gemeinsamen
Session-/Protokollpfaden. JUCE-Standalone, Browser und VST3 bleiben konsistent.
Änderungen vorhandener Protokolle werden zusammen mit Firmware, CLI-Probe,
Editoren und Tests versioniert. Bestehende Geräte und Patches bleiben bedienbar.
Keine unabhängige Editor-/Transportkopie für jedes neue Instrument.

Zustand beim Verbinden zuerst lesen. Annahme eines Befehls und tatsächliche
Anwendung im Audiopfad auseinanderhalten. Reset, Modulwechsel, Reconnect und
Presetladen besitzen definierte Regeln. Hardware-/Gerätedaten gehören nicht
unbemerkt in einen musikalischen Patch.

## 5. Verbindlicher Reviewpunkt

Jeder neue Instrument-/FX-Arbeitsauftrag und PR beschreibt:

* Modulgrenzen, IDs/Versionen, Verträge und Produktzusammensetzung;
* Austauschweg für FX und Wiederverwendung vorhandener gemeinsamer Dienste;
* getrennte Modulprüfungen sowie Tests der betroffenen Gesamtprodukte/Apppfade;
* Speicher-/CPUannahmen, eigene Klangabweichungen und noch nicht gemessene Teile.

Hardwarefreigaben, Echtzeitregeln, Lizenz-/Datengrenzen und Actionsbudget aus
[AGENTS.md](../AGENTS.md) gelten weiterhin. Diese Richtlinie verlangt keine
breite automatische Buildmatrix und startet keine Migration oder Hardwareaktion.
