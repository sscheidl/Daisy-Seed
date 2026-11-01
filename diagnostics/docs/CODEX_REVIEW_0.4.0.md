# Prüfauftrag an Codex: Firmware/App 0.4.0 — Audio-Erweiterung (nur Code)

## Auftrag und Grenzen

Prüfe die neuen Funktionen der Diagnose-Suite 0.4.0 **auf Codeebene**: Firmware,
Protokoll, Host-Parser/Session, Simulator und ihre Tests. **Keine GUI-Tests** und
keine Bewertung der Oberfläche; die macht der Mensch selbst. Kein Flashen, keine
Hardware, keine Änderungen an libDaisy, keine teuren GitHub-Workflows. Befunde als
Bericht (P0–P3 mit Datei:Zeile, Auslösung, Auswirkung, minimaler Korrektur); nur
korrigieren, wenn der Mensch das danach beauftragt.

Stand: Branch `claude/diagnostics-audio-0.4.0` bzw. der Merge in `main`
(Commit in der PR-Beschreibung). Firmware `kFirmwareVersion` 0.4.0, App
`__version__` 0.4.0, DDP weiterhin `proto=1` mit neuer Fähigkeit `audio`.

## Was neu ist (Dateien)

| Bereich | Dateien |
| --- | --- |
| Audio-Callback: Ton, Pegel, Schnappschuss | `firmware/src/main.cpp` (`AudioCallback`, `DevkitBoard::ApplyTone/ReadAudioLevels/StartScope/ScopeData`, `RoStatus`) |
| Hauptschleife | `firmware/src/diag_app.cpp/.h` (`SilenceOutputs`, `PumpScope`, Tonbestätigung, bedingtes KeepAlive, AU-Rahmen) |
| Protokollkern | `firmware/src/diag_core.cpp/.h` (`TONE`, `SCOPE`, `WriteAudio`, `WriteScopeChunk`, `LedMode::PotDim`, `LedBrightness`) |
| Spezifikation | `protocol/PROTOCOL.md` (Abschnitt „Audio extension"), `protocol/vectors.txt` (neun neue Vektoren) |
| Host | `windows/daisy_diag/protocol.py` (`parse_audio`, `parse_scope_chunk`, `ScopeAssembler`, `cmd_tone`, `cmd_scope`), `session.py` (AU/SC, `request_tone/scope`, Timeout), `simulator.py` (TONE/SCOPE/AU, POTDIM, `audio_patched`) |
| Messung | `windows/daisy_diag/gui/scope.py` (`measure`: Frequenz aus Nulldurchgängen, Peak) — nur die Funktion `measure`, nicht die Widgets |
| Tests | `tests/firmware/test_diag_core.cpp` (`TestAudioExtension`), `tests/firmware/test_diag_app.cpp` (`TestToneLifecycle`, `TestOutputsSilencedBeforeBlockingSdTest`, `TestScopeTransfer`, `TestScopeUnsupportedBoard`, `TestPotDimBrightness`), `tests/python/test_protocol.py`, `test_session.py` |

## Prüfpunkte

1. **Echtzeit im Callback** (`main.cpp`): keine Allokation, keine Sperren, kein
   Blockieren. Kosten von `sinf` pro Sample bei 48 kHz / Blockgröße 48; Phasen-
   akkumulator ohne Drift/Überlauf; Verhalten bei Frequenzwechsel (Phasensprung?);
   Pegelbegrenzung wirklich ≤ 0,5 FS auf beiden Kanälen; Kanalwahl L/R/LR.
2. **Ton-Wächter:** `tone_until_ms` wird nur von der Hauptschleife gesetzt
   (`ApplyTone(now + 1000)`, Auffrischung alle 200 ms, sofort bei Änderung). Prüfe
   den u32-Wrap des Vergleichs, das Verhalten bei blockierter Hauptschleife
   (SDTEST/SHARE), bei Sitzungsende (BYE, Timeout) und die Reihenfolge in
   `SilenceOutputs`. Gibt es einen Pfad, auf dem der Ton länger als 1 s nach
   Verlust des Hosts weiterläuft?
3. **Atomics und Datenflüsse zwischen Callback und Hauptschleife:** `peak_*`
   (Single-Writer-Maximum, `exchange(0)` im Leser), `scope_state`-Zustandsautomat
   (Idle/Armed/Capturing/Ready), Memory-Order, Wiederbewaffnung während `Ready`,
   Lebensdauer des `scope_buffer` während des Versands (StartScope während
   `PumpScope` ist durch `scope_active_` in der App ausgeschlossen — stimmt das
   auf allen Pfaden, auch nach Sitzungsende?).
4. **Trigger:** steigender Nulldurchgang nur auf dem linken Eingang, 200-ms-Timeout,
   `scope_prev_sample` über Blockgrenzen; Kapazität 2048 Frames bei Blockgröße 48
   (Teilblock am Anfang und Ende).
5. **Rahmen und Puffer:** `SC`-Body ≤ 400 Bytes (40 Frames = 320 Hex + Kopf),
   `WriteScopeChunk` bei `frames % 40 != 0`, `TxQueue::Room()`-Prüfung vor dem
   Anhängen, maximal drei Chunks pro Schritt, Reihenfolge und Vollständigkeit,
   Interaktion mit ST/SYS/READER-Rahmen und `kRequestNewSession` (Discard).
6. **Parser-Strenge beidseitig:** `TONE`/`SCOPE` lehnen `+`, `_`, Überlängen,
   Bereichsverletzungen ab (C++ `LookupField`/`ParseU32`; Python `_opt_int`,
   `parse_audio`, `parse_scope_chunk`); Host verwirft AU/SC ohne `audio`-Fähigkeit;
   `ScopeAssembler` bei fehlenden, doppelten oder gemischten Chunks.
7. **POTDIM:** `LedBrightness` nur in diesem Modus, Rundung, `ST br` effektiv,
   Simulator `effective_brightness` identisch; `ApplyLeds`-Rate (`kLedMinIntervalMs`)
   bei kontinuierlicher Helligkeitsänderung und der dokumentierte LED-Treiber-
   Hänger nach fehlgeschlagenem `TransmitDma()`.
8. **Review-Korrekturen:** F1 (`RoStatus` bei `State != READY`): verändert das den
   Gutfall (FatFs ruft `disk_status` auch zwischen Transfers)? F3b: bedingtes
   KeepAlive nur bei Bytes im RX-Ring — kann ein Host, der während des Blocks nur
   einen Teil eines Rahmens geschickt hat, fälschlich verlängert oder getrennt werden?
9. **Tests bewerten:** Zählen die neuen Tests unabhängige Szenarien oder bestätigen
   sie Fake-Eigenschaften (`FakeBoard` in `test_diag_app.cpp`, Simulator in
   Python)? Fehlende Fälle benennen (z. B. SCOPE mit Trigger ohne Signal,
   TX-Queue voll während Chunks, Tonwechsel bei laufendem Schnappschuss).
10. **Dokumentation gegen Code:** `PROTOCOL.md`-Beispiele und Vektoren gegen
    `WriteAudio`/`WriteScopeChunk`/Simulator; `FIRMWARE.md` 0.4.0; Zusagen
    („verstummt nach 1 s", „≤ −6 dBFS") gegen die tatsächlichen Konstanten.

## Befehle

```text
diagnostics\scripts\run-host-tests-msvc.cmd
make -C diagnostics/firmware test HOST_CXX=clang++
python -m pytest diagnostics/tests/python/test_protocol.py diagnostics/tests/python/test_session.py -q
bash diagnostics/scripts/build-firmware.sh
```

Keine GUI-Tests (`test_gui*.py`) und keine EXE nötig.

## Ergebnis

Bericht als eigene Datei (z. B. `CODEX_REVIEW_0.4.0_RESULT.md`) mit Prüfstand,
Befunden P0–P3, geprüften Punkten ohne Befund und einem Urteil für
Firmware/Protokoll und Host-Code: bereit / bereit mit klaren Einschränkungen /
nicht bereit für den Hardwaretest. Keine Hardwarefreigabe ableiten.
