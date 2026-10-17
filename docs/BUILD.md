# Reproduzierbare Projekt-Builds

Alle Befehle gelten im Repository-Hauptverzeichnis. `projects.mk` enthält Projekt-IDs,
Pfade und Implementierungsstatus. Firmwarequellen, Tests und Ausgabe liegen im
jeweiligen Projekt. `common/desktop.mk` bündelt den offiziellen Board-Build;
die Bibliotheken werden nur einmal unter `libDaisy/` und `DaisySP/` verwaltet.

## Festgelegte Abhängigkeiten

| Bestandteil | Commit / Version |
| --- | --- |
| libDaisy | `f715e4875b2d28445d0c11a0a17f064a248f69d0` |
| DaisySP | `2c72eaf9eac5fc0dca1919d65d606da907832618` |
| ARM-Toolchain (getestet) | xPack GNU Arm Embedded GCC `13.2.1-1.1`, GCC `13.2.1 20231009` |
| Sprache / CPU | GNU++17, Cortex-M7, Thumb, FPv5-D16, Hard Float |
| Anwendung | `APP_TYPE=BOOT_SRAM`, `-O3`, 48 kHz / 48 Samples |

libDaisy enthält die benötigte `desktop_devkit::Hardware`-API und die
Seed3-Codec-Unterstützung. Ältere Bibliotheksstände aus DaisyExamples sind hierfür
nicht automatisch geeignet. libDaisys eigene Submodule werden rekursiv auf ihre
festgelegten Commits gesetzt. DaisySP-LGPL wird für dieses Projekt nicht benötigt.

Das lokale Installationsskript lädt das Linux-x86_64-Archiv aus dem xPack-Release
`v13.2.1-1.1` und prüft dessen veröffentlichten SHA-256:

```text
1252a8cafe9237de27a765376697230368eec21db44dc3f1edeb8d838dabd530
```

Es installiert ausschließlich in `.tools/`. Für andere Plattformen eine passende
ARM-Toolchain gemäß [offizieller Anleitung](https://docs.daisy.audio/tutorials/cpp-dev-env/)
installieren und `arm-none-eabi-gcc`, `g++`, `ar`, `objcopy` und `size` in PATH bereitstellen.
Der dokumentierte Cloud-Build wurde mit der oben festgelegten Version durchgeführt.

## Befehle

```bash
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
bash scripts/install-toolchain.sh
export PATH="$PWD/.tools/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin:$PATH"
make list
make PROJECT=toy-piano -j4
make PROJECT=toy-piano test
make PROJECT=toy-piano test-sanitize
make PROJECT=toy-piano test-thread
make test-release
python3 scripts/check-firmware.py
```

Ohne `PROJECT=` wird weiterhin Toy Piano ausgewählt. Der Direktaufruf
`make -C instruments/toy-piano` baut dasselbe Projekt. Die BIN liegt jetzt unter
`instruments/toy-piano/build/`; der frühere Root-Pfad `build/ToyPiano-v0.1.bin`
wird nicht mehr erzeugt oder verwendet.

`make all-projects` beziehungsweise `make test-projects` bearbeiten alle Einträge
aus `BUILDABLE_PROJECTS`. Physical Modeling, Granular und Effektgerüste haben noch
keinen Build. Ein expliziter Aufruf eines geplanten oder unbekannten Projekts
bricht verständlich ab. `make list` ist der schnelle Statusüberblick.

`make libs` baut beide Bibliotheken explizit, beispielsweise nach einer bewusst
geänderten Abhängigkeit. `make PROJECT=toy-piano clean` entfernt nur dessen Build.
Nach Toolchain- oder Compilerflag-Änderungen auch die Bibliotheken sauber neu bauen:

```bash
make clean
make -C libDaisy clean
make -C DaisySP clean
make -j4
```

Die offiziellen Makefiles bauen die Bibliotheken als Archive. Trotz deren großer
Modulauswahl gelangen nur benötigte Funktionen in die gelinkte Anwendung
(`--gc-sections`). Die Anwendung nutzt DaisySPs `mtof()` und eigene kleine,
host-testbare AD- und Sinustabellen-Logik. Ein UI-Framework ist nicht erforderlich.

## Ein weiteres Projekt implementieren

1. Im vorhandenen Projektbereich `src/`, `tests/` und später einen Prüfbericht unter
   `docs/` ergänzen. `Makefile` setzt einen eindeutigen `TARGET`, eigene Quellen und
   `.DEFAULT_GOAL := all` und bindet `../../common/desktop.mk` ein.
2. Erst nach einem echten erfolgreichen ARM-Build den Eintrag in `projects.mk`
   auf `buildable` setzen und in `BUILDABLE_PROJECTS` aufnehmen.
3. CI um den konkreten Projektpfad und Dateinamen ergänzen; Host-Tests, RAM-/CPU-
   Grenzen und manuelle Hardwaretests dokumentieren.

Die aktuelle CI-Matrix enthält nur Toy Piano. Ihr Pfad und der aus der Projektversion
abgeleitete BIN-Dateiname sind dafür angegeben; bei weiteren Projekten müssen diese Metadaten ergänzt
werden. Die Matrix behauptet keine Builds der noch geplanten Engines.

Generierte Dateien und die Toolchain bleiben aus Git ausgeschlossen.
Der GitHub-Actions-Workflow lädt nach erfolgreichem Build die BIN, ELF und
Prüfsumme als Artefakt hoch. In einem privaten Repository ist dafür eine
GitHub-Anmeldung mit Zugriff auf das Repository nötig.

`make test` prüft DSP und den echten Anwendungscallback mit simulierten
Hardwareeingängen und der unveränderten DigitalControl-Entprellung aus libDaisy.
`make test-sanitize` führt beide Tests mit AddressSanitizer und UndefinedBehaviorSanitizer
aus. `-fno-sanitize-recover=all` macht jeden erkannten Fehler zum Testfehler.
Die GPIO-/ADC-Stubs ersetzen keinen Hardwaretest.

`make PROJECT=toy-piano test-thread` prüft die MIDI-Warteschlange mit ThreadSanitizer.
Die gemeinsamen Release-Schutztests liegen unter `tests/` und laufen separat mit
`make test-release` (temporäres Git-Repository und simulierte API).

## Version und Release

Die Toy-Piano-Version steht ausschließlich in `instruments/toy-piano/src/version.h`.
Sie bestimmt den Dateinamen `instruments/toy-piano/build/ToyPiano-v<version>.bin`,
die eingebettete Kennung `TOYPIANO-FW-VERSION=<version>` und den CI-Artefaktnamen.
`make -s PROJECT=toy-piano version` gibt die Version aus. Die Binärprüfung liest
dieselbe Versionsdatei und prüft Dateiname und Image-Kennung.

Der Release-Workflow `.github/workflows/release.yml` baut Toy Piano aus `main` und
erzeugt `v<version>` mit BIN, ELF und `SHA256SUMS`. Er liest die Änderungen aus
`instruments/toy-piano/CHANGELOG.md`. Tags und alle Releases einschließlich Entwürfen
werden vorab geprüft; eine vorhandene Version wird abgewiesen. API-Fehler brechen ab.

1. Für eine neue Version die Projekt-Versionsdatei und den Projekt-Changelog ändern.
2. Nach dem Merge startet der Workflow bei Änderungen der Versionsdatei automatisch;
   alternativ *Actions → Release → Run workflow* auf `main`.
3. Standardmäßig bleibt das Release ein **Entwurf** (`publish=false`). Die BIN am
   Gerät testen und erst danach den Entwurf veröffentlichen. Dabei erzeugt GitHub
   den Tag auf dem tatsächlich gebauten Commit. `publish=true` veröffentlicht sofort.

Nur der Release-Job fordert `contents: write` an; normale Builds haben Leserechte.
Der vorhandene Name `v0.2` bleibt für Toy Piano erhalten. Für weitere Instrumente
müssen vor deren erstem Release eigene Versions-/Tagregeln ergänzt werden.
