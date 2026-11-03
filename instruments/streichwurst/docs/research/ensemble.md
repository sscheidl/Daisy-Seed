> Forschungssnapshot vom 01.11.2026. Links für diese Übergabe angepasst;
> historische Aussagen und eigene Ersatzverfahren beachten. Siehe [Herkunft](../PROVENANCE.md).

# Ensemble, Animate und nachgeschaltete Effekte

01.11.2026; [Quelle und Prüfgrade](../PROVENANCE.md).
Der Ensemblekern ist früh vertieft worden. Die folgenden Befunde ersetzen
keinen späteren Originalrender und keine vollständige Klangrekonstruktion.

## Platzierung und Speicher

**SF-D01 BESTÄTIGT:** Ein gemeinsamer Ensemblezustand C=E+0xA3C verarbeitet
die bereits summierte und rekursiv geformte String-Sektion. Kein
Ensemblezustand pro Note. Die Solo-Sektion wird erst später zugemischt.
Phaser folgt auf die gemeinsame String-Ensemblesumme; zwei separat
gespeicherte Taps gehen anschließend in unterschiedliche Stereoarme.

| Feld relativ zu C | Bedeutung / Beleg |
| --- | --- |
| +0 / +4 | langsamer/schneller Phasenakkumulator; 32-Bit-Wrap |
| +8 | zusätzlicher Chorus-Phasenakkumulator |
| +0xC | blockweiser Schreibindex |
| +0x10..0x80F | **eine** Hauptleitung, 512×int32 = 2048 Byte |
| +0x810..0x84F | Randkopie von 16 Samples für zusammenhängende Lesezugriffe |
| +0x890 / +0x8D0 | zwei Tap-Ausgaben, je 16×int32; keine weiteren langen Delayleitungen |
| +0x910..0x924 | vorangegangene Q8-Lesepositionen der Modustaps |

**SF-D02 BESTÄTIGT:** In allen drei Routinen steigt der Blockindex um
16 und wird mit `0x1F0` maskiert. Die neue Eingangsgruppe wird in die
Leitung kopiert. Reader maskieren den groben Readindex mit `0x1FF`.
512 Samples sind Speicherkapazität, kein festes hörbares Delay von 12,8 ms.
Einige Adressbereiche zwischen Guard und Tapcaches sind noch nicht vollständig typisiert.

## Modusgraph

Aus Enable E+0x1364 und Mode E+0x1365 in `0x2675E6..0x267636`:

| nativer Mode | Routine | tatsächliche Reader-Aufrufe | gemeinsame Summe vor späteren Stufen |
| --- | --- | --- | --- |
| 0 | `0x27D1C0` | `0x27C770`, `0x27C410` ×2 | drei Taps; erster ersetzt Eingang |
| 1 | `0x27CD60` | `0x27CA50` ×3, `0x27C410` ×2 | Eingang plus fünf Taps |
| 2 | `0x27D030` | `0x27CA50`, `0x27C410` ×2 | Eingang plus drei Taps |
| anderer Wert | `0x27D1C0` | gleicher Fallback wie 0 | nicht als zusätzlicher Modus zählen |
| Enable 0 | Routine übersprungen | keine Reader in diesem Zweig | Bypass des Kerns |

**SF-D03 BESTÄTIGT** für Dispatch, Tapzahl und erste Readerzuweisung;
keine unbewiesenen Produkt-/UI-Modusnamen zugeordnet.

## Lokale Mathematik

Notation: W32 modulo 2^32, S32 signed Interpretation, ASR arithmetische
Rechtsschiebung, unsigned Phase mit Umlauf 2^32. Pro Kernblock B=16.

**SF-D04 BESTÄTIGT, manuelle Breiten-/Vorzeichenprüfung:** Für die
Modulation wird ein vorzeichenabhängiger quadratischer Ausdruck benutzt:

`p=S32(phase); q=ASR64(p·p,31)`

`H(p)= p−q für p>0, sonst p+q`.

Nachgelagerte arithmetische Shifts skalieren H. In reeller Näherung,
mit `v=p/2^31`: `H/2^29 ≈ 4v(1−|v|)`. Es handelt sich um eine
stückweise Parabel, **nicht** um den Nachweis einer Sinusfunktion.
Die Rundung ist durch die Shifts festgelegt; die Näherungsformel bewahrt
nicht automatisch jedes Integer-LSB.

**SF-D05 BESTÄTIGT unter der gestützten 40-kHz-Zeitbasis:**

| Phasenfeld | Inkrement pro 16 Samples | daraus f in Hz | aktive Modi |
| --- | --- | --- | --- |
| C+0 | 1.391.569 | 0,809999765 | 0/1 |
| C+4 | 13.348.758 | 7,769999793 | 0/1 |
| C+8 | 549.755 | 0,319999526 | 1/2 |

`f=increment·40000/(16·2^32)`. Bei anderer interner Rate wären die
Frequenzen entsprechend anders; den Hostwert nicht direkt einsetzen.

Für Modus 2 ist der Q8-Positionsausdruck des ersten Taps lokal geschlossen:

`D0 = 67584 − ASR32(ASR32(H(p),1),13)`.

Die anderen beiden verwenden Phasenverschiebungen +/−`0x60000000`
(+/−135°) und speichern die Ausgaben für Stereo. Mit dem Speicherumlauf
entspricht dies näherungsweise `delay = 248 + 128·h(p)` Samples,
also 120..376 Samples oder **3..9,4 ms** bei 40 kHz, vor den kleinen
Unterschieden der Blockinterpolation. Grundverzögerung 6,2 ms.

Die ersten kombinierten Positionen in Modi 0/1 benutzen:

`D ≈ 67584 + ASR(H(p_s),15) + ASR(H(p_f),18)`

mit zusätzlicher Begrenzung auf Q8-Werte 4096..131072.
Die ungekappte Näherung hat eine Positionsauslenkung von 64+8 Samples,
also einen effektiven Delaybereich von ungefähr 176..320 Samples.
Die drei Paare verwenden jeweils **gleiche** Verschiebungen des langsamen
und schnellen Oszillators: 0, +`0x55550000`, −`0x55550000`.
Der Betrag entspricht 119,998168945°, also nicht exakt 120°.
Diese Zuordnung wurde anschließend über getrennte acyclische Präfixpfade
bis zum zweiten/dritten Tap geprüft (128 bzw. 1024 symbolische Pfade,
keine Ausführung des Plugins). Die Vorzeichenfälle der Parabel müssen
weiterhin manuell mit den Breiten-/Branchbedingungen gelesen werden.

Modus 1 ergänzt **Chorusphase 0 und +135°**, nicht +/−135°.
Die beiden zusätzlichen Tapziele bei `0x27CFAE/0x27CFFC` benutzen
C+8 bzw. C+8+`0x60000000`. Im Modus 2 sind es dagegen
0,+135°,−135°. Das erklärt einen konkreten Unterschied zwischen
„drei plus zwei“ und einem unspezifischen Fünfstimmen-Chorus.

**SF-D06 BESTÄTIGT für den ersten Sample jedes Readeraufrufs:**
Mit Position d in Q8 und Schreibindex w:

`j=(w+ASR32(d,8)) & 511; f=d & 255`

`t = W32(r[j] + ASR32(r[j+1]−r[j],8)·f)`.

Damit wird zuerst die Differenz quantisiert und dann multipliziert.
Das ist bei Integerdaten **nicht** exakt gleich
`r[j] + ((r[j+1]−r[j])·f >> 8)`.
Reader `0x27C770` ersetzt die Summe, `0x27CA50` addiert,
`0x27C410` addiert und schreibt zugleich einen Tapcache.
Es gibt **keinen aus diesem Reader belegten Feedbackterm**.

Der Aufrufer übergibt alte Position d und Differenz Δ zur neuen Position.
Ein weiter geschlossener Teil ist der schnelle Readerzweig: `v=ASR32(Δ,4)`;
bei `0x27CA8A` wird `f_end=f−v+Δ` mit `0xFFFFFF00` getestet.
Falls alle oberen Bits null sind, läuft der ungerollte Zweig mit
`j_k=j+k`, `f_k=f+k·v`, k=0..15. Dort gilt dieselbe Interpolationsgleichung
mit diesen Indizes und Bruchteilen. Die Guardkopie ermöglicht j+1 am Rand.
Der konservative Endtest benutzt Δ selbst, nicht bloß 16·v.

Bei Fractional-Überlauf oder negativer Fraction gibt es getrennte
Korrekturschleifen. Deren vollständige Index-/Rundungsgleichung, insbesondere
bei großen Positionssprüngen, bleibt **OFFEN**. Die schnelle Gleichung ist
ausdrücklich kein vollständiger Blockreader.

## Stereo, Gain und Übergänge

**SF-D07 BESTÄTIGT, lokale Gleichung:** `0x266CE0` multipliziert 16
Tapwerte mit −704643072 und schiebt signed64 um 32 zurück. Der Faktor
ist exakt **−21/128 = −0,1640625**. Die beiden Aufrufer benutzen
E+0x12CC=C+0x890 und E+0x130C=C+0x8D0, jeweils für einen Kanal.

Im betrachteten Zweig gilt lokal, mit F als gemeinsamer Stringsumme nach Phaser:

`L=F − (21/128)·t_a; R=F − (21/128)·t_b`, jeweils Integerquantisierung.

Die Monosumme enthält somit `F−(21/256)(t_a+t_b)`; sie ist weder ein
unveränderter Drypfad noch eine vollständige Auslöschung der Modulation.
Modusabhängige Tapzahl ändert die Dichte und den kohärenten Pegel.
Die nachfolgenden Gain-/Balance-/Limiterstufen gehören deshalb zur
noch offenen **Gesamtnormierung**. Kein identischer Lautheitspegel aller
Modi allein aufgrund vorhandener Gainwörter behauptet.

**SF-D08 OFFEN, teilweise belegte Übergänge:** Initialisierung `0xC56A0`
setzt Phasen/Index und Caches zurück, löscht die Leitung und ruft die
Modusroutine `0x27CD60` vor dem Abschluss auf. Deshalb ist der endgültige
Startzustand nicht schlicht „alle Phasen null“. Bypass überspringt den
Renderkern; ein Freeze der dort fortgeschriebenen Zustände ist plausibel.
Klickfreiheit beim Moduswechsel, Nachlauf beim Bypass, Wiederanlauf,
State-Speicherung und Sampleratenwechsel sind noch nicht vollständig geklärt.

## Animate, Phaser und Reverb

**Nachtrag zur gezielten Vertiefung:** [SF-F06..F08](focused-findings.md)
ordnen die drei relativen Gainwörter zu, bestätigen Freeze und unveränderte
Delay-/Phasenzustände der Setter und definieren die eigene 20-ms-Übergangsregel.
Die weiter oben als offen bezeichneten Übergänge sind damit im betrachteten
Setter-/Engineumfang geklärt; Host-State und vollständiger Reset bleiben getrennt offen.

**SF-D09 PLAUSIBEL:** Animate-Setter `0x262130` ändert Formungs-/LFO-
Steuerfelder und Dirtyflags. Die Formung läuft vor Ensemble. Die drei
Kontexte in `0x27A670` haben rekursive Zustände und fünf Summanden/
Koeffizientenbeziehungen; vollständige Ordnung und Koeffizientensynthese
sind noch OFFEN. Animate nicht ohne weiteren Nachweis als zusätzlicher
Chorus oder Pitchshifter bezeichnen.

**SF-D10 BESTÄTIGT für Zuordnung, Mathematik teilweise:** Phaser-Registry
führt zu E+0x11D38 und Renderer `0x265F40`. Er benutzt eigene
Koeffizientendaten (RVA 0x2AAD60), rekursive Floatzustände, nichtlineare
Modulation und u.a. eine lokale 0,9-Rekurrenz. Stufenzahl, exakte
Allpass-/Feedbacktopologie und vollständige Parameterkurve bleiben OFFEN.

**SF-D11 BESTÄTIGT für Strukturanker:** Reverb `0x265680` besitzt einen
gemeinsamen Ring mit 16.384 Floats (64 KiB), Wrapmasken und Guardkopien.
Er ruft achtmal `0x264EF0`, zweimal `0x264D90` und die weiteren
Verarbeitungsstufen `0x264250/0x263CD0` auf. Zwei Phasenfelder steigen
um 1.050.892 bzw. 864.318. Ihre physikalische Rate ist ohne bestätigte
Aufrufkadenz dieser Teilstufen nicht angegeben. Vollständige Diffusions-/
Feedbackmatrix und Decay-/Wetkennlinie sind OFFEN. Keine Übertragung
des getrennten Iridium-Shimmerbefunds auf dieses Plugin.

## Eigenes Modell und Grenzen

[Eigenes Modell](../../tests/reference/own_dsp_model.py): eine Floatleitung,
stückweise parabolische Modulation, die drei verschiedenen Summenformen,
Stereo-Tapkorrektur, **eigene** DC-Normierung und 20-ms-Wetsmoothing.
Abweichungen: per-Sample statt 16er-Block, gewöhnliche Floatinterpolation,
exakte Drittelphasen für die kombinierten Taps, eigene Reset-/Bypassregeln,
keine Original-Gainwörter oder Herstellerdaten. Diese Entscheidungen schließen
die offenen Originalgleichungen nicht durch Annahmen.

Impuls, Sinus, drei Tonlagen, Akkord, langsame Parameterfahrt, DC und
Blockkontinuität werden [numerisch geprüft](../VALIDATION.md).
Es gibt keinen Hör-/Referenzvergleich mit dem Original und noch keinen
abgeschlossenen klanglichen Nachbau.
