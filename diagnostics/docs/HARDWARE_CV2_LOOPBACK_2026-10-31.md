# CV-2-Loopback: Hardwaretest vom 31.10.2026

## Aufbau und Umfang — GEMESSEN

Fortsetzung des [CV-1-Tests](HARDWARE_CV_LOOPBACK_2026-10-31.md) am selben
Seed3 Desktop über J7/COM15 mit derselben CardReader-Firmware 0.3.1,
Quellstand `b89186576837f62eb7e4d5687a465af9e3e31338`.
Auf die Aufforderung, auf **CV OUT 2 → CV IN 2** umzustecken und zu bestätigen,
antwortete der Bediener „ok“. Die Messung zeigt, dass nun Eingang 2 folgt;
Eingang 1 bleibt nahe null. Echte USB-CDC-Rückmeldungen, kein Simulator.
CV OUT 1 wurde bei allen Befehlen auf DAC-Code 0 gehalten.
Es wurde nicht geflasht; kein SDTEST ausgelöst; Reader bleibt SHARED.

## Feste Werte — GEMESSEN

350 ms Einschwingzeit je Befehl, anschließend rund 1,3 s Beobachtung
(65–66 Zustandsrahmen). Modus und beide gemeldeten Ausgangscodes wurden mit
dem Befehl verglichen. Normalisierte Werte und ADC-Rohwerte sind keine
unabhängig gemessenen Voltwerte.

| DAC-Code CV OUT 2 | CV IN 2, Mittelwert | ADC-Rohwert, Mittelwert | ADC-Minimum–Maximum |
| --- | --- | --- | --- |
| 0 | 0.006960 | 32539.75 | 32528–32551 |
| 512 | 0.125906 | 28642.32 | 28626–28669 |
| 1024 | 0.250769 | 24550.62 | 24540–24569 |
| 1536 | 0.375559 | 20461.61 | 20446–20476 |
| 2048 | 0.500532 | 16366.54 | 16351–16382 |
| 2560 | 0.625567 | 12268.97 | 12251–12289 |
| 3072 | 0.750727 | 8167.91 | 8157–8182 |
| 3584 | 0.875598 | 4076.58 | 4059–4097 |
| 4095 | 0.990238 | 320.08 | 299–336 |
| 2048 | 0.500497 | 16367.29 | 16351–16384 |
| 0 | 0.006951 | 32539.97 | 32527–32556 |

OFF vor Beginn: CV IN 2 im Mittel 0,006924; abschließend 0,006964.
Bei erneutem Code 2048: 0,500497 gegenüber zuvor 0,500532.
CV IN 1 lag während der festen Phasen im Mittel etwa zwischen −0,00112 und
−0,00080. Dies ist keine spezifizierte Übersprechmessung.

Auch Kanal 2 hat Endpunktabweichungen: Code 0 etwa +0,0070, Code 4095 etwa
+0,9902. Der Anteil von Ein- und Ausgang ist ohne unabhängiges Messgerät
nicht bestimmt. Keine absolute Spannungskalibrierung oder Toleranzabnahme.

## Langsame Treppenrampe — GEMESSEN

Zwei hostgesteuerte Dreieckzyklen per FIX, 32 Stufen je Richtung und 60 ms je
Befehl, CV OUT 1 konstant Code 0. Native Firmware-Modi RAMP/SAW nicht getestet.

Für 197 Rahmen mindestens 25 ms nach dem jeweiligen Befehl:
Korrelation DAC-Code/4095 zu CV IN 2 = 0.999990.

Dies ist keine Messung von Bandbreite, Einschwingzeit oder Interrupt-Latenz.

## Abschluss und Grenzen

CV-2-Signalkette funktioniert im Loopback; Eingang folgt den festen Werten
monoton und wiederholbar. Gesamtdauer 32,265 s, 1889 gültige Rahmen,
0 CRC-/Inhaltsfehler, 0 verlorene Rahmen und 0 Wiederverbindungen.
Abschließend beide Ausgänge OFF, Firmware-Rückmeldung OFF/0/0 und ADC-Rückkehr
zur Ausgangsbasis bestätigt. Danach BYE und Schließen der CDC-Verbindung.

**H15 GEMESSEN am 31.10.2026** für die beiden einzeln gepatchten CV-Kanäle,
zusammen mit dem CV-1-Protokoll. H12/H14 bleiben ohne unabhängige
Spannungsquelle beziehungsweise Messgerät **NICHT GEMESSEN**. Beide Gate-
Eingänge, negative CV, natives RAMP/SAW und Abschalten bei Sitzungs-Timeout
wurden nicht hardwaregeprüft.

[Maschinenlesbarer Nachweis](evidence/cv2-loopback-2026-10-31.json).
Vollständiges Rohprotokoll lokal unter
`.work/diagnostics-cv-test-2026-10-31/cv2-loopback-214203.json`, SHA-256 im Nachweis.
