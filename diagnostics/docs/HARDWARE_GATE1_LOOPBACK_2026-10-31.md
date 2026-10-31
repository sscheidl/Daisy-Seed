# Gate-1-Loopback: Hardwaretest vom 31.10.2026

## Aufbau — GEMESSEN

Seed3 Desktop über J7/COM15, Diagnose-Firmware CardReader 0.3.1,
Quellstand `b89186576837f62eb7e4d5687a465af9e3e31338`, BIN-Hash wie im
[Kartenleserprotokoll](HARDWARE_CARD_READER_2026-10-31.md).
Der Bediener antwortete „ok“ auf die Aufforderung, **CV OUT 1 → GATE IN 1**
zu stecken und zu bestätigen. Die Rückmeldungen zeigen Gate 1 folgend;
Gate 2 bleibt Low. Echte USB-CDC-Messreihe, kein Simulator, kein Flashen.

CV OUT 1 wurde mit FIX zwischen DAC-Code 0 und 4095 umgeschaltet;
CV OUT 2 blieb bei Code 0. Dies ist keine unabhängige Messung der Spannung
oder der Gate-Schaltschwelle. Reader unverändert SHARED; kein SDTEST.

## Beobachtungen — GEMESSEN

| Abschnitt | Befehl / angeforderte Haltedauer | Beobachtung |
| --- | --- | --- |
| Ausgangsbasis | OFF, 1 s | Beide Gates Low, beide Zähler 0 |
| Gehaltenes High | Code 4095, 2 s | Gate 1 dauerhaft High, Zähler genau 1; keine Wiederholungszählung |
| Gehaltenes Low | Code 0, 1 s | Gate 1 Low, Zähler bleibt 1; fallende Flanke zählt nicht |
| 20 Impulse | Je 250 ms High und 250 ms Low per Host | Gate 1 folgt in allen Beobachtungsphasen; je High genau +1, je Low unverändert; Endzähler 21 |
| Nicht gepatchter Eingang | GATE IN 2 | In allen Beobachtungsphasen Low, Zähler unverändert 0; kein Funktionstest dieses Eingangs |
| Abschluss | OFF | Firmware meldet OFF/0/0, beide Gates Low; danach BYE und CDC geschlossen |

Nach jedem Befehl wurden zunächst 50 ms verworfen; während der restlichen
Beobachtungsphase wurden Zustand und konstanter erwarteter Zähler geprüft.
Modus und beide Ausgangscodes mussten zur Firmware-Rückmeldung passen.
Die Host-Haltezeiten sind keine mit Oszilloskop gemessenen Pulsbreiten.

Gesamtdauer 17,594 s; 1014 gültige Rahmen, 0 CRC-/Inhaltsfehler,
0 verlorene Rahmen und 0 Wiederverbindungen. Beide Zähler vor/nach der
Impulsprobe: `[0, 0]` → `[21, 0]`, also 21 beobachtete steigende Gate-1-Flanken
(ein gehaltenes High plus 20 Impulse). Die Zähler liefen dabei nicht über.

## Grenzen

**H13 teilweise GEMESSEN:** GATE IN 1 funktioniert für die geprüften langsamen
Low-/High-Wechsel mit korrekter Polarität und Flankenzählung. GATE IN 2,
kurze Pulse, Schaltschwelle/Hysterese, Störfestigkeit, elektrische Pulsform und
Latenz sind **NICHT GEMESSEN**. Kein Nachweis für Pulse im Bereich der
1-ms-Abtastgrenze oder darunter; kein Hardware-Nachweis des Zählerüberlaufs.

[Maschinenlesbarer Nachweis](evidence/gate1-loopback-2026-10-31.json).
Rohprotokoll lokal unter
`.work/diagnostics-cv-test-2026-10-31/gate1-loopback-214431.json`, SHA-256 im Nachweis.
