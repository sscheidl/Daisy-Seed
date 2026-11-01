# Streichwurst FX: Regeln

Es gelten die Repositoryregeln und der
[Streichwurst-Auftrag](../../instruments/streichwurst/docs/CLAUDE_AUFTRAG.md).

* Eigenständiger FX-Kern mit separater Version und Hosttests; keine Abhängigkeit
  von String-/Soli-Implementierung, Hardwareklasse oder App.
* [Modul-/Routingvertrag](../../instruments/streichwurst/docs/MODULAR_ARCHITECTURE.md)
  beachten. String-FX und Globalreverb erhalten getrennte Aufrufgrenzen.
* Eigene Daten und eigener Code. Keine Herstellerprogramme, Tabellen, Presets,
  Samples, Dekompilate, Disassemblierungen oder direkten Codeübersetzungen.
* Keine Hardwareaktion, kein Pluginbetrieb, kein Flash-/QSPI-Schreiben und
  keine Veröffentlichung von Firmwareartefakten im Implementierungsauftrag.
* Kein Allokieren, Sperren oder blockierendes I/O in der Audiobearbeitung.
