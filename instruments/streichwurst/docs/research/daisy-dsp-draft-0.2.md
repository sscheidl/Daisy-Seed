> Forschungssnapshot vom 01.11.2026. Links für diese Übergabe angepasst;
> historische Aussagen und eigene Ersatzverfahren beachten. Siehe [Herkunft](../PROVENANCE.md).

# Eigenständige Daisy-DSP-Spezifikation, Entwurf 0.2

01.11.2026. Eigene funktionale Konstruktion, keine Übersetzung des
Herstellercodes. **Umsetzbarer Entwurf mit offenen Referenzbeziehungen**;
keine Zusage eines klanggleichen Streichfett-Nachbaus. Keine Firmware begonnen.

Grundlagen: [Audiopfad/Stimmen](audio-and-voices.md),
[Parameter](parameters.md),
[Ensemble](ensemble.md),
[Ressourcen](daisy-budget.md),
[eigene Validierung](../VALIDATION.md).

Die [gezielte Vertiefung](focused-findings.md) ergänzt
die tatsächliche Hüllkurvenrekurrenz, Registrierungs-/Oktavtransformation,
Balance und Ensemble-Gain-/Bypassregeln. Verbleibende Originaldaten und
Detailpfade werden für diesen eigenen Entwurf ausdrücklich ersetzt oder
zurückgestellt; das ist keine vollständige Referenzrekonstruktion.

## Belegte Eigenschaften und unabhängige Entscheidungen

| Gegenstand | Referenzbefund | eigener Vertrag / Abweichung |
| --- | --- | --- |
| Takterzeugung | zwölf gemeinsame String-Takte, oktavbezogene Ereignisse | zwölf freie Grundphasen und mathematische Oktavteilung; eigene Wavebanks |
| Stringhüllkurven | Notenziel/-pegel pro MIDI-Index, zweispezifische Rekurrenz | 128 logische Notenzustände, physikalisch definierte Attack/Release |
| Klangformung | acht Registergewichte und rekursive Kontexte | vier eigene Spektralfamilien und kontinuierliches Morphing; bewusste Vereinfachung |
| Solo | acht Poolobjekte, eigene Modulation/Hüllkurven | acht eigene Stimmen, deterministische Allokation; keine behauptete Übernahme der Originalpriorität |
| Ensemble | eine Leitung, Modi mit 3/5/3 Taps, drei LFO-Zeitskalen | entsprechende Summenformen; Floatinterpolation, eigene Normierung und Übergänge |
| Stereo | zwei verschiedene gecachte Tapkorrekturen | eigene Matrix mit Faktor 21/128, danach explizite Normalisierung |
| Phaser / Reverb | getrennte globale Originalstufen; Mathematik unvollständig | eigene optionale Allpasskaskade / kleine FDN; Originaltopologien offen |
| Samplerate | Originalengine 40 kHz plus Hostresampler | eigene Engine direkt bei 48 kHz, Zeitgrößen in Sekunden |

Die Modelle dürfen funktionale Konzepte übernehmen; Tabellen, Presets,
Waveforms, Kernelkoeffizienten und Herstellerquellcode werden nicht übernommen.

## Module und Kommunikation

`NoteState → StringSource → AnimateForming → Ensemble → StringPhaser`
läuft neben `SoloPool → SoloModulation`. Die Zusammenführung lautet
`BalanceMix → GlobalReverb → Gain → OutputSafety → StereoOut`.

Eigene Balance u∈[0,1]: Stringgain=min(2(1−u),1),
Sologain=min(2u,1). Die Mitte enthält beide Sektionen ungedämpft;
kein Equal-Power-Crossfade. Die eigene kontinuierliche Bedienkennlinie
ersetzt die leicht asymmetrische native Integerabbildung.

Alle Module haben eigene Zustände. `prepare(Fs,maxBlock)` reserviert
sämtliche Speicher außerhalb der Audiobearbeitung; `reset()` setzt klar
definierte Zustände. `render(events,parameters,L,R,N)` benötigt N≤maxBlock,
zwei Float32-Ausgabekanäle und keine Audiodatei-/GUI-/Netzwerkoperation.
N=0 übernimmt Parameter/Reset, ohne Phasen fortzuschreiben; Notenereignisse
bleiben begrenzt für den nächsten nichtleeren Block vorgemerkt.

Eigene Events: `NoteOn(channel,note,velocity,frame)`, `NoteOff`,
`Sustain`, `PitchBend(semitones)`, `AllNotesOff`, `AllSoundOff`.
frame liegt in [0,N); gleiche frame-Werte behalten die Eingangsreihenfolge.
Velocity null ist NoteOff. Ungültige Werte verwerfen und lokal zählen.
Events innerhalb eines Blocks segmentieren die Verarbeitung samplegenau.
Diese Regel ist unser Vertrag; keine bereits belegte Original-Offsettreue.

Änderungen am nicht hörbaren Verwaltungszustand sofort; hörbare Gain-,
Timbre-, Depth- und Wetparameter mit `s+=α(target−s)`,
`α=1−exp(−1/(Fs·0,020))`. Für Tonhöhe separat τ=0,005 s.
Gate-Ereignisse werden nicht zeitlich geglättet; sie starten die Hüllkurve.
Moduswechsel über zwei Effektzustände mit 20-ms-Überblendung; dafür
temporär doppelten Effektzustand und Last budgetieren.

## Eigene Stringquelle und Datenentstehung

Für Tonklasse c=0..11: `f_c=440·2^((c−69)/12)` Hz,
`φ_c[n+1]=frac(φ_c[n]+f_c/Fs)`. Für Note m=c+12o gilt
`φ_m=frac(2^o·φ_c)`. Phasen laufen auch bei stummen Noten weiter.
Neue Noten setzen keine der zwölf Phasen zurück. Pitchbend multipliziert
alle Phaseninkremente mit `2^(bend/12)`. Große Bendänderungen werden
geglättet. Die obersten Noten werden vor Erzeugung auf Bandgrenzen geprüft.

Wavebanks entstehen ausschließlich aus eigener Harmonischer Synthese:

`W_r(φ,H)=A_r(H)·Σ[h=1..H] a_r(h) sin(2πhφ)`.

Vier eigene Spektralfamilien:

* `a_0(h)=1/h`;
* `a_1(h)=1/h` für ungerade h, sonst 0;
* `a_2(h)=sin²(πh/3)/h`;
* `a_3(h)=exp(−h/6)/h`.

`A_r(H)` normalisiert auf maximalen Betrag ≤1 bei der eigenen Generierung.
H aus {1,2,4,8,16,32,64,128}; nur H·f≤0,45Fs zulassen.
Wavebanks enthalten 1024 äquidistante Phasenwerte plus wiederholten
Randwert. Lineare Phaseninterpolation; zwischen zwei zulässigen
Spektralstufen überblenden. Falls H=1 bereits unzulässig ist: Note stummschalten.
Ein initialer Entwurf kann statt Tabellen dieselbe Sinussumme offline
berechnen; das Pythonmodell verwendet maximal 16 Harmonische und eine
eigene geglättete Sägezahnfamilie. Es ist noch kein Wavebank-Generator.

Registrierung u∈[0,1] wählt vier benachbarte Klangfarben über
lineare Segmentgewichte auf Positionen 0,1/3,2/3,1. Gewichtssumme 1.
Eigene Oktavstellung 0 verwendet die Grundlage, Stellung 2 die höhere
Oktave, Stellung 1 die Summe beider mit jeweils Faktor 0,75. Dies
übernimmt die funktionale Nachbarsummenregel, ohne Originalgewichte zu
übernehmen. Die Stellung mit beiden Oktaven ist nicht auf Summe 1 normiert.
Die Wahl von vier statt acht Gewichten, eigene Spektren und eigener
Bandbegrenzungsmechanismus verändern Timbre, Onset und Aliasing.
Das Original besitzt einen achtteiligen Ereigniskern mit Leckintegration;
diese Konstruktion behauptet dessen numerische oder klangliche Gleichheit nicht.

Für Note m: `x_m=e_m·v_m·Σ_r w_r W_r(φ_m,H)`.
Stringmono ist die Summe aktiver x_m mit festem Headroomfaktor,
anfänglich 1/16 für höchstens 16 gleichzeitig berechnete Tonlagen;
beide Oktaven zählen als zwei Tonlagen. Dies ist eine ungemessene
Qualitäts-/Lastgrenze des eigenen Entwurfs, kein Original-Polyphoniewert.
**Keine** laufende Division durch die Zahl gedrückter Tasten, die gehaltene
Töne bei neuen Noten abrupt verändert. Headroom und sanfte Begrenzung
werden mit Worst-Case-Akkorden geprüft.

## Eigene Hüllkurven und Belegung

Attack `τ_a=0,003·2^(11u_a)` Sekunden, Release
`τ_r=0,020·2^(9u_r)` Sekunden, u∈[0,1]. Dies sind eigene Kennlinien.
Für Ziel g∈{0,1}: `e'=g+(e−g)exp(−1/(Fs·τ))`;
Attack für g=1, Release für g=0. Bei e<10^-7 und g=0 wird e=0.
Retrigger beginnt am aktuellen Pegel, nicht bei null. Velocity v/127
wird als eigener linearer Pegelskalar benutzt; ein eigener Velocitiesmoothing-
vertrag verhindert Sprünge bei wiederholter Note.

Stringverwaltung: NoteOn erhöht einen saturierenden Referenzzähler pro
Channel/Note; NoteOff reduziert ihn bis null. Sustain hält das Ziel nach
dem letzten Off; PedalOff gibt diese Ziele frei. Zählerüberlauf und
fehlendes NoteOff erhalten eine definierte AllNotesOff-Entlastung.
Der kleine Python-GateBank prüft zunächst eine kanalunabhängige Teilmenge;
16-Kanal-Tracking ist eine noch nicht implementierte Anforderung.

Solo: acht Objektplätze. Zuerst gleicher Channel/Note für Retrigger,
dann niedrigster freier Index, dann kleinster Releasepegel, dann kleinster
aktueller Pegel; bei Gleichheit ältester Start, danach niedrigster Index.
Alter ist monotoner uint64-Eventzähler; dessen Überlauf durch Neu-Ranking
außerhalb der Audio-Schleife behandeln. Die Regel ist eigener Entwurf.
Bei Steal 1-ms-Überblendung des alten Signals in den neuen Zustand.

Eigene Soloquelle zunächst Pulse/Saw-Morph aus den selbst erzeugten Banks.
Attack/Release verwenden die oben definierten Zeitkennlinien; Attack geht
zum Ziel 1 und wechselt bei e≥0,999 in Decay. Decay geht mit τ=0,08 s
zum Sustainwert s∈[0,1]; bei |e−s|<10^-5 folgt der Sustainzustand.
Die Solo-Summe erhält zunächst festen Faktor 1/8. NoteOff wechselt
aus jedem aktiven Zustand sofort in Release. Retrigger
beginnt am aktuellen Pegel. Diese eigenen Schwellen sind keine Originalbefunde.
Tremolo:
`g(t)=1−d/2+(d/2)sin(2πft)`. Parameter f in Hz, d∈[0,1].
Dies ersetzt die noch nicht rekonstruierte Original-Solowellenbildung.

## Ensemble als Teil des Gesamtklangs

Eine Mono-Floatleitung; erforderliche Länge
`ceil(0,0128·Fs)+2`, bei 48 kHz **617 Floats = 2468 Byte**.
Die Länge muss nicht Zweierpotenz sein. Echte Delays in Sekunden,
keine unskalierte Übernahme von 40-kHz-Samplezahlen auf 48 kHz.

Eigene normierte Parabel `h(φ)=4v(1−|v|)` mit
`v=2frac(φ+1/2)−1`. Eigene LFOs 0,81/7,77/0,32 Hz,
pro Sample fortgeschrieben. Resetphasen alle null.

Modi 0/1: drei Delays
`d_k=(248−64h(φ_s+k/3)−8h(φ_f+k/3))/40000` Sekunden.
Die exakten Drittelphasen sind eine **eigene Vereinfachung** gegenüber
den belegten Originalwerten +/−0x55550000 eines 32-Bit-Umlaufs.
Modus 1 ergänzt zwei Delays `(248+128h(φ_c+q))/40000`, q=(0,3/8).
Modus 2 hat drei Delays `(248+128h(φ_c+q_k))/40000`,
q=(0,3/8,−3/8). Floatinterpolation am echten Readindex pro Sample.

Seien t_k die Taps, `F=Σt_k` in Modus 0, sonst `F=x+Σt_k`.
`L0=F−(21/128)t_last; R0=F−(21/128)t_penultimate`.
Eigene DC-Normierung `G=0,8/(tapCount+dryCount−21/128)`;
L=G·L0, R=G·R0. Dry/Wet über 20-ms-geglätteten Wert.
Kein Feedback im eigenen Ensemble. Bypass lässt Phasen und Delay weiterlaufen;
Die gezielte Vertiefung bestätigt den Freeze im untersuchten Originalpfad.
Sie ordnet außerdem die Original-Gainfaktoren 0,8125/0,625/0,75 zu.
Der eigene Entwurf behält bewusst seine DC-Normierung nur für die
String-Sektion; die Solo-Lautstärke folgt keinem Ensemble-Modusfaktor.
Damit werden weder globale Original-Pegelkoppelung noch Ausgangsdynamik übernommen.

Phaserposition: gemeinsamer Summenarm vor der Stereokorrektur. Der
Ensemble-Teilversuch lässt Phaser deaktiviert; bei späterer Integration
darf die gemeinsame/tapseparate Filterung nicht verwechselt werden.
Ein einfacher nachträglicher Stereo-Chorus auf einem beliebigen Synth
bildet diese Interaktion nicht automatisch ab.

## Optionale eigene Formung und Effekte

Animate als eigene wandernde Bandbetonung: φ_a läuft mit
`f_a=0,1+0,9u_rate` Hz. Für Tiefe d∈[0,1] sei
`f_c=700·exp(0,4d·h(φ_a))` Hz. Zwei Einpole bilden `LP_fc(x)` und
`LP_200(x)`, jeweils `z'=a z+(1−a)x`, `a=exp(−2πf/Fs)`.
Ausgabe `x_a=x+0,3d(LP_fc(x)−LP_200(x))` vor Ensemble.
Das ist eine vollständig eigene Entwurfsalternative; Originalordnung
und Originalkurve bleiben offen. Koeffizienten pro Sample aus geglätteten
Steuerwerten oder mit gesondert überprüfter Kontrollinterpolation aktualisieren.

Eigener Phaser: vier erste Allpässe mit
`g=(tan(πf/Fs)−1)/(tan(πf/Fs)+1)`,
`y[n]=g x[n]+x[n−1]−g y[n−1]`, 80..3000 Hz und sanfter Modulation.
Kein Feedback bis zu separat geprüfter Stabilität. Dies behauptet weder
Originalstufenzahl noch dessen Koeffiziententabelle.

Eigener Reverb als optionale Vierzweig-FDN: eigene Delayzeiten
29,7/37,1/41,1/43,7 ms, Einpol-Dämpfung pro Rückweg, normierte
4×4-Hadamardmatrix, Feedbackbetrag ≤0,75. Vertrag:
`v[n]=inputVector·x[n]+g·H·LP(delayOut[n])`.
H ist die Sylvester-Hadamardmatrix geteilt durch 2; Eingangsvektor
`(1,1,1,1)/2`, Dämpfung als obiger Einpol bei min(6500,0,2Fs) Hz.
`g=0,1+0,65u_decay`. Ausgangsvektoren
`L=(d0+d1−d2−d3)/2`, `R=(d0−d1+d2−d3)/2`.
Reverb-Wet w∈[0,0,4], linearer Dry/Wet-Crossfade; keine Delaymodulation
im ersten eigenen Entwurf. Das verändert Dichte und stationäre Moden.
H ist orthogonal, LP hat Betragsmaximum ≤1. Daraus folgt für den
linearen Entwurf eine kontraktive Rückführung bei |g|<1;
das ersetzt keine Prüfung einer späteren Implementierung oder des Originals.

## Reset, Rate, Grenzen und Abnahme

AllNotesOff setzt Gates frei und erhält Ausklang; AllSoundOff leert
Hüllkurven und Effektleitungen mit 2-ms-Ausgangsrampe. Reset setzt zusätzlich
Phasen, Smoothingwerte und Verwaltungszähler auf definierte Defaults.
Sampleratenwechsel nur außerhalb des Callbacks: ausblenden, neue Puffer/
Koeffizienten erzeugen, vollständig resetten, einblenden. Keine alten
Sampleindizes mit neuer Rate weiterverwenden. Fs=0/NaN/∞ ablehnen.

Float32-Audio. Eigene Ausgangsbegrenzung: für |x|≤0,8 y=x, sonst
`y=sign(x)·[0,8+0,2 tanh((|x|−0,8)/0,2)]`; Wert und erste Ableitung
sind an der Grenze stetig, |y|<1. Nicht endliche Werte ergeben Ausgabe
null und einen lokalen Diagnosezähler. Rechenreihenfolge und Subnormal-
behandlung im späteren C++-Kern festlegen. Kein Speicherallokieren, keine Locks und
keine Dateizugriffe im Callback. Bei Queueüberlauf deterministischer
AllNotesOff-Fallback und Diagnosezähler; keine hängenden Gates.

Abnahmekriterien: Quelle nachweislich bandbegrenzt; Phasen über Blocks
stetig; Hüllkurvenzeit analytisch passend; Wiederholnoten/Sustain/Steal
deterministisch; Ensemble-Indexzugriffe im Puffer; DC/Impuls/Stereo/Mono
über alle Modi; sanfte Parameterwechsel; maximale Callbackzeit unter
vereinbartem Budget am erst später bestätigten Board.

Noch **kein** eigener Solo-, Phaser-, FDN-, Wavebank- oder Daisy-C++-Code
geschrieben. Das ausführbare Teilmodell prüft Quelle/Gates/Ensemble.
Offene Originalgleichungen und eigene Designentscheidungen bleiben auch
nach grünen Modelltests voneinander getrennt.
