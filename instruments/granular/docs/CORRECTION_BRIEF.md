# Correction Brief: Nebulae on Daisy

Stand 2026-10-25. Basis ist `main` @ `5f25359` (v0.1.1). Details stehen in [INDEPENDENT_REVIEW.md](INDEPENDENT_REVIEW.md).

**Keine blockierenden Korrekturen.** C1, C2 und C4 aus dem Erstbrief vom 2026-10-20 sind erledigt (`183b792`, PR #17, CI-Run 39485407516). Die folgenden Punkte sind P2 und klar abgegrenzt. Sie sollten vor oder während des Hardwaretests erledigt werden, aber nur nach Freigabe durch den Benutzer.

## C3 (P2): CPU-Spitze glätten und sichtbar machen (F3)

**Betroffen:** `src/reference_dsp.cpp` (`Mincer::Reset`), `src/main.cpp` (`engine_storage`, `callback_overruns`), `docs/HARDWARE_TEST_PLAN.md`.

* **Hop-Phase versetzen:** Kanal 1 startet mit `cnt_ = HOP/2`, etwa per Init-Parameter. Dann rechnet jeder 256er-Block höchstens die FFTs eines Kanals. Den Versatz von 256 Samples im README vermerken.
* **Speicherort:** Den Platzbedarf von `Mincer`/`RFFT` (~210 KB) in AXI-SRAM prüfen. Laut MAP sind dort ~316 KB frei. Die PCM-Bänke bleiben im SDRAM.
* **Sichtbarkeit:** `callback_overruns > 0` ohne Debugger anzeigen, etwa als Blinkmuster auf einer in NEBULAE reservierten LED. Einen Schritt im HARDWARE_TEST_PLAN ergänzen.

**PASS-Check:**

* `make PROJECT=granular test` und `test-sanitize` enden mit exit 0.
* `check-granular-elf.py --host-test` endet mit exit 0.
* Die Review-Gegenproben stehen auf 20/20.
* Der CPU-Messwert bleibt NICHT GEMESSEN, bis die Hardware getestet ist.

## C5 (P2): Bootloader-Geste nicht als Musikgeste auslösen (F7)

**Betroffen:** `src/main.cpp` (`PollControls`), README-Abschnitt „Firmware update mode“.

**Soll (minimal, eine Variante wählen):**

* **a)** Die Tastengeste nur im NEBULAE-Modus auswerten, wo S9/S16 reserviert sind. Im Code: `boot_keys.Process(mode==NEBULAE ? held : 0, ms)`. Der SysEx-Weg bleibt in allen Modi aktiv.
* **b)** Die Geste in allen Modi beibehalten, wie es die repo-weite Regel „same behaviour in every firmware“ vorsieht. Dann muss das README ausdrücklich sagen, dass die flüchtige Live-Schleife dabei verloren geht.

Variante a weicht von der gemeinsamen Bootloader-Regel ab. Die Entscheidung liegt beim Benutzer.

**Regression bei Variante a:** `test_callback`: KEYBOARD-Modus, S9+S16 für 6 s gehalten → kein Request. NEBULAE-Modus → Request.

## Nicht erforderlich (P2, nur dokumentieren oder später)

* **F6:** Grain-Größe im Trigger-Modus auf das 6-Fache der geglätteten Clock-Periode begrenzen (Original-instr Z. 462–467).
* **R6:** Für v0.1.1 gibt es keinen CI-Firmware-Hash. Wenn der Benutzer ihn braucht, startet er `Actions → Firmware → granular` manuell (Budget-Regel in AGENTS.md). Danach den Artefakt-SHA mit `e30380f7…e50a` vergleichen und in VALIDATION.md eintragen.
