# Bedienentwurf: Streichwurst

2026-11-01. Eigene Adaption für das Seed3 Desktop Development Kit:
acht Potis, 16 Taster/LEDs laut [Repository-Hardwarebasis](../../../docs/HARDWARE.md).
Noch nicht implementiert oder am Gerät getestet. Kein Display vorausgesetzt.
Keine Behauptung einer identischen Waldorf-Beschriftung oder Parameterrange.

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
