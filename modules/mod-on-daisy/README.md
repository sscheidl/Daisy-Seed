# Mod on Daisy

Universelle, wiederverwendbare **Modulationsmatrix** für Instrumente und Effekte auf dem
Seed3 Desktop Development Kit. Das Modul ist eigenständig und wird in vorhandene Projekte
eingebunden. Es ist ausdrücklich nicht fest in eine einzelne Firmware eingebaut.

**Status: Konzeptphase (Phase 1).** Es gibt noch keinen Code und keine Firmware.

| Dokument | Inhalt |
| --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Architektur, DSP-Raten, Integration, Ressourcen, Speicherung, Editor/VST3, Phasen, Risiken, **offene Entscheidungen** |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | Entwurf des MIDI-Protokolls (Live-CCs und SysEx-Konfiguration) |

## Leitgedanke

Die Hardware bleibt unmittelbar spielbar. Komplexe Modulationszuordnungen werden in einem
grafischen Editor über MIDI eingestellt, nicht über Hardware-Menüs.

## Kernideen

* **Quellen → kompiliertes Routing → normierte Ziel-Offsets.** Instrumente beschreiben
  ihre modulierbaren Parameter in einer `constexpr`-Tabelle mit stabilen IDs. Der Editor
  liest sie vom Gerät, deshalb braucht ein neues Instrument keinen eigenen Editor.
* **Control-Rate (~1 kHz) als Standard**, Audio-Rate nur für ausdrücklich markierte
  Ziele. Geschätzte Last: < 0,5 % CPU und ~6–8 KB RAM bei 32 Routen. Gemessen wird ab
  Phase 2 bzw. 4.
* **Header-only C++17-Kern ohne libDaisy-Abhängigkeit**, auf dem Host testbar. Der
  Protokoll-Codec wird zwischen Firmware und Editor geteilt.
* Echtzeitregeln wie in allen Projekten des Repositories: kein Heap, keine Locks und kein
  USB oder QSPI im Audio-IRQ; lock-freier Triple-Buffer zwischen Main-Loop und Audio.

## Vorgehen

1. Architekturkonzept ← **aktuell**
2. Eigenständiger Prototyp mit Host-Tests und erster CPU-Messung
3. MIDI-Editor (JUCE-Standalone, vorher technischer Test des MIDI-Wegs unter Windows)
4. Performanceprüfung auf Hardware
5. Integration, vorgeschlagener Pilot: Physical Modeling, danach Looper
6. Erweiterung: weitere Quellen und Instrumente, VST3

Die Phasen sind ein Vorschlag und noch nicht verbindlich beschlossen.
