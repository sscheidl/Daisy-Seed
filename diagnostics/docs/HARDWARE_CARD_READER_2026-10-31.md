# Kartenleser: Hardwaretest vom 31.10.2026

## Aufbau und Nachweis

Windows 11 Pro 10.0.26300, Seed3 Desktop über J7, eingelegte FAT32-microSD.
Kartenhersteller und Modell wurden nicht erfasst. Befehle und
Messwerte stammen aus echtem USB-CDC/DDP-Verkehr, nicht aus dem Simulator.
Am Anfang lief eine MIDI-Instrumenten-Firmware. Nach vom Bediener aktiviertem
Bootloader und Flash-Freigabe wurde ausschließlich die Anwendung an QSPI-Adresse
`0x90040000` ersetzt; der Bootloader wurde nicht installiert oder verändert.

## Ausgangsstand 0.3.0 — GEMESSEN

Firmware `DaisyDiagnostic-CardReader.bin`, 131128 Bytes, SHA-256:
`7d9c7e3606313282195db08749448db5b48956f9b5dc0dab24f97250c487c3b5`.

| Prüfung | Tatsächlich beobachtet |
| --- | --- |
| Flashen | dfu-util 0.11, externe Flash-Geometrie ab 0x90000000, Anwendung erfolgreich an 0x90040000 übertragen, Exit 0 |
| USB-Anmeldung | Composite-Gerät, CDC COM15 und MSC/Disk „DAISY SD CARD READER“, Windows-Gerätestatus OK; serieller Suffix A1 |
| DDP | Firmware 0.3.0, Protokoll 1, BOOT_SRAM, Bootloader v6_1, libDaisy f715e48; `simulated=false`, Fähigkeit `sdmsc` |
| Ausgangszustand | LOCAL, Karte erkannt, keine freigegebenen Sektoren |
| Direkter SDTEST | MOUNTED, FR_OK, FAT32, 7572 MiB, zwei Wurzelverzeichniseinträge |
| Windows-Freigabe | Zunächst SHARED mit 15523840 Sektoren, nach ungefähr einer Sekunde ERROR; CDC blieb verbunden |
| Protokoll während der Probe | 0 CRC-/Inhalts-/verlorene Rahmen; gemeldete Steuerrate 1000 Hz |
| Rückgabe nach dem fehlgeschlagenen Zugriff | Explizites READER RETURN führte zurück zu LOCAL |

**Die Windows-Laufwerksfunktion ist damit für 0.3.0 fehlgeschlagen.** Die
Existenz eines Disk-Geräts und Laufwerksbuchstabens allein beweist keinen
lesbaren Datenträger. Es wurden durch den Testcode keine Testdateien auf der
Karte erstellt. Ein erfolgreicher Explorer-Lese-/Schreibtest ist nicht belegt.

## Korrektur 0.3.1 — Softwareprüfung

Der bisherige MSC-Datenpfad verwendet HAL-Polling. Ein dabei durch Audio-/USB-
Interrupts ausgelöster FIFO-Über-/Unterlauf ist eine **Hypothese aus dem Code**;
der alte Build meldet keinen konkreten HAL-Fehlercode und beweist diese Ursache
nicht. Der direkte, DMA-basierte FatFs-Lesetest funktioniert dagegen am selben
Aufbau. Die Korrektur verwendet deshalb zeitbegrenzte Single-Buffer-IDMA-
Transfers, einen exklusiven, 32-Byte-ausgerichteten SDRAM-Puffer und passende
Cache-Operationen. USB/Audio-Interrupts bleiben während des Wartens zugelassen.

`READER` enthält ergänzende Diagnosefelder `io`, `fault`, `he`, `lba`, `ms`.
Ein Fehler wird vor dem HAL-Abbruch erfasst. DMA-Abschluss allein gilt beim
Schreiben nicht als Erfolg: Der Karten-Programmierzustand muss ebenfalls fertig
sein. Der normale Diagnose-Build behält seine vollständige Schreibsperre.

Zusätzlich blieb ein direkt empfangenes SDTEST-Ergebnis im Python-Host zeitweise
nicht erhalten. BOT-busy-/STATUS-Meldungen bei unverändert lokalem Kartenbesitz
dürfen das Ergebnis jetzt nicht mehr löschen. Ein Regressionstest deckt das ab.

Lokale Softwareprüfung: ARM-Build und Symbolprüfung beider Varianten bestanden;
C++-Tests einschließlich ASan/UBSan bestanden, neuer DMA-Test 4173 Prüfungen;
Python-/GUI-Tests 222 bestanden. Diese Tests sind kein Hardware-Nachweis.

## Gegenprobe 0.3.1 — GEMESSEN am 31.10.2026

Quellstand `b89186576837f62eb7e4d5687a465af9e3e31338` auf
`codex/diagnostics-reader-hardware` (Implementierung in `60a4621`),
[PR #70](https://github.com/sscheidl/Daisy-Seed/pull/70).
Nach erneutem manuellem Bootloader-Einstieg wurde ausschließlich die Anwendung
geflasht, dfu-util Exit 0. Binärdatei: 131844 Bytes, SHA-256
`bf23fc4978cb693c801d3d917d4e7ba9ec59a0ffd7fef07cc967c5c06345b7bf`.
Die gepackte EXE hat SHA-256
`abec5e476d933804ed903b0a55d381c0e56e11fffeaa8d0120a26736ae850842`.

| Prüfung | Tatsächlich beobachtet |
| --- | --- |
| Anmeldung und Freigabe | DDP meldet 0.3.1, zunächst LOCAL; nach explizitem SHARE dauerhaft SHARED, 15523840 Sektoren à 512 Bytes; CDC COM15 bleibt verbunden |
| Windows-Laufwerk | G:, FAT32, Volume-Seriennummer 383EAD1A, Volumegröße 7939817472 Bytes; Bediener bestätigt Sichtbarkeit und Zugriff |
| Schreiben und Lesen | Eigene zufällige 1-MiB-Datei geschrieben, geflusht und mit SHA-256 zurückgelesen; Übereinstimmung |
| Dateioperationen | Eigene Textdatei erstellt und ergänzt, Binärdatei umbenannt, eigene temporäre Datei erstellt und gelöscht |
| CDC während Dateioperationen | 477 Rahmen, 0 CRC-/Inhaltsfehler, 0 verlorene Rahmen, 0 Wiederverbindungen; gemeldete Steuerrate 1000 Hz; READER fault=0, he=0 |
| Besitzsperre | Host verweigert SDTEST während SHARED; direkt gesendetes SDTEST wird von der Firmware mit ERR c=SDTEST e=BUSY abgewiesen |
| Erstes Windows-Auswerfen | Windows-Shell-Auswerfen führt zu EJECTED, io=SYNC, fault=0, he=0; CDC bleibt verbunden |
| Rückgabe und erneute Freigabe | RETURN führt zu LOCAL; neuer SDTEST liefert FR_OK/FAT32/3 Wurzelverzeichniseinträge; SHARE bindet dieselbe Karte wieder als G: ein |
| Persistenz nach erneutem Einbinden | Beide Dateihashes unverändert; Umbenennung und Löschen bleiben erhalten |
| Gepackte EXE mit echter Hardware | Selbsttest 5 s Exit 0; nach Schließen und neuer CDC-Sitzung weiterhin SHARED |
| Windows-Auswerfen bei laufender EXE | Weiterer Selbsttest 15 s Exit 0; währenddessen Shell-Auswerfen, GUI zeigt EJECTED; unabhängige CDC-Abfrage bestätigt EJECTED |
| Endzustand | Erneutes RETURN und SHARE erfolgreich; G: wieder zugänglich, Zustand SHARED |

Die kombinierte kleine Dateioperationsprobe dauerte 6,688 s; dies ist **kein**
USB-Durchsatzbenchmark. Die gemeldete Steuerrate ist keine Messung der Interrupt-
Latenz. Es gab zwei beobachtete Auswerfoperationen, keine zehn Wiederholungen.

Testverzeichnis: `G:\DaisyDiagnosticTest-20261031-d88d82f9`.

| Datei nach erneutem Einbinden | Bytes | SHA-256 |
| --- | --- | --- |
| payload-renamed.bin | 1048576 | `24e0738eb16c1d3ae125f4172b635203f7d2dc3ee00270e32582fd7fe953b3ba` |
| edit.txt | 92 | `9f7068d955bf57425084536e8eb15f499d7c5e4e44ace652853f060486e96ebc` |

Der Bediener legte zusätzlich `Hallo Codex.txt` in diesem Verzeichnis ab; die Datei
war lesbar. Deshalb wurde die geplante Bereinigung abgebrochen und das gesamte
Verzeichnis erhalten. Vorhandene Benutzerdateien wurden nicht überschrieben oder
gelöscht. Kompakter maschinenlesbarer Nachweis:
[card-reader-2026-10-31.json](evidence/card-reader-2026-10-31.json).
Rohprotokolle, Flash-Logs, EXE-Bildschirmaufnahme und geprüfte Binärdateien liegen
lokal unter `.work/diagnostics-reader-test-2026-10-31/` (nicht eingecheckt).

**Grundtest bestanden; keine vollständige Hardware-Abnahme.** Unabhängige Prüfung
der Korrektur durch Claude und Merge stehen aus.

## Noch offen — NICHT GEMESSEN

Die Unterbrechungs-, Kartenentnahme-, Suspend-/Resume-, Langzeit- und zusätzlichen
Medienfälle aus [SD_CARD_READER.md](SD_CARD_READER.md) bleiben ohne gesonderten
Nachweis NICHT GEMESSEN. Dazu gehören Dateien über 100 MiB, zehn Freigabe-/Auswerf-
Zyklen, kein/unformatiertes/exFAT-Medium und unabhängige Prüfung der Karte nach
einer Unterbrechung. Ebenso wurden CV-/Gate-Spannungen und Audio nicht getestet.
