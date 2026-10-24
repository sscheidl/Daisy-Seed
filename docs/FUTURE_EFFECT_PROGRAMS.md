# Künftige Effektprogramme

**Wunsch vom 24.10.2026, noch nicht implementiert.** Für die weitere
Daisy-Entwicklung soll der bisher mitgelieferte, als Clouds-Reverb bezeichnete
Effekt durch folgende eigene Programme abgelöst bzw. erweitert werden:

* Ein Stereo-Delay-Programm.
* Unser bereits entwickelter Ambient-Reverb aus der Slöer-Arbeit,
  [effects/reverb](../effects/reverb/README.md).

Der vorhandene Ambient-Reverb ist eine eigene Entwicklung mit Slöer als
Gestaltungsreferenz. Er ist kein identifizierter Original-Walrus-Algorithmus.
Ein Stereo-Delay ist noch nicht als eigenes Projekt implementiert. Zuordnung
zu konkreten Instrument-Firmwares, Wahl zwischen Programmen und ein möglicher
gleichzeitiger Betrieb bleiben bei der Umsetzung festzulegen.

Die Erwartung ist, dass Seed3-Leistung und Speicher ausreichen. **CPU-Reserve,
Callback-Spitzen, RAM/SDRAM-Belegung und Verhalten der Kombination sind
NICHT GEMESSEN.** Vor Integration die Effektprogramme einzeln und zusammen mit
der jeweiligen Klangerzeugung profilieren; Delayzeit, Pufferformat und
Reverb-Speicherbedarf daraus ableiten. Keine Allokation oder blockierende
Operation im Audiocallback.

Bestehende Instrumente und ihre Golden-Hashes bleiben bis zu einer gezielten,
dokumentierten Klangänderung unverändert. Dieser Eintrag startet weder einen
Firmwareumbau noch einen Release-Workflow.
