# Übergabe — lokal weiterarbeiten

**Stand Acid Seq 28.10.2026:** Phasen 0–6 (PR #54–#60) und die Nachprüfung (PR #61)
sind in `main`. Codex hat zwei Prüfungen gemacht: das Review des Arbeitsauftrags
([CODEX_REVIEW.md](docs/acid-seq/CODEX_REVIEW.md), R01–R16, vor der Umsetzung
eingearbeitet) und die Fehlersuche über alle Phasen
([CODEX_FEHLERSUCHE.md](docs/acid-seq/CODEX_FEHLERSUCHE.md), Stand `d3297fd`, Verdikt
damals NICHT BEREIT: 6 P1, 11 P2, 1 P3). Alle 18 Befunde F01–F18 sind mit
Regressionstests behoben ([Arbeitsauftrag § 10](docs/acid-seq/ARBEITSAUFTRAG.md));
Q1–Q4 entschieden (D16). Nach dem Merge auf `main` @ `d2a3e1f` GEMESSEN 27.10.2026:
ASan/UBSan und TSan für alle elf Ziele PASS, JUCE 9.0.4 Selbsttest PASS, Browser-e2e
grün, Goldens unverändert. Eine erneute Codex-Prüfung der Korrekturen gibt es nicht;
das Verdikt in CODEX_FEHLERSUCHE.md bezieht sich auf den Stand vor den Korrekturen.
Offen: Hardwaretests ([Laufzettel](sequencers/acid-seq/docs/HARDWARE_TEST.md), alle
NICHT GEMESSEN) und der Release-Entwurf `acid-seq-v0.1.0-test1` (Draft, nicht
veröffentlicht). Beides entscheidet der Benutzer.

**Integration 25./26.10.2026:** Branch `integration/shimmer-bootloader` führt PR #40
(Bootloader-Einstieg), #38 (modulare FX) und #41 (Toy Piano + Ambient mit Shimmer)
zusammen. Testimages: `toy-piano-ambient 0.1.2` und `reverb-fx 0.3.1`; Komponente
`bootloader-entry 1.0.0`. Wegen des Bootloader-Einstiegs ebenfalls neu versioniert: Toy Piano 0.3.1,
Physical Modeling 0.1.1 (MOD-IDENTIFY `0.1.1-mod4`), PM-FX 0.2.1, Nebulae 0.1.1, Looper 0.3.1,
Mod bench 0.1.1, Diagnose 0.2.1, Panels 0.1.1. Prüfbericht und Laufzettel:
[docs/HARDWARE_TEST_INTEGRATION.md](docs/HARDWARE_TEST_INTEGRATION.md). Hardware
**NICHT GEMESSEN**.

**Ergänzung 26.10.2026 (Tape FX):** Neue Komponente `tape-fx 0.1.0` in
`effects/tape-fx/` (Port von drumlogue TapeVibe, Referenzpfad bitgleich, optionale
MixForge-Stufen) mit zwei neuen Buildzielen: `tape-fx` (Stereo-Effekt) und
`toy-piano-tape` (Toy Piano → Tape FX). Bestehende Images bytegleich, Stardust/Looper
unverändert. [Bericht](effects/tape-fx/docs/REPORT.md),
[Hörtest-Laufzettel](effects/tape-fx/docs/HARDWARE_TEST.md). Hardware **NICHT GEMESSEN**.

**Ergänzung 25.10.2026:** Auf `feat/toy-piano-ambient` liegt das separate Produkt
`toy-piano-ambient 0.1.1`, abhängig vom modularen FX-Unterbau in PR #38.
[Bedienung, Build, Prüfergebnisse und Hardware-Laufzettel](instruments/toy-piano/docs/AMBIENT.md).
Host-Tests, ASan/UBSan und ARM-Build bestanden; Toy Piano v0.3 bleibt bytegleich.
Sechs echte Reverb-Modi einschließlich Shimmer (B / unten); VR7 regelt dessen
Oktavanteil. Kern `ambient-reverb 0.3`, reine FX-Fassung: Shimmer auf S6.
Gemeinsame CPU-Last und Audiotest am Kit sind **NICHT GEMESSEN**.
Die folgenden älteren Abschnitte beschreiben ihren jeweils datierten Stand.

**Stand:** 24. Oktober 2026 abends (PR #29 gemergt, mod3 geflasht, lokale Sitzung; § 3a).
Zuvor: Korrekturauftrag umgesetzt in der Cloud-Sitzung. Ab jetzt wird lokal unter
Windows mit Claude Code und Codex weitergearbeitet, mit USB-Zugriff auf das Seed3
Desktop Kit. Für einen neuen Chat genügt der Einstiegstext in § 7.

## 1. Wo der Code liegt

| Ort | Inhalt |
| --- | --- |
| `main` | PR #22 (Modul), #23 (Toy Piano v0.3, Physical Modeling `MOD=1`, Browser-Editor), #24 (JUCE-Editor, Übergabe), #27 (kostenbewusste CI) |
| `main` seit 24.10.2026 abends | dazu PR #29: Editor-Fixes aus dem ersten Hardwaretest, Physical Modeling mod2 (Level-Spalte, Potistellungen), Korrekturauftrag vom 24.10.2026, LFO-Quermodulation (mod3) und `mod_probe.py` für Windows (`bee9023`) |
| Branch **`feat/editor-no-simulators`** (PR #33) | Physical Modeling **mod4** (aktives Modell, Schutz gegen die Studio-One-Rückkopplung), Editor ohne Simulatoren, Spaltenwerte, Modellanzeige; am Kit mit Studio One getestet (§ 3b) |
| Branch **`docs/modular-kernel-idea`** | diese Übergabe und die Idee [Modularer Firmwarekern](docs/MODULAR_KERNEL_IDEA.md) |

```bash
git fetch origin
git checkout main && git pull                     # neuester Stand
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
| SHA-256 `ToyPiano-v0.4.bin` (Rückkopplungsschutz, Branch `feat/toy-piano-echo-guard`) | `20a5ea23…` |
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

Für den Test neu bauen (lokal unter Windows gebaut am 24.10.2026: xPack-GCC aus `.tools/`,
Editor mit VS Build Tools 2022):
* Firmware: `make PROJECT=physical-modeling-mod` oder `python scripts/hardware_test_package.py`.
* Editor: `.exe` über Actions → Editor auf dem Branch oder lokal mit CMake.

## 3a. Hardwaretest am 24.10.2026 abends (lokal, Windows 11)

**`GEMESSEN` am 24.10.2026:**

| Was | Ergebnis |
| --- | --- |
| Image | `PhysicalModelingMod-v0.1.0.bin` lokal gebaut, SHA-256 `ccabdaa9…` (= Übergabewert mod3), mit dem Daisy Web Programmer geflasht |
| USB nach dem Flashen | zunächst Gerätemanager-Fehlercode 10 (`VID_0483 PID_5740`, ohne Name/Seriennummer); nach Abziehen und Einstecken an J7 sauber als „Daisy Seed External“ |
| IDENTIFY (19:14, `mod_probe.py`) | `physical-modeling`, `0.1.0-mod3`, Fähigkeiten 122 (`0x7A`), Protokoll 1.0, Modul 0.1.0, Schema `942B2D07` |
| `targets` | Pitch, Structure, Brightness, Damping, Position, Excitation (Anschlag), **Level**, Dry/Wet |
| `matrix` | leer, Patch-CRC `C422B385` (Standard-Patch Format 2) |

**Befunde:**
* `mod_probe.py` fand unter Windows den Eingang nicht (rtmidi hängt je Richtung eine andere
  Nummer an: „Daisy Seed External 18“/„… 19“), `matrix` brach in der cp1252-Konsole ab →
  behoben in `bee9023`, auf `main`.
* Der JUCE-Editor war mit einem **Simulator** verbunden statt mit dem Kit („toy-piano 0.3“,
  danach „Simulator: Physical Modeling“ mit dessen festen Grundwerten). Der Simulator meldet
  dasselbe IDENTIFY wie die Firmware und war nicht als Simulator erkennbar → Branch
  `feat/editor-no-simulators`. **E9b ist deshalb noch nicht am Kit gelaufen** (`NICHT GEMESSEN`).

**Gesammelte Wünsche für das nächste Editor-/Firmware-Update** (noch nicht umgesetzt außer 1–2):
1. Simulatoren nur noch mit `--simulators` — Branch `feat/editor-no-simulators`.
2. Daisy-Port wird auch vorgeschlagen, wenn das Kit erst nach dem Editor erscheint — derselbe Branch.
3. Prozentanzeige im Spaltenkopf zeigt den **aktuellen Wert** der Reglerstellung statt des festen
   Hubs „100 %“ (Hub z. B. klein dahinter oder als Tooltip).
4. **Aktives Modell** (SW17/SW18 bzw. CC28) oben im Verbindungsbereich, live. Braucht Firmware
   und Protokoll (optionales Monitorfeld), also neues Image.
5. **FX-Alternativen** zum Torus-Reverb („Rings into Clouds“: Stereo Delay, Ambient Reverb,
   Granular à la Clouds) — aufgegangen in der Idee [Modularer Firmwarekern](docs/MODULAR_KERNEL_IDEA.md).

Punkte 1–4 sind in PR #33 umgesetzt (§ 3b).

## 3b. mod4 am Kit, Studio One parallel (24.10.2026, ab 21:55)

* **Ursache von E7b gefunden** (Agent, `mod_probe.py` parallel zu Studio One): Windows MIDI
  Services lässt beide Programme an den Port; Befehle wirkten, Antworten fehlten. Studio One
  schickt per Input-Monitoring alles vom Daisy zurück; die Firmware beantwortete jeden
  zurückgeschickten Antwortrahmen mit NAK → endlose NAK-Schleifen (rund 500 Rahmen/s).
  Behoben in mod4: Rahmen mit Antwortbit werden nie beantwortet (PROTOCOL § 2, Schritt 2a).
* **`GEMESSEN`** mit mod4 (`9517db27…`): IDENTIFY, `modes` und Monitor (61 Rahmen in 3 s) parallel
  zu Studio One. Vom Betreiber berichtet: **E7b bestanden** (Studio One zuerst, dann Editor:
  Status grün, Modulation hörbar, CRC bestätigt), Spaltenwerte folgen den Potis, Modell wechselt
  mit SW17/SW18. Einzelheiten: VALIDATION § 1b.
* `NICHT GEMESSEN`: umgekehrte Reihenfolge (Editor zuerst), Modwheel → Level, CC28 „(MIDI)“, E19,
  E11–E17, R1–R7, R9, R10.
* **Toy Piano v0.3 hat den Rückkopplungsschutz noch nicht** (Image soll bytegleich zum
  Release-Entwurf bleiben) und hätte mit Studio One dasselbe Problem → umgesetzt in
  **Toy Piano v0.4** (25.10.2026, Freigabe Betreiber; Branch `feat/toy-piano-echo-guard`):
  zunächst nur die Regel aus PROTOCOL § 2, Schritt 2a. **Nach dem Merge von PR #42
  (26.10.2026)** mit allen Mod-on-Daisy-Erweiterungen wie Physical Modeling: Grundwerte
  (Potistellungen), erweiterter Monitor, LFO-Quermodulation; Klang mit leerer Matrix
  unverändert. Am Kit `NICHT GEMESSEN` (T16/T17 in
  [HARDWARE_TEST_V0.4](instruments/toy-piano/docs/HARDWARE_TEST_V0.4.md)).

**Weiter mit:** PR #33 mergen; restliche Schritte aus dem Laufzettel.

## 4. Offene Entscheidungen und Aufgaben

| Thema | Stand | Nächster Schritt |
| --- | --- | --- |
| Studio One + Editor gleichzeitig | **gelöst** mit mod4 (§ 3b): Windows MIDI Services teilt den Port, die Rückkopplungsschleife ist behoben | umgekehrte Reihenfolge noch prüfen; VST3-Fassung bleibt wünschenswert, ist dafür aber nicht mehr nötig |
| PR #33 (mod4, Editor) | am Kit getestet, Quick checks | mergen |
| Rückkopplungsschutz für Toy Piano | in v0.4 umgesetzt (Branch `feat/toy-piano-echo-guard`), Host-Tests und ARM-Build lokal unter Windows | am Kit T16/T17 prüfen; Linux-Golden-Hash und TSan noch offen |
| Modularer Firmwarekern | Idee, [MODULAR_KERNEL_IDEA.md](docs/MODULAR_KERNEL_IDEA.md), GitHub-Milestone | ausgearbeiteten Plan schreiben, später |
| Toy Piano veröffentlichen | Entwurf v0.3 wartet auf T1–T15; v0.4 = v0.3 + Rückkopplungsschutz | entscheidet der Mensch (v0.3-Entwurf oder v0.4) |
| Grundwerte für Toy Piano | in v0.4 eingeschaltet (volles Panel wie Physical Modeling) | am Kit T18/T19 prüfen |
| Lizenz | **entschieden 24.10.2026**: Open Source; JUCE-Editor und VST3-Plugin AGPLv3, alles Eigene sonst MIT (README § License) | Repo ist öffentlich (Actions-Minuten dafür kostenlos); Inhalt und Historie noch auf fremdes Material prüfen |
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

Stand: main enthält PR #22–#24, #27 und #29 (Physical Modeling mod3 mit LFO-Quermodulation,
Editor-Fixes, mod_probe für Windows). mod3 ist auf dem Kit geflasht, IDENTIFY bestätigt
(HANDOVER.md § 3a). PR #33 (Branch feat/editor-no-simulators): Physical Modeling mod4 mit
Modellanzeige und Schutz gegen die Studio-One-Rückkopplung, Editor ohne Simulatoren; am Kit
mit Studio One getestet (§ 3b). Idee für später:
docs/MODULAR_KERNEL_IDEA.md (GitHub-Milestone „Modularer Firmwarekern“).

Nächste Aufgaben:
1. PR #33 mergen (falls noch offen).
2. Mit dem JUCE-Editor am Kit die offenen Schritte aus § 3b und E11–E17 durchgehen (ich höre
   und bediene, du führst die Befehle aus und trägst die Ergebnisse als GEMESSEN mit Datum ein).
3. Rückkopplungsschutz für Toy Piano: v0.4, am Kit noch zu prüfen (T16/T17).
4. Danach Toy Piano T1–T15 und Messfirmware H/S; VST3-Fassung planen.

Regeln: Flashen nur nach meiner Freigabe im Bootloader-Modus, keine Releases
veröffentlichen, Ergebnisse nur wie beobachtet, Golden-Hashes und bytegleiche
Images (Toy Piano v0.3, reguläres Physical Modeling, ModBench) nicht versehentlich ändern.
```
