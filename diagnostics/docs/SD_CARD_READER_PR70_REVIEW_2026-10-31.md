# PR #70: unabhängiger Review und übernommene Testlogs

**Nachtrag 31.10.2026:** Die nachfolgenden Abschnitte archivieren den damaligen
Reviewstand. Die späteren Korrekturen 0.3.2, Softwareprüfungen und noch offenen
Hardwaretests stehen im [Korrekturbericht](PR70_REVIEW_FIXES_2026-10-31.md).
Der Originalreview und seine Logs bleiben unverändert; der neue Fix wurde nicht
von diesem älteren Claude-Review geprüft.

Der Benutzer hat den unabhängigen Claude-Code-Review zu
[PR #70](https://github.com/sscheidl/Daisy-Seed/pull/70) und vier zugehörige Logs
bereitgestellt. Sie werden hier unverändert archiviert. Dieses Dokument ordnet
die Nachweise ein; es ist kein neuer Testlauf und keine Firmwarekorrektur.

## Herkunft und Quellstand

- Laut bereitgestelltem Review wurde `f10864d` geprüft. Der dort zuletzt genannte
  PR-Kopf ist `2eeb21c` (CV-1-Nachweis).
- Bei der Übernahme steht der PR auf
  `4fca4f11b1989f00d8d6b0c4432b54de0356562c`. Die Änderungen von `f10864d` bis
  dorthin betreffen ausschließlich Dokumentation und Nachweise: beide CV-Kanäle
  und beide Gate-Eingänge. Der geprüfte Firmwarecode blieb unverändert.
- Auch zwischen dem im Hardwarebericht genannten Implementierungsstand
  `b89186576837f62eb7e4d5687a465af9e3e31338` und `f10864d` liegen ausschließlich
  Dokumentations-/Nachweisänderungen.
- Die Zuordnung der Testausführung zu `f10864d` stammt aus dem Review. Die Logs
  selbst enthalten keinen eigenständigen Git-Commitnachweis. Der erfolgreiche
  Buildlog enthält dagegen die vollständigen Firmwarehashes.
- Originaltext einschließlich seiner damaligen Zeilenangaben und Aussagen:
  [review-original.txt](evidence/pr70-review-2026-10-31/review-original.txt).
- Alle fünf Quelldateien wurden bytegleich kopiert; die Prüfsummen stehen in
  [SHA256SUMS](evidence/pr70-review-2026-10-31/SHA256SUMS). Die lokale
  `.gitattributes` verhindert Zeilenendenkonvertierung dieser Originaldateien.

Die ursprünglichen lokalen Scratchpad-Pfade bleiben in den Logs sichtbar.
Test-/Buildbefehle wurden für diese Dokumentationsübernahme nicht wiederholt.

## Softwareergebnisse aus den bereitgestellten Logs

**GEMESSEN 31.10.2026, laut übernommenem Claude-Testlauf; Loginhalt bei der
Übernahme geprüft.** Keine neuen Hardwareergebnisse.

| Befehl laut Review | Beobachtung im Log | Originalnachweis |
| --- | --- | --- |
| `make -C diagnostics/firmware test` (clang++) | Kern: 397, Anwendung: 659, Reader/BOT: 4631, USB-Adapter: 53 Prüfungen ohne Fehler; Reader-App bestanden; DMA: 4173 Prüfungen ohne Fehler; `EXIT test=0`. | [host-test.log](evidence/pr70-review-2026-10-31/host-test.log) |
| `make -C diagnostics/firmware test-sanitize` (clang++, ASan/UBSan) | Dieselben Suites bestanden, keine Sanitizerfehler im Log; `EXIT san=0`. | [host-san.log](evidence/pr70-review-2026-10-31/host-san.log) |
| `python -m pytest diagnostics/tests/python -v` | 222 bestanden in 15,67 s. | [pytest.log](evidence/pr70-review-2026-10-31/pytest.log) |
| `bash diagnostics/scripts/build-firmware.sh` | Beide Varianten gebaut; Vektor-/Symbolprüfungen PASS; Reader 131844 Bytes; `EXIT fw=0`. Linkerwarnungen zu newlib-Systemaufrufstubs und RWX bleiben im Original sichtbar. | [fw-build.log](evidence/pr70-review-2026-10-31/fw-build.log) |

Firmwarehashes aus dem Buildlog:

| Variante | SHA-256 |
| --- | --- |
| Geschützte Diagnose | `4978614641996a12e361f0fa9a5b9cfddbbebd9a3d0f535e739bd4052a2e8824` |
| CardReader | `bf23fc4978cb693c801d3d917d4e7ba9ec59a0ffd7fef07cc967c5c06345b7bf` |

Der Readerhash stimmt mit dem bereits abgelegten
[Hardware-JSON](evidence/card-reader-2026-10-31.json) überein. Damit stützen die
übernommenen Logs die Reproduzierbarkeit des dort getesteten Images. Die
vorhandenen Hardwarebefunde werden dadurch weder erweitert noch auf Fehlerfälle
übertragen. 4173 DMA-Prüfungen bedeuten insbesondere nicht 4173 Fehlerszenarien:
laut Review entfallen 4096 auf einzelne Bytevergleiche.

## Offene Befunde aus dem Review

| Nr. | Schwere | Befund | Status bei Übernahme |
| --- | --- | --- | --- |
| 1 | Hoch | `HAL_SD_Abort()` kann beim Abbruch eines laufenden Schreibtransfers die Hauptschleife praktisch blockieren. Die äußeren 1-s-Grenzen begrenzen diesen HAL-Aufruf nicht. | **Offener Merge-Blocker.** Aufruf und HAL-Warteschleifen im gepinnten Code bei Übernahme nachvollzogen; Fehler auf Hardware **NICHT GEMESSEN**. |
| 2 | Mittel | Sofortiger Fake-Abort modelliert weder HAL-Warteschleifen noch verbleibendes BUSY; `Sync()` und mehrere Schreibfehler-/Resetfälle fehlen im DMA-Test. | Offen; bestandene Host-/Sanitizertests widerlegen Befund 1 nicht. |
| 3 | Niedrig | CMD13-Statusfehler gelangen in `ErrorCode` und werden als DMA-Fehler beschriftet. | Offen; sicherer Fehlerabbruch, ungenaue Diagnose. |
| 4 | Niedrig | Neue Readerfelder `io`, `fault`, `he`, `lba`, `ms` werden vom Python-Host nicht ausgewertet/exportiert. | Offen; Rohprotokoll und JSON-Nachweis haben unterschiedliche Aussagekraft. |
| 5 | Info | USB-Reset während eines Transfers kann ERROR statt INTERRUPTED ergeben; laut Review kein neuer Fehler dieses PR. | Dokumentierter Hinweis, keine Korrektur in dieser Übernahme. |
| 6 | Info | Die PR-Beschreibung berücksichtigte zum Reviewzeitpunkt den CV-1-Commit noch nicht. | Am Übernahmestand waren CV-1/2 und Gate-1/2 bereits beschrieben; der nun überholte Hinweis „Review noch nicht durchgeführt“ wird ersetzt. |

Der Originalreview bestätigt außerdem DMA-Pufferlage/Cachebehandlung,
Kartenbesitztrennung, getrennte Schreibschutzprüfungen und die `session.py`-
Änderung. Das ist keine vollständige Hardwareabnahme und keine Aussage, dass
sämtliche HAL-Fehlerpfade ausreichend begrenzt sind.

## Befund 1: konkrete Ansatzpunkte für einen späteren Korrekturauftrag

Belegstellen im bei Übernahme unveränderten Quellstand:

- `diagnostics/firmware/src/reader_dma.h:80`: `Fail()` ruft den Backend-Abort auf.
- `diagnostics/firmware/src/main.cpp:272`: `ReaderBackend::Abort()` ruft direkt
  `HAL_SD_Abort(&hsd1)` auf.
- `diagnostics/firmware/src/main.cpp:301`: auch `ReaderCard::Close()` benutzt
  diesen HAL-Abbruch vor DeInit.
- Gepinnte HAL: `libDaisy/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sd.c`,
  `HAL_SD_Abort()`, Warteschleifen um Zeilen 2918–2944.
- `libDaisy/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_sdmmc.h:295`:
  `SDMMC_DATATIMEOUT = 0xFFFFFFFF`.

Diese Konstante entspricht als Millisekundengrenze rund 49,7 Tagen. Der Review
beschreibt insbesondere den CMD12-Response-Timeout beim Schreiben, nach dem die
HAL nur auf DATAEND wartet. Außerdem werden frühe HAL-Fehlerrückkehr mit
verbleibendem BUSY/IDMA und ein Interrupt-Wettlauf genannt. Diese Szenarien sind
aus Quelltext abgeleitet, nicht am Kit reproduziert.

Der vorgeschlagene eigene begrenzte Abort ist noch **kein geprüfter Fix**.
Ein späterer Auftrag muss alle Abort-/Close-Wege, SDMMC-/IDMA-Zustände,
Interruptreihenfolge und die Möglichkeit eines Neustarts nach einem Fehler
gemeinsam behandeln; die gepinnte HAL bleibt unverändert. Der Vorschlag im
Original nennt 100–500 ms und mehrere Abschluss-/Abbruchflags. Diese Zahlen und
die Registersequenz sind vor Umsetzung zu begründen und dürfen nicht einfach
als fertige Lösung übernommen werden.

Erforderliche spätere Prüfung: realistischere Backend-/HAL-Fehlermodelle,
Schreib- und Sync-Fehlerpfade, Busy-nach-Abort sowie USB-Reset/Entnahme. Danach
gezielte lokale Softwareprüfungen und ein ausdrücklich vom Menschen begleiteter
Hardwaretest für Kartenentnahme während eines Schreibvorgangs. Hardware:
**NICHT GEMESSEN**. Keine Korrektur, kein neuer Build und kein Flashvorgang wurden
mit dieser Nachweisübernahme ausgeführt.
