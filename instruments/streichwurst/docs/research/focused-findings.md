> Forschungssnapshot vom 01.11.2026. Links für diese Übergabe angepasst;
> historische Aussagen und eigene Ersatzverfahren beachten. Siehe [Herkunft](../PROVENANCE.md).

# Gezielte Vertiefung: Grundklang, Hüllkurven und Ensemblepegel

01.11.2026. Nutzer hat die vorgeschlagene begrenzte Vertiefung bestätigt:
Grundklang/Hüllkurven und Ensemblepegel/Übergänge. Kein neuer Auftrag für
vollständige Pluginverwaltung, Phaser-/Reverb-Rekonstruktion oder Firmware.
[Quelle/Adresskonvention](../PROVENANCE.md)
unverändert; alle Adressen sind RVA. E=P+0x338, C=E+0xA3C.

## Hüllkurve mit physikalischer Zeit

**SF-F01 BESTÄTIGT, lokale Aufrufer-/Branch-/Breiteninterpretation:**
`0x27B500` wählt bei `0x27B55E` mit JNS nach der Pegeldifferenz
den Crescendo-Koeffizienten bei steigendem und den Release-Koeffizienten
bei fallendem Pegel. Aufruf `0x27BEE4`; Koeffizientenfelder
E+0x988/0x98C, zuvor über die bekannten Registrycallbacks gesetzt.

Für Ziel t und aktuellen Integerpegel a:

`δ=t−a; k=k_crescendo für δ≥0, sonst k_release`

`a'=W32(a+ASR64(δ·(k<<16),32))`.

Im untersuchten gültigen Pegelbereich ohne Differenzüberlauf entspricht
das einem asymmetrischen Einpol mit **α=k/65536**, nicht k/32768.
Update einmal pro 16 Samples bei 40 kHz: T=0,0004 s.

`τ=−T/ln(1−k/65536)`; `t_99=ln(100)·τ`.

Die Zeitkonstante bezeichnet 63,2 % einer steigenden Zieländerung bzw.
Abfall auf 36,8 %, nicht automatisch die beschriftete Attack-/Releasezeit.
Rundung, kleiner Pegel und die gesonderte Bereinigung verändern das Ende.

**SF-F02 BESTÄTIGT für das untersuchte native Fenster, Hostdomain OFFEN:**
128 uint16-Einträge ab 0x2AADF0 sind positiv und monoton nicht steigend.
Aus ihnen ergeben sich für native Indizes 0, 64 und 127 die Zeitkonstanten
0,000577103 s, 0,151328236 s und 4,368866664 s. Dies ist **keine kopierte
Koeffiziententabelle** und kein Nachweis, dass der gesamte Host-/UI-Bereich
genau diese Indizes umfasst. Die drei Werte sind bedingte Kalibrierpunkte
des belegten Verbrauchers. Eine einfache logarithmische Knopfkennlinie
ist dadurch nicht bewiesen.

Bei Ziel null prüft `0x27BE80` außerdem zyklisch einen Notenplatz:
Ziel null und Pegel unter 8192 führen zur Bereinigung. Der Index läuft
mit &127 fort. Diese Prüfung ist von der laufenden Einpolrechnung getrennt;
eine vollständige Hüllkurve ist daher nicht bloß eine kontinuierliche Exponentialfunktion.

## Registrierung und Oktavschalter

**SF-F03 BESTÄTIGT, lokaler Datenfluss `0x27AB60`:** Native Registrierung r
bei E+0x99E und Animateoffset m bei E+0x998 ergeben

`z=clamp(256r+m,0,32498)`

`c=floor(355z/4096); j=c>>8; f=c&255`.

Die acht Ausgangsgewichte werden zwischen benachbarten Frames interpoliert:

`W_i=T[j,i]+ASR32(T[j+1,i]−T[j,i],8)·f`, i=0..7.

Auch hier wird **vor** der Bruchmultiplikation geschoben. Maximal c=2816,
also j=11 mit anschließendem Framezugriff 12. Das untersuchte Fenster
umfasst 13 Frames mit acht nichtnegativen int32-Werten, jeweils ≤2^25.
Keine Framewerte wurden übernommen. Die musikalischen Herstellerlabels
und die vollständige Erzeugung dieser Daten bleiben offen.

**SF-F04 BESTÄTIGT für die Transformation vor der anschließenden Begrenzung:**
Mit W[-1]=0 und nativem Oktavwert o bei E+0x99F:

* o=0: W'_i=W_i;
* o=1: W'_i=(W_i+W[i−1])−ASR32(W_i+W[i−1],2);
* o=2: W'_0=0, W'_i=W[i−1] für i≥1;
* sonst: derselbe unveränderte Zweig wie o=0.

Danach obere Begrenzung auf 2^26. Der mittlere Zweig ist in reeller
Näherung **0,75 mal Nachbarsumme**, keine pegelneutrale Halbierung.
Branchbelege: 0x27ACBD, 0x27ACC2; Shiftzweig bis 0x27AD18,
Summenzweig ab 0x27AD20, gemeinsame Begrenzung ab 0x27ADB3.
Die Zwischenwerte wurden mit eigenen W0..W7-Symbolen statt Quelldaten verfolgt.

**SF-F05 PLAUSIBEL, nicht vollständig geschlossen:** E+0x900 enthält
128 Aktivierungs-/Stärkebytes. `0x27AE90` bildet daraus die Notenziele;
die ersten beiden lokalen Beiträge haben die Beziehungen
`T[n−12]+=(W0·a_n)>>8` für zulässigen Zielindex und
`T[n]+=(W1·a_n)>>8`. Weitere Beiträge laufen in Oktavschritten mit
abgezählter Schleife. Die vollständige Schleifeninvariante, alle Grenzen
und die Note-on/Velocity-Zuweisung sind weiterhin offen. Eine symbolisch
erreichte negative Zieladresse bei n=0 ist ein **unerfüllter Branchpfad**,
kein Nachweis eines tatsächlichen Schreibzugriffs außerhalb des Arrays.

Damit sind Registrierungsinterpolation und Oktavtransformation geschlossen;
die komplette Ereignisamplitudenbildung des Originalkerns ist es weiterhin nicht.

## Pegel nach Ensemble und Mischung

**SF-F06 BESTÄTIGT, Setter/Dispatch und lokale Skalierung:**

| Zustand | Gainwort E+0x1366 | relativer Betrag gegenüber Bypass |
| --- | --- | --- |
| Ensemble aus | −32768 | 1 |
| Mode 0, drei reine Taps | −26624 | 0,8125 |
| Mode 1, Dry plus fünf Taps | −20480 | 0,625 |
| Mode 2, Dry plus drei Taps | −24576 | 0,75 |

Native Moduszuordnung: 0x18114→0x1813A, 0x18119→0x1812D;
die Konstanten werden unmittelbar vor den Stores gesetzt. Der feste
Skalar E+0x11D78 wird in `0xC54B0` auf 16384 initialisiert.
Der Verbraucher bei 0x2677A8/0x2677C9 liegt **nach** Solo-Mischung und
Reverb. Die Tabelle bezeichnet einen relativen Endstufenskalar vor der
weiteren Ausgangsdynamik, keine gemessene Gesamtlautheit und keinen
vollständig rekonstruierten Limiter.

Mit kohärentem Einheits-DC an jedem Tap, ohne Phaser, liefern die
Tapmatrix und diese Faktoren gegenüber Dry näherungsweise 2,30420,
3,64746 und 2,87695. Diese bedingte Rechnung zeigt: Die Gainwörter
stellen keine einheitliche DC-Normierung her. Musikalische Signale,
Modulation, Filter und Ausgangsdynamik haben andere Pegelverhältnisse.

**SF-F07 BESTÄTIGT für die lokale Balancekennlinie:** Balancebyte r bei
E+0x11D86 kommt aus `balance`/0x17FD0. Der Zielskalar lautet

`B_t=2^24 r` für r≤64;

`B_t=2^24 r+266305(r−64)` für r>64.

Außerhalb |B_t−B|≤255: `B'=B+ASR32(B_t−B,6)`;
innerhalb dieses Fensters wird auf das Ziel gesetzt.
Die Näherungszeitkonstante beträgt 25,3995 ms bei 40 kHz/Hop16.
Mit b=B/2^30 mischt 0x266AA0 näherungsweise

`out=min(2−b,1)·strings + min(b,1)·solo`.

Die originale Integerrechnung summiert zwei High32-Produkte und
verschiebt anschließend um zwei Bits. Mitte r=64 bedeutet **beide
Sektionen mit Faktor 1**, kein Equal-Power-Crossfade mit −3 dB je Sektion.
Die getrennten Kanal-Masterziele E+0x11D88/8C werden mit 1/128 geglättet
(Zeitkonstante ungefähr 51,0 ms). Die panartige Steuerung ist nicht mit
diesem String-/Solo-Balanceparameter zu verwechseln.

## Modus, Bypass und eigene Übergangsregel

**SF-F08 BESTÄTIGT innerhalb der untersuchten Setter/Engine:** Der
erreichbare Enable-Setter 0x18180 schreibt nur P+0x169C/0x169D/0x169E
(Enable, Modus, Gainwort). Keine Calls und keine Delay-/Phasenlöschung.
Der Mode-Setter 0x18100 ändert ebenfalls nur Modus/Gainwort.
Die Engine überspringt bei ausgeschaltetem Enable den gesamten C-Renderer.
Ring, Index und dort erzeugte Phasen laufen währenddessen nicht weiter.
In Mode 0 laufen nur die beiden kombinierten LFOs, in Mode 2 nur der
Chorus-LFO, in Mode 1 alle drei. Beim Wechsel bleiben die anderen Zustände erhalten.

Im untersuchten Graphen gibt es keinen zweiten Ensemblekern für eine
explizite Überblendung. Die vorhandene 16-Sample-Lesepositionsinterpolation
ist davon zu unterscheiden. Potenzielle Einschwing-/Pegeländerungen sind
damit erklärbar; tatsächliche hörbare Klicks wurden nicht gemessen.

**Eigene Entscheidung:** kontinuierliche Ring-/LFOhistorie auch im Bypass;
20-ms-Überblendung zweier Modi mit gleicher Eingangshistorie. Das vermeidet
die Übernahme alter eingefrorener Delaydaten. Das eigene Modell behält
seine gesonderte DC-Normierung; Originalpegel und globale Dynamik werden
damit ausdrücklich nicht behauptet. Doppelte Übergangslast bleibt im Budget.

## Prüfweg und Stand

[focused_dsp.py](../PROVENANCE.md) prüft die begrenzten
Settergraphen, unmittelbar gesetzte Gainwörter, den Hüllkurvenbranch,
Koeffizienten-/Registrierungsaggregate und eigene mathematische Teilmodelle.
Acht zusätzliche Tests bestanden. Ein eigener 440-Hz-Quellversuch
vergleicht abrupten und 20-ms-überblendeten Moduswechsel; kein Originalrender.
Das ist keine vollständige formale Interpretation des Plugins.

Grundklangdaten und vollständige Ereignisamplituden werden im Daisy-Entwurf
weiterhin eigenständig ersetzt. Numerische VST3-IDs, Hersteller-State,
exakte Steal-Tie-Breaks und vollständiger Phaser/Reverb sind für den
jetzt begrenzten Prototypentwurf zurückgestellt. Weiter offen bleiben
die als PLAUSIBEL bezeichneten Referenzbeziehungen und Original-Reader-Grenzfälle.
