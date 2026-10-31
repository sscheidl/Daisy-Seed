# Kartenleser: Hardwaretest vom 31.10.2026

## Aufbau und Nachweis

Windows-PC, Seed3 Desktop über J7, eingelegte FAT32-microSD. Befehle und
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

## Gegenprobe 0.3.1 — NICHT GEMESSEN

Noch offen: erneutes Flashen nach manuellem Bootloader-Einstieg, Windows-Mount,
Lesen, eigene Testdateien schreiben/zurücklesen und Hashvergleich, Windows-
Auswerfen/Rückgabe sowie Verhalten bei erneutem Freigeben und CDC-Neuverbindung.
Vorhandene Dateien werden vom Test nicht überschrieben oder gelöscht.

Die Unterbrechungs-, Kartenentnahme-, Suspend-/Resume-, Langzeit- und zusätzlichen
Medienfälle aus [SD_CARD_READER.md](SD_CARD_READER.md) bleiben ohne gesonderten
Nachweis NICHT GEMESSEN. Ebenso wurden CV-/Gate-Spannungen und Audio nicht getestet.
