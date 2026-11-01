# Hardware-Laufzettel Programmverwaltung

**Alle Punkte: NICHT GEMESSEN (Stand 01.11.2026).** Jeder Schritt braucht die
Mitwirkung des Menschen (Bootloader-Modus, Flashen, Beobachtung). Ergebnisse als
`GEMESSEN (Datum, Image-SHA-256, Beobachtung)` eintragen; nichts aus Erwartung ausfüllen.

## Voraussetzungen

* Installierter Daisy-Bootloader (Diagnose meldet `bl=v6_1`); **kein** `program-boot`.
* Images und SHA-256 aus [SOFTWARE_LOG.md](SOFTWARE_LOG.md) (Abschnitt „Artefakte").
  Flashweg wie bisher: Update-Modus (S9+S16 5 s / Editor / DDP `BOOT`), dann
  `make PROJECT=program-manager-probe program-dfu` bzw. Web Programmer „Custom" mit
  dem BIN nach `0x90040000`.
* **Wiederherstellung:** erneut in den Update-Modus (bei hängender Verwaltung: Reset,
  dann BOOT innerhalb von ~2 s, `docs/FLASHING.md`) und ein bekanntes Instrument-BIN
  oder die Verwaltung erneut flashen. Letzter Weg: ROM-DFU am Modulport.
* Karte nur nach `python tools/pm_card.py check <Vorlage>` einlegen (Wurzel ohne `.bin`).

## P – Probe (`DaisyProgramManagerProbe-v0.1.0.bin`)

| ID | Schritt | Erwartung (zu beobachten, nicht vorauszusetzen) | Ergebnis |
| --- | --- | --- | --- |
| P1 | Probe nach `0x90040000` flashen, Reset | Nach ~2 s: LED 16 gedimmt, USR-LED blinkt langsam | NICHT GEMESSEN |
| P2 | `python tools/pm_cli.py -p COMx info` | `app=DaisyProgramManagerProbe profile=probe cat=UNSUPPORTED` | NICHT GEMESSEN |
| P3 | 5 Minuten laufen lassen, `status` wiederholt | stabil, keine Hänger (XIP mit 480 MHz) | NICHT GEMESSEN |
| P4 | Taste 16 drücken/loslassen; S1+S16 3 s | DIAG (LEDs folgen Tasten), zurück zu SELECT | NICHT GEMESSEN |
| P5 | `pm_cli.py mode DIAG`, `raw "LED m=ON"`, `raw "CVOUT m=FIX c1=2048"`, `mode SELECT` | LEDs an; CV OUT per Multimeter: Wert notieren; nach SELECT DAC 0 – Spannung messen | NICHT GEMESSEN |
| P6 | S9+S16 5 s bzw. `pm_cli.py service --yes` | DFU `0483:DF11` erscheint; danach Instrument-BIN flashen (Wiederherstellung geprobt) | NICHT GEMESSEN |

## M – Manager (`DaisyProgramManager-v0.1.0.bin`)

| ID | Schritt | Erwartung | Ergebnis |
| --- | --- | --- | --- |
| M1 | Manager flashen, ohne Karte starten | `info`: `cat=NOCARD sd=0`; nur LED 16 gedimmt | NICHT GEMESSEN |
| M2 | Karte mit Vorlage (`pm_card.py check` PASS, FAT32) einlegen | nach ~0,3 s `cat=OK`, Plätze gedimmt; `list` zeigt Einträge; `rootbin=0` | NICHT GEMESSEN |
| M3 | exFAT-Karte | Bootloader startet trotzdem die Verwaltung; `cat=OK` mit `fs=EXFAT` (SDTEST in DIAG) | NICHT GEMESSEN |
| M4 | Erster Start: Taste 1 (Toy Piano v0.4 Paket) | LED-Balken, dann Reset, kurz DFU, Toy Piano meldet sich (MIDI IDENTIFY); Tastentest | NICHT GEMESSEN |
| M5 | `pm_cli.py start --slot 1 --id toy-piano --hash <Präfix> --gen <gen>` | `ACK`, `pct` steigt, `RESTARTING`, Port weg | NICHT GEMESSEN |
| M6 | Rückkehr aus Toy Piano: S1+S16 3 s | Verwaltung startet (nach 100 ms Fenster), `info` antwortet | NICHT GEMESSEN |
| M7 | Karte während `LOADING` ziehen | `uerr=NOCARD`, Verwaltung bedienbar, kein Start | NICHT GEMESSEN |
| M8 | Paket mit verändertem Byte (Hashfehler) | `e=HASH`, kein Start; danach gültiges Paket startet | NICHT GEMESSEN |
| M9 | Fünf Wechsel hintereinander (Toy Piano ↔ Reverb) | jedes Mal Start und Rückkehr | NICHT GEMESSEN |
| M10 | CV OUT mit Multimeter: SELECT → DIAG FIX 2048 → SELECT → Programmstart → Rückkehr | Spannungen je Zustand notieren; DAC-Code 0 ≠ 0 V annehmen | NICHT GEMESSEN |
| M11 | Strom weg während `LOADING` | nächster Start: Verwaltung (kein Flash verändert) | NICHT GEMESSEN |
| M12 | DFU-Update der Verwaltung abbrechen (Stecker), dann erneut flashen | Wiederherstellung gelingt | NICHT GEMESSEN |

## Protokoll

| Datum | Image / SHA-256 | Schritt | Beobachtung |
| --- | --- | --- | --- |
| – | – | – | – |
