# Hardwaretest: Bootloader ohne BOOT/RESET

Prüft den Software-Einstieg in den Daisy-Bootloader
([common/bootloader](../common/bootloader/README.md), [FLASHING.md](FLASHING.md)).
Flashen, Tasten und Kabel bedient der Mensch; der Agent trägt nur ein, was berichtet
wurde. Alle Ergebnisse stehen auf **NICHT GEMESSEN**, bis jemand sie am Kit prüft
(dann **GEMESSEN** mit Datum).

Voraussetzungen: externer Daisy-Bootloader v6.x installiert (laut Diagnose vom
17.10.2026 `bl=v6_1`), Kit über J7 verbunden, Firmware aus diesem Stand gebaut.
Für den ersten Test braucht es noch einmal RESET/BOOT, denn die alte Firmware kennt
den Einstieg nicht.

| # | Test | Erwartung | Ergebnis |
| --- | --- | --- | --- |
| B1 | Toy Piano: S13 + S14 allein 3 s halten | Nach 3 s leuchten alle 16 LEDs, USR-LED pulsiert, Windows zeigt DFU `0483:DF11`; der Web Programmer verbindet | NICHT GEMESSEN |
| B2 | Bootloader wartet ohne Zeitlimit | Nach B1 mindestens 60 s warten, dann flashen: klappt | NICHT GEMESSEN |
| B3 | LEDs nach dem Flashen | Neue Firmware startet, alle LEDs gehen aus (`hw.Init()`) | NICHT GEMESSEN |
| B4 | Fehlgriffe | S13 + S14 + eine dritte Taste 5 s: nichts; S13 + S14 2 s, loslassen: nichts; S13 allein 5 s: nichts | NICHT GEMESSEN |
| B5 | Editor (HTML, JUCE, VST3) mit Toy Piano | **Firmware-Update …** → Rückfrage → „Update-Modus“-Meldung, Kit wie B1 | NICHT GEMESSEN |
| B6 | Editor ohne Mod-on-Daisy-Verbindung, Nebulae | **Firmware-Update …** mit gewähltem Port → Kit wie B1 | NICHT GEMESSEN |
| B7 | Physical Modeling (normaler und MOD-Build), Looper, Reverb, Mod bench | S13 + S14 3 s → Kit wie B1 | NICHT GEMESSEN |
| B8 | Looper während einer Kartenspeicherung | Aufnahme beenden, sofort S13 + S14: Reset erst nach dem Speichern; `loop.wav` danach lesbar | NICHT GEMESSEN |
| B9 | Diagnose-Firmware | Windows-App **Firmware update mode …** → Kit wie B1; im Kartenleser-Build bei geteilter Karte gesperrt | NICHT GEMESSEN |
| B10 | Ältere Firmware | Editor-Knopf mit einem Image ohne Einstieg: „Keine Antwort“ nach 2 s, Kit läuft weiter | NICHT GEMESSEN |
| B11 | Studio One mit Input-Monitoring | Antwort des Kits zurückgeschickt: kein zweiter Reset, keine Schleife | NICHT GEMESSEN |
| B12 | Wiederherstellung | RESET, dann BOOT innerhalb von etwa 2 s funktioniert weiterhin | NICHT GEMESSEN |
| B13 | Hörtest | Klang der Instrumente unverändert (kein Knacken beim Spielen von S13/S14) | NICHT GEMESSEN |
