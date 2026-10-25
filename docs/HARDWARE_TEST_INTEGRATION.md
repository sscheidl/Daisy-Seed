# Hardwaretest: Integration Bootloader + Toy Piano Ambient + Shimmer

Integrationsbranch `integration/shimmer-bootloader` (PR #40, #38, #41). Flashen, Hören,
Tasten, Schalter und Kabel übernimmt der Mensch; der Agent trägt nur Berichtetes ein.
Alle Ergebnisse stehen auf **NICHT GEMESSEN**, bis sie am Kit geprüft sind (dann
**GEMESSEN** mit Datum). Die bisherigen BINs (ToyPianoAmbient v0.1.1, AmbientReverb
v0.3) bleiben unverändert erhalten.

| Image | Bytes | SHA-256 |
| --- | --- | --- |
| `ToyPianoAmbient-v0.1.2.bin` | 184220 | `0ecfb4f9f44a7a8b51fafe1a7b1ec9d81cb1dce420b7ce46cbeecfc00f9753e0` |
| `AmbientReverb-v0.3.1.bin` | 145748 | `3a19183d3474b00979ae2578ee8744865ff2aa0559f4df4b1de658503568ea62` |

Vorher prüfen: SHA-256 des zu flashenden BINs gegen die Tabelle. Das erste Flashen
braucht noch RESET + BOOT, weil die laufende Firmware den Einstieg nicht kennt.
Mit niedrigem Abhörpegel beginnen.

## Toy Piano + Ambient v0.1.2

| # | Test | Erwartung | Ergebnis |
| --- | --- | --- | --- |
| P1 | VR6 ganz links, S1–S16 einzeln | 16 Töne C4…C6 wie Toy Piano, LED folgt der Taste | NICHT GEMESSEN |
| P2 | VR1 Transposition, VR2 Decay | ±12 Halbtöne, Mitte = Originaltonhöhe; Decay 50–3000 ms | NICHT GEMESSEN |
| P3 | Sechs Schalterstellungen (SW17 A/B × SW18 oben/Mitte/unten) | Cloud, Halo, Scatter, Abyss, Bloom, Shimmer; Schalterrichtung notieren | NICHT GEMESSEN |
| P4 | Shimmer (B/unten), VR7 hoch, VR3 lang | Aufsteigende Oktaven im Ausklang, kein Aufschaukeln | NICHT GEMESSEN |
| P5 | VR8 Stretch an beiden Enden | Hörbarer Unterschied, kein Knacken | NICHT GEMESSEN |
| P6 | Schnelle Anschläge und MIDI-Akkorde | Keine Aussetzer; `mod_probe.py stats`: Last, Überläufe notieren | NICHT GEMESSEN |
| P7 | Editor verbinden | `toy-piano-ambient 0.1.2`, 8 Spalten, Modus-Anzeige | NICHT GEMESSEN |

## Ambient Reverb v0.3.1 (reine FX-Firmware)

| # | Test | Erwartung | Ergebnis |
| --- | --- | --- | --- |
| R1 | S1–S6 | Cloud, Halo, Scatter, Abyss, Bloom, **Shimmer (S6, LED D6)** | NICHT GEMESSEN |
| R2 | S15 Bypass, S16 Sustain | wie bisher | NICHT GEMESSEN |
| R3 | USB an J7 | Erscheint als „Daisy Seed External“ (USB-MIDI); Klang unverändert, auch beim Ab-/Anstecken | NICHT GEMESSEN |

## MIDI

| # | Test | Erwartung | Ergebnis |
| --- | --- | --- | --- |
| M1 | Noten von DAW an Toy Piano Ambient | Spielt mit Anschlagstärke; Tasten senden Noten | NICHT GEMESSEN |
| M2 | Studio One mit Input-Monitoring | Keine Antwortschleife (Mod on Daisy und Bootloader) | NICHT GEMESSEN |

## Bootloader (beide Images)

| # | Test | Erwartung | Ergebnis |
| --- | --- | --- | --- |
| B1 | S9 + S16 allein 5 s | Alle 16 LEDs an, DFU-Gerät `0483:DF11` erscheint, Web Programmer verbindet | NICHT GEMESSEN |
| B2 | S9 + S16 + dritte Taste 7 s; S9 + S16 4 s loslassen | Kein Einstieg | NICHT GEMESSEN |
| B3 | **Firmware-Update …** im Editor | „Anfrage angenommen“, danach DFU-Gerät; Bootloader wartet ohne Zeitlimit (60 s warten, dann flashen) | NICHT GEMESSEN |
| B4 | Nach dem Flashen | Neue Firmware startet, LEDs aus | NICHT GEMESSEN |
| B5 | RESET + BOOT | Rettungsweg funktioniert weiter | NICHT GEMESSEN |

Ausdrücklich ungeprüft bis zum manuellen Test: Hardwareklang, CPU-Last,
Stack-Hochwasser (AmbientReverb v0.3.1: nur noch 10488 B statische DTCM-Reserve),
Schalterrichtung und tatsächliches DFU-Verhalten.
