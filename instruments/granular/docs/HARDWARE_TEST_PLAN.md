# Hardwaretest Nebulae on Daisy v0.2.0 – PENDING-HARDWARE

Manuell durch den Menschen auszuführen. Ergebnisse je Schritt als **GEMESSEN (Datum)**
oder **NICHT GEMESSEN** eintragen, nie Sollwerte als Ergebnis. Der Agent hat nichts
geflasht und nichts gehört. Bootloader nicht aktualisieren.

Benötigt: Seed3 Desktop Kit mit externem Daisy-Bootloader v6.x, USB-Kabel an **J7**,
Kopfhörer/Monitor leise, **Test-microSD ohne wichtige Daten** (zusätzlich ideal: eine
FAT32-Karte ≤ 32 GB und eine exFAT-Karte > 32 GB), Windows-PC mit Control Panel 0.5.0
(`DaisyDiagnostic.exe`, mit python-rtmidi gebaut), DAW oder MIDI-Monitor.

Testkarte vorbereiten (am PC, mit normalem Kartenleser oder später über Schritt 6):

```text
NEBULAE/SAMPLES/A Ton 48k stereo.wav      (L und R verschieden, PCM16)
NEBULAE/SAMPLES/b Ton 44k1 mono.wav
NEBULAE/SAMPLES/c 24bit.wav               (muss abgelehnt werden: Code 2)
NEBULAE/SAMPLES/Drones/2 Low.wav
NEBULAE/SAMPLES/Drones/10 Air.wav         (Reihenfolge: 2 vor 10)
NEBULAE/SAMPLES/Leer/notiz.txt            (kein WAV: keine Bank)
```

## 1. Flashen

1. `SHA256SUMS` gegen `NebulaeOnDaisy-v0.2.0.bin` prüfen.
2. RESET drücken/loslassen, innerhalb ~2 s BOOT drücken; im
   [Daisy Web Programmer](https://flash.daisy.audio/) die BIN flashen
   ([FLASHING](../../../docs/FLASHING.md)). Nicht `program-boot`.
3. Ergebnis: Kit startet, Windows meldet ein **USB-MIDI**-Gerät, **keinen** COM-Port.

## 2. Grundfunktionen (Regression zu v0.1.1)

1. Ohne Karte starten; SW18 oben/Mitte/unten = NEBULAE/SAMPLE/KEYBOARD. Hebelrichtung
   notieren.
2. Karte einstecken: Bank 1 wird gescannt, Slot 1 („A Ton…“) geladen; Stereo L/R
   getrennt; 44,1-kHz-Datei ohne Tonhöhenfehler; „c 24bit“ zeigt 2 Pulse, alter
   Klang bleibt.
3. Acht Potis, SOURCE kurz/lang, FILE, RESET, FREEZE, RECORD/Overdub/Circular wie in
   v0.1.1; keine Aussetzer.

## 3. Bänke und SW17

1. SAMPLE-Modus, SW17 umlegen: **notieren, welche Hebelstellung „Bank“ ist**
   (Firmware: `tog_sw17.Pressed()` = Bank).
2. Bankansicht: aktive Bank hell, andere vorhandene gedimmt, fehlende aus. S2 drücken:
   Bank 2 („Drones“) hell, **Klang läuft unverändert weiter** (Bankwahl lädt nichts).
3. SW17 zurück, S1: „2 Low“, S2: „10 Air“ (natürliche Sortierung).
4. NEBULAE: FILE steppt nur durch Bank 2. Karte ziehen: Tabellen leer, Live geht weiter;
   wieder einstecken: neu scannen.
5. Optional: exFAT-Karte > 32 GB und lange/Umlaut-Dateinamen von Windows und macOS.

## 4. USB-MIDI, Bootloader-Geste (F7)

1. DAW: Note On/Off, Pitch Bend, CC20–27 in allen Modi; KEYBOARD sendet Noten.
2. KEYBOARD: S9+S16 (C5+C6) 6 s halten → **kein** Neustart, Live-Schleife bleibt.
3. NEBULAE: S9+S16 allein 5 s → alle LEDs an, Bootloader (DFU). Danach RESET.

## 5. CPU-Spitze und Overrun-LED (F3)

1. Ungünstige Einstellung: DENSITY max, OVERLAP max, BLEND Mitte bis voll, Live-
   Aufnahme läuft, 2 Minuten. **D16 im NEBULAE-Modus** leuchtet nur nach einem
   Audio-Overrun (bis Neustart). Ergebnis notieren.
2. Im Control Panel (Tab Nebulae) „Audio deadline“ ablesen: Peak-% und Overrun-Zähler
   als **Firmware-Zähler** notieren (keine Labormessung).
3. Hörvergleich gegen v0.1.1 (gleiche Datei/Einstellung): Unterschiede notieren.

## 6. SD-Verwaltung über Windows (Kern von v0.2.0)

1. Control Panel starten, Tab **Nebulae**: „Performance (USB-MIDI)“, Firmware 0.2.0.
2. Kurze Live-Schleife aufnehmen, dann **Share SD card with Windows** →
   Warnung „Live loop … LOST“ erscheint; **Cancel** → Nebulae spielt weiter.
3. Erneut teilen und bestätigen. Erwartet: Ausblenden ohne Knacken (notieren),
   gehaltene Noten in der DAW werden beendet, alle LEDs kurz aus, Neustart; im Panel
   „SD management“, D16 blinkt langsam, D2 leuchtet, Windows zeigt ein Laufwerk.
   Zeit vom Klick bis zum Laufwerk notieren.
4. Im Explorer: neuen Ordner `NEBULAE/SAMPLES/Neu` anlegen, zwei WAVs kopieren, eine
   Datei umbenennen, eine löschen. Kopierzeit für eine große Datei notieren.
5. **Return to Nebulae** muss gesperrt sein, solange Windows die Karte hat.
6. Windows „Hardware sicher entfernen“ / Auswerfen → Panel „Windows ejected …“, D4.
7. **Return to Nebulae** → Neustart in Performance, neue Bank „Neu“ vorhanden, vorherige
   Bank/Slot wieder geladen; Dateien klingen korrekt. Am PC (anderer Leser) die Datei-
   Hashes optional vergleichen.
8. Ohne PC: erneut teilen, in Windows auswerfen, Panel schließen, **S4 2 s allein**
   halten → Rückkehr in Performance.

## 7. Fehlerfälle und Recovery (nur Testkarte, keine Nutzerdaten)

1. Geteilt, **ohne** Auswerfen USB-Kabel ziehen und wieder stecken → Kit startet in
   **Performance** (Power-Cycle), Karte wird normal gelesen; Ergebnis notieren.
2. Geteilt, im Panel „Disconnect“, wieder verbinden → Zustand bleibt SHARED (keine
   automatische Rückgabe).
3. Geteilt, Karte während eines Kopiervorgangs ziehen → INTERRUPTED, D5 blinkt; Panel
   verlangt **Acknowledge**; danach Return. Nie „ausgeworfen“ angezeigt.
4. Teilen ohne Karte → „No microSD card“; Teilen während RECORD → „recording“.
5. Falls nach dem Teilen wieder MIDI statt Laufwerk erscheint: Panel meldet „did not
   enter SD management“ (Risiko R1: Bootloader überschreibt Backup-SRAM). Notieren.

## 8. Diagnostics-Regression

Diagnostics CardReader 0.3.2 und App 0.5.0: Tests aus
[HARDWARE_CARD_READER_0.3.2](../../../diagnostics/docs/HARDWARE_CARD_READER_0.3.2_2026-10-31.md)
stichprobenartig wiederholen (Image ist byte-identisch, App neu).

## Ergebnisse

| Schritt | Ergebnis | Datum |
| --- | --- | --- |
| 1–8 | NICHT GEMESSEN | – |
