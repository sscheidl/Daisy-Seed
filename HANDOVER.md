# Übergabe — lokal weiterarbeiten

**Stand:** 24. Oktober 2026 (Korrekturauftrag umgesetzt, Cloud-Sitzung). Ab jetzt wird lokal unter
Windows mit Claude Code und Codex weitergearbeitet, mit USB-Zugriff auf das Seed3
Desktop Kit. Für einen neuen Chat genügt der Einstiegstext in § 7.

## 1. Wo der Code liegt

| Ort | Inhalt |
| --- | --- |
| `main` | PR #22 (Modul), #23 (Toy Piano v0.3, Physical Modeling `MOD=1`, Browser-Editor), #24 (JUCE-Editor, Übergabe), #27 (kostenbewusste CI) |
| Branch **`ccr-352fb5fc-fxr9ox`** (PR #29) | **noch nicht auf `main`**: `67eb8da` Editor-Fixes aus dem ersten Hardwaretest, `70f1e4c` Physical Modeling mod2 (Level-Spalte, Potistellungen), dazu der Korrekturauftrag vom 24.10.2026 (Kompatibilitätsprüfung, Portfreigabe, Bereichsanzeige, Verbindungszustand, gemeinsame Aftertouch-Anzeige, Regressionstests) und die LFO-Quermodulation (mod3). Per PR zusammenführen |

```bash
git fetch origin
git checkout ccr-352fb5fc-fxr9ox && git pull      # neuester Stand
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
```

**CI** (seit PR #27):
* `build.yml` (Firmware) und `editor.yml` (JUCE-Editor mit Windows-`.exe`) starten nur
  von Hand: Actions → Workflow → „Run workflow“, Branch wählen.
* `quick.yml` führt bei Push und PR die betroffenen Host-Tests aus.

## 2. Was fertig ist

| Teil | Stand | Wo |
| --- | --- | --- |
| Mod on Daisy (Modul) | Kern, Host-Tests, Messfirmware, `mod_probe.py`, Protokoll 1.0; Erweiterung „Grundwerte im Monitor“ (optional, abwärtskompatibel) | `modules/mod-on-daisy/` |
| Toy Piano v0.3 | Matrix integriert; mit leerer Matrix bitgleich zu v0.2; Release-**Entwurf** v0.3 auf GitHub; sendet noch keine Grundwerte | `instruments/toy-piano/` |
| Physical Modeling + Matrix | eigene Fassung `MOD=1`, IDENTIFY `0.1.0-mod3`: Spalte 7 = **Level** (statt Reverb), meldet Grundwerte, erweiterter Monitor, LFO-Quermodulation (Fähigkeiten `0x7A`); reguläre Fassung bytegleich | `instruments/physical-modeling/` |
| JUCE-Editor | Standalone (Windows-`.exe` aus der CI), Simulator, Selbsttest, Potistellungen in den Spaltenköpfen | `editors/mod-on-daisy/src/` |
| Browser-Editor | Schnellvariante, gleiche Dateien und Anzeigen | `editors/mod-on-daisy/web/` |
| Übergabe-Werkzeuge | Laufzettel, Paket-Skript, Agentenregeln | [HARDWARE_TEST_RUN.md](modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md), `scripts/hardware_test_package.py`, [AGENTS.md](AGENTS.md) |

Prüfwerte zum Wiedererkennen:

| Was | Wert |
| --- | --- |
| Golden-Hash Toy Piano (v0.2 = v0.3 mit leerer Matrix) | `52580874FA24361F` |
| Golden-Hash Physical Modeling (regulär = `MOD=1` mit leerer Matrix) | `37DE0BF9D93B81CE` |
| SHA-256 `ToyPiano-v0.3.bin` (= Release-Entwurf) | `6dd79871…` |
| SHA-256 `PhysicalModeling-v0.1.0.bin` (regulär) | `eb8f412a…` |
| SHA-256 `ModBench-v0.1.0.bin` / `-block256` / `-max64` | `9f50a57b…` / `5cdeb0f6…` / `7f209c15…` |
| SHA-256 `PhysicalModelingMod-v0.1.0.bin` (mod3, Branch) | `ccabdaa9…` |
| Standard-Patch-CRC / Patch-Testvektor | `61C264C4` / `E2340CC6` |
| dasselbe in Format 2 (mit LFO-Quermodulation) | `C422B385` / `9D6E020E` |

## 3. Hardwaretest: Stand

Laufzettel: [modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md](modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md).
Ergebnisse: [VALIDATION.md](modules/mod-on-daisy/docs/VALIDATION.md) § 1b „Am Gerät“.

**`GEMESSEN` am 23.10.2026** (Physical Modeling `MOD=1`, Stand PR #24, JUCE-Editor):

| Schritt | Ergebnis |
| --- | --- |
| E1 | bestanden: Verbindung, alle 8 Spalten |
| E2–E8 | bestanden bis auf die Befunde unten |
| E9 | Poly AT → Brightness bestätigt, Poly AT × Excitation schraffiert |
| R8 | MIDI-Clock synchronisiert die LFOs |

**Befunde und was daraus wurde** (Branch, am Gerät noch nicht nachgeprüft):

1. Kontextmenü an fester Stelle und in Systemfarben → öffnet jetzt am Mauszeiger, in
   App-Farben.
2. Schnelle LFOs schienen zu hängen (20 Hz Abtastung) → Band über 5 Hz.
3. Studio One belegt den Port exklusiv → Meldung nennt die Ursache, Anzeige „keine
   Live-Daten“. **Lösung offen**, siehe § 4.
4. LFO-Anzeige im Clock-Modus blieb aus → am Simulator nicht nachvollziehbar.
   Wahrscheinlich Folge von 3. Quellen ohne aktive Verbindung sind jetzt abgedunkelt.
5. „Vom Gerät laden“ ohne erkennbare Wirkung → meldet jetzt die geladene Prüfsumme,
   mit Tooltip.
6. Spalte Reverb wirkungslos → **Level** (Entscheidung Betreiber).
7. Potistellungen fehlten → Grundwerte im Monitor, Strich und Balken in den Editoren
   (Entscheidung Betreiber).

**Noch offen am Gerät:**
* Physical Modeling mit Branch-Stand (`mod3`) nachprüfen: E9b (Grundwert, Level), E7b in
  beiden Reihenfolgen (Windows MIDI Services), E11–E17 (Bereiche, Aftertouch, FM,
  Kompatibilitätsfrage, Portfreigabe, Verbindungszustand), R1–R7, R9, R10.
* Toy Piano T1–T15 und E1–E10 mit Toy Piano.
* Messfirmware H1–H8, S0–S8.

Für den Test vom Branch neu bauen:
* Firmware: `make PROJECT=physical-modeling-mod` oder `python scripts/hardware_test_package.py`.
* Editor: `.exe` über Actions → Editor auf dem Branch oder lokal mit CMake.

## 4. Offene Entscheidungen und Aufgaben

| Thema | Stand | Nächster Schritt |
| --- | --- | --- |
| Studio One + Editor gleichzeitig | Windows gibt den Port einer Anwendung | E7b prüfen. Wenn Windows MIDI Services nicht hilft: Durchleitung im Editor (Editor hält den Daisy-Port, Studio One nutzt einen virtuellen Port, z. B. loopMIDI) — auf Wunsch umsetzen |
| Branch → `main` | PR #29 offen | Review, mergen |
| Toy Piano v0.3 veröffentlichen | Entwurf wartet auf T1–T15 | entscheidet der Mensch |
| Grundwerte für Toy Piano | absichtlich aus, damit v0.3 dem Entwurf entspricht | mit der nächsten Toy-Piano-Version (z. B. 0.3.1) einschalten |
| JUCE-Lizenz | AGPLv3 oder JUCE-Lizenz | vor Weitergabe der `.exe` entscheiden |
| Speicher (QSPI) | nur spezifiziert (D8) | eigene Freigabe nötig |
| Editor-Feinschliff | offen: Rückgängig/Wiederholen, Wahl beim Wiederverbinden, Schema-Cache | nach den Hardwaretests |
| Nebulae als dritter Pilot, VST3 | geplant | später |

## 5. Lokal einrichten (Windows)

| Wofür | Was gebraucht wird |
| --- | --- |
| Firmware bauen | `make` und xPack GCC **13.2.1-1.1** (auch für Windows) wie beim bisherigen Nebulae-Build; alternativ WSL mit `bash scripts/install-toolchain.sh` |
| Nur testen | Images aus Actions → **Firmware** (von Hand starten) |
| JUCE-Editor | `.exe` aus Actions → **Editor** oder Visual Studio 2022 + CMake ([Editor-README](editors/mod-on-daisy/README.md)) |
| `mod_probe.py`, Paket-Skript | Python 3, `pip install mido python-rtmidi` |
| Flashen | Daisy Web Programmer (Chrome/Edge) oder `dfu-util` ([docs/FLASHING.md](docs/FLASHING.md)) |

Kurzprobe:

```bash
make PROJECT=toy-piano test
make -C modules/mod-on-daisy test
make -C editors/mod-on-daisy test
python modules/mod-on-daisy/tools/mod_probe.py selftest
```

## 6. Wichtige Dokumente

* Regeln für Agenten: [AGENTS.md](AGENTS.md). Hardware und Releases bleiben beim Menschen;
  Ergebnisse nur wie beobachtet; Golden-Tests; feste Versionen.
* Modul: [DECISIONS](modules/mod-on-daisy/docs/DECISIONS.md),
  [ARCHITECTURE](modules/mod-on-daisy/docs/ARCHITECTURE.md),
  [PROTOCOL](modules/mod-on-daisy/docs/PROTOCOL.md) (SysEx 0x7D, Monitor 0x7E, Grundwerte § 3.3),
  [STORAGE](modules/mod-on-daisy/docs/STORAGE.md), [VALIDATION](modules/mod-on-daisy/docs/VALIDATION.md).
* Instrumente: [Toy Piano HARDWARE_TEST_V0.3](instruments/toy-piano/docs/HARDWARE_TEST_V0.3.md),
  [Physical Modeling MODULATION](instruments/physical-modeling/docs/MODULATION.md).
* Editoren: [README](editors/mod-on-daisy/README.md) (inkl. Studio One / Windows MIDI
  Services), [GUI.md](editors/mod-on-daisy/docs/GUI.md).

## 7. Einstiegstext für einen neuen Chat

Zum Kopieren in einen neuen Chat (Claude Code oder Codex, lokal im Repo-Ordner):

```text
Projekt: sscheidl/Daisy-Seed, Mod on Daisy (8x8-Modulationsmatrix für Seed3 Desktop),
lokal unter Windows mit USB-Zugriff auf das Kit.

Bitte zuerst lesen: HANDOVER.md, AGENTS.md, modules/mod-on-daisy/docs/HARDWARE_TEST_RUN.md
und modules/mod-on-daisy/docs/VALIDATION.md § 1b.

Stand: main enthält PR #22–#24 und #27. Der Branch ccr-352fb5fc-fxr9ox (PR #29) hat
mehr: Editor-Fixes aus dem ersten Hardwaretest, Physical Modeling mod2/mod3 (Spalte 7 =
Level, Grundwerte, erweiterter Monitor, LFO-Quermodulation) und den Korrekturauftrag
vom 24.10.2026 in beiden Editoren. Am Gerät noch nicht nachgeprüft.

Nächste Aufgaben:
1. Branch ccr-352fb5fc-fxr9ox auschecken, Host-Tests laufen lassen, PR nach main anlegen.
2. PhysicalModelingMod neu bauen und mit dem neuen JUCE-Editor E9b, E7b und E11–E17 aus
   dem Laufzettel durchgehen (ich höre und bediene, du führst die Befehle aus und trägst
   die Ergebnisse als GEMESSEN mit Datum ein).
3. Je nach E7b: Studio-One-Koexistenz lösen (Windows MIDI Services oder Durchleitung
   über einen virtuellen Port im Editor).
4. Danach Toy Piano T1–T15 und Messfirmware H/S.

Regeln: Flashen nur nach meiner Freigabe im Bootloader-Modus, keine Releases
veröffentlichen, Ergebnisse nur wie beobachtet, Golden-Hashes und bytegleiche
Images (Toy Piano v0.3, reguläres Physical Modeling, ModBench) nicht versehentlich ändern.
```
