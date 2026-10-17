# Reverb — Clouds/Nimbus und Slöer

**Stand: Projektplanung, noch keine Reverb-Firmware in Daisy-Seed.**
Projekt-ID: `reverb-fx`.

## 1. Clouds-/Nimbus-Reverb-Experimente

**Vom Nutzer bestätigt:** Gemeint ist der Reverb aus Mutable Instruments
**Clouds** beziehungsweise dessen Daisy-Port **Nimbus**. Diese Sektion soll auf
dem Seed3 Desktop Dev Kit ausprobiert und eigenständig weiterentwickelt werden.
Grundlagen sind der öffentliche
[Clouds-Reverb](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/clouds/dsp/fx/reverb.h)
und der vorhandene
[Nimbus-Reverb](https://github.com/daisyaudio/DaisyExamples/blob/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Nimbus/dsp/fx/reverb.h).
CloudSeed ist ein anderes Projekt und hier nicht die gewählte Grundlage.

Nächste Arbeit: Reverb aus der Granular-Gesamtanwendung abgrenzen, Abhängigkeiten,
Board-Build und Speicherplatzierung prüfen,
dann Audioeingang, Dry/Wet, begrenzten Ausgangspegel und wenige Parameter anbinden.
Anschließend Impulsantwort, Stabilität, RAM und Callback-Laufzeit am eigenen Kit
prüfen. Leistungsangaben aus fremden Projekten sind keine eigenen Messwerte.

## 2. Walrus Audio Slöer — Stereo-Reverb als Portierungsziel

**Vom Nutzer bestätigt:** Gemeint ist das **Walrus Audio Slöer Stereo-Reverb-Pedal**.
Es ist die zweite gewünschte Arbeitsrichtung. Es wird nicht mit dem Slö oder einem
beliebigen FV-1-Reverb gleichgesetzt.

**Offen:** interner DSP-Chip, konkrete Algorithmus-/Quellgrundlage, Version und ein
bereits existierender nutzbarer Port. Die vorliegende FV-1-Forschung weist für Slöer
weder ein Originalprogramm noch den Chip nach. Eine tatsächliche Portierung bleibt
bis zur Klärung dieser Grundlage offen. Eine eigenständige klangliche Annäherung
müsste als solche getrennt vom Originalport beschrieben werden.

## 3. Vorhandene FV-1-Portierungsgrundlagen

Vorhandene eigene Grundlage:
[fv-1-DSP-xtractor](https://github.com/sscheidl/fv-1-DSP-xtractor/tree/d1ef0525d072091d9d4da46f1821d45e930a2745).
Dort sind plattformneutrale C++17-Referenzen für Plate und Hall dokumentiert;
sie sind keine Slöer-Implementierung und keine bitgenaue FV-1-Emulation.

Diese Referenzen können die Entwicklung eines nativen Daisy-Reverbs unterstützen,
beweisen aber keine technische Verwandtschaft zum Slöer.
Für eine Portierung müssen Abtastrate, Delayzeiten, Modulation, Parameterabbildung
und Signalpegel ausdrücklich definiert werden. Anschließend Host-Vergleich,
ARM-Build und manuelle Hardwareprüfung. Forschungsdaten bleiben im Quellrepository;
hier wird ein eigener integrierbarer Firmwarepfad aufgebaut.

## Gemeinsamer Projektstand

Noch keine der Arbeitsrichtungen ist in dieses Repository importiert oder als
Firmware implementiert. `make PROJECT=reverb-fx` meldet deshalb den Planungsstand.
Clouds/Nimbus und Slöer sollen zunächst getrennt untersucht werden; eine gemeinsame
Effektkette oder ein Wechsel zwischen Engines zur Laufzeit ist nicht implementiert.

Quellenstände und Prüfgrenzen: [Referenzübersicht](../../docs/REFERENCES.md).
