# Übergabe — lokal weiterarbeiten

**Stand:** 23. Oktober 2026, Ende der Cloud-Sitzung. Ab jetzt wird lokal unter Windows
mit Claude Code und Codex weitergearbeitet, mit USB-Zugriff auf das Seed3 Desktop Kit.

## 1. Was fertig ist

| Teil | Stand | Wo |
| --- | --- | --- |
| Mod on Daisy (Modul) | Phase 2 fertig: Kern, Host-Tests, Messfirmware, `mod_probe.py` | `modules/mod-on-daisy/` |
| Toy Piano v0.3 | Matrix integriert; mit leerer Matrix bitgleich zu v0.2; Release-**Entwurf** v0.3 auf GitHub | `instruments/toy-piano/` |
| Physical Modeling + Matrix | eigene Fassung `MOD=1`; reguläre Fassung bytegleich | `instruments/physical-modeling/` |
| Browser-Editor | nutzbar, Ende-zu-Ende-Test gegen den Firmware-Controller | `editors/mod-on-daisy/web/` |
| JUCE-Editor | Windows-`.exe` aus der CI, eingebauter Simulator, Selbsttest | `editors/mod-on-daisy/src/` |
| Hardwaretests | **alle offen**, Laufzettel bereit | [HARDWARE_TEST_RUN.md](modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md) |

Auf `main` sind PR #22 (Modul) und PR #23 (Toy Piano v0.3, Physical Modeling, Browser-Editor).
Der JUCE-Editor und diese Übergabe liegen auf dem Branch `ccr-352fb5fc-fxr9ox`. Nach
dem Merge ist alles auf `main`.

## 2. Lokal einrichten (Windows)

```bash
git clone https://github.com/sscheidl/Daisy-Seed.git
cd Daisy-Seed
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
```

Ein vorhandener Checkout: `git checkout main && git pull`. Die Submodule bleiben auf den
gepinnten Ständen ([docs/BUILD.md](docs/BUILD.md)).

| Wofür | Was gebraucht wird |
| --- | --- |
| Firmware bauen | `make` und die ARM-Toolchain wie beim bisherigen lokalen Nebulae-Build. Getestet ist xPack GCC **13.2.1-1.1**; es gibt sie auch für Windows. Alternativ WSL mit `bash scripts/install-toolchain.sh` |
| Nur testen, nicht bauen | die Images aus der CI: GitHub → Actions → **Firmware** → Artifacts |
| Host-Tests | `g++` (MSYS2/WSL) oder die CI |
| JUCE-Editor | `.exe` aus Actions → **Editor** → Artifacts, oder Visual Studio 2022 mit CMake ([Editor-README](editors/mod-on-daisy/README.md)) |
| `mod_probe.py`, Paket-Skript | Python 3 und `pip install mido python-rtmidi` |
| Flashen | Chrome/Edge mit dem [Daisy Web Programmer](https://flash.daisy.audio/) oder `dfu-util` ([docs/FLASHING.md](docs/FLASHING.md)) |

Kurzprobe nach dem Einrichten:

```bash
make PROJECT=toy-piano test
make -C modules/mod-on-daisy test
make -C editors/mod-on-daisy test
python modules/mod-on-daisy/tools/mod_probe.py selftest
```

## 3. Hardwaretest

1. Paket bauen: `python scripts/hardware_test_package.py`. Das baut alle Images, prüft
   sie und legt sie mit `mod_probe.py`, den Editoren und dem Laufzettel in
   `build/hardware-test/` ab. Ohne lokale Toolchain mit `--no-build` und den CI-Images
   an ihren Build-Pfaden.
2. Den [Laufzettel](modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md) abarbeiten:
   * V: Vorbereitung
   * H/S: Messfirmware; zuerst `lengths`, sonst lohnt der Rest nicht
   * T1–T15: Toy Piano
   * R1–R10: Physical Modeling
   * E1–E10: Editoren am Gerät
   Jede Zeile sagt, ob der Agent oder der Mensch sie ausführt.
3. Die Ergebnisse auf einem Branch eintragen und per PR zusammenführen.

## 4. Offene Entscheidungen (beim Menschen)

* **Toy Piano v0.3 veröffentlichen**: Der Entwurf auf GitHub → Releases wartet auf
  bestandene T1–T15.
* **JUCE-Lizenz**: Vor einer Weitergabe der `.exe` an andere zwischen AGPLv3 und einer
  JUCE-Lizenz entscheiden ([Editor-README](editors/mod-on-daisy/README.md#lizenz)).
* **Speicher (QSPI)**: Nur spezifiziert ([STORAGE.md](modules/mod-on-daisy/docs/STORAGE.md)).
  Schreibfunktionen brauchen eine eigene Freigabe (D8).
* **Portzugriff im VST3-Plugin**: Darüber entscheidet das Ergebnis von H4 bzw. E7.

## 5. Vorgeschlagene nächste Schritte

1. Hardwaretest nach Laufzettel, Befunde beheben.
2. Toy Piano v0.3 veröffentlichen.
3. Editor-Feinschliff, der nach dem Hardwaretest ansteht. Bekannt offen:
   Rückgängig/Wiederholen, beim Wiederverbinden die Wahl „vom Gerät laden / zum Gerät
   senden“, Schema-Cache für die Offline-Bearbeitung ([GUI.md](editors/mod-on-daisy/docs/GUI.md)).
4. Speicherplätze im QSPI, nach Freigabe.
5. Nebulae als dritter Pilot (ARCHITECTURE.md § 6).
6. VST3-Fassung des Editors für Studio One.

Für Agenten gelten die Regeln in [AGENTS.md](AGENTS.md).
