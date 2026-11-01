# Streichwurst FX 0.1.0 (Standalone): Laufzettel für die FX-Abnahme

Stand 01.11.2026. Nach der Regel „Modular FX first“ (AGENTS.md) wird der FX zuerst
allein am Kit abgenommen, getrennt von der CPU-/Hörabnahme des Produkts Streichwurst.
Alle Punkte **NICHT GEMESSEN**, bis der Mensch sie durchführt und berichtet. Der Agent
flasht nicht ([FLASHING.md](../../../docs/FLASHING.md)).

Image: `effects/streichwurst-fx/build/StreichwurstFx-v0.1.0.bin` (lokal gebaut, nicht
veröffentlicht), Komponente `streichwurst-fx` 0.1.0. Eingang: Line-Stereo; nur der linke
Stecker = Mono auf beide Seiten. Bedienung: VR1 Animate, VR2 Animate-Rate, VR3 Phaser,
VR4 Phaser-Rate, VR5 Reverb, VR6 Reverb-Decay, VR7 Eingangspegel (Verstärkung 1 bei etwa 70 %),
VR8 Ausgangspegel; S1–S3 Ensemble-Modus 0/1/2, S12 Ensemble an/aus, S16 Leitung und
Hall löschen (beim Loslassen), S9+S16 5 s Bootloader. LEDs S1–S3 Modus, S12 Ensemble,
S16 Callback-Überlauf. Die Potis wirken erst, wenn sie bewegt werden.

| Nr. | Prüfung | Erwartung (keine Messung) | Ergebnis |
| --- | --- | --- | --- |
| F1 | Start mit Eingangssignal, Ensemble Modus 1 | stereo verbreiterter Chorus, etwas Hall | NICHT GEMESSEN |
| F2 | S12 aus, VR1/VR3/VR5 auf 0, VR7 etwa 70 %, VR8 Maximum | Ausgang ≈ Eingang (Mono-Summe auf beiden Seiten) | NICHT GEMESSEN |
| F3 | S1/S2/S3 Modus 0/1/2 | drei unterscheidbare Ensembledichten, Wechsel ohne Klick | NICHT GEMESSEN |
| F4 | Ensemble an/aus | Modus 0–2 lauter als aus (Gainwort und kohärente Taps, FIDELITY) | NICHT GEMESSEN |
| F5 | VR1/VR2 Animate | wandernde Bandbetonung | NICHT GEMESSEN |
| F6 | VR3/VR4 Phaser | Phaserbewegung auf der Ensemblesumme | NICHT GEMESSEN |
| F7 | VR5/VR6 Reverb | Hallanteil und -länge steigen | NICHT GEMESSEN |
| F8 | S16 | Hallfahne bricht ab | NICHT GEMESSEN |
| F9 | Mono-Eingang (nur links) | beide Seiten bekommen das Signal | NICHT GEMESSEN |
| F10 | MIDI CC 102–109 (USB/DIN) | wirkt wie die Potis | NICHT GEMESSEN |
| F11 | Control App verbinden | „streichwurst-fx 0.1.0 · SQ“, nur Seite FX, Potis erscheinen in der App | NICHT GEMESSEN |
| F12 | CPU: Callback-Spitze (App → Zähler) | Ziel ≤ 60 % je 1-ms-Block | NICHT GEMESSEN |
| F13 | Lautstärke/Übersteuerung bei Line-Pegel | keine Verzerrung bis zum Begrenzerknie | NICHT GEMESSEN |
| F14 | S9+S16 5 s | Bootloader, alle LEDs | NICHT GEMESSEN |

Erst nach dieser Abnahme gilt der FX als Baustein für die Produktabnahme von Streichwurst
([Laufzettel des Produkts](../../../instruments/streichwurst/docs/HARDWARE_TEST.md)).
