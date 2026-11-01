# Softwareprotokoll Programmverwaltung

Nur ausgeführte Prüfungen; Hardware durchgehend **NICHT GEMESSEN**. Umgebung: Windows 11,
xPack GCC 13.2.1-1.1, GNU Make 4.4.1, clang 23.1.3 (llvm-mingw, Host-Tests),
Python 3.12.10; Basis `origin/main` `ab79ccf` (PR #79), libDaisy `f715e487`,
DaisySP `2c72eaf9`, keine Abhängigkeit geändert.

## 01.11.2026 – Phasen 0–3, 5 (Software)

| Zeit | Befehl | Ergebnis |
| --- | --- | --- |
| 20:40 | `make -C system/program-manager test HOST_CXX=clang++ HOST_CC=clang` | test_sha256 14/0, test_image 51/0, test_catalog 95/0, test_loader 77/0, test_manager 125/0, test_protocol 96/0, test_diag_hooks 26/0, test_return_entry 349/0 (Prüfungen/Fehler) |
| 20:45 | `make -C diagnostics/firmware test HOST_CXX=clang++` (Regression nach Hook/Adapter) | core 465, app 733, reader 4654, usb 53, dma 4262, abort 42 – 0 Fehler |
| 21:05 | `python -I -m unittest discover -s system/program-manager/tests/python` | 5 Tests OK |
| 21:12 | `make -C diagnostics/firmware -j8` und `CARD_READER=1`, `make check` | beide Varianten gebaut (130 788 B / 138 416 B), BOOT_SRAM-Vektoren und Symbolpolitik PASS |
| 21:13 | `make -C system/program-manager PROFILE=probe -j8`, `make -C system/program-manager -j8` | Probe: QSPI 141 128 B, DTCM 101 360 B, SRAM_TARGET 0; Manager: QSPI 156 752 B, DTCM 104 200 B, SRAM_HELPER 4 096 B, RAM_D2_DMA 17 096 B, SRAM_TARGET 0 |
| 21:20 | `make … check PYTHON=python` beide Profile | `PASS: BOOT_QSPI placement, free SRAM target, vectors, marker`; Manager zusätzlich `PASS sd symbol policy: 39 banned, 6 required` |
| 21:22 | `python scripts/component_manifest.py --validate`, `python -m unittest discover -s tests -p test_components.py` | 25 Komponenten, 22 Produkte; 10 Tests OK |
| 21:23 | `python scripts/component_manifest.py --project program-manager --built` (und `-probe`) | `COMPONENTS.json` geschrieben |

Sanitizer-Lauf (`make test-sanitize`) und TSan: siehe Einträge unten, sobald ausgeführt.

## Artefakte (letzter Build dieses Protokolls)

Die BIN-Hashes hängen vom eingebetteten `build=`-Commit ab; Werte des finalen Builds
stehen im PR-Text und werden hier nachgetragen:

| Profil | Datei | Bytes | SHA-256 | Quellcommit |
| --- | --- | --- | --- | --- |
| probe | `build/probe/DaisyProgramManagerProbe-v0.1.0.bin` | 141 128 | `7fc2ce90…9986c8` | `ff6c634` (+ Marker-Fix) |
| manager | `build/manager/DaisyProgramManager-v0.1.0.bin` | 156 752 | `9149288b…351a0` | `ff6c634` (+ Marker-Fix) |

## Bewusst nicht ausgeführt / offen

* **RM0433**: nicht beschaffbar (CONTRACT §11 C1).
* **§12 Fable-Versuch**: nicht durchgeführt (Entscheidung des Nutzers am 01.11.2026 wegen
  Kontext-/Budgetgrenzen; die Sitzung lief ohnehin auf Fable 5.1).
* **CI**: keine Änderung an `quick.yml`; Host-Tests lokal.
* **JUCE/VST3**: nachgelagert, nur Semantik in PROTOCOL.md beschrieben.
* **Rückkehrdienst**: Integration in Produkte siehe Abschnitt „Produkte" (wird mit
  Phase 4 ergänzt); übrige Produkte zunächst Legacy.

## Produkte (Imageprofil / NOLOAD / Rückkehr)

| Produkt | `dsy_pack.py check` | NOLOAD im Ziel | Rückkehrdienst |
| --- | --- | --- | --- |
| (wird mit Phase 4 ausgefüllt) | | | |
