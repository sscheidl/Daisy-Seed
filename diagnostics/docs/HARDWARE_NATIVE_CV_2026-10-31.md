# Kurze native CV-Probe vom 31.10.2026

## Aufbau — GEMESSEN

Seed3 Desktop J7/COM15, CardReader-Firmware **0.3.2**, unverändert seit dem
[SD-Hardwaretest](HARDWARE_CARD_READER_0.3.2_2026-10-31.md), App-Quellstand auf
dem Control-Panel-Branch. Der Bediener bestätigte **CV OUT 1 → CV IN 1**.
Native Firmwarebefehle RAMP und SAW, jeweils `ms=2000`, jeweils 6,1 s Beobachtung
nach 0,2 s Einschwingzeit. Ausgang 2 wird dabei laut Firmware invers angesteuert;
seine physische Spannung wurde in dieser Probe nicht rückgemessen.

## Tatsächliche Ergebnisse

| Modus | Rahmen | CV IN 1, normalisiert | Aufsteigende Mittendurchgänge, Firmware-ms | Korrelation DAC-Echo / Eingang |
| --- | --- | --- | --- | --- |
| RAMP | 306 | 0,0053 bis 0,9912 | 2182526, 2184526, 2186526; Abstände 2000/2000 | 0,999401 |
| SAW | 305 | 0,0051 bis 0,9906 | 2189026, 2191026, 2193026; Abstände 2000/2000 | 0,961593 |

RAMP bestand das kurze funktionale Prüfkriterium. Bei SAW scheiterte die
vorgegebene Korrelationsgrenze `>0.97`; das Rohprotokoll meldet deshalb
**`completed=false`, `AssertionError()`**. Dieses Ergebnis wird nicht nachträglich
auf bestanden gesetzt. Trotzdem sind der wiederholte Verlauf und 2-s-Abstände
im Rückkanal beobachtet, bezogen auf die Firmware-Zeitstempel.

Zwei auffällige SAW-Rahmen am Rücksprung:

| Firmware-ms | Gemeldeter DAC-Code OUT 1 | CV IN 1 |
| --- | --- | --- |
| 2190006 | 12 | 0,9906 |
| 2192006 | 12 | 0,9905 |

Ob die Unterschiede aus ADC-/Filter-/Berichts-Latenz, nicht gleichzeitigen
Feldern, einem ungeeigneten Vergleichskriterium oder einem Implementierungsfehler
entstehen, ist **OFFEN**. Eine Erklärung darf erst nach Prüfung als Befund gelten.
Dies ist ein expliziter Schwerpunkt des unabhängigen Meilenstein-Reviews.

## Abschluss und Grenzen

Gesamtdauer 16,125 s, 878 gültige Rahmen, 0 CRC-/Inhaltsfehler, 0 verlorene
Rahmen und 0 Wiederverbindungen. Abschließend OFF/0/0 bestätigt, CDC geschlossen.
Die Karte blieb SHARED; kein SDTEST und kein neuer Flashvorgang.

Keine unabhängig kalibrierte Volt-, Latenz- oder Oszilloskopmessung. Die 2-s-
Angabe stammt aus den Firmware-Zeitstempeln und ersetzt keine externe Zeitbasis.
Die Hardware-Testreihe endet für heute; die offene SAW-Einordnung bleibt erhalten.

[Zusammenfassung](evidence/native-cv-summary-2026-10-31.json),
[verlustfrei komprimiertes Original](evidence/native-cv-0.3.2-2026-10-31.json.gz).
Der SHA-256 des unkomprimierten Originals steht in der Zusammenfassung.

Zum unabhängigen Auslesen:

```python
import gzip, json
from pathlib import Path
trace = json.loads(gzip.decompress(Path(
    "diagnostics/docs/evidence/native-cv-0.3.2-2026-10-31.json.gz"
).read_bytes()))
```
