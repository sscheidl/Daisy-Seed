# Streichwurst: Prüfstatus

Stand 01.11.2026, Produkt 0.1.0 (Instrument 0.1.0, FX 0.1.0), Control App 0.3.0.

| Gegenstand | Stand |
| --- | --- |
| Instrument-DSP, FX-DSP, Produkt, Firmware-Hostfakes | Hosttests bestanden, auch mit ASan/UBSan (GEMESSEN 01.11.2026) |
| ARM-Firmware | gebaut, Imageprüfung bestanden, Speicher aus der Mapdatei (GEMESSEN 01.11.2026) |
| Control App (Kern, Browser, JUCE-Standalone, VST3) | Tests, Selbsttest und Plugin-Check bestanden (GEMESSEN 01.11.2026) |
| Standalone-FX-Firmware (FX zuerst) | gebaut und auf Hostfakes geprüft; Abnahme am Kit **NICHT GEMESSEN** ([FX-Laufzettel](../../../effects/streichwurst-fx/docs/HARDWARE_TEST.md)) |
| Board-Audio / Regler / LEDs / USB- und DIN-MIDI / CPU-Last | **NICHT GEMESSEN**, Laufzettel [HARDWARE_TEST.md](HARDWARE_TEST.md) |
| Klangvergleich zum Originalplugin/-gerät | **NICHT GEMESSEN** |

Softwaretests belegen keine Klangidentität mit dem Original. Abweichungen je Modul:
[FIDELITY.md](FIDELITY.md).

## Umgebung (GEMESSEN 01.11.2026)

Worktree `.work/streichwurst`, Branch `claude/streichwurst`, Windows 11.
xPack ARM GCC 13.2.1-1.1, xPack build tools 4.4.1-3, LLVM-MinGW Clang 23.1.3 (Host),
Visual Studio 18 2026 (MSVC) mit JUCE 9.0.4, Python 3, Node 24, Playwright mit dem
installierten Edge. libDaisy `f715e48`, DaisySP `2c72eaf` unverändert (lokal gebaut).

## Befehle und Ergebnisse (alle GEMESSEN 01.11.2026)

| Befehl | Ergebnis |
| --- | --- |
| `make -C effects/streichwurst-fx test HOST_CXX=clang++` | bestanden: Delaybereiche 4,40–8,00 ms bzw. 3,00–9,40 ms, Chorus-LFO-Periode 3,125 s, DC je Modus 2,30420/3,64746/2,87695 (SF-F06), kein Feedback, exakter Bypass, Übergang ohne Sprung, Animate ±8000, Reverb fällt ab, Adapter = Komponente, 20 s Rauschen endlich |
| `make -C effects/streichwurst-fx test-sanitize HOST_CXX=clang++` | bestanden (ASan + UBSan) |
| `make -C instruments/streichwurst test HOST_CXX=clang++` | bestanden: Instrument (Takte, Tonhöhe, Bandgrenze: Alias der 5. Harmonischen −30 dB, τ-Messung 0,630/0,370, Registrierung/Oktaven, gleiche Ausgabe bei Blockgrößen 48/7/1, Sustain/Retrigger/Steal, Queue- und Null-Blöcke, NaN), Produkt (Register, Routing, Gainwort 0,6250, Balance-Mitte Faktor 1, leere Matrix bitgleich, Panik, Kopfreserve: Spitze 0,691, Reverb→Delay-Tausch, Bedienung inkl. Bootloader-Chord, eigener Fingerprint), Firmware auf Hostfakes (SQ 1.1, Lesen zuerst, ACK/Revision, Wiederholung, Echo, CC, Potis im Monitor, Matrix, Kanalfilter, USB-Verlust, Queue-Überlauf ohne hängende Noten, Bootloader nach Panik, keine Allokation im Callback) |
| `make -C instruments/streichwurst test-sanitize HOST_CXX=clang++` | bestanden (ASan + UBSan) |
| TSan (`test-thread`) | **nicht ausführbar**: eine Probeübersetzung mit `clang++ -fsanitize=thread` meldet „unsupported option … for target x86_64-w64-windows-gnu“. Die Übergabe nutzt die mit TSan geprüfte SPSC-Queue und den Triple-Buffer von Step Seq sowie einen atomaren Zähler |
| `make PROJECT=streichwurst` (ARM, `-O3`, BOOT_SRAM) | gebaut: `Streichwurst-v0.1.0.bin`, 239 020 Byte, SHA-256 `f244ae6b…681ca75e9` (lokales Build nach Merge von `main` und `ClearTails`-Erweiterung, nicht veröffentlicht) |
| `python scripts/check-firmware.py instruments/streichwurst/build/Streichwurst-v0.1.0.bin` | `PASS: v0.1.0, 239020 bytes, stack=0x20020000, reset=0x24000749` |
| `make -C effects/streichwurst-fx test` / `test-sanitize` (inkl. Standalone-Firmware auf Hostfakes) | bestanden: exakter Durchgang bei Ensemble aus, Mono-Eingang, Modi, Potis, CC, SQ mit Monitor, S16 löscht (Restenergie 0), Bootloader-Tasten, keine Allokation |
| `make PROJECT=streichwurst-fx` (Standalone-FX, ARM) | gebaut: `StreichwurstFx-v0.1.0.bin`, 164 508 Byte, SHA-256 `1a0f4c96…5479f9b6`; DTCM 112 760 B (86,0 %), Stackreserve 18 312 B, SDRAM 31 872 B |
| `python scripts/check-firmware.py effects/streichwurst-fx/build/StreichwurstFx-v0.1.0.bin` | `PASS: v0.1.0, 164508 bytes, stack=0x20020000, reset=0x240003d1` |
| `python -m unittest discover -s tests -p test_firmware_checker.py` | bestanden (neue Streichwurst-Fälle, Acid Seq unverändert) |
| `python -m unittest discover -s tests -p test_components.py` | bestanden (Register, Produkt = BUILDABLE_PROJECTS, CI-Auswahl) |
| `python scripts/component_manifest.py --validate` | `PASS: 23 components, 19 firmware products` |
| `python sequencers/step-seq/tools/gen_params.py --check` | bestanden |
| `make -C sequencers/step-seq test HOST_CXX=clang++` (C++-Teile) und `python tools/sq_probe.py selftest` | bestanden (geteilte SQ-Gerätedatei geändert) |
| `make -C sequencers/acid-seq test HOST_CXX=clang++` | bestanden |
| `python -m unittest discover -s tests -p test_acid_seq_contracts.py` | bestanden |
| Editor-Kern: `test_core`, `test_sq`, `test_streichwurst` (Clang, auch ASan/UBSan) | bestanden |
| `node web/tests/sq_protocol.test.cjs`, `sq_client.test.cjs`, `protocol.test.cjs` | bestanden (inkl. Streichwurst und `.swpatch`) |
| `node web/tests/e2e_sq.cjs build/host/sq_sim build/host/mod_sim` (Playwright/Edge) | bestanden: Acid Seq unverändert, Streichwurst liest SQ vor MD, Seiten, Matrixumschaltung; MD-Gerät unverändert |
| `node web/tests/e2e.cjs build/host/mod_sim` | lokal **nicht bestanden**, aber ebenso mit dem unveränderten Stand `2a3affb` (Gegenprobe): Umgebungsproblem dieses PCs, nicht durch diese Änderung |
| JUCE: `cmake -S editors/mod-on-daisy -B build/editor -G "Visual Studio 18 2026" -A x64 -DMOD_EDITOR_JUCE_DIR=…/juce-9.0.4/src`, `cmake --build build/editor --config Release` | Standalone, VST3, Plugin-Check und Kern-Tests gebaut |
| `"Mod on Daisy Editor.exe" --selftest selftest-sw2` (mit `MOD_ON_DAISY_NO_AUTOCONNECT=1`) | bestanden, inkl. „PASS Streichwurst: SQ pages by contract page, matrix alongside, edits, song patch with matrix“ |
| `ModOnDaisyPluginCheck "Mod on Daisy.vst3" legacy-reverb.modpatch` | `PASS: VST3 plugin loads, passes audio, keeps its state` |
| `make compare-rates` | Tabelle in [RATE_COMPARISON.md](RATE_COMPARISON.md) |
| `python -B tests/reference/own_dsp_model.py --test` | unverändertes historisches Modell, nicht erneut als Nachweis verwendet |

Nicht lokal ausgeführt: Linux-g++-Läufe (übernimmt `quick.yml` bei Push/PR mit der neuen
Suite `streichwurst`), manuelle Actions-Workflows (nicht gestartet).

## Ressourcen aus der Mapdatei (GEMESSEN 01.11.2026, keine Laufzeitmessung)

| Bereich | Belegung | Inhalt |
| --- | --- | --- |
| DTCM (128 KiB) | 122 296 Byte (93,3 %) | `.data` 13 448 + `.bss` 108 848; davon libDaisy-`hw` 38,7 KiB und `usb_midi` 37,2 KiB, Produkt-DSP-Zustand ≈ 17 KiB |
| Stackreserve DTCM | 8 776 Byte | Rest bis `_estack` 0x20020000; Laufzeitbedarf über Zähler „Stack frei“ prüfen (H18) |
| AXI-SRAM (480 KiB, Image) | 239 020 Byte (48,6 %) | Code, Konstanten inkl. 8-KiB-Ereigniskern (`constexpr`) |
| SDRAM | 38 016 Byte | Reverbleitungen (31,9 KiB) und Tastentabellen des Instruments (6 KiB), nur bei Ereignissen benutzt |
| RAM_D2_DMA | 18 084 Byte | Audio-DMA (libDaisy) |

Die Audiolast auf dem Cortex-M7 ist **NICHT GEMESSEN** (Ziel ≤ 60 %). Aus Host- oder
Pythonlaufzeiten wird keine M7-Last abgeleitet. Die Firmware meldet Callback-Spitze,
-Überläufe, Queue-Hochwasser und freien Stack über SQ `GET_STATS`.

## Eigener Klang-Fingerprint

`test_product.cpp` prüft vier RMS-Werte einer festen Sequenz (0,116761 / 0,079304 /
0,027349 / 0,002420, Toleranz 0,2 %). Er hält das **eigene** Verhalten fest, damit
Klangänderungen bewusst erfolgen; er sagt nichts über den Originalklang. Die Toleranz
deckt Compiler-/libm-Unterschiede zwischen Windows-Clang und Linux-g++ ab; gleiche Werte
unter `-O0` und `-O2` wurden lokal beobachtet.

## Historische Vorbereitung (01.11.2026, unverändert)

Basis `origin/main` bei `654e42e`, eigener Worktree `.work/streichwurst/`. Damals:
Compilerproben ARM/Host, neun Modelltests, zehn Registry-Prüfungen; `make
PROJECT=streichwurst` wurde absichtlich als „planned“ abgewiesen. Das historische
Pythonmodell bleibt eine eigene Referenz für Teilbeziehungen, kein Originalrenderer.
