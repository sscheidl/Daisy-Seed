# Hardwaretest: Bootloader ohne BOOT/RESET

Prüft den Software-Einstieg in den Daisy-Bootloader
([common/bootloader](../common/bootloader/README.md), [FLASHING.md](FLASHING.md)).
Alle Firmwares verhalten sich gleich: **S9 + S16 fünf Sekunden** halten, oder
**Firmware-Update …** im Editor (bei der Diagnose: **Firmware update mode …** in der
Windows-App). Flashen, Tasten und Kabel bedient der Mensch; der Agent trägt nur ein,
was berichtet wurde. Alle Ergebnisse stehen auf **NICHT GEMESSEN**, bis jemand sie am
Kit prüft (dann **GEMESSEN** mit Datum).

Voraussetzungen: externer Daisy-Bootloader v6.x installiert (laut Diagnose vom
17.10.2026 `bl=v6_1`), Kit über J7 verbunden, Firmware aus diesem Stand gebaut.
Für den ersten Test braucht es noch einmal RESET/BOOT, denn die alte Firmware kennt
den Einstieg nicht.

| # | Test | Erwartung | Ergebnis |
| --- | --- | --- | --- |
| B1 | Toy Piano: S9 + S16 allein 5 s halten | Nach 5 s leuchten alle 16 LEDs, USR-LED pulsiert, Windows zeigt DFU `0483:DF11`; der Web Programmer verbindet | NICHT GEMESSEN |
| B2 | Bootloader wartet ohne Zeitlimit | Nach B1 mindestens 60 s warten, dann flashen: klappt | NICHT GEMESSEN |
| B3 | LEDs nach dem Flashen | Neue Firmware startet, alle LEDs gehen aus (`hw.Init()`) | NICHT GEMESSEN |
| B4 | Fehlgriffe | S9 + S16 + eine dritte Taste 7 s: nichts; S9 + S16 4 s, loslassen: nichts; S9 allein 7 s: nichts | NICHT GEMESSEN |
| B5 | Editor (HTML, JUCE, VST3) mit Toy Piano | **Firmware-Update …** → Rückfrage → „Update-Modus“-Meldung, Kit wie B1 | NICHT GEMESSEN |
| B6 | Editor ohne Mod-on-Daisy-Verbindung: Nebulae, Looper, Ambient Reverb | Kit erscheint als „Daisy Seed External“; **Firmware-Update …** mit gewähltem Port → Kit wie B1 | NICHT GEMESSEN |
| B7 | Tasten in Physical Modeling (normaler und MOD-Build), Nebulae, Looper, Reverb, Mod bench, Diagnose | S9 + S16 5 s → Kit wie B1 | NICHT GEMESSEN |
| B8 | Looper während einer Kartenspeicherung | Aufnahme beenden, sofort S9 + S16 bzw. Editor-Knopf: Antwort sofort, Reset erst nach dem Speichern; `loop.wav` danach lesbar | NICHT GEMESSEN |
| B9 | Diagnose-Firmware | Windows-App **Firmware update mode …** → Kit wie B1; im Kartenleser-Build bei geteilter Karte gesperrt (Knopf und Tasten) | NICHT GEMESSEN |
| B10 | Ältere Firmware | Editor-Knopf mit einem Image ohne Einstieg: „Keine Antwort“ nach 2 s, Kit läuft weiter | NICHT GEMESSEN |
| B11 | Studio One mit Input-Monitoring | Antwort des Kits zurückgeschickt: kein zweiter Reset, keine Schleife | NICHT GEMESSEN |
| B12 | Wiederherstellung | RESET, dann BOOT innerhalb von etwa 2 s funktioniert weiterhin | NICHT GEMESSEN |
| B13 | Looper und Reverb mit USB-MIDI | Normaler Betrieb unverändert (Aufnahme, Wiedergabe, Effekt), kein Knacken; USB-Kabel ab- und anstecken ohne Störung | NICHT GEMESSEN |
| B14 | Hörtest | Klang der Instrumente unverändert | NICHT GEMESSEN |
