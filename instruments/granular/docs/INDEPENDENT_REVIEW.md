# Independent Review: Nebulae on Daisy

**VERDICT (Stand 2026-10-25): REVIEW PASS / HARDWARE PENDING**, für v0.1.1 auf `main`.

* Prüfer: Claude Opus 5.5, unabhängig. Am Code habe ich nichts geändert.
* Erstreview: 2026-10-20, Ergebnis HOLD-SOFTWARE, siehe [Verlauf](#verlauf-der-befunde).
* Aktualisierung: 2026-10-25.
* Geprüft: `main` @ `5f253591e9619b8a71c5ad591bde5c6b772617bb`. Nebulae kam über PR [sscheidl/Daisy-Seed#17](https://github.com/sscheidl/Daisy-Seed/pull/17) (Head `87f6205`, Merge `66b7028`).
  Seitdem kamen die Bootloader-Einstiegs-Commits `417a906`, `ac279c1` und `5e90427` dazu (v0.1.1).
* Firmware: `instruments/granular/build/NebulaeOnDaisy-v0.1.1.bin`, 163 124 B.
  SHA256 `e30380f73cdd95db12f9919c5770348316872970d5328462286906473a18e50a`.
  Das ist ein lokaler Clean-Build (xPack GCC 13.2.1-1.1). Ein CI-Firmware-Artefakt für v0.1.1 gibt es nicht, siehe R6.
* Referenzen (am 2026-10-20 selbst geholt):
  * QB_Nebulae_V2 `4be42b5`, `a_granularlooper.instr`.
  * Manual v2.1.2, SHA256 `439f92c8…a8d6`. Seiten sind gedruckte Seitenzahlen.
  * De-Pattern `073a1ae`.
  * libDaisy `f715e487`, DaisySP `2c72eaf9`. Beide Submodule sind unverändert.

Das Urteil bedeutet: ein flashbarer Software-Testkandidat. Es ist **keine** Freigabe für Hardware oder Klang.

## Zusammenfassung

* Der P1 aus dem Erstreview ist behoben (F1, freier Lauf nach Noten). F2 und F5 sind ebenfalls behoben.
* Meine 20 unabhängigen Gegenproben laufen jetzt alle grün gegen den aktuellen Code.
* Die DSP-Kerne (`reference_dsp.cpp`) sind seit dem Erstreview unverändert. Die R1-Aussagen gelten weiter.
* Neu ist der gemeinsame Bootloader-Einstieg (S9+S16 für 5 s oder USB-SysEx). Er ist sauber zwischen ISR und Main getrennt.
  * Dabei gibt es eine neue P2-Bedienfalle: Im KEYBOARD-Modus sind S9+S16 die Tasten C5+C6. Wer diese Oktave 5 s hält, startet den Bootloader, und die flüchtige Live-Schleife im RAM geht verloren (F7).
* Offen bleiben F3 (CPU-Spitze) und F6 (Trigger-Grain-Größe). Beide sind P2.

## Gate-Matrix R0–R6 (Stand 2026-10-25)

| Gate | Status | Evidenz |
| --- | --- | --- |
| R0 Integrität/Artefakt | **PASS** | <ul><li>Clean-Build `make PROJECT=granular -j4` → exit 0.</li><li>`check-firmware.py` → `PASS: v0.1.1, 163124 bytes, stack=0x20020000, reset=0x240006b5, BOOT_SRAM vectors`.</li><li>`check-granular-elf.py --host-test` → exit 0: hard-float, BIN = ELF, isolierter AXI-SD-DMA-Sektor, 16 KiB Stack-Boden, 8 MiB SDRAM-Reserve, Dateikapazität 41 695 360 B.</li><li>`git diff --check c71c67a..main -- instruments/granular` ist sauber.</li><li>Die Submodule entsprechen den Pins.</li></ul> |
| R1 DSP/Klangtreue | **PASS** (strukturell/numerisch, kein Hörtest) | <ul><li>Fenster, Dichte, Overlap, BLEND-Kennlinien und Mincer-Parameter entsprechen dem Original-Csound (Details im Erstreview).</li><li>`reference_dsp.cpp` ist seit `c71c67a` unverändert.</li><li>`engine.cpp` hat sich nur bei Lautstärke, Quelle und Lock geändert.</li><li>Gegenproben T1–T4 und T7–T10 grün: L/R-Isolation unter −80 dB, Mono auf beiden Seiten gleich, 44,1 kHz → 441 Hz, Pitch unabhängig von SPEED, Stille → 0, Extremwerte endlich und ≤ 0,95.</li></ul> |
| R2 Modi/Board | **PASS** (Software), Hebelrichtung **HARDWARE PENDING** | <ul><li>Switch3-API und 20-ms-Mapping sind unverändert.</li><li>F1 ist behoben: `engine.cpp:186` mit `musical_target = note<0 ? 1 : velocity`. Gegenproben T5 und T6 grün.</li><li>Neu: S9+S16 starten in allen Modi den Bootloader (`main.cpp` `PollControls`). Siehe F7.</li></ul> |
| R3 SD/WAV/SDRAM | **PASS** | <ul><li>Unverändert (Nebulae nutzt weiter den eigenen `src/sd_driver.c`, nicht `common/sd`).</li><li>Host- und ELF-Kapazitätstests sind grün.</li><li>`docs/SAMPLE_BANKS.md` ist nur ein Plan und wurde nicht geprüft.</li></ul> |
| R4 Live/Echtzeit | **PASS** (statisch), CPU **HARDWARE PENDING** | <ul><li>Der Bootloader-Pfad hält die Echtzeitregeln ein: Tastenerkennung im TIM5-ISR, SysEx-Matcher im USB-IRQ, Übergabe per atomarem `Request`.</li><li>LED-Transfer, Antwort-Tx und Reset laufen nur im Main (`ServiceBootloader`). `neb_sd_service` ruft ihn nicht auf, also kein Reset aus FatFS heraus.</li><li>F3 bleibt offen.</li></ul> |
| R5 USB-MIDI | **PASS** (Software), Enumeration **HARDWARE PENDING** | <ul><li>Freier Lauf nach Note-Off, Panic, Reset und Disconnect ist belegt (Gegenprobe T5/T6, Projekttest „audio regressions“).</li><li>SysEx `F0 7D 42 4C 01 01 'BOOT' F7` wird parallel zum MIDI-Parser erkannt, die Antwort geht nur aus dem Main.</li></ul> |
| R6 Tests/CI | **PASS** (Firmware-Artefakt in CI: NOT VERIFIED) | <ul><li>Lokal (llvm-mingw clang++), alle mit exit 0: `test` mit 5 852 467 Assertions inklusive „audio regressions“, „recording regressions“ und „bootloader keys/SysEx“; `test-sanitize` (ASan/UBSan/float-cast); Gegenproben 20/20 unter ASan/UBSan.</li><li>CI: Quick checks [run 40236513348](https://github.com/sscheidl/Daisy-Seed/actions/runs/40236513348) auf `410168f`, „Nebulae host tests: success“. Der Nebulae-, Bootloader- und Submodul-Stand dort ist identisch mit `main`.</li><li>Volle CI mit TSan, ARM und Artefakt: [run 39485407516](https://github.com/sscheidl/Daisy-Seed/actions/runs/39485407516) am v0.1.0-Head `87f6205` (laut VALIDATION.md und den PR-Checks, alle SUCCESS).</li><li>Für v0.1.1 gibt es keinen manuellen Firmware-Run. Firmware-CI ist laut AGENTS.md bewusst manuell (Budget). TSan wurde für die v0.1.1-Änderung nicht ausgeführt; TSan läuft unter Windows nicht.</li></ul> |

## Offene Befunde

Es gibt kein offenes P0 und kein offenes P1.

### P2

**F3: CPU-Spitzenrisiko ungemessen (`reference_dsp.cpp:145`, `main.cpp:19`)**

* **Problem:**
  * Beide Kanal-Vocoder starten in derselben Hop-Phase. Alle 6 komplexen 2048er-FFTs fallen in jeden zweiten 256er-Block.
  * Das `Engine`-Objekt (~240 KB) liegt im SDRAM.
  * `callback_overruns` ist nur per Debugger sichtbar.
* **Wirkung:** Es besteht ein Dropout-Risiko. Reserve und Überschreitung sind nicht belegt.
* **Stand:** offen (C3).

**F6: Trigger-Modus ohne Clock-Begrenzung der Grain-Größe (`engine.cpp:101`)**

* **Problem:** Das Original begrenzt die Grain-Größe bei Dichte 0 auf das 6-Fache der geglätteten Clock-Periode (instr Z. 462–467). Die Implementierung tut das nicht.
* **Wirkung:** Bei langsamer externer Clock können Grains bis 1 s lang werden.
* **Stand:** offen.

**F7 (neu): KEYBOARD-Oktave C5+C6 löst nach 5 s den Bootloader aus (`main.cpp` `PollControls`, `common/bootloader/bootloader_entry.h` `kKeyMask`)**

* **Problem:** S9+S16 sind im KEYBOARD-Modus C5 und C6. Hält man genau diese Oktave 5 s, startet die Firmware ohne Rückfrage in den Bootloader.
* **Wirkung:**
  * Bei einem Granular-Drone ist eine lang gehaltene Oktave eine plausible musikalische Geste.
  * Die Live-Schleife ist flüchtig (README) und geht verloren.
  * Das README erwähnt die Geste, aber nicht den Verlust der Schleife.
* **Repro:** Code-Lesung: `boot_keys.Process(held,…)` läuft unabhängig von `ui.mode`.
  Der Projekttest „bootloader keys/SysEx“ bestätigt den Auslöser.
* **Stand:** offen (C5).

Bewusst dokumentierte Abweichungen sind **kein** Befund. Sie sind unverändert gegenüber dem Erstreview:

* Pitch-Random ±2 Oktaven
* 10 Grains pro Kanal
* Erstaufnahme Live unabhängig vom Dry-Faktor
* kein Speichern über FILE+FREEZE
* 256er-Block

## Verlauf der Befunde

| ID | Prio | Erstreview 2026-10-20 (`c71c67a`) | Stand 2026-10-25 (`5f25359`) |
| --- | --- | --- | --- |
| F1 | P1 | Noten-Gate rastet dauerhaft ein, NEBULAE/SAMPLE stumm | **behoben** in `183b792`. Gegenprobe T5/T6 grün |
| F2 | P2 | SOURCE-Tap während Overdub wird überschrieben | **behoben** in `183b792` (`PublishLive`: `if(!overdub_) live=true`). T11 grün |
| F3 | P2 | CPU-Spitze ungemessen | offen |
| F4 | P2 | kein PR, keine CI | **behoben**: PR #17, CI-Run 39485407516 (v0.1.0) |
| F5 | P2 | START/SIZE/SPEED-Lock nur bei Erstaufnahme | **behoben** in `183b792` (`engine.cpp:147`) |
| F6 | P2 | Trigger-Grain-Größe ohne Clock-Begrenzung | offen |
| F7 | P2 | – | **neu** mit v0.1.1 |

## Software-verifiziert vs. Hardware-Test durch den Benutzer

* **Software-verifiziert:**
  * ARM-Image v0.1.1, Vektoren, Speichergrenzen
  * L/R-Signalweg, Kennlinien, Rate-Skalierung
  * WAV-Validierung, Kapazitäten, Lade-Transaktion
  * UI-Dekodierung über die API
  * MIDI-Parser und Queues
  * freier Lauf nach Noten
  * Bootloader-Auslöser im Host-Test
  * keine blockierenden Aufrufe im Callback
* **HARDWARE PENDING / USER TEST:**
  * SW18-Hebelrichtung
  * SD-Detect-Polarität und echte microSD
  * USB-Enumeration an J7
  * CPU-Spitze und Dropouts (F3)
  * Stereo-Pegel
  * LED-Reihenfolge
  * Bootloader-Einstieg per Tasten und SysEx samt DFU danach
  * Hörvergleich mit dem Original

## Anhang A: Eigene Gegenproben

Quelle: `.work/independent-review/nebulae/review_nebulae.cpp`. Sie wird gegen die unveränderten Produktionsquellen gebaut:

```sh
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc \
  ../../.work/independent-review/nebulae/review_nebulae.cpp src/reference_dsp.cpp src/control.cpp src/engine.cpp src/wav.cpp
```

* Stand 2026-10-25: 20/20 ok (`ALL REVIEW CHECKS PASSED`).
* Stand 2026-10-20: 4 FAIL (T5 a/b, T6, T11).
