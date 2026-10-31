# CV-Loopback: Hardwaretest vom 31.10.2026

## Aufbau und Umfang — GEMESSEN

Seed3 Desktop über J7/COM15, Diagnose-Firmware CardReader 0.3.1,
Quellstand `b89186576837f62eb7e4d5687a465af9e3e31338`, derselbe BIN-Hash wie im
[Kartenleserprotokoll](HARDWARE_CARD_READER_2026-10-31.md).
Der Bediener bestätigte die Verbindung **CV OUT 1 → CV IN 1** mit Patchkabel.
Echte USB-CDC-Rückmeldungen, kein Simulator. Es wurde nicht geflasht.
CV OUT 2 wurde bei allen Befehlen auf DAC-Code 0 gehalten.
Die SD-Karte blieb SHARED; es wurde kein SDTEST ausgelöst.

## Feste Werte — GEMESSEN

Nach jedem Befehl 350 ms Einschwingzeit, danach rund 1,3 s Beobachtung
(65–66 Zustandsrahmen). Die Firmware-Rückmeldung von Modus und beiden
Ausgangscodes wurde gegen den Befehl geprüft. CV ist der normalisierte
Firmware-Eingangswert; ADC-Rohwerte sind keine Spannungsmessung.

| DAC-Code CV OUT 1 | CV IN 1, Mittelwert | ADC-Rohwert, Mittelwert | ADC-Minimum–Maximum |
| --- | --- | --- | --- |
| 0 | 0.005594 | 32584.33 | 32570–32597 |
| 512 | 0.124832 | 28677.28 | 28664–28694 |
| 1024 | 0.249867 | 24580.38 | 24569–24590 |
| 1536 | 0.374966 | 20481.32 | 20468–20490 |
| 2048 | 0.500071 | 16381.48 | 16369–16394 |
| 2560 | 0.625202 | 12281.58 | 12271–12291 |
| 3072 | 0.750466 | 8176.49 | 8165–8193 |
| 3584 | 0.875595 | 4076.33 | 4062–4092 |
| 4095 | 0.991005 | 294.42 | 281–307 |
| 2048 | 0.500065 | 16382.09 | 16368–16397 |
| 0 | 0.005623 | 32584.02 | 32571–32594 |

Bei OFF vor Beginn lag CV IN 1 im Mittel bei 0,005633; nach Rückkehr zu OFF
bei 0,005566. Der wiederholte Mittelwert bei Code 2048 war 0,500065 gegenüber
zuvor 0,500071. CV IN 2 blieb nahe null (Phasenmittel etwa 0,00018–0,00053).
Das ist eine Beobachtung des nicht gepatchten Kanals, keine spezifizierte
Übersprechmessung.

Die Endpunkte sind ausdrücklich nicht ideal: Code 0 ergibt rund +0,0056,
Code 4095 rund +0,9910 im normalisierten Eingang. Ob Ausgang, Eingang oder beide
diese Abweichung verursachen, ist ohne unabhängiges Messgerät nicht bestimmt.
Es wird daraus weder eine gemessene Voltzahl noch eine bestandene absolute
Spannungstoleranz abgeleitet.

## Langsame Treppenrampe — GEMESSEN

Zwei Dreieckzyklen per Host mit FIX-Befehlen, 32 Stufen je Richtung und 60 ms
pro Befehl, CV OUT 2 konstant Code 0. Dies prüft die analoge Rückführung bei
wechselnden Werten, **nicht** die nativen Firmware-Modi RAMP oder SAW.

Für 207 Zustandsrahmen mindestens 25 ms nach dem jeweiligen Befehl war
die Korrelation zwischen gemeldetem DAC-Code/4095 und CV IN 1 0.999985.
Dies ist keine Messung der Einschwingzeit, Bandbreite oder Interrupt-Latenz.

## Abschluss und Grenzen

Funktion der gepatchten CV-1-Signalkette nachgewiesen: Eingang folgt den Stufen
monoton und die wiederholten festen Werte sind stabil. Gesamtdauer 32,266 s;
1889 gültige Rahmen, 0 CRC-/Inhaltsfehler, 0 verlorene Rahmen und 0
Wiederverbindungen. Beide Ausgänge wurden abschließend auf OFF gesetzt;
Firmware meldet OFF/0/0, CV IN 1 kehrt zur Ausgangsbasis zurück. Danach BYE
und Schließen der seriellen Verbindung. Reader-Zustand weiterhin SHARED.

**H15 in diesem Teiltest teilweise GEMESSEN:** nur CV OUT 1 → CV IN 1.
Der nachfolgende [CV-2-Teiltest](HARDWARE_CV2_LOOPBACK_2026-10-31.md) ergänzt
den zweiten Kanal. In diesem Teiltest waren CV-2-Loopback, beide
Gate-Eingänge, negative Eingangsspannungen und unabhängige absolute
Spannungsmessungen (H12/H14) **NICHT GEMESSEN**. Automatisches OFF bei
Sitzungs-Timeout und native RAMP/SAW-Modi wurden hier nicht hardwaregeprüft.

[Maschinenlesbarer Nachweis](evidence/cv1-loopback-2026-10-31.json).
Das vollständige lokale Rohprotokoll liegt unter
`.work/diagnostics-cv-test-2026-10-31/cv1-loopback-213702.json`;
sein SHA-256 ist im Nachweis hinterlegt.
