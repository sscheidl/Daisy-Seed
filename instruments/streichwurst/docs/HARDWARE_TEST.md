# Streichwurst 0.1.0: Laufzettel für den Hardwaretest

Stand 01.11.2026. Alle Punkte sind **NICHT GEMESSEN**, bis der Mensch sie am Kit
durchführt und das Ergebnis berichtet. Der Agent flasht nicht; Flashen nur, wenn der
Mensch das Kit in den Bootloader gebracht und darum gebeten hat ([FLASHING.md](../../../docs/FLASHING.md)).

Image: `instruments/streichwurst/build/Streichwurst-v0.1.0.bin` (lokal gebaut, nicht
veröffentlicht). Vorher: `python scripts/check-firmware.py <bin>`.

| Nr. | Prüfung | Erwartung (keine Messung) | Ergebnis |
| --- | --- | --- | --- |
| H1 | Start, Ausgang Kopfhörer/Line | Stille ohne Noten, kein Knacken beim Start | NICHT GEMESSEN |
| H2 | S1–S8 spielen C4–C5 | Strings mit Ensemble Modus 1 und etwas Hall, Stereo | NICHT GEMESSEN |
| H3 | USB-MIDI (J7) Noten, Pitchbend, Sustain | Klang, Bend ±2 Halbtöne, Pedal hält | NICHT GEMESSEN |
| H4 | DIN-MIDI Noten | wie H3 (gleicher Kanalfilter) | NICHT GEMESSEN |
| H5 | VR1–VR8 Hauptseite | Registrierung, Crescendo, Release, Balance, Animate, Phaser, Reverb, Master; Übernahme erst nach Überqueren | NICHT GEMESSEN |
| H6 | S9 Seitenwechsel Haupt→Solo→FX, LEDs S9–S11 | Seite wechselt beim Loslassen, keine Sprünge der Werte | NICHT GEMESSEN |
| H7 | S10 Oktaven, S11 Modus, S12 Ensemble, S13 Solo-Sustain, S14 Split | hörbare Umschaltung, Ensemblewechsel ohne Klick (20 ms Überblendung) | NICHT GEMESSEN |
| H8 | S15 RAM-Presets 0–3 | Blinkfolge an S15, Klang wechselt, Kanal/Master/Balance bleiben | NICHT GEMESSEN |
| H9 | S16 Panik | Ton bricht mit kurzer Rampe ab, danach spielbar | NICHT GEMESSEN |
| H10 | S9+S16 kurz gedrückt, losgelassen | weder Seitenwechsel noch Panik | NICHT GEMESSEN |
| H11 | S9+S16 5 s halten | alle LEDs an, Bootloader wartet (DFU-Gerät erscheint) | NICHT GEMESSEN |
| H12 | Editor (JUCE 0.3.0 oder Browser 0.3.0) verbinden | „streichwurst 0.1.0 · SQ + Matrix“, Werte vom Gerät, nichts überschrieben | NICHT GEMESSEN |
| H13 | Parameter in der App ändern, Poti drehen | App-Änderung hörbar; Potibewegung erscheint in der App | NICHT GEMESSEN |
| H14 | Matrix: LFO 1 → Registrierung | wandernde Klangfarbe; Matrix leeren → Grundklang | NICHT GEMESSEN |
| H15 | `.swpatch` speichern, ändern, laden + übertragen | Zustand wie gespeichert, Matrix inklusive | NICHT GEMESSEN |
| H16 | Dichter Akkord (10 Tasten, Oktaven „Beide“, Modus 1, Hall) | keine Aussetzer; Zähler in der App: Callback-Frist 0 | NICHT GEMESSEN |
| H17 | CPU: Callback-Spitze (App → Zähler lesen) | Ziel ≤ 60 % von 480 000 Zyklen je 1-ms-Block | NICHT GEMESSEN |
| H18 | Stack frei (Zähler 16) | deutlich > 0 nach H16 (Linkerreserve 8,6 KiB) | NICHT GEMESSEN |
| H19 | Firmware-Update aus der App | Rückfrage, Bootloader, alle LEDs | NICHT GEMESSEN |
| H20 | Hörvergleich mit Originalplugin/-gerät | eigene Annäherung, Abweichungen siehe [FIDELITY.md](FIDELITY.md) | NICHT GEMESSEN |

Berichtete Ergebnisse trägt der Agent mit Datum als GEMESSEN in diese Tabelle und in
[VALIDATION.md](VALIDATION.md) ein.
