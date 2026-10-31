# CardReader 0.3.2: Hardware-Gegenprobe vom 31.10.2026

## Aufbau und Firmware — GEMESSEN

Seed3 Desktop J7, Windows 11 Pro 10.0.26300, CDC COM15, FAT32 G:,
Volume-Seriennummer 383EAD1A. Der Benutzer erlaubte Flashen und Testen und
bestätigte eine vollständig gesicherte Testkarte für die Schreibunterbrechung.
Das richtige Gerät war bereits manuell im Bootloader (0483:df11, Seriennummer
356533613433); G: war zu diesem Zeitpunkt nicht vorhanden. Ein Windows-Auswerfen
vor diesem Bootloader-Einstieg wurde nicht beobachtet.

Nur Anwendung an 0x90040000 geflasht, dfu-util 0.11 Exit 0; Bootloader unverändert.
Getesteter Quellstand: `e72db103b2127fb3b3782ed884b6e0c9c7092cad`.
CardReader-BIN 132332 Bytes, SHA-256:
`78c7f8536c5d798ec02675c18b3b2b0fc62e6257d53816915d10cf765e9c35c5`.
Geschütztes 0.3.2-Image nur softwaregeprüft, nicht geflasht.

## Grundtest — GEMESSEN

- HELLO meldet 0.3.2, Start LOCAL, kein automatischer Export.
- Direkter lokaler SDTEST: FR_OK, FAT32, 7572 MiB.
- SHARE: Windows G: zugänglich, 15523840 Sektoren. SDTEST auf Host und Firmware
  während SHARED blockiert.
- Eigene 1-MiB-Binärdatei und 92-Byte-Textdatei erstellt, gelesen, Text ergänzt,
  Binärdatei umbenannt und eigene temporäre Datei gelöscht. 469 Rahmen, keine
  CRC-/Inhaltsfehler, verlorenen Rahmen oder Wiederverbindungen.
- Windows-Auswerfen führt zu EJECTED/io=SYNC/fault=0/he=0. RETURN führt durch
  den neuen Reset-basierten Close-Pfad; anschließender lokaler SDTEST und SHARE
  erfolgreich. Beide Hashes nach erneutem Einbinden unverändert.

Dateien im neu angelegten Ordner `G:\DaisyDiagnosticTest-20261031-e8a4b572`:

| Datei | Bytes | SHA-256 |
| --- | --- | --- |
| payload-renamed.bin | 1048576 | `7fba4cc2341d7cc2beafc5b05952244ef594d290723c00f4f9e6a13e25f84b70` |
| edit.txt | 92 | `9f7068d955bf57425084536e8eb15f499d7c5e4e44ace652853f060486e96ebc` |

## Kartenentnahme während Schreiben — GEMESSEN

Nur eine neue exklusive Datei im Ordner `DaisyDiagnosticInterrupt-032-*`
beschrieben, in 64-KiB-Blöcken mit Flush/fsync. Bei laufendem Schreibzugriff
zog der Benutzer die Karte, USB und Strom blieben verbunden. Vor dem Fehler
wurden 2293760 Bytes erfolgreich geflusht. Windows-Schreiber endete mit
`PermissionError(13, 'Permission denied')` nach der Entnahme.

Firmware blieb ohne Neustart erreichbar. Die Statusmeldung lautete:

```text
READER state=INTERRUPTED sectors=15523840 busy=0 a=STATUS io=WRITE fault=3 he=6 lba=25627 ms=0
```

Die berichteten 0 ms sind die Firmware-Auflösung des fehlgeschlagenen Vorgangs,
keine unabhängig gemessene Reaktionszeit. Die HAL-Fehlerbits wurden trotz Reset
erhalten. Es wurde nicht der alte, lang wartende HAL-Abbruch aufgerufen; beide
Image-Symbolprüfungen verbieten ihn.

**Grenze des Testskripts:** Der Windows-Schreiber meldete seinen Fehler vor der
nächsten READER-Meldung. Das Skript beendete deshalb den Beobachtungsabschnitt
zu früh und schrieb `completed=false`; diese Rohdatei bleibt unverändert.
Im abschließenden Poll stand bereits INTERRUPTED. Anschließende unabhängige
15-s-Probe: 918 Rahmen, 0 CRC-/Inhalts-/Verlustfehler, 0 Wiederverbindungen,
Karte erkannt als abwesend, unverändert INTERRUPTED/WRITE/fault=3/he=6.
Die Probe erhielt für spätere Läufe eine Wartefrist nach Ende des Schreibers.

Gepackte EXE 0.3.2 mit echter Firmware 0.3.2 nach dem Fehler: Selbsttest Exit 0;
verbunden und INTERRUPTED im Ereignisprotokoll. Die strukturierte Hostantwort
enthält alle fünf Diagnosefelder. Die Windows-Fontkonfiguration ist für die
lokale Offscreen-Bildschirmaufnahme gesetzt.

## Wiedereinsetzen und Wiederanlauf — GEMESSEN

Der Benutzer setzte die Karte wieder ein. Zustand blieb zunächst INTERRUPTED,
keine automatische Freigabe. Erst explizites RETURN ermöglichte lokalen SDTEST
(FR_OK/FAT32, vier Wurzelverzeichniseinträge), danach SHARE. Die beiden bekannten
Dateihashes blieben auch nach der Schreibunterbrechung identisch.

Windows bot „Prüfen und reparieren“ an; fsutil bestätigte das Dirty-Bit.
Ein erster CHKDSK ohne Reparaturparameter meldete keine Probleme, warnte aber
vor einem nicht exklusiv gesperrten Volume. Sauberes Auswerfen löschte die
bestehende Dirty-Markierung nicht. Daraufhin CHKDSK /F auf der bestätigten
Testkarte: Exit 0, keine Dateisystemprobleme gemeldet. Kein /X, kein erzwungenes
Dismount, kein Formatieren. Danach fsutil: NICHT fehlerhaft.
Zum Verhalten des Dirty-Bits: [Microsoft](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/fsutil-dirty).

Anschließend neue 64-KiB-Datei geschrieben, geflusht und gelesen. Ein erster
Shell-Auswurf führte noch nicht zu EJECTED; ein späterer normaler Auswurf war
erfolgreich, ohne erzwungene Trennung. Nach RETURN/SHARE stimmt auch deren Hash:
`62b533dbc96dc74a90671685e84fb5cf09a1841a938f7b29c180037e01c54348`.
Nach diesem weiteren Schreib-/Auswerf-/Einbindezyklus bleibt das Volume laut
fsutil NICHT fehlerhaft. Endzustand SHARED, G: zugänglich; Testdateien erhalten.

## Aussagekraft und offene Fälle

**Grundtest und eine physische Schreibunterbrechung mit Wiederanlauf bestanden.**
Es gibt keinen Silizium-Trace des DPSM/IDMA-Zustands beim Abbruch; nicht jeder
BUSY-/CMD12-/Timeout-Zweig wurde gezielt auf Hardware erzwungen. Die ergänzenden
Hosttests prüfen die Produktions-Abbruchsequenz für BUSY, READY und spätes IRQ.
Ein unabhängiger Kartenleser wurde zur Medienprüfung nicht verwendet.

Dateien über 100 MiB, Dauerlast, weitere Medien, alle Reset-Zeitpunkte sowie die
noch offenen CV-/Gate-Prüfungen bleiben NICHT GEMESSEN. Ein korrekt reagierender
Abbruch garantiert keine Dateisystemkonsistenz bei beliebiger Strom-/Kartenentnahme.
Die frühere 0.3.1-CV-/Gate-Messreihe wird nicht als 0.3.2-Messung ausgegeben.

[Strukturierter Nachweis](evidence/card-reader-0.3.2-2026-10-31.json),
[Softwarekorrekturen](PR70_REVIEW_FIXES_2026-10-31.md).
Rohprotokolle und Flash-Log lokal unter `.work/diagnostics-0.3.2-hardware-2026-10-31/`.
