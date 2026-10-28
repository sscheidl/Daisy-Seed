# Übergabe an Claude: Acid Seq #54–#60

Stand 27.10.2026. Geprüfter Produktstand: `claude/acid-seq-phase-6` @ `d3297fdc6f7c2faf0b781f7cb1b5b25364c24170`, einschließlich `bfd02c4`.

**Verdikt: NICHT BEREIT. 6 P1, 11 P2, 1 P3.** Maßgeblich ist `CODEX_FEHLERSUCHE.md` mit F01–F18, Datei:Zeile, konkreten Auslösern und vorgeschlagenen Regressionen. F01–F15 behalten ihre bisherigen IDs. F16–F18 wurden bei der nachgeholten Browserprüfung ergänzt.

Keine Firmware-, Editor- oder bestehenden Testquellen korrigiert. Keine Phasen-Branches geändert, nichts geflasht, kein Release. Der Benutzer lässt die Befunde anschließend phasenweise einarbeiten. Die vier Entscheidungen Q1–Q4 stehen noch beim Benutzer; Empfehlungen im Hauptbericht sind keine erteilte Freigabe.

## Was jetzt tatsächlich ausgeführt ist

* Eigener Linux-Lauf: **alle elf Ziele PASS**, g++ 13.3.0, geprüfte Quellen exakt `d3297fdc`: ASan/UBSan und `test-thread` mit TSan für Step Seq, Acid 303, Drive, Stereo Delay und Acid Seq; zusätzlich Editor `test-sanitize`. [GitHub-Lauf 40460614328](https://github.com/sscheidl/Daisy-Seed/actions/runs/40460614328). Die Komponenten-Goldens bleiben auch in beiden Sanitizer-Builds gleich.
* Reguläre Linux/g++-Goldens einschließlich Toy Piano, Physical Modeling und Tape-Referenzvergleichen: vorhandener erfolgreicher #60-Lauf, im Hauptbericht einzeln belegt. Eigener Tape-Vorher/Nachher-Renderer ebenfalls bitgleich.
* Bestehende lokale JUCE-App (Build mit JUCE 9.0.0): `--selftest`, nur Simulatoren, **Exit 0**, einschließlich SQ-Seiten und Songzustand/Bestätigung. Dies ist kein frischer Build des Prüf-Heads gegen 9.0.4. Kein vorhandener 9.0.4-Quellstand gefunden, auftragsgemäß nichts heruntergeladen.
* Playwright mit bereits vorhandener Chromium-Headless-Shell: **Original-e2e FAIL wegen F16**. Danach mit extern gebundenen Timern weitergeprüft: F17 bei der Testkalibrierung; nach expliziter Fake-Kalibrierung F18 bei der MD-Testbereinigung. Mit allen drei ausschließlich externen Hilfen bestehen alle SQ-/MD-Assertions und der diagnostische Lauf endet mit Exit 0. Dieser diagnostische PASS ersetzt keinen grünen Originaltest.

## Einarbeitung nach Phase

| Phase / Branch | Befunde / Arbeit |
| --- | --- |
| 0 / `claude/acid-seq-phase-0-f1c35b` | Keine eigenständige neue Produktkorrektur hier; Vertrag und CI-Grundlage bei Änderungen konsistent halten |
| 1 / `claude/acid-seq-phase-1` (#55) | **F03 MIDI-Legato-Reihenfolge**, **F04 Clock-Quellenwechsel**; F08 INT32_MIN in Kalibrierprüfung; F09 fehlender Antwortvalidator im Python-Probe |
| 2 / `claude/acid-seq-phase-2` (#56) | **F01 verlorene kritische Eingaben bei voller Voice-Queue**; F10 Revisionsweitergabe; F12 angebotener Slide-Modus wird von SW18 überschrieben; erste Ergänzung der Verbraucherregeln F14 |
| 3 / `claude/acid-seq-phase-3` (#57) | F10/F14 auch für Drive; Q3/Q4 zu Auto-Level und Fold bleiben Entscheidungen des Benutzers |
| 4 / `claude/acid-seq-phase-4` (#58) | **F02 vorzeitiges Tail-Ende und Wiederauftauchen alter Echos**; F10/F14 für Delay; F15 irreführende Test-PASS-Zeile. Unity-Bypass Q1 noch nicht entschieden |
| 5 / `claude/acid-seq-phase-5` (#59) | **F05 JUCE zu früh Ready**, **F16 Browser-Timer blockieren Verbindung**; F06 Monitorverluste ohne Abgleich, F07 ungültige Patchzahlen, F13 Kalibrierpräzision. F17 Testkalibrierung explizit aufsetzen; F18 MD-Simulator über korrektes Prozessende bereinigen |
| 6 / `claude/acid-seq-phase-6` (#60) | **F01 gleicher Queue-Verlust im Produkt**; F11 globale Revision über zwei Queues. Frühere Komponentenfehler über ihre jeweiligen Phasen übernehmen |

P1 zuerst beheben. Bei den Regressionen nicht nur die jetzigen bestandenen Goldens wiederholen: Ein voller Queue-Fall, ein Delay mit Echoabstand >500 ms, MIDI-Bytefolge bei Legato, Clock-Wechsel während Run und zeitlich getrennte GUI-Protokollantworten fehlen dort. TSan findet diese logischen Ablaufprobleme nicht automatisch.

## Neue Browserbefunde knapp

* **F16 / P1:** `web/sq_protocol.js:219`, `:247`, `:266`: Window-Timer werden unverändert gespeichert und als Clientmethoden aufgerufen. Chromium meldet `Illegal invocation`; selbst ein korrekt antwortendes SQ-Gerät wird als fehlend behandelt. Beide Timer berücksichtigen. Die Node-Tests decken diesen Browser-Empfängerfehler nicht ab.
* **F17 / P2:** `web/tests/e2e_sq.cjs:92`: Der unkalibrierte Simulator liefert Nullwerte; der Test sendet diese unverändert und erwartet trotzdem CV-Freigabe. Gültige Fixture explizit in beide Kalibrierformulare schreiben und Übernahme abwarten. Die Prüfwerte gehören ausschließlich zum simulierten Gerät.
* **F18 / P2:** `web/tests/e2e_sq.cjs:113`: MD-`mod_sim` unterstützt kein Q zum Beenden, antwortet darauf aber mit OK. Der Test hat keinen wartenden Command dafür und kann in Zeile 30 abstürzen. EOF/Prozessende kontrolliert verwenden.

## Belege und Grenzen

`CODEX_REVIEW_EVIDENCE.zip` enthält die vollständigen Linux-Einzelprotokolle/Exitcodes, eigenen Reproduktionsprogramme, Browserfehler und diagnostischen Lauf, JUCE-Selbsttest und Screenshots. Der Hauptbericht ist ebenfalls im Paket. Sanitizer-Fehler in den separaten Repros F07/F08 sind erwartet und belegen die Befunde; die vorhandenen Suites melden sie nicht.

Der isolierte Prüfbranch `codex/acid-seq-review-validation-20261027` enthält ausschließlich den einmaligen Workflow, Commit `a26443db118039d328b291993646019ba82b3667`. **Nicht in den Produktbranch mergen.** Sein Checkout ist absichtlich auf `d3297fdc` festgelegt. Für die Prüfung späterer Korrekturen muss der Ref bewusst geändert werden; ein unveränderter grüner Wiederholungslauf würde nur den alten Stand prüfen. Die Belege wurden lokal gesichert, das GitHub-Artefakt läuft nach drei Tagen ab.

Hardware bleibt überall **NICHT GEMESSEN**. Neuer JUCE-9.0.4-Build bleibt mangels vorhandener Abhängigkeit ausgenommen. Kein grüner Original-Browser-e2e nach Korrekturen behauptet. Für Details, alle Fundstellen und offene Benutzerfragen den Hauptbericht verwenden.
