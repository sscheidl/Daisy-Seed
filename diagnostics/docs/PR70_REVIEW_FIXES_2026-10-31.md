# PR #70: Review-Korrekturen 0.3.2 vom 31.10.2026

## Umfang und Ergebnis

Die sechs Punkte aus dem [archivierten unabhängigen Review](SD_CARD_READER_PR70_REVIEW_2026-10-31.md)
wurden bearbeitet. Der Benutzer hat Korrektur und anschließenden Merge ausdrücklich
beauftragt. Die folgenden Ergebnisse stammen aus neuen lokalen Softwareprüfungen;
eine erneute unabhängige Claude-Prüfung dieser Korrektur wird nicht behauptet.

| Reviewpunkt | Korrektur und Prüfung |
| --- | --- |
| 1: HAL-Abbruch kann hängen | `reader_abort.h` setzt SDMMC1 per RCC zurück, ohne CMD12, Kartenantwort oder Warteschleife. Nur SDMMC1 IRQ wird deaktiviert, mit Barrieren und Löschen des Pending-IRQ; Audio/USB/SysTick bleiben frei. IDMA/DPSM werden vor Wiederverwendung des Puffers zurückgesetzt. HAL-State RESET verhindert Starts bis vollständiger Initialisierung. Alle drei Diagnose-Aufrufe (Reader-Fehler, Close, geschützter Lesetest) ersetzt; beide nm-Prüfungen verbieten HAL_SD_Abort. |
| 2: Fake und Fehlerfälle | Produktions-Abbruchfunktion gegen CMSIS/RCC-Stubs ausgeführt: BUSY/READY/RESET, späte Fertigstellung, Fehlererhalt, Idempotenz, Startverweigerung bis Reinitialisierung. DMA-Tests ergänzen Schreibstart/-DMA-Fehler, Entnahme in DMA/Programmierung, USB-Unterbrechung und Sync-Erfolg/-Fehler/-Timeout. Fake behauptet keine HAL-Fehlerlöschung mehr. |
| 3: CMD13 falsch beschriftet | Eigener fault=7/CardStatus vor und nach CardReady geprüft, auch wenn die HAL zugleich READY meldet. Fehlerbits vor Reset erhalten. |
| 4: Diagnose fehlt im Host | Vollständige optionale Gruppe io/fault/he/lba/ms strikt geparst, als ReaderInfo erhalten, in GUI angezeigt und mit Save diagnostic report als JSON exportiert. Ältere Firmware ohne Gruppe bleibt kompatibel. Protokoll-, Session- und GUI/Dateiexport-Regressionstests. Im alten Hardware-JSON die bereits vorhandenen Rohstatusmeldungen archiviert, kein neuer Hardwaretest. |
| 5: Reset ergibt ERROR statt INTERRUPTED | Ein bereits beobachtetes INTERRUPTED wird beim I/O-Fehler erhalten; nachträglich zugestelltes Interrupt-Ereignis kann auch ERROR nach INTERRUPTED überführen. Tests für beide Reihenfolgen, Quarantäne bleibt bis explizitem RETURN. |
| 6: PR-Metadaten | Beschreibung wird auf den endgültigen Software- und Hardwarestand aktualisiert; historische Messungen bleiben mit 0.3.1 gekennzeichnet. |

Der Reset ist absichtlich keine Simulation eines erfolgreichen HAL-Abbruchs mit
READY-Zustand: Die Karte kann während einer fehlgeschlagenen Übertragung nicht
zuverlässig antworten. Der Host bekommt keinen erfolgreichen Schreib-/Eject-
Abschluss. Vor erneutem Export ist eine explizite Rückgabe und vollständige
Karteninitialisierung erforderlich. Dateien mit unterbrochenen Schreibzugriffen
können beschädigt sein; der Reset repariert kein Dateisystem.

Der neue Abort selbst hat keine Wartebedingung. Die 1-s-Budgets der portablen
DMA-/Programmierwarteschleifen schließen die separaten SDK-Kommandofristen nicht
ein. Die gepinnte HAL sowie libDaisy und Compiler-Version bleiben unverändert.

## Softwareprüfung — GEMESSEN am 31.10.2026

- `make -C diagnostics/firmware test test-sanitize HOST_CXX=clang++`: bestanden.
- `diagnostics/scripts/run-host-tests-msvc.cmd`, /W4 /WX: bestanden.
- Kern 397, Anwendung 659, Reader/BOT 4654, USB-Adapter 53 Einzelprüfungen;
  Reader-App bestanden; DMA 4262, Abbruchadapter 42 Einzelprüfungen.
  In der DMA-Zahl sind 4096 Bytevergleiche enthalten, keine 4262 unabhängigen Szenarien.
- `python -B -m pytest diagnostics/tests/python -q -p no:cacheprovider`: 239 bestanden.
- `bash diagnostics/scripts/build-firmware.sh`: beide Varianten mit Vektor- und
  Symbolprüfung bestanden, insbesondere kein HAL_SD_Abort in beiden Images.
- `diagnostics/windows/build_exe.cmd`: EXE gebaut. Gepackte EXE mit
  `--sim --selftest 5`: Exit 0. Neuer Host 0.3.2 mit echter Firmware 0.3.1 über
  COM15, `--selftest 5`: Exit 0. Das ist keine Hardwareprüfung der neuen Firmware.

| Variante | Bytes | SHA-256 |
| --- | --- | --- |
| readonly | 124580 | `4dc2e93940ccd8e3b7d1929f610a2a6656c6ca51535613d1936569750968e836` |
| cardreader | 132332 | `78c7f8536c5d798ec02675c18b3b2b0fc62e6257d53816915d10cf765e9c35c5` |
| windows | 48114069 | `391a11a18d356773c1df44b2284c8c4d98b0bd9f65e71e8f00a3d1832dcc5127` |

[Maschinenlesbarer Prüfbericht](evidence/pr70-fixes-0.3.2-2026-10-31.json).
Lokale vollständige Logs und EXE-Bildschirmaufnahmen liegen im Arbeitsbaum unter
`.work/review-fixes-*`; Binärkandidaten separat unter
`.work/diagnostics-reader-test-2026-10-31/candidate-0.3.2/` im Hauptarbeitsverzeichnis.

## Hardware — GEMESSEN für CardReader 0.3.2

Nach expliziter Freigabe wurde e72db10 mit dem oben angegebenen Readerhash
auf die Anwendung geflasht, Bootloader unverändert. Grundtest, Hashvergleich
nach Auswerfen, eine vom Menschen ausgeführte Kartenentnahme während Schreiben,
CDC-Liveness, Quarantäne und expliziter Wiederanlauf bestanden. Die Testkarte
war dafür ausdrücklich als gesichert bestätigt. Windows-Dirty-Bit nach CHKDSK /F
gelöscht und nach erneutem Schreib-/Auswerfzyklus weiterhin sauber.

[Hardwareprotokoll und Testskript-Grenze](HARDWARE_CARD_READER_0.3.2_2026-10-31.md).
Nicht alle Abbruchzustände wurden auf Silizium gezielt erzwungen. Geschütztes
Image nicht geflasht; große Dateien, Dauerlast, weitere Medien, native CV-Modi,
kurze Gate-Pulse und absolute Spannungsprüfungen bleiben separat offen.

Die frühere bekannte CV-OUT-Abschaltverzögerung während blockierendem SDTEST
ist nicht Gegenstand der sechs hier bearbeiteten Reviewpunkte und bleibt offen.
Merge-Freigabe des Benutzers ersetzt keine vollständige Hardware-Abnahme oder
Release-Freigabe. Die ältere unabhängige Claude-Prüfung gilt nicht als erneute
Prüfung dieser späteren Korrektur.
