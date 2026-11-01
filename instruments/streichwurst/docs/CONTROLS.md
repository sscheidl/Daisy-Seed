# Bedienentwurf: Streichwurst

2026-11-01. Eigene Adaption für das Seed3 Desktop Development Kit:
acht Potis, 16 Taster/LEDs laut [Repository-Hardwarebasis](../../../docs/HARDWARE.md).
**Implementiert in Firmware 0.1.0** ([src/controls.h](../src/controls.h), Hosttests in
`tests/test_product.cpp`); am Gerät **NICHT GEMESSEN**. Kein Display vorausgesetzt.
Keine Behauptung einer identischen Waldorf-Beschriftung oder Parameterrange.

Die Zuordnung gehört zum Firmware-/Bedienadapter gemäß
[Modulvertrag](MODULAR_ARCHITECTURE.md). String- und Soli-Regler bedienen das
Instrument; Animate/Ensemble/Phaser/Reverb bedienen die separate FX-Komponente.
Kit, MIDI und Mod-on-Daisy-Control-App verwenden dasselbe Parameterregister.
Die App bietet alle Parameter unabhängig von der gewählten Reglerseite an.

## Reglerseiten

| Regler | Hauptseite | Solo-Seite | FX-Seite |
| --- | --- | --- | --- |
| VR1 | String Registration | Solo Tone | Animate Amount |
| VR2 | String Crescendo | Solo Attack | eigene Animate Rate |
| VR3 | String Release | Solo Release | Phaser Amount |
| VR4 | Balance String/Solo | Solo Tremolo Depth | eigene Phaser Rate |
| VR5 | Animate Amount | Solo Tremolo Rate | Reverb Wet |
| VR6 | Phaser Amount | Solo Sustain Level | eigene Reverb Decay |
| VR7 | Reverb Wet | Splitpunkt | Master Level |
| VR8 | Master Level | Master Level | Balance String/Solo |

Belegte Originalbeziehungen bestimmen interne Skalierung, soweit bekannt.
Zusätzliche getrennte Rate-/Wet-/Decayregler sind eigene Parameter, keine
behaupteten Waldorf-Originalknöpfe. Defaultwerte, Bereich und MIDI CCs vor der
Implementierung in einer versionierten Parametertabelle festlegen.

## Taster und LEDs

* S1..S8: lokale Spieltasten, zunächst eine eigene C-Dur-Oktave ab MIDI 60;
  externe MIDI-Tastatur bleibt für volle Notenbelegung vorgesehen.
* S9: Reglerseite zyklisch Haupt/Solo/FX; S9..S11 zeigen die aktive Seite an.
* S10: String-Oktavstellung Grundlage/Beide/Höhere zyklisch.
* S11: Ensemblemodus 0/1/2 zyklisch. Native Werte statt ungeprüfter Produktlabels.
* S12: Ensemble ein/aus.
* S13: Solo-Sustainmodus; genaue eigene Zustandssemantik dokumentieren.
* S14: Layer/Split zyklisch; Note-genaue Grenze in eigener Parametertabelle.
* S15: vier eigene RAM-Presets zyklisch; keine Herstellerpresets importieren.
* S16: Panic/AllSoundOff.

S9+S16 für 5 s bleibt dem gemeinsamen Bootloader-Einstieg vorbehalten.
Einzelaktionen dieser beiden Taster bis zur Auswertung des Chords verzögern
oder bei Chordbeginn unterdrücken. Halte-, Loslass- und Abbruchpfade testen;
kein unbeabsichtigtes Seitenwechseln/Panic nach Bootloaderchord.

LED-Rückmeldung für Seite und Auswahl muss unterscheidbar sein, z.B.
kurze zeitlich begrenzte Auswahlblinkfolge. LED-I/O im Hauptprogramm,
maximal mit der bestehenden begrenzten Servicerate. Kein LED-Timing im Audio.

Beim Seitenwechsel springen Parameter nicht auf fremde Potipositionen:
Soft takeover bis zum Überqueren des gespeicherten Werts mit Hysterese;
Anzeige/Latch pro Parameter. MIDIänderungen berücksichtigen. Glättung und
MIDI-/Poti-Priorität dokumentieren. Dieser Entwurf darf bei realen
Bedienkonflikten begründet verbessert werden.

## Umsetzung in 0.1.0 (eigene Festlegungen)

* Parameter je Seite und Taster wie oben; Bereiche, Standardwerte und CCs in
  [PARAMETERS.md](PARAMETERS.md). Ganzzahl-/Enum-Parameter am Poti in gleich breiten Zonen.
* **Soft takeover:** Nach Start, Seitenwechsel, RAM-Preset oder einer Änderung über MIDI
  bzw. App ist ein Poti gesperrt, bis es den gespeicherten Wert überquert oder ihm auf
  2 % nahekommt. Danach schreibt es erst ab 0,4 % Bewegung (Rauschschwelle). Letzte
  Änderung gewinnt; die Änderung erscheint im SQ-Monitor und damit in der App.
* **S9/S16:** wirken beim Loslassen und nur, wenn der jeweils andere Taster zwischendurch
  nicht gedrückt war. Ein begonnener S9+S16-Chord unterdrückt beide Einzelaktionen, auch
  nach dem 5-s-Bootloadereinstieg oder einem Abbruch durch eine dritte Taste.
* **S13 Solo-Sustainmodus:** 0 „Abklingen auf Pegel“ (Decay 80 ms auf VR6 Solo),
  1 „Halten“ (bleibt auf vollem Pegel), 2 „Perkussiv“ (klingt auch gehalten aus,
  Abklingzeit 0,05–3,2 s über VR6 Solo).
* **S14:** Layer (alle Tasten an Strings und Soli) / Split (unter dem Splitpunkt Strings,
  ab dem Splitpunkt Soli). Zuordnung und Transposition gelten je Taste bis zum Loslassen.
* **S15:** vier eigene RAM-Presets (0 Vertragsstandard, 1 dunkle Strings, 2 helle Strings
  mit beiden Oktaven und Phaser, 3 Split mit Solo); MIDI-Kanal, Master und Balance bleiben.
  Kein Flash-/QSPI-Schreiben.
* **LEDs:** S1–S8 gedrückte Spieltasten; S9–S11 aktive Seite (kurzes Blinken nach dem
  Wechsel); S12 Ensemble an; S13 aus/schwach/hell je Sustainmodus; S14 Split;
  S15 blinkt Presetnummer + 1 Mal; S16 leuchtet während der Panikrampe. LED-I²C nur im
  Hauptprogramm, höchstens alle 10 ms.
* Glättung: Parameter wirken über die Glieder des DSP (20 ms Mengen, 25,4 ms Balance,
  51 ms Master, 5 ms Tonhöhe); Potis werden nicht zusätzlich gefiltert.
