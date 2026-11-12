# Nebulae 0.2.2 listening test: vocoder scheduling A / B / C

Short operating guide for the owner. Background: [OVERLOAD_DIAGNOSIS.md](OVERLOAD_DIAGNOSIS.md)
section 11, method and full sheet: [CODEX_SECOND_OPINION_2.md](CODEX_SECOND_OPINION_2.md).
German translation below. Result: **NOT MEASURED** until the owner has listened.

## What is compared

Three images from the firmware store, identical except for when the right vocoder channel
computes its frame: **A** common grid (today's behaviour; at extreme settings the overload
protection makes the vocoder stutter), **B** plain stagger (right channel one block later
with its own position: a different stereo image), **C** compensated stagger (channels as A,
the vocoder layer 5.3 ms later; no overruns, MEASURED 2026-11-11). Claude flashes them in a
hidden order as **X, Y, Z** and keeps the key until your notes are written down.

## You need

* Kit on J7 to the PC, test card inserted, audio out to your interface and DAW.
* Two stereo samples in bank 1: **slot 1** near-mono with clear attacks (voice, drums),
  **slot 2** wide stereo ambience. A two-channel file is needed; a one-channel (mono) WAV
  hides the difference.
* About 30 minutes. Monitor at a moderate level (segment 4 is dense).

## Recording (per variant X, Y, Z)

1. Say **"flash X"** (then Y, Z). Claude sends the kit into the bootloader and flashes it;
   no buttons. The kit restarts and plays bank 1 slot 1.
2. SW18 up (NEBULAE). **Do not touch the pots** from now on: the script sets them.
3. Start the DAW recording on a new stereo track, say **"go"**. Claude runs a 60 s script:

   | Time | Segment | Settings |
   | --- | --- | --- |
   | 0-15 s | 1 vocoder only | BLEND min, speed 1x |
   | 15-30 s | 2 near freeze | speed about 0 |
   | 30-45 s | 3 vocoder + grains | BLEND centre, DENSITY and OVERLAP centre |
   | 45-60 s | 4 load | PITCH max, DENSITY and OVERLAP max |

4. Stop the recording. Press **S2 (FILE)** once (slot 2), record again, say "go".
5. Name the tracks X1, X2 (Y1, Y2, Z1, Z2) and say "next".

## Listening (in the DAW)

1. Align the takes of one slot at the start of segment 1; switch between X, Y, Z by solo.
2. Per segment listen in **stereo** and in **mono** (mono sum of the master).
3. Listen for: centre and position of voice and drums; soft, smeared or doubled attacks;
   hollow or phasey colour in mono; stability in segment 2; dropouts or stutter in
   segment 4.
4. Fill in the table on the last page. "No difference" is a valid answer. Then tell Claude;
   Claude reveals which of X, Y, Z is A, B, C and records the result.

---

# Nebulae 0.2.2 Hörtest: Vocoder-Zeitplan A / B / C (Deutsch)

Kurzanleitung für den Besitzer. Hintergrund: [OVERLOAD_DIAGNOSIS.md](OVERLOAD_DIAGNOSIS.md)
Abschnitt 11, Methode und vollständiges Blatt: [CODEX_SECOND_OPINION_2.md](CODEX_SECOND_OPINION_2.md).
Ergebnis: **NICHT GEMESSEN**, bis gehört wurde.

## Was verglichen wird

Drei Images aus dem Store, gleich bis auf den Zeitpunkt, zu dem der rechte Vocoder-Kanal
rechnet: **A** gemeinsames Raster (heutiges Verhalten; bei Extremeinstellungen stottert der
Vocoder durch den Überlastschutz), **B** reiner Versatz (rechter Kanal einen Block später
mit eigener Position: anderes Stereobild), **C** Versatz mit Ausgleich (Kanäle wie A, die
Vocoder-Ebene 5,3 ms später; keine Overruns, GEMESSEN 2026-11-11). Claude flasht sie in
verdeckter Reihenfolge als **X, Y, Z** und behält den Schlüssel, bis Ihre Notizen stehen.

## Sie brauchen

* Kit über J7 am PC, Testkarte eingesteckt, Audio-Ausgang ins Interface und die DAW.
* Zwei Stereo-Samples in Bank 1: **Slot 1** fast mono mit klaren Anschlägen (Stimme,
  Schlagzeug), **Slot 2** breite Stereo-Fläche. Es muss eine Zweikanal-Datei sein; eine
  einkanalige (Mono-)WAV verdeckt den Unterschied.
* Etwa 30 Minuten. Abhören bei mäßiger Lautstärke (Abschnitt 4 ist dicht).

## Aufnahme (je Variante X, Y, Z)

1. Sagen Sie **„flash X"** (dann Y, Z). Claude schickt das Kit in den Bootloader und flasht;
   keine Tasten. Das Kit startet neu und spielt Bank 1, Slot 1.
2. SW18 oben (NEBULAE). **Ab jetzt die Potis nicht berühren:** das Skript stellt sie.
3. DAW-Aufnahme auf einer neuen Stereospur starten, **„los"** sagen. Claude fährt ein
   60-s-Skript:

   | Zeit | Abschnitt | Einstellung |
   | --- | --- | --- |
   | 0-15 s | 1 nur Vocoder | BLEND min, Tempo 1x |
   | 15-30 s | 2 fast Freeze | Tempo etwa 0 |
   | 30-45 s | 3 Vocoder + Grains | BLEND Mitte, DENSITY und OVERLAP Mitte |
   | 45-60 s | 4 Last | PITCH max, DENSITY und OVERLAP max |

4. Aufnahme stoppen. Einmal **S2 (FILE)** drücken (Slot 2), erneut aufnehmen, „los" sagen.
5. Spuren X1, X2 (Y1, Y2, Z1, Z2) benennen und „weiter" sagen.

## Hören (in der DAW)

1. Die Takes eines Slots am Beginn von Abschnitt 1 ausrichten; per Solo zwischen X, Y, Z
   wechseln.
2. Jeden Abschnitt in **Stereo** und in **Mono** (Mono-Summe am Master) hören.
3. Achten auf: Mitte und Ort von Stimme und Schlagzeug; weiche, verschmierte oder doppelte
   Anschläge; hohlen oder phasigen Klang in Mono; Ruhe in Abschnitt 2; Aussetzer oder
   Stottern in Abschnitt 4.
4. Tabelle auf der letzten Seite ausfüllen. „Kein Unterschied" ist eine gültige Antwort.
   Dann Claude Bescheid geben; Claude löst auf, welches von X, Y, Z A, B, C ist, und trägt
   das Ergebnis ein.

---

## Result sheet / Ergebnisblatt

Date / Datum ______ Listener / Hörer ______ Monitoring ______ Slot 1 file ______ Slot 2 file ______

| Slot | Segment | Stereo: difference? which? / Unterschied? welcher? | Mono: difference? which? | Preference (X/Y/Z), confidence / Vorzug, Sicherheit | Dropouts / Aussetzer |
| --- | --- | --- | --- | --- | --- |
| 1 | 1 vocoder only / nur Vocoder | | | | |
| 1 | 2 near freeze / fast Freeze | | | | |
| 1 | 3 vocoder + grains | | | | |
| 1 | 4 load / Last | | | | X ___ Y ___ Z ___ |
| 2 | 1 | | | | |
| 2 | 2 | | | | |
| 2 | 3 | | | | |
| 2 | 4 | | | | X ___ Y ___ Z ___ |

Overall / Gesamt: preferred / bevorzugt ______ acceptable / akzeptabel ______ notes / Notizen ______
