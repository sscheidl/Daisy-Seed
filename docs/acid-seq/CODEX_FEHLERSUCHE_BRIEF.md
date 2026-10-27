# Prüfauftrag an Codex: Fehlersuche Acid Seq (Phasen 0–6)

* **Stand:** 2026-10-27
* **Anlass:** Entscheidung D15 im [Arbeitsauftrag](ARBEITSAUFTRAG.md): „Ziehe alle
  Phasen bis zur letzten durch. Dann macht Codex Fehlersuche.“
* **Gegenstand:** die gestapelten PRs sscheidl/Daisy-Seed#54 bis #60, zusammen auf
  Branch `claude/acid-seq-phase-6`. Der Diff gegen `main` ist
  `git diff main...claude/acid-seq-phase-6` (Merge-Basis `b5fe004`).
* **Verfasser des Codes:** Claude Opus 5.5
* **Hardware:** Nichts ist auf der Hardware gemessen. Alle Hardwarewerte stehen auf
  NICHT GEMESSEN.

| Phase | PR | Branch | Inhalt |
| --- | --- | --- | --- |
| 0 | #54 | `claude/acid-seq-phase-0-f1c35b` | Gerüst, Register, Verträge, Fixtures, CI-Regeln |
| 1 | #55 | `claude/acid-seq-phase-1` | `step-seq` 0.1.0: Sequencer-Kern, Transport, CV, MIDI, SQ-Gerät |
| 2 | #56 | `claude/acid-seq-phase-2` | `acid-303` 0.1.0: Stimme, Stimmenbesitz |
| 3 | #57 | `claude/acid-seq-phase-3` | Sättigung nach `common/dsp`, `drive-fx` 0.1.0 |
| 4 | #58 | `claude/acid-seq-phase-4` | `delay-fx` 0.1.0, Delay-Speicher im SDRAM |
| 5 | #59 | `claude/acid-seq-phase-5` | Editor: SQ-Seiten (JUCE, Browser), `.acidpatch`, Editor 0.2.0 |
| 6 | #60 | `claude/acid-seq-phase-6` | Produkt `acid-seq` 0.1.0 |

## Auftrag

Suche unabhängig und kritisch nach Fehlern im Code der Phasen 0–6. Gesucht sind
Fehler, die auf der Hardware oder im Editor falsches Verhalten, Knacken, Hänger,
Speicherfehler oder Regelverstöße erzeugen. Stil- und Geschmacksfragen nur als P3.

* **Ändere keinen Code und keinen Branch.** Schreib das Ergebnis nach
  `docs/acid-seq/CODEX_FEHLERSUCHE.md`. Claude arbeitet die Befunde danach
  phasenweise in die passenden Branches ein, der Benutzer entscheidet strittige Punkte.
* Wenn sich ein Fehler mit einem Test zeigen lässt, schreib den Test als Ausschnitt in
  den Befund (Datei, Testfunktion, erwartetes und tatsächliches Ergebnis).
* Die Regeln in [AGENTS.md](../../AGENTS.md) gelten: nicht flashen, keine Workflows
  starten, keine Downloads ohne Freigabe des Benutzers. Berichte nur, was du selbst
  geprüft oder ausgeführt hast. Was du nicht prüfen konntest, gehört in „Nicht geprüft“.
* Prüfe Behauptungen am Code, nicht an READMEs, CHANGELOGs oder PR-Texten.
* Ordne jeden Befund der Phase zu, in der der Fehler entstanden ist. Ein Fehler in
  `effects/stereo-delay` gehört zu Phase 4, auch wenn er erst im Produkt auffällt.

## Was lokal schon gelaufen ist (Windows, clang 23 llvm-mingw, xPack ARM GCC 13.2.1-1.1)

GEMESSEN 2026-10-26/27, Einzelheiten in den PR-Texten:

* `make -C <komponente> test test-sanitize` für `sequencers/step-seq`,
  `instruments/acid-303`, `effects/drive`, `effects/stereo-delay`,
  `sequencers/acid-seq`: PASS.
* ARM-Builds aller fünf Firmwares, `scripts/check-firmware.py`,
  `scripts/check-elf-placement.py`, `make PROJECT=<id> manifest`: PASS.
* `make -C editors/mod-on-daisy test`, JUCE-Selbsttest gegen ein lokales JUCE 9.0.0
  (mit einer nicht committeten Anpassung in `MidiPort.h`).
* Linux-CI (`quick.yml`): #54–#59 grün. #60 war rot wegen einer GCC-13-Fehlwarnung
  `-Wmaybe-uninitialized` in `chain.h`. Die Korrektur steht im selben Commit wie
  dieser Auftrag.

**Nicht ausgeführt, bitte in deiner Linux-Umgebung nachholen, soweit vorhanden:**

1. `make -C <komponente> test-thread` (TSan) für alle fünf Komponenten oben.
2. `make -C <komponente> test test-sanitize` mit **g++** (x86 GCC 13 meldet Warnungen,
   die clang und ARM-GCC nicht melden; zwei solche Fälle sind schon aufgetreten).
3. `make -C editors/mod-on-daisy test-sanitize` (lokal am Leerzeichen im Pfad gescheitert).
4. JUCE-Build gegen das gepinnte **JUCE 9.0.4** und `--selftest`, falls JUCE 9.0.4 schon
   vorhanden ist. Nicht selbst herunterladen.
5. `make -C editors/mod-on-daisy test-editor-sq` (Playwright), falls Chromium schon
   vorhanden ist. Nicht selbst herunterladen.
6. Golden-Hashes unter Linux/g++: Komponenten `0x7528DC6F` (step-seq), `0xB05F5A1F` und
   `0x355159C8` (acid-303), `0x50B9CF6E` (drive), `0xD11BFF18` (delay), Produkt
   `0xBC3D9C4E`. Dazu die bestehenden Goldens von Toy Piano, Physical Modeling und
   Tape FX, die durch Phase 3 (Sättigung nach `common/dsp`) nicht wandern dürfen.

## Prüfpunkte

### 1. Echtzeit (alle fünf Firmwares, `*/src/main.cpp`)

* Keine Allokation, keine Locks, keine HAL-Aufrufe mit Lock und kein Blockieren im
  Audio-Callback. Besonders der eigene DAC-Schreibpfad (R01, Arbeitsauftrag 3.3) in
  `sequencers/step-seq/include/step_seq/cv.h`.
* SPSC-Queues und Triple-Buffer in `step_seq/spsc.h`: Speicherreihenfolge (acquire/release),
  Verhalten bei voller Queue, Zähler für Überläufe. Was geht verloren, wenn die
  VoiceCommand-Queue (64) im Produkt voll ist?
* Blockgröße: `acid_seq::Chain::Process` kürzt `n` auf `kBlock` (48). Prüfe, dass jeder
  Aufrufer genau 48 liefert und der Callback einen anderen Wert sicher behandelt
  (`main.cpp` prüft `n != kBlockSize`).
* Stack im Callback: lokale Puffer in `chain.h` und in den Effekt-Firmwares gegen die
  DTCM-Reserve (Produkt: 17 912 B laut `check-elf-placement.py`).
* Denormals, NaN und Inf in Filter, Drive, Delay-Feedback und Limiter.

### 2. Sequencer (`sequencers/step-seq/include/step_seq/engine.h`, `controls.h`, `pattern.h`)

* Transport nach `docs/TRANSPORT.md` und `contracts/transport_vectors.json`: Start,
  Stop, Continue, Reset, scharfgeschaltet bei externer Clock, Ausfall der Clock,
  Rangfolge im Modus „Auto“ (D10), SW17 beim Einschalten auf Run (D2).
* Swing, Gate-Länge und Gate-Grenzen bei 1 ms Auflösung (R04). Tempo 30–300 BPM,
  Tempowechsel mitten im Step.
* Modi vorwärts, rückwärts, zufällig: Moduswechsel und Längenänderung während des
  Laufs, Indexgrenzen, Zufallsquelle.
* Bedienung: Step-Wahl, langer Druck, Akkorde, Bootloader-Geste S9 + S16 (R07).
  Poti-Glättung und Stufen-Hysterese (D6).
* Notenbereich, Transponierung und Skalen (D11) an den Grenzen 0 und 127.

### 3. CV und Kalibrierung (`step_seq/cv.h`, R08, Arbeitsauftrag 4.5)

* CV OUT bleibt gesperrt bis zur Freigabe, auch nach Neustart, USB-Verlust und
  Kalibrierung. Ruhecode vor der Freigabe.
* Kalibrierwerte: Plausibilitätsprüfung, keine Flash-Schreibvorgänge (AGENTS.md,
  STORAGE-Regel).

### 4. MIDI (`step_seq/midi_out.h`, Firmwares)

* DIN und USB: Sendebudget, Timeout, `PollTx` in `main`, laufender Status, Panic und
  All-Notes-Off auf dem richtigen Kanal, Kanalwechsel mit hängenden Noten.
* Echtzeit-Bytes innerhalb von SysEx (Abweichung aus Phase 0, PROTOCOL.md § 2).
* Echo-Schutz im Auto-Modus (Abweichung aus Phase 1).
* USB-Verlust: Welche Noten enden, welche bleiben?

### 5. SQ-Protokoll (`step_seq/sq_protocol.h`, `step_seq/device.h`, `docs/PROTOCOL.md`)

* Rahmen, Fragmente, atomare Übernahme, Revisionen (R09, R10): fehlerhafte, zu lange,
  abgeschnittene und doppelte Rahmen. Pufferüberläufe, Vorzeichen bei I32, U14-IDs.
* Gleichheit der vier Umsetzungen: Firmware (`sq_protocol.h`), Python
  (`tools/sq_codec.py`, `tools/sq_probe.py`), Browser (`web/sq_protocol.js`,
  `web/sq_contract.js`) und JUCE (`src/core/SqSession.h`). Die Vektoren in
  `contracts/sq_vectors.json` müssen alle vier abdecken.
* Das Mod-on-Daisy-Protokoll (`modules/mod-on-daisy/docs/PROTOCOL.md`) darf sich nicht
  geändert haben. Prüfe die Erkennung SQ vor MD im Editor.

### 6. Stimme (`instruments/acid-303/include/acid303/voice.h`)

* Stimmenbesitz (R16): zwei Quellen mit eigenem Notenstapel, gleiche Note von beiden
  Quellen, Note-Off für unbekannte Noten, Überlauf des Stapels, Übernahme der zuletzt
  gehaltenen Note der anderen Quelle. `AllNotesOff(source)` gegen `Mute()`.
* Slide, Accent, Hüllkurven, Selbstoszillation bei Resonanz 100 %, Parameterfahrten
  ohne Sprünge.

### 7. Drive (`effects/drive/include/drive/drive.h`, `common/dsp/saturation.h`)

* Auto-Level: Stabilität, Verhalten in Stille, Grenzen ±3 dB, Tabelle aus
  `tools/gen_autolevel.cpp`. Die beiden begründeten Abweichungen aus #57
  (Kurve vom Eingangspegel abhängig, Fold auf Rechtecken bis −17 dB leiser) bitte
  bewerten.
* Die Auslagerung der Sättigung darf Tape FX nicht verändern
  (`effects/tape-fx/src/tape_fx.h`).

### 8. Delay (`effects/stereo-delay/include/stereo_delay/delay.h`, `host.h`)

* Indexrechnung im SDRAM-Puffer (12 s je Leitung, 4,6 MB, D12), Interpolation an der
  maximalen Zeit, Zeitwechsel ohne Klick, Sync 30–300 BPM, Feedback 95 %.
* Ende des Nachhalls nach dem Ausschalten und Wiedereinschalten.
* **Offene Frage für den Benutzer:** Die Einzel-Firmware `delay-fx` gibt ausgeschaltet
  `dry · (1 − mix)` aus, bei Mix 25 % also rund 2,5 dB leiser. Das Produkt blendet nach
  dem Nachhall auf exakt trocken (`sequencers/acid-seq/include/acid_seq/chain.h`).
  Ist das in `delay-fx` ein Fehler, der in Phase 4 behoben werden soll?

### 9. Produkt (`sequencers/acid-seq/src/main.cpp`, `include/acid_seq/chain.h`)

* Aufteilung Callback / `main` laut README, Queues, Snapshot.
* Noten und CCs auf dem Stimmen-Kanal über USB und DIN, Zuordnung nach
  `sequencers/step-seq/docs/PARAMETERS.md`. Empfangene Noten dürfen nicht an die
  Ausgänge weitergehen.
* Panic, Kanalwechsel, USB-Verlust und Bootloader (Mute vor dem Einstieg).
* Tempo-Übergabe ans Delay, auch bei externer Clock.
* Soft-Limiter: monoton, unter 1, stetig bei 0,8.
* **Abweichung zur Prüfung:** `callback-diagnostics` aus der Komponentenliste in 4.10
  ist im Register der Mod-on-Daisy-Adapter. Das Produkt nutzt
  `step_seq/diagnostics.h` und listet `callback-diagnostics` nicht.

### 10. Editor (`editors/mod-on-daisy`)

* `.acidpatch` (`src/core/AcidPatch.h`, Browser-Gegenstück): Laden beschädigter oder
  böswilliger Dateien, Versionen, Grenzwerte, Komponenten, die das Gerät nicht hat.
* D13: Das Gerät hat Vorrang. Vom Panel aufs Gerät nur auf ausdrückliche Aktion, im
  VST3 beim Songladen nach Bestätigung mit der Option „immer“.
* Gleiches Verhalten von JUCE- und Browser-Seiten (Wertebereiche, Rundung, Labels aus
  `SqLabels.h` und `sq_contract.js`).
* Lizenz: Editor AGPL, eigener Code MIT (D8), Herkunftsvermerke.

### 11. Build, Register und CI

* `components/registry.json`, `scripts/component_manifest.py`: Abhängigkeiten der
  neuen Komponenten und Produkte vollständig und richtig.
* `scripts/select_test_suites.py`: Löst jede Änderung an einer Komponente die Suites
  aller Verbraucher aus (R13)? Fehlt eine Regel, etwa für `common/dsp`?
* `scripts/check-firmware.py`, `scripts/check-elf-placement.py`: Kennungen und
  Prüfungen für alle fünf Images.
* `.github/workflows/quick.yml`: ein Job, 15 Minuten, Doku-Ausschlüsse,
  keine neuen breiten Trigger (Budget-Regel in AGENTS.md).
* Gepinnte Versionen unverändert: libDaisy, DaisySP, xPack GCC 13.2.1-1.1, JUCE 9.0.4.

### 12. Dokumentation gegen Code

* READMEs, CHANGELOGs und Hardwaretestpläne (Deutsch) gegen das tatsächliche
  Verhalten: Bedienung, CC-Nummern, Versionen, Image-Namen, Kommandos.
* Liste NICHT GEMESSEN (Arbeitsauftrag Abschnitt 8): Fehlt eine Größe, die nur auf der
  Hardware zu prüfen ist?

## Format des Ergebnisses

```
# Codex Fehlersuche: Acid Seq Phasen 0–6
VERDICT: BEREIT FÜR HARDWARETEST | BEREIT NACH KORREKTUREN | NICHT BEREIT
Geprüft: claude/acid-seq-phase-6 @ <Commit>, Datum, Umgebung (OS, Compiler)

## Ausgeführt
| Kommando | Ergebnis (GEMESSEN Datum) |

## Befunde
| ID | Schwere | Phase/PR | Datei:Zeile | Befund | Auslöser (Eingabe/Zustand → falsches Ergebnis) | Vorschlag |

## Offene Fragen an den Benutzer
| Nr. | Frage | Empfehlung |

## Nicht geprüft
```

* **P1:** falsches Verhalten, Absturz, Hänger, Speicherfehler, Echtzeitverstoß,
  ungewollte Klangänderung (Golden) oder Verstoß gegen AGENTS.md. Vor dem Hardwaretest
  beheben.
* **P2:** Fehler in Rand- oder Fehlerfällen, Abweichung vom Arbeitsauftrag ohne
  Begründung, fehlender Test für ein PASS-Kriterium. Vor dem Merge beheben.
* **P3:** Hinweis, Vereinfachung, Doku.

IDs bitte als F01, F02, … vergeben, damit Claude sie in den Commits und im
Arbeitsauftrag (neuer Abschnitt „Einarbeitung der Fehlersuche“) referenzieren kann.
