# Daisy Seed — Instrumente und Effekte

Gemeinsame Werkstatt für eigenständige Firmwareprojekte auf dem
**Electro-Smith Seed3 Desktop Development Kit**. Jedes Instrument und jeder Effekt
hat ein eigenes Verzeichnis, eine eigene Beschreibung und später einen eigenen
Firmware-Build. Bibliotheken, Toolchain und Board-Buildkonfiguration werden geteilt.

## Projekte

| Projekt | Bereich auf GitHub und lokal | Stand |
| --- | --- | --- |
| [Toy Piano v0.2](instruments/toy-piano/) | `instruments/toy-piano/` | USB-MIDI; ARM-/Host-Tests bestanden; Tasten, AD, Polyphonie, schnelle Folgen und MIDI mit Studio One gemeldet |
| [Physical Modeling](instruments/physical-modeling/) | `instruments/physical-modeling/` | Projektgerüst und Referenzvergleich; eigene Engine geplant |
| [Granular](instruments/granular/) | `instruments/granular/` | Projektgerüst und Referenzvergleich; eigene Engine geplant |
| [Reverb](effects/reverb/) | `effects/reverb/` | Clouds-/Nimbus-Reverb-Experimente und Slöer-Stereo-Reverb als Portierungsziel |
| [Granular-Effekt](effects/granular/) | `effects/granular/` | Geplante Live-/Freeze-Anwendung der Granular-Arbeit |

Das Repository bleibt **`sscheidl/Daisy-Seed`**. Die Projektbereiche besitzen eigene
Quellen, Tests, Prüfdokumente und Buildverzeichnisse im selben Git-Verlauf.
Projektbranches, PRs und Issues lassen sich getrennt bearbeiten. Separate
GitHub-Repositories oder verschachtelte Git-Repositories werden dafür nicht benötigt.

## Lokale Struktur / Playground

```text
Daisy-Seed/
├── instruments/
│   ├── toy-piano/          # Makefile, README, src, tests, docs, build (generiert)
│   ├── physical-modeling/  # eigener Projektplan
│   └── granular/           # eigener Projektplan
├── effects/
│   ├── reverb/             # Clouds/Nimbus und Slöer-Stereo-Reverb
│   └── granular/           # Effektplan
├── common/desktop.mk      # gemeinsamer offizieller Seed3-Desktop-Build
├── docs/                  # Hardware, Build, Flashen und Quellenübersicht
├── scripts/               # Toolchain und BOOT_SRAM-Binärprüfung
├── projects.mk            # Projekt-IDs, Pfade und Implementierungsstatus
├── libDaisy/              # ein gepinntes offizielles Submodul
└── DaisySP/               # ein gepinntes offizielles Submodul
```

In dieser Cloud liegt die Struktur unter `/workspace/Daisy-Seed`.
Ein Clone oder Pull im eigenen Playground erzeugt dieselben Unterordner.
Ein Desktop-Verzeichnis außerhalb dieses Workspace wird aus der Cloud nicht verändert.

Die Hardware-Diagnose ist separat in
[Daisy-Seed3-Desktop-Diagnostics](https://github.com/sscheidl/Daisy-Seed3-Desktop-Diagnostics)
untergebracht.

## Bauen und testen

```bash
git clone https://github.com/sscheidl/Daisy-Seed.git
cd Daisy-Seed
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
bash scripts/install-toolchain.sh  # Linux x86_64, ohne sudo
export PATH="$PWD/.tools/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin:$PATH"

make list
make PROJECT=toy-piano -j4
make PROJECT=toy-piano test
make PROJECT=toy-piano test-sanitize
make PROJECT=toy-piano test-thread
make test-release
```

Ohne `PROJECT=` bleibt Toy Piano der Standard. Direkter Projekteinstieg:
`make -C instruments/toy-piano`. Das Ergebnis liegt unter
**`instruments/toy-piano/build/ToyPiano-v0.2.bin`**. Die Version steht im Projekt
unter `src/version.h`; `make -s version` gibt sie aus. Geplante Projekte werden bei
einem Buildversuch ausdrücklich abgewiesen; es wird keine Ersatzfirmware gebaut.
`make all-projects` und `make test-projects` bearbeiten alle implementierten Projekte.

Jedes Projekt ist zunächst eine separate Firmware. Zwischen Instrumenten wird
durch Flashen der jeweiligen BIN gewechselt; ein Laufzeit-Instrumentenwechsel
oder eine gemeinsame Effektkette ist noch nicht implementiert.

## Gemeinsame Grundlagen und nächste Schritte

* [Build und Toolchain](docs/BUILD.md), [Hardware](docs/HARDWARE.md), [Flashen](docs/FLASHING.md).
* [Toy Piano: Bedienung und Hardwaretest](instruments/toy-piano/README.md),
  [USB-MIDI](instruments/toy-piano/docs/MIDI.md),
  [Changelog](instruments/toy-piano/CHANGELOG.md), [Prüfbericht](instruments/toy-piano/docs/VALIDATION.md).
* [Quellenübersicht und Vergleichsgrenzen](docs/REFERENCES.md): Rings/Elements,
  vorhandene Daisy-Ports, Iridium-Resonator, GR-1, Iridium Particle, Clouds, Beads und Reverb-Grundlagen.

Reihenfolge: Toy Piano am Gerät prüfen; dann einen kleinen eigenen
Physical-Modeling-Prototyp entwickeln; anschließend einen begrenzten Granular-Prototyp.
Effektvarianten verwenden später nachweislich gemeinsame DSP-Bausteine, sobald
diese existieren. Bis dahin bleiben Planung und implementierte Firmware klar getrennt.
Resonatoren gehören zum Physical-Modeling-Projekt. Im Reverb-Bereich werden die
Clouds-/Nimbus-Reverb-Experimente und das Slöer-Stereo-Reverb als Portierungsziel
getrennt verfolgt. Am geöffneten Nutzergerät wurde ein FV-1 mit Zusatzmarkierung
„2211“ identifiziert; die konkrete Slöer-Algorithmusgrundlage bleibt offen.
Die Reverb-Arbeit kann unabhängig von den Instrumentprojekten weitergeführt werden.
