# Reproduzierbarer Build

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
make -j4
make test
make test-sanitize
python3 scripts/check-firmware.py
```

`make libs` baut beide Bibliotheken explizit, beispielsweise nach einer bewusst
geänderten Abhängigkeit. `make clean` entfernt nur den Anwendungsbuild.
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

Generierte Dateien und die Toolchain bleiben aus Git ausgeschlossen.
Der GitHub-Actions-Workflow lädt nach erfolgreichem Build die BIN, ELF und
Prüfsumme als Artefakt hoch. In einem privaten Repository ist dafür eine
GitHub-Anmeldung mit Zugriff auf das Repository nötig.

`make test` prüft DSP und den echten Anwendungscallback mit simulierten
Hardwareeingängen und der unveränderten DigitalControl-Entprellung aus libDaisy.
`make test-sanitize` führt beide Tests mit AddressSanitizer und UndefinedBehaviorSanitizer
aus. `-fno-sanitize-recover=all` macht jeden erkannten Fehler zum Testfehler.
Die GPIO-/ADC-Stubs ersetzen keinen Hardwaretest.

## Version

Die Version („major.minor“) steht ausschließlich in `src/version.h`. Daraus entstehen:

* der Dateiname `build/ToyPiano-v<version>.bin` (Makefile, `make -s version` gibt sie aus),
* die Zeichenkette `TOYPIANO-FW-VERSION=<version>` im Image,
* der Name des GitHub-Actions-Artefakts.

`scripts/check-firmware.py` prüft, dass Dateiname, eingebettete Version und `src/version.h`
übereinstimmen. Zum Erhöhen die Zahl in `src/version.h` ändern, `CHANGELOG.md` ergänzen und
neu bauen; ein `make clean` ist nicht nötig, alte Dateien anderer Versionen bleiben aber in
`build/` liegen, bis `make clean` läuft. Git-Tags werden nicht von `make` gesetzt, das
übernimmt der Release-Workflow.

## Release

Der Workflow `.github/workflows/release.yml` baut die Firmware aus `main`, führt alle Host-Tests
und die Binärprüfung aus und legt ein GitHub-Release `v<version>` mit BIN, ELF und `SHA256SUMS`
an. Der Release-Text besteht aus dem Abschnitt der Version im `CHANGELOG.md` und den Angaben zur
BIN (Größe, Prüfsumme, Commit). Ein vorhandenes Release oder ein vorhandener Tag wird nie
überschrieben; dann bricht der Workflow ab.

1. Version in `src/version.h` erhöhen, Abschnitt im `CHANGELOG.md` ergänzen, per PR nach `main`.
2. *Actions → Release → Run workflow* (Branch `main`). Der Workflow startet auch von selbst, wenn
   ein Push auf `main` `src/version.h` ändert.
3. Standardmäßig entsteht ein **Entwurf** (Haken „publish“ nicht gesetzt). BIN herunterladen,
   flashen und testen, dann den Entwurf auf der Releases-Seite **veröffentlichen**. Erst dabei
   legt GitHub den Tag `v<version>` auf den gebauten Commit. Mit gesetztem Haken „publish“
   wird sofort veröffentlicht.

Für den Release-Schritt hat der Workflow Schreibrechte auf den Inhalt (`contents: write`),
sonst nur Leserechte.
