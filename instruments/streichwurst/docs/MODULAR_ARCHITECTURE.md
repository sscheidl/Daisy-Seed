# Verbindliche Modulstruktur: Streichwurst

2026-11-01. Nutzerpräzisierung nach der ersten Vorbereitung. Diese Struktur
ergänzt den historischen Forschungsentwurf und ist für Claude maßgeblich.
Die **bestehende Mod-on-Daisy-Control-App** wird erweitert: JUCE-Standalone,
Browser und die gemeinsame VST3-Oberfläche. Die App läuft auf dem Rechner und
steuert die zusammengesetzte Daisy-Firmware über deren MIDI-/Control-Anbindung.

## Komponenten und Besitz

| Baustein | Ort / geplante ID | Verantwortung |
| --- | --- | --- |
| Instrument | `instruments/streichwurst/include/streichwurst/`, `streichwurst-instrument` | Stringsection und Soli, eigene Ton-/Notenzustände, Hüllkurven, Registrierung, Oktaven, Solo-Timbre/Tremolo, Layer/Split |
| FX | `effects/streichwurst-fx/`, `streichwurst-fx` | Streichfett-FX-Funktionen: Animate, Ensemble/Chorus, Phaser, Reverb; unabhängige Zustände und Tests |
| MIDI-/Control-Engine | vorhandene gemeinsame MIDI-/Protokollbausteine mit gezieltem Adapter | Transport, Parsing, Zeitstempel, begrenzte Queues, Note-/Controller-/SysEx-Verteilung und konsistente Parameterübergabe |
| Firmwareprodukt | `instruments/streichwurst/src/`, Build-ID `streichwurst` | Zusammensetzung, Board, Speicher, Routing, Balance, endgültiger Gain/Outputschutz, Bootloader und Diagnose |
| Control App | `editors/mod-on-daisy/` | Komponenten-/Parameterseiten, Status, Patchverwaltung und vorhandene Matrixoberfläche; gemeinsame Firmwareparameter bedienen |

Das sind geplante Komponenten. Registereinträge mit eigenen Versionsquellen
entstehen erst mit den entsprechenden Implementierungen. Produktversion,
Instrumentversion, FX-Version und Appversion bleiben getrennt. Das Produkt-
manifest nennt die eingebetteten Komponenten und kompatiblen Panels.

Instrument und FX sind ohne Board, MIDItransport oder Editor auf dem Host
testbar. Die Firmware besitzt den Speicher und reserviert ihn vor Audiostart.
Keine GUI-, HAL- oder Protokollabhängigkeit im DSP-Kern. Die App kennt Parameter
und Zustandsverträge statt DSP-Speicheradressen.

## Audio- und Kontrollfluss

```text
USB/DIN MIDI ─┐
Kit-Bedienung ├─> MIDI-/Control-Engine ─> NoteEvents + Parameterupdates
Control App ─┘                          │
                           ┌───────────┴───────────┐
                           v                       v
                    Instrumentparameter       FX-Parameter
                           │                       │
Instrument:   Stringsection dry             Soli dry
                    │                         │
FX:          Animate/Formung                   │
                    │                         │
                Ensemble                      │
             gemeinsame Summe                  │
               -> Phaser                      │
             -> Stereo-Tapkorrektur            │
                    └──────────┬──────────────┘
Firmware:                Balance/Mix
                               │
FX:                     globaler Reverb
                               │
Firmware:               Gain -> Outputschutz -> Stereo
```

Der FX-Baustein hat deshalb getrennt aufrufbare String- und Globalstufen.
Die Firmware orchestriert sie um die Mischung herum. Der trockene Soli-Bus
umgeht String-Ensemble und String-Phaser. Die Stereo-Tapkorrektur bleibt Teil
der Ensemble-/Phaser-Routingbeziehung, einschließlich der dafür nötigen
Tapzustände. Eine flache Stereo-FX-Kette hinter der bereits gemischten
Instrumentausgabe würde diese Beziehung verändern.

Animate ist als FX-Funktion besessen. Soweit seine Wirkung einen
Registrierungs-/Klangformungssteuerwert benötigt, veröffentlicht das FX-Modul
einen typisierten Modulationswert für die Instrument-Stringquelle; die Firmware
übergibt ihn mit definierter Kontrollkadenz. Die statische Klangfarbenbasis und
Notenverwaltung bleiben im Instrument. Gemeinsame Zeitbasis und Reihenfolge
definieren, keine direkten gegenseitigen Schreibzugriffe auf private Zustände.
Die genaue Original-Formungsrekurrenz ist weiterhin teilweise offen.

## Zu implementierende Verträge

* Instrument: `Prepare`, `Reset`, `ApplyEvent`, `SetParameters`,
  `RenderSections`. Ausgabe getrennte String-/Soli-Busse mit gleicher
  Framezahl und Zeitbasis. Physisches Kanalformat, Maximalblock, Pufferaliasing
  und Nullblocksemantik vor Implementierung festlegen und testen.
* FX: `Prepare`, `Reset`, `SetParameters`, Stringstufe und Global-Reverbstufe,
  abgegrenzte Animate-Steuerschnittstelle. Kein Besitz des Instruments,
  kein MIDIparser und keine eingebettete Bedienlogik.
* Gemeinsamer Host: definierte Queue-/Snapshotübergabe von Main/USB zu Audio,
  gleiche Zeitbasis für Events und DSP. ACK unterscheidet angenommene von
  tatsächlich angewandten Parametern; angewandene Revision aus Audio melden.
* Die vorhandene [FX-Schnittstelle](../../../common/audio/fx_contract.h)
  bleibt für vorhandene Verbraucher unverändert. Wiederverwendbare
  Streichwurst-Teilstufen können passende Adapter anbieten. Der zusätzliche
  Mehrbus-Vertrag wird explizit versioniert, ohne Strings und Soli vorzeitig
  zusammenzufassen. Die Hostmischung verhindert doppeltes Dry-Signal und hält
  Original-Modusgain nach Solo/Reverb von eigenen Headroomregeln getrennt.
* Gleicher Parameterbestand für Potis, MIDI CC und App: stabile Komponenten-
  und Parameter-IDs, Typ/Einheit/Range/Default/Enum, Glättung, Speicher- und
  Modulierbarkeitseigenschaften. Anwendung, Readback und Reset eindeutig.
  Die acht Matrixziele sind eine eigene Auswahl, keine Begrenzung der gesamten
  Instrument-/FX-Parameterseiten.

## MIDI und bestehende Control App

Claude prüft zuerst die tatsächlich vorhandenen Mod-on-Daisy- und SQ-
Parameter-/Deskriptor-/Sessionbausteine im Repository. Ein gemeinsamer MIDI-
Dispatcher muss musikalische Events, Geräteparameter, Matrixnachrichten und
Bootloaderbefehle kollisionsfrei zuordnen. Vorhandene Nachrichtencodes und
Komponenten-IDs dürfen nicht umgedeutet werden. Kein zweiter Transportstack
im Instrument oder im FX; gemeinsame Helfer gezielt wiederverwenden.

Die aktuelle Matrixschnittstelle allein deckt die vollständige Klangbedienung
nicht ab. Instrument- und FX-Seiten sowie Produkt-/Komponentenidentität sind
Teil dieses Auftrags. Benötigte Protokollerweiterung zuerst dokumentieren,
versionieren und zusammen in Firmware, CLI-Probe, JUCE/Browser sowie den
gemeinsamen VST3-Sessionpfaden implementieren und testen. Bestehende Geräte und
deren Patches müssen weiterhin korrekt erkannt und bedient werden.

Beim Verbinden zunächst Zustand lesen. Kein ungefragtes Überschreiben mit
Appdefaults. Poti-/MIDIänderungen werden in der App sichtbar; Appänderungen
kommen über denselben Parametervertrag bei der Firmware an. Rückkopplungen,
veraltete Antworten, fehlende ACKs und Verbindungsabbrüche begrenzt behandeln.
Eigene Produktpatches enthalten Instrument- und FX-Parameter, Komponentenschema
und gegebenenfalls Matrixzustand. Bestehende `.modpatch`-/`.acidpatch`-Formate
bleiben kompatibel. Der Editor führt keinen Streichwurst-Audio-DSP aus.

Die vorhandene Mod-Matrix wird als eigenes gemeinsames Modul angebunden;
Streichwurst-Ziele und angewandte Parameter im Vertrag beschreiben. Leere Matrix
liefert unveränderten eigenen Basisklang. Neue Modulationsquellen oder ein
allgemeiner Laufzeit-Instrumentlader sind kein notwendiger Teil dieses Produkts.

## Verbindliche Abnahme

Getrennte Hosttests für Instrument und FX, Tests des MIDI-/Parameter-Routers,
Protokoll-/Sessiontests der Control App und End-to-End-Tests der Firmware auf
Host-Fakes. Besonders prüfen: Soli umgehen String-FX; Reverb sieht die spätere
Mischung; FX-Bypass, Tails und Reset besitzen definierte Regeln; App, Potis
und MIDI sehen denselben angewandten Parameterstand. Komponentenaustausch
erfordert keinen Eingriff in den Kern des anderen DSP-Moduls.

Danach echter lokaler ARM-Build und Ressourcenprüfung. App-Selbsttests sind
Softwaretests; MIDI-/Audioverhalten auf dem Board und Klangvergleich zum
Original bleiben bis zur realen Durchführung **NICHT GEMESSEN**.
