# Hardwaretest Delay FX 0.1.0

**Stand:** 27. Oktober 2026 · Firmware `effects/stereo-delay/build/DelayFx-v0.1.0.bin`
(`make PROJECT=delay-fx`). Den Test führt der Benutzer am Kit durch. Alle Ergebnisse sind
**NICHT GEMESSEN**, bis ein Eintrag mit Datum steht.

Quelle: kurze Klänge (Klicks, Drums, Acid 303) in den Stereo-Eingang. Kopfhörer leise
beginnen: Bei 95 % Feedback baut sich das Signal auf.

## 1. Hörtest

| Prüfung | Ergebnis |
| --- | --- |
| Ping-Pong (SW18 oben/Mitte): Echos wechseln links/rechts im eingestellten Abstand | NICHT GEMESSEN |
| Stereo L/R (SW18 unten): links bleibt links, rechts bleibt rechts | NICHT GEMESSEN |
| Sync: 1/8 punktiert zum Tempo von VR5, Notenwerte an VR6 | NICHT GEMESSEN |
| Sync mit externer MIDI-Clock (DAW über USB, Gerät über DIN), LED D4 | NICHT GEMESSEN |
| 30 BPM und 1/1 punktiert (VR6 ganz rechts): 12 s, keine Klemmung (D3 aus) | NICHT GEMESSEN |
| Zeit schnell ändern (VR1, VR6): kein Knacken, sanftes Gleiten bzw. Überblenden | NICHT GEMESSEN |
| Feedback 95 % mit lautem Eingang: bleibt begrenzt, schaukelt nicht auf | NICHT GEMESSEN |
| Dämpfung (VR4) macht die Wiederholungen dunkler | NICHT GEMESSEN |
| SW17 aus: Nachhall klingt aus, neue Klänge kommen nicht mehr hinein | NICHT GEMESSEN |
| SW17 während des Nachhalls wieder an: Echos laufen weiter | NICHT GEMESSEN |

## 2. Speicher und Ressourcen-PASS (Arbeitsauftrag 5, R15)

| Wert | Grenze | Ergebnis |
| --- | --- | --- |
| SDRAM-Platzierung laut ELF-Prüfung | `delay_memory` im SDRAM | GEMESSEN 2026-10-27 (Build-Rechner): 4 608 512 Byte bei `0xc0000000` |
| `callback_overruns` bei Feedback 95 % und schnellen Zeitwechseln | 0 | NICHT GEMESSEN |
| `callback_peak_cycles` | < 480 000 | NICHT GEMESSEN |
| `queue_high_water` | < 32 | NICHT GEMESSEN |
| SDRAM-Zugriffszeit (zwei Lesezeiger beim Überblenden) | keine Fristüberschreitung | NICHT GEMESSEN |

Statische Werte aus der ELF-Prüfung (GEMESSEN 2026-10-27 auf dem Build-Rechner, kein
Hardwarewert): DTCM 107 072 / 131 072 Byte, statische Reserve 24 000 Byte, SRAM 146 220 Byte.
