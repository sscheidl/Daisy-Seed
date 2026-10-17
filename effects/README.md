# Effektprojekte

Eigene Projektbereiche für Audioeingangs-Anwendungen auf dem Seed3 Desktop Dev Kit:

| Effekt | Projekt-ID | Stand |
| --- | --- | --- |
| [Reverb](reverb/) | `reverb-fx` | Clouds-/Nimbus-Reverb und Slöer-Stereo-Reverb als Portierungsziel |
| [Granular / Freeze](granular/) | `granular-fx` | geplant |

Jeder Effekt bekommt bei seiner Implementierung eigene Quellen, Tests, README
und ein eigenes Buildverzeichnis. DSP, den Instrument und Effekt tatsächlich
gemeinsam nutzen, wird dann nach `common/` ausgelagert; keine zweite Engine-Kopie.
Derzeit existieren keine Effekt-Firmware und keine zusammenstellbare Effektkette.
Resonatoren bleiben als Modelle und Anregungsvarianten beim
[Physical Modeling](../instruments/physical-modeling/).
