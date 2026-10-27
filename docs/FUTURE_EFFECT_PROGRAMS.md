# Künftige Effektprogramme

**Wunsch vom 24.10.2026, noch nicht implementiert.** Für die weitere
Daisy-Entwicklung soll der bisher mitgelieferte, als Clouds-Reverb bezeichnete
Effekt durch folgende eigene Programme abgelöst bzw. erweitert werden:

* Ein Stereo-Delay-Programm.
* Unser bereits entwickelter Ambient-Reverb aus der Slöer-Arbeit,
  [effects/reverb](../effects/reverb/README.md).

Der vorhandene Ambient-Reverb ist eine eigene Entwicklung mit Slöer als
Gestaltungsreferenz. Er ist kein identifizierter Original-Walrus-Algorithmus.
Das Stereo-Delay gibt es seit Acid Seq Phase 4 als Komponente `stereo-delay` mit eigener
Firmware `delay-fx` ([effects/stereo-delay](../effects/stereo-delay/)): Ping-Pong und
Stereo L/R, Sync bis 12 s im SDRAM. Das Produkt Acid Seq setzt es hinter den Drive. Andere
Instrument-Firmwares binden es noch nicht ein.

Die Erwartung ist, dass Seed3-Leistung und Speicher ausreichen. **CPU-Reserve,
Callback-Spitzen, RAM/SDRAM-Belegung und Verhalten der Kombination sind
NICHT GEMESSEN.** Vor Integration die Effektprogramme einzeln und zusammen mit
der jeweiligen Klangerzeugung profilieren; Delayzeit, Pufferformat und
Reverb-Speicherbedarf daraus ableiten. Keine Allokation oder blockierende
Operation im Audiocallback.

Bestehende Instrumente und ihre Golden-Hashes bleiben bis zu einer gezielten,
dokumentierten Klangänderung unverändert. Dieser Eintrag startet weder einen
Firmwareumbau noch einen Release-Workflow.
