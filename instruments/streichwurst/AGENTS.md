# Streichwurst: Projektregeln

Zusätzlich zu den Repositoryregeln:

* Maßgeblicher Implementierungsauftrag: [CLAUDE_AUFTRAG.md](docs/CLAUDE_AUFTRAG.md).
  Name und Build-ID sind `Streichwurst` / `streichwurst`.
* [Modulstruktur](docs/MODULAR_ARCHITECTURE.md) ist verbindlich: Instrument
  Stringsection/Soli, separate FX unter `effects/streichwurst-fx/`, gemeinsame
  MIDI-/Control-Anbindung und Erweiterung der Mod-on-Daisy-Control-App.
* Eigener DSP-Code und eigene Daten. Keine Herstellerprogramme, Samples,
  Presets, Wellen-/Filtertabellen, Dekompilate, Disassemblierungen, direkten
  Codeübersetzungen oder rekonstruierbaren Dumps importieren oder hochladen.
  Das gilt auch für den öffentlichen Branch, PR, Issues, Logs und Artefakte.
* `docs/research/` enthält selbst formulierte Befunde und einen historischen
  Entwurf. BESTÄTIGT, PLAUSIBEL, OFFEN und eigene Entwurfsentscheidungen bleiben
  getrennt. Widersprüche zunächst im Befundregister erklären.
* Klangtreue ist das Ziel; generische Synth-/Chorus-/Reverbbausteine nur als
  dokumentierte Annäherung einsetzen. Keine behauptete Klangidentität aufgrund
  bestandener Softwaretests.
* Firmware nur lokal bauen; kein Flashen, Pluginbetrieb, Hardwarezugriff oder
  Release in diesem Auftrag. Keine Herstellerdateien öffnen oder analysieren;
  weitere Originalanalyse ist ein eigener Arbeitsschritt.
* Änderungen auf Instrument, separate Streichwurst-FX, gemeinsame gezielte
  MIDI-/Control-Adapter, Mod-on-Daisy-App und notwendige Registry-/CI-
  Erweiterungen begrenzen. Andere Instrumente und Bibliothekspins erhalten.
* Language: English first, as in the repository rule (AGENTS.md). New and revised
  documents, manuals and UI texts are English; the existing German project documents
  are migrated deliberately.
