# Streichwurst: Klangtreue je Modul

Stand 01.11.2026, Instrument 0.1.0, FX 0.1.0, Produkt 0.1.0. Grundlage sind die
selbst formulierten Befunde in [research/](research/) mit ihren Beleggraden
(BESTÄTIGT, PLAUSIBEL, OFFEN). Keine Herstellerdaten, Tabellen, Presets oder
Codeübersetzungen; alle Daten sind eigene Formeln oder aus den dokumentierten
Kalibrierpunkten abgeleitet. **Klangvergleich mit Originalplugin oder -gerät:
NICHT GEMESSEN.** Bestandene Softwaretests belegen keine Klangidentität.

Spalten: belegte Originalbeziehung (Befund-ID) · implementiertes Verfahren ·
Abweichung · erwartete hörbare Folge · Test · offene Referenzfrage.

## Instrument (`streichwurst-instrument`)

| Modul | Beleg | Verfahren | Abweichung | Erwartete Klangfolge | Test | Offene Referenzfrage |
| --- | --- | --- | --- | --- | --- | --- |
| Takterzeugung | SF-C01 BESTÄTIGT: zwölf gemeinsame Taktobjekte, obere Periode = 40000/f(127) | Zwölf frei laufende Tonklassenphasen bei 2·f(höchste Note der Klasse); neue Noten setzen keine Phase zurück; Pitchbend/Tune/Matrix skalieren alle Takte gemeinsam (5 ms geglättet) | Float-Phasen statt Q24; eigener Tabellenersatz durch `exp2` | Gemeinsame Phasen erzeugen die typische Schwebungsfreiheit gleicher Tonklassen; Bend verbiegt alle Töne gleich | `TestClocks`, `TestPitchAndSpectrum` | Grenzpfade großer Bend-/Transposewerte |
| Oktavteilung | SF-C02: Pegelzugriff im 12er-Abstand BESTÄTIGT; Divide-down PLAUSIBEL | Binärer Zähler je Tonklasse; Bit b = Rechteck der Note `top − 12b` | Interpretation als Teilerkette ist eigene Auslegung des PLAUSIBEL-Befunds | Rechteckspektren je Oktavlage; die Summe der Fußlagen bildet eine Treppensäge | `TestPitchAndSpectrum` (Oktave 880 Hz vorhanden) | Vollständige Entstehung jedes ΔA, Oktavübergänge |
| Ereigniskern | SF-C02 BESTÄTIGT: acht Beiträge je Ereignis, Zeile aus 8 Bit der Bruchzeit | Eigener 8-Tap-Bandbegrenzungskern, 256 Bruchzeilen (gefensterter Sinc, Zeilensumme 1), ΔA = neuer − alter Ausgang der umgeschalteten Notenbits | Eigene Koeffizienten statt der unbekannten Originalkerne | Anderes Aliasing-/Obertonverhalten nahe Nyquist; Pegelwechsel wirken erst an der nächsten Flanke (wie im Original-Ereignismodell) | `TestBandLimit` (Alias der 5. Harmonischen −30 dB unter f0) | Kernelkoeffizienten, Kernel-DC, Ringbereinigung |
| Leckintegration | SF-C02 BESTÄTIGT: `z[n] = z[n−1] + r − z/256` | Gleiche Rekurrenz in Float, Leck auf 48 kHz umgerechnet (gleiche Zeitkonstante 6,4 ms) | Keine Integerquantisierung | Hochpass ≈ 25 Hz: tiefe Rechtecke hängen leicht durch | indirekt über alle Spektraltests | Randkopien, Endzustandslöschung |
| Hüllkurven | SF-F01 BESTÄTIGT: asymmetrischer Einpol α=k/65536 alle 0,4 ms, Crescendo bei steigendem, Release bei fallendem Pegel | Gleiche Struktur mit physikalischer Zeitkonstante; Update alle 16 Samples (0,33 ms) mit α aus τ; zyklische Bereinigung eines Index je Update unter 8192/2^26 | Kennlinie Regler→τ: eigene log-lineare Kurve durch die drei SF-F02-Kalibrierpunkte (0,577 ms / 151 ms / 4,37 s); Zwischenwerte angenommen | Zeitverhalten an den drei Punkten originalnah, dazwischen eigene Interpolation | `TestEnvelopeTimes` (63,2 %/36,8 % nach τ) | Vollständige Index-/UI-Domain, Rundung kleiner Pegel |
| Registrierung | SF-F03 BESTÄTIGT: z = clamp(256r+m), c = 355z/4096, Interpolation von 13 Frames à 8 Gewichten | Gleiche Indexrechnung in Float; Animate-Offset m aus dem FX | **Eigene Framedaten** (Formel: wandernde Fußlagenmitte, oktavweise halbierte Gewichte, Summe 1) statt der unbekannten Originalframes | Timbreverlauf dunkel→hell vorhanden, aber nicht die Originalfarben | `TestRegistrationAndOctaves` | Originalframes und Herstellerlabels |
| Oktavschalter | SF-F04 BESTÄTIGT: o=1 → 0,75·Nachbarsumme, o=2 → Verschiebung, Grenze 2^26 | Exakt diese Transformation (reell), Grenze 2 relativ | — | „Beide“ ist lauter als „Grundlage“ (keine Pegelnormierung, wie belegt) | `TestRegistrationAndOctaves`, Spektraltest „Höhere“ | — |
| Notenziele | SF-F05 PLAUSIBEL: T[n−12]+=W0·a, T[n]+=W1·a, weitere in Oktavschritten | Fußlagen −12, 0, +12 … +72 Halbtöne; Aktivierung a ∈ (0,1]; Ziele auf 4 begrenzt | Schleifenende, Grenzen und Velocity-Zuweisung sind eigene Annahmen; Strings standardmäßig anschlagsunabhängig (`string_velocity` 0) | Hohe Fußlagen fallen oberhalb MIDI 127 weg | `TestRegistrationAndOctaves` (Ziel = Gewicht je Fußlage) | Vollständige Schleifeninvariante, Velocity |
| Polyphonie/Steal Strings | 128 logische Notenpegel (keine Oszillatoren) | Volle 128 Pegel; Rechenlast hängt von Ereignissen ab, nicht von der Tastenzahl | Keine Begrenzung auf 16 Tonlagen nötig (eigene Abweichung vom Entwurf 0.2) | Paraphones Verhalten wie im Original-Notenmodell | `TestInvalidParamsAndLongRender` (24 Tasten, beide Oktaven) | — |
| Soli | SF-A04: acht Poolobjekte; Wellenbildung, Hüllkurvenmodi, Stealing OFFEN | Acht eigene Stimmen: PolyBLEP-Rechteck/Säge/Puls-Morph + zwei Einpol-Tiefpässe (Tone), Attack/Decay/Sustain/Release, Modi Abklingen/Halten/Perkussiv, globales Tremolo | **Vollständig eigener Ersatz** | Solo klingt nach eigenem Synth, nicht nach dem Originalsolo | `TestSoloAllocation`, Routingtests | Solo-Wellen, -Hüllkurven, -Tremolo, Allokation |
| Ereignisse | SF-B02: getrennte String-/Soloänderungen | Samplegenaue Segmentierung, 16-Kanal-Zählung, Sustain, Split/Layer je Taste fixiert, Null-Block-Vormerkung | Eigener Vertrag (Original-Offsettreue unbelegt) | — | `TestEventOrderAndBlocks`, `TestNotesSustainRetrigger`, `TestPendingQueue` | Retrigger-/Sustain-Kanten des Originals |

## FX (`streichwurst-fx`)

| Modul | Beleg | Verfahren | Abweichung | Erwartete Klangfolge | Test | Offene Referenzfrage |
| --- | --- | --- | --- | --- | --- | --- |
| Ensemble-Topologie | SF-D01/D03 BESTÄTIGT: eine Leitung der Stringsumme; Modus 0 drei Taps (erster ersetzt Eingang), 1 Eingang+5, 2 Eingang+3 | Genau diese Summenformen auf einer Leitung | — | Die drei Modi unterscheiden sich in Dichte und Pegel wie belegt | `TestDcSumsAndStereo`, `TestImpulseNoFeedback` | — |
| Ensemble-LFOs | SF-D04/D05 BESTÄTIGT: stückweise Parabel, 0,81/7,77/0,32 Hz, Phasen ±0x55550000 bzw. 0/+135°/−135° | Parabel 4v(1−|v|), exakte Raten und die belegten Phasenoffsets (119,998°), LFO-Fortschaltung je 16 Samples | Float statt Integer-LSB | Originalnahe Modulationsmuster | `TestDelayRangesAndRates` (Perioden, Bereiche) | — |
| Delaypositionen | SF-D05 BESTÄTIGT (Modus 2 geschlossen, 0/1 Näherung): 248±… Samples bei 40 kHz | Delays in Sekunden aus den 40-kHz-Zählwerten (zeitskaliert), lineare Positionsrampe je 16er-Block | Eigene Float-Interpolation am echten Leseort (Original quantisiert Differenz vor der Multiplikation) | Geringfügig weniger Interpolationsrauschen | `TestDelayRangesAndRates` (3,0–9,4 ms, 4,4–8,0 ms) | Korrekturschleifen großer Positionssprünge |
| Stereo-Tapkorrektur | SF-D07 BESTÄTIGT: L = F − 21/128·ta, R = F − 21/128·tb | Exakt, nach dem Phaser | Zuordnung erster Cache → links PLAUSIBEL | Stereobreite der Originaltopologie; Monosumme nicht ausgelöscht | `TestDcSumsAndStereo` | Kanalzuordnung der Caches |
| Gainwörter | SF-F06 BESTÄTIGT: aus 1, Modus 0/1/2 = 0,8125/0,625/0,75, nach Solo und Reverb | Produkt multipliziert die Gesamtausgabe mit dem (übergeblendeten) Gainwort | Keine eigene DC-Normierung (Abkehr vom Entwurf 0.2) | Ensemble an ist lauter als aus (kohärent ×2,30/3,65/2,88), Soli werden mit leiser | `TestDcSumsAndStereo`, Produkttest Verhältnis 0,625 | Gesamte Ausgangsdynamik |
| Bypass/Moduswechsel | SF-F08 BESTÄTIGT: Original friert Leitung/Phasen ein, kein Überblenden | **Eigene Regel:** Leitung und LFOs laufen immer; Wechsel blenden zwei Tapsätze derselben Leitung in 20 ms über | Bewusste Abweichung gegen eingefrorene alte Delaydaten | Keine Klicks bei Wechseln (Original: mögliche Sprünge, ungemessen) | `TestBypassAndTransitions` | Hörbare Klicks des Originals |
| Animate | SF-D09 PLAUSIBEL: Formung vor Ensemble; SF-F03: Registrierungsoffset m | LFO (eigene Rate) erzeugt m = Tiefe·8000·h; eigene Bandbetonung x + 0,3d(LP_fc − LP_200) | Formungsrekurrenz und Tiefenkurve eigen | Wandernde Klangfarbe über die Registrierung; nicht die Originalformung | `TestAnimateAndPhaser` | Drei Formungskontexte, Kennlinie v=128(u²+640) |
| Phaser | SF-D10: Position BESTÄTIGT, Stufen/Topologie OFFEN | Vier Allpässe 80 Hz–3 kHz, Rückkopplung 0,5a, Mischung x+0,5a(y−x), eigene Rate | Eigener Phaser | Klassischer 4-Stufen-Phaser auf der Ensemblesumme | `TestAnimateAndPhaser` | Stufenzahl, Koeffiziententabelle, 0,9-Rekurrenz |
| Reverb | SF-D11: 16384-Float-Ring, 8+2 Teilstufen, zwei Phasen; Topologie OFFEN | Zwei Diffusor-Allpässe + Vier-Leitungs-FDN (Hadamard, Dämpfung 6,5 kHz), g = 0,1+0,83·Decay, Wet 0–50 % | Eigener, kleinerer, unmodulierter Hall | Anderer Raumeindruck, keine Modulation | `TestReverb`, Produkttest (Hall sieht Solo) | Diffusions-/Feedbackmatrix, Decay-/Wet-Kennlinie |

## Produkt (`streichwurst`)

| Modul | Beleg | Verfahren | Abweichung | Erwartete Klangfolge | Test | Offene Referenzfrage |
| --- | --- | --- | --- | --- | --- | --- |
| Signalfluss | Engine-Graph BESTÄTIGT: Ensemble→Phaser→Tapkorrektur, Solo danach, Reverb auf der Mischung, Gain danach | Genau diese Reihenfolge, Soli umgehen die String-FX | — | — | `TestRouting` | — |
| Balance | SF-F07 BESTÄTIGT: b=r/64 bis 64, dann linear bis exakt 2; Mitte = beide Faktor 1; τ≈25,4 ms | Kontinuierliche Form derselben Kennlinie, Glättung 25,4 ms | Float statt High32-Produkte | Keine −3-dB-Mitte | `TestBalanceCurve`, `TestBalanceMix` | — |
| Master | SF-F07: Kanalmaster mit 1/128 geglättet (≈51 ms) | Glättung 51 ms, eigene quadratische Kennlinie | Kennlinie eigen | — | Fingerprint | Kanalmaster-Domain |
| Gain/Headroom | OFFEN (Ausgangsdynamik) | Feste Busfaktoren 0,25 (Strings) / 0,5 (Soli), Mischheadroom 0,35 vor dem Gainwort | Eigener Gainplan | Sechsstimmiger Akkord im Modus 1 mit Hall bleibt unter dem Knie (Spitze 0,69) | `TestHeadroom` | Originalpegel und Limiter |
| Ausgangsschutz | OFFEN (0x266E50) | Identität bis 0,8, tanh-Knie, |y|<1; NaN→0 gezählt; Panik mit 2-ms-Rampe | Eigener Limiter | Weiche Begrenzung nur bei Übersteuerung | `TestPanic` | Originale Begrenzungs-/Quantisierungskennlinie |
| Abtastrate | SF-A05 BESTÄTIGT: Original 40 kHz/Hop 16 + Hostresampler | Direkt 48 kHz, alle Zeitgrößen in Sekunden umgerechnet | Kein 40-kHz-Kern im Produkt (Begründung: [RATE_COMPARISON.md](RATE_COMPARISON.md)) | Andere Aliasingmuster als ein 40-kHz-Kern | `make compare-rates` | Originales Resamplingfilter |

Die Module sind austauschbar gehalten: Registrierungsframes, Ereigniskern, Solo,
Animate-Formung, Phaser und Reverb haben eigene Stellen im Code und können mit
späteren Belegen ersetzt werden, ohne die Modulgrenzen zu ändern.
