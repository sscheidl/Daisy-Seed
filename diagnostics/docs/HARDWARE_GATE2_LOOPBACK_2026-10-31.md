# Gate-2-Loopback: Hardwaretest vom 31.10.2026

## Aufbau — GEMESSEN

Fortsetzung des [Gate-1-Tests](HARDWARE_GATE1_LOOPBACK_2026-10-31.md) am selben
Seed3 Desktop über J7/COM15, Diagnose-Firmware CardReader 0.3.1,
Quellstand `b89186576837f62eb7e4d5687a465af9e3e31338`.
Der Bediener antwortete „ok“ auf die Aufforderung, auf **CV OUT 1 → GATE IN 2**
umzustecken und zu bestätigen. Nun folgt Gate 2 den Ausgangsbefehlen;
Gate 1 bleibt Low. Echte USB-CDC-Messreihe, kein Simulator, kein Flashen.
CV OUT 2 blieb bei Code 0; Reader unverändert SHARED; kein SDTEST.

## Beobachtungen — GEMESSEN

| Abschnitt | Befehl / angeforderte Haltedauer | Beobachtung |
| --- | --- | --- |
| Ausgangsbasis | OFF, 1 s | Beide Gates Low, Zähler Gate 1 = 21 aus dem vorherigen Test, Gate 2 = 0 |
| Gehaltenes High | CV OUT 1 Code 4095, 2 s | Gate 2 durchgehend High, Zähler genau 1; keine Wiederholungszählung |
| Gehaltenes Low | Code 0, 1 s | Gate 2 Low, Zähler bleibt 1; fallende Flanke zählt nicht |
| 20 Impulse | Je 250 ms High und 250 ms Low per Host | Gate 2 folgt in allen Beobachtungsphasen; je High genau +1, je Low unverändert; Endzähler 21 |
| Nicht gepatchter Eingang | GATE IN 1 | In allen Beobachtungsphasen Low, Zähler unverändert 21 |
| Abschluss | OFF | Firmware meldet OFF/0/0, beide Gates Low; danach BYE und CDC geschlossen |

Je Befehl 50 ms Einschwingzeit, danach Prüfung von Gate-Zustand,
konstantem erwartetem Flankenzähler und gemeldeten Ausgangscodes/Modus.
Die vom Host angeforderten Haltezeiten sind keine mit einem Oszilloskop
gemessenen Pulsbreiten; die DAC-Codes sind keine unabhängige Spannungsmessung.

Gesamtdauer 17,594 s; 1013 gültige Rahmen, 0 CRC-/Inhaltsfehler,
0 verlorene Rahmen und 0 Wiederverbindungen. Zähler vor/nach der Probe:
`[21, 0]` → `[21, 21]`; Differenz `[0, 21]`. Kein Zählerüberlauf beobachtet.

## Ergebnis und Grenzen

**Gate-2-Grundtest bestanden, GEMESSEN am 31.10.2026:** richtige Low-/High-
Polarität, einmalige Zählung beim steigenden Pegel und unveränderter Zähler
bei gehaltenem High oder fallendem Pegel. Zusammen mit dem Gate-1-Protokoll
sind beide Gate-Eingänge für langsame Pulse geprüft.

**H13 bleibt teilweise GEMESSEN:** kurze Pulse, Schaltschwelle/Hysterese,
Störfestigkeit, elektrische Pulsform, genaue Latenz und Zählerüberlauf sind
**NICHT GEMESSEN**. Kein Nachweis für Pulse nahe oder unter der 1-ms-Abtastgrenze.
Auch die CV-Loopbacks ersetzen keine unabhängige Spannungsprüfung (H12/H14).

[Maschinenlesbarer Nachweis](evidence/gate2-loopback-2026-10-31.json).
Rohprotokoll lokal unter
`.work/diagnostics-cv-test-2026-10-31/gate2-loopback-214920.json`, SHA-256 im Nachweis.
