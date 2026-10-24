# Laufzettel Hardwaretest — Mod on Daisy, Toy Piano v0.3, Physical Modeling, Editoren

**Stand:** 24. Oktober 2026 (Korrekturauftrag: E7b in zwei Reihenfolgen, E11–E17
neu). Ausgeführt wird lokal unter Windows am Seed3 Desktop Kit.
Die Firmware-Images und Werkzeuge sind gebaut und auf dem Host getestet. Am Gerät
wurde noch nichts gemessen.

Dieser Laufzettel fasst die einzelnen Checklisten zu einer Reihenfolge zusammen. Er
liegt auch als `README.md` im Hardwaretest-Paket.

**Rollen:**
* **[A] Agent** (Codex oder Claude Code lokal): Kommandozeile, USB-MIDI, Skripte,
  Ergebnisse eintragen.
* **[M] Mensch**: hören, Tasten und Regler, Keyboard, Kabel, BOOT/RESET. Bei
  Hörtests beschreibt der Mensch das Ergebnis, der Agent trägt es ein.

`P` steht für `python modules/mod-on-daisy/tools/mod_probe.py`, im Paket für
`python mod_probe.py`.

## 0. Vorbereitung

| Nr. | Rolle | Schritt |
| --- | --- | --- |
| V1 | A | Repo aktualisieren und das Paket bauen: `python scripts/hardware_test_package.py`. Ergebnis in `build/hardware-test/`. Ohne lokale ARM-Toolchain die CI-Artefakte nehmen (Actions → Firmware bzw. Editor) |
| V2 | A | JUCE-Editor: `ModOnDaisyEditor.exe` aus Actions → Editor holen oder selbst bauen ([Editor-README](../../../editors/mod-on-daisy/README.md)). Mit `--editor-exe <Pfad>` kommt er ins Paket |
| V3 | A | `pip install mido python-rtmidi`, dann `P selftest` (ohne Gerät): `PASS` |
| V4 | A | Selbsttest des JUCE-Editors unter Windows, Ergebnis in `out/selftest.txt` (endet mit `PASS`). Mit dem CI-Artefakt im entpackten Ordner: `.\ModOnDaisyEditor.exe --selftest out`. Mit eigenem Build vom Repo-Wurzelverzeichnis: `& "build/editor/ModOnDaisyEditor_artefacts/Release/Mod on Daisy Editor.exe" --selftest out` |
| V5 | M | Studio One schließen oder dort „Daisy Seed External“ deaktivieren. Windows gibt den Port nur einer Anwendung |
| V6 | M | Flashen nach [docs/FLASHING.md](../../../docs/FLASHING.md): J7, RESET, dann BOOT im 2-s-Fenster, Web Programmer oder `make PROJECT=<id> program-dfu`. Den externen Bootloader v6.x nur installieren, wenn er fehlt |

Agenten flashen nur, wenn der Mensch den Bootloader-Modus hergestellt und das Flashen
freigegeben hat.

## 1. Messfirmware ModBench (Grundlage)

Image `ModBench-v0.1.0.bin`, Anleitung in [bench/README.md](../bench/README.md), Plan in
[TEST_PLAN_PHASE2.md](TEST_PLAN_PHASE2.md) § 3–4. Ergebnisse in
[VALIDATION.md](VALIDATION.md) § 3.

| Nr. | Rolle | Schritt | Erwartet |
| --- | --- | --- | --- |
| H1 | A | `P ports` | „Daisy Seed External“ als Ein- und Ausgang |
| H2/H3 | A | `P identify`, dann `P lengths` | `mod-bench`; `frame lengths OK` in beide Richtungen |
| H4 | M+A | Studio One mit aktivem Port öffnen, dann `P identify` | Ergebnis notieren: lässt sich der Port gleichzeitig öffnen? |
| S0–S8 | A | `P bench --seconds 60 --csv s48.csv` | Tabelle § 3.1: Zyklen, Last, Überläufe 0, MIDI verloren 0 |
| S6 | M+A | `ModBench-v0.1.0-max64.bin` flashen, `P bench --seconds 60 --csv s48-max64.csv` | Szenario S6 gültig, keine Überläufe |
| B256 | M+A | `ModBench-v0.1.0-block256.bin` flashen, `P bench --seconds 60 --csv s256.csv` | zweite Tabelle für Block 256 |
| H5 | A | `P monitor --seconds 20` mit offenen CV-Buchsen, dann mit 0 V | Mittelwert und Rauschen der CV-Rohwerte |
| H6 | M+A | CC1, Channel AT und Poly AT vom Keyboard senden, dabei `P monitor` | Werte sichtbar; Poly AT der richtigen Note zugeordnet |
| H7 | M | Hörtest: LFO 0,02–20 Hz, Modwheel-Sweep, Poly AT | keine Zipper-Geräusche bei Block 48 |
| H8 | M+A | LFO auf MIDI-Clock (Studio One) und auf Gate 1/2 mit Impulsen von 1, 5 und 20 ms | im Tempo; Start auf Taktanfang; Impulse gezählt |

**Abbruch:** Scheitert `P lengths` (H2/H3), dann hier anhalten. Alles Weitere hängt
davon ab, dass SysEx unter Windows sauber ankommt. Bitte die Konsolenausgabe
festhalten.

## 2. Toy Piano v0.3

Image `ToyPiano-v0.3.bin`, Checkliste **T1–T15** in
[instruments/toy-piano/docs/HARDWARE_TEST_V0.3.md](../../../instruments/toy-piano/docs/HARDWARE_TEST_V0.3.md).

* [M]: T1 (ohne PC wie v0.2), T5–T13 (hören, Tasten, Keyboard, CV, Clock, Gate), T15
  (aus- und wieder einschalten).
* [A]: T2–T4, T14 und die `P`-Befehle in T5–T13.
* Ergebnisse in die Tabelle der Checkliste und in
  [instruments/toy-piano/docs/VALIDATION.md](../../../instruments/toy-piano/docs/VALIDATION.md).
* **Release:** Ein Entwurf v0.3 liegt unter GitHub → Releases. Veröffentlichen erst, wenn
  T1–T15 bestanden sind. Das entscheidet der Mensch.

## 3. Physical Modeling mit Matrix

Image `PhysicalModelingMod-v0.1.0.bin`, Checkliste **R1–R10** in
[instruments/physical-modeling/docs/MODULATION.md](../../../instruments/physical-modeling/docs/MODULATION.md).
Zum Vergleich bei R1 kurz `PhysicalModeling-v0.1.0.bin` flashen. Diese reguläre
Fassung ist bytegleich zum bisherigen Stand.

* [A]: R2, R3, R9 (`cpu_probe.py`) und die `P`-Befehle.
* [M]: R1 und R4–R8, R10 (hören, Poly-AT-Keyboard, Clock).

## 4. Editoren am Gerät

Mit `ToyPiano-v0.3.bin`, für E9 mit `PhysicalModelingMod-v0.1.0.bin`. Vorher `P` und
andere MIDI-Programme schließen, weil der Port nur einer Anwendung gehört.

| Nr. | Rolle | Schritt | Erwartet |
| --- | --- | --- | --- |
| E1 | M | `ModOnDaisyEditor.exe` starten, Daisy-Port wählen, **Verbinden** | Status „toy-piano 0.3 · ID 0“; Spalten wie T3; Poly AT unter Attack/Decay und Spalten 6–8 schraffiert |
| E2 | M | Note mit langem Decay halten, LFO 1 → Pitch auf +30 ziehen | Vibrato hörbar; gelber Punkt verschwindet; unten „Gerät bestätigt (Patch-CRC …)“ |
| E3 | M | Live-Anzeige beobachten, Modwheel und Aftertouch bewegen | LFO-Balken laufen; Modwheel, Channel AT und Poly AT schlagen aus; der Pitch-Balken unter der Spalte zeigt den Offset |
| E4 | M | LFO 1: Form S&H, Rate verstellen; Random: geglättet | hörbar, ohne Knacken |
| E5 | M | 8 Verbindungen anlegen, eine 9. versuchen | 9. bleibt aus (gestrichelt), Meldung „Höchstens 8 …“ |
| E6 | M | Datei speichern, Matrix leeren, Datei öffnen | Klang wie vor dem Leeren; dieselbe Datei lädt auch im Browser-Editor |
| E7 | M | Studio One mit aktivem Daisy-Port öffnen, dann im Editor **Verbinden** | klare Meldung „… belegt (z. B. Studio One) …“, kein Absturz |
| E8 | M | Während der Verbindung das USB-Kabel an J7 ziehen, wieder stecken, **Verbinden** | „Gerät getrennt“, offline weiter bearbeitbar; danach Zustand vom Gerät geladen |
| E9 | M | Physical Modeling: verbinden, Poly AT → Brightness +70, Akkord halten, eine Taste drücken | Poly AT × Excitation schraffiert; nur die gedrückte Stimme ändert sich |
| E9b | M | Physical Modeling (`0.1.0-mod4`), im Editor **„Daisy Seed External“** gewählt (kein Simulator): Potis drehen, Modwheel → Level −100 | Spaltenköpfe zeigen einen weißen Strich am Grundwert, der dem Poti folgt, und darüber den aktuellen Wert (z. B. „63 %“, Pitch in Halbtönen); bei Level steht er fest bei 0,5 („+0 %“). Spalte 7 heißt „Level“, das Modwheel blendet bis Stille aus |
| E7b | M | Mehrere Programme pro Port prüfen (Windows MIDI Services), siehe [Editor-README](../../../editors/mod-on-daisy/README.md#studio-one-und-der-editor-gleichzeitig). **Beide Reihenfolgen**: (a) erst Studio One, dann Editor; (b) erst Editor, dann Studio One | Ergebnis je Reihenfolge notieren: gehen beide gleichzeitig? Bei (b): bekommt Studio One Noten, bleibt der Editor grün? |
| E11 | M | Physical Modeling, LFO 1 → Structure +50, LFO 1 frei auf 20 Hz, dann MIDI-Clock 150 BPM 1/32 aus Studio One | LFO-1-Balken zeigt ein breites Band (nicht einen stehenden Wert); Spaltenkopf Structure zeigt den Bereich um den Grundwert, rote Enden, wenn er an 0 oder 1 anstößt. Clock stoppen: Zeile „Takt“ meldet „Kein MIDI-Clock – LFO steht“, das Band schrumpft |
| E12 | M | Potis Structure auf ca. 10 %, 50 %, 90 %, jeweils mit LFO ±50 | Bereich ungefähr 0…0,6 / 0…1 / 0,4…1, begrenzte Enden rot; Klang passt zur Anzeige |
| E13 | M | Keyboard nur mit Channel AT, dann nur mit Poly AT; bei Poly AT zwei Tasten halten und nur auf die **erste** drücken; loslassen | Anzeige „Aftertouch · Kanal“ bzw. „Poly · Note“, Punkt an der passenden Zeile; bei der ersten von zwei Tasten zeigt sie deren Druck; nach dem Loslassen 0. Klangwirkung wie bisher |
| E14 | M | LFO 2 langsam (0,3 Hz) → nichts; LFO 1 → Pitch +20, „FM ← L2“ auf +60 | LFO 1 wird periodisch schneller und langsamer (hörbar am Vibrato). Bei LFO 1 auf MIDI-Clock ist der FM-Regler gesperrt |
| E15 | M | Datei `legacy-reverb.modpatch` (aus `editors/mod-on-daisy/web/tests/`) im verbundenen Editor öffnen | Frage „Patch passt nicht genau …“ nennt „Reverb → Level“; „Abbrechen“ ändert nichts; „Ohne betroffene laden“ lässt Modwheel → Level ausgeschaltet |
| E16 | M | Studio One belegt den Port, dann im Editor **Verbinden**; danach Studio One schließen und erneut **Verbinden** (beide Editoren) | Erster Versuch: klare Meldung, Port wird freigegeben; zweiter Versuch verbindet ohne Neustart des Editors |
| E17 | M | Verbunden, dann in Studio One den Daisy-Eingang aktivieren (falls möglich) oder Kabel ziehen | Status wird nach ca. 1,5 s gelb „keine Live-Daten“, nach 5 s rot „keine Antwort“, eine Meldung im Log; nach Wiederherstellung grün und „Zustand wird neu geladen“ |
| E18 | M | Physical Modeling (`0.1.0-mod4`): SW17/SW18 durch alle sechs Stellungen, dann CC28 aus Studio One oder vom Keyboard | Oben neben dem Status steht „Modell: …“ passend zur Schalterstellung (Tooltip: Bank/SW18); nach CC28 „… (MIDI)“. Beide Editoren gleich |
| E19 | M | Toy Piano v0.3 verbinden | Keine Modell-Anzeige; Spaltenköpfe zeigen weiter den Hub (Toy Piano meldet keine Grundwerte) |
| E10 | M | `editor/ModOnDaisyEditor.html` in Chrome/Edge, SysEx erlauben, verbinden, eine Zelle ändern | Bestätigung mit Patch-CRC wie im JUCE-Editor |

Ergebnisse in [VALIDATION.md](VALIDATION.md) § 1b (JUCE-Editor) bzw. § 1a
(Browser-Editor) unter einem neuen Punkt „Am Gerät“.

## 5. Ergebnisse festhalten

* Nur eintragen, was beobachtet oder gemessen wurde. Jede Zeile kennzeichnen:
  `GEMESSEN` mit Datum oder `NICHT GEMESSEN`.
* Konsolenausgaben und CSV-Dateien (`s48.csv`, `s256.csv`, …) gehören in den Bericht
  oder als Anhang in den PR.
* Bei einem Fehler zuerst die genaue Ausgabe und die Schritte festhalten, dann im
  selben Branch beheben. Dabei gilt: Host-Tests und CI grün, bevor es wieder ans Gerät
  geht.
* Ergebnisse auf einem eigenen Branch committen und per PR zusammenführen.
