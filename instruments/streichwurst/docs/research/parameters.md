> Forschungssnapshot vom 01.11.2026. Links für diese Übergabe angepasst;
> historische Aussagen und eigene Ersatzverfahren beachten. Siehe [Herkunft](../PROVENANCE.md).

# Parameter: Registry, Callback und Verbraucher

01.11.2026; [Adresskonvention](../PROVENANCE.md).
**SF-C03:** Die IDs unten sind APVTS-Registrystrings, keine aus einer
Stringliste geratenen UI-Positionen. In Konstruktor `0xE3230` stehen
Stringaufbau und die danach aufgebaute Callbackfassung desselben Bindingablaufs.
Beispiel: Stringreferenz `stringsEnsemble` bei `0xE3DAC`, Callbacktabelle
`0x3337B0`, deren Aufrufslot `+0x10` auf `0x18180` zeigt. Die davor
liegende Callbacktabelle gehört zum vorherigen Binding und darf nicht
nach Stringnähe falsch zugewiesen werden.

Das Binding-/Listenerobjekt benutzt Vtable `0x333858`; der Weiterleiter
`0x16060` ruft die gespeicherte Funktion über deren Slot `+0x10` auf.
Die Konstruktor-/Capture-Zusammenhänge sind manuell geprüft, nicht vollständig
im 36-Anker-Prüfer formalisiert. **PLAUSIBEL** bedeutet hier eine belastbare
Zuordnung mit noch fehlendem vollständigen Registry→normalisierten Hostwert→
nativen Callbackwert-Nachweis. Ein Name allein bestätigt keine Kennlinie.

P = Processor; E = P+0x338. Offsets beziehen sich auf E, außer anders vermerkt.

| Registry-ID | Callback-RVA | native Stufe / DSP-Ziel | Grad / offene Skalierung |
| --- | --- | --- | --- |
| `stringsRegistration` | `0x18230` | Float→Integer→Byte; +0x99E; skaliert zusätzlich +0x9B8 um 2^12; Dirtyflag | PLAUSIBEL; Registrierung→acht Gewichte nutzt proprietäre Daten |
| `stringsEnsemble` | `0x18180` | Nulltest; Enable +0x1364, Gainwort +0x1366 | BESTÄTIGT lokaler Setter; Host-Normalisierung offen |
| `stringsEnsembleEffect` | `0x18100` | Byte +0x1365; modusabhängiges Gainwort | BESTÄTIGT lokaler Setter; UI-Labels der drei Werte offen |
| `stringsOctaves` | `0x180B0` | Byte +0x99F; Ziel-Neuberechnungsflag | PLAUSIBEL; genaue Enum-/Registerkombination offen |
| `stringsCrescendo` | `0x18060` | native Byteposition→uint16-Tabelle RVA 0x2AADF0→+0x988 | PLAUSIBEL; UI-Zeit und Tabellenerzeugung offen |
| `stringsRelease` | `0x18010` | gleiche Koeffiziententabelle→+0x98C | PLAUSIBEL; Zeitkennlinie offen |
| `balance` | `0x17FD0` | spätere Mischsteuerung; lokaler Verbraucher 0x266AA0 | PLAUSIBEL; Stereo-/Gainkurve noch nicht geschlossen |
| `soloTone` | `0x17F80`→`0x261610` | globaler Solo-Klangkontext; Datenbasis 0x2AA960 | PLAUSIBEL; keine Datenkopie, Morph-/Wavebedeutung offen |
| `soloSustain` | `0x17F00` | Solozustands-/Envelopekonfiguration | PLAUSIBEL; vollständige State-Enum offen |
| `soloTremolo` | `0x17EB0` | Solo-Modulationsparameter | PLAUSIBEL; vollständige Rate-/Depth-Kennlinie offen |
| `soloSplit` | `0x17DE0` | gemeinsame Split-Konfiguration | PLAUSIBEL; Capture/Teilfeld weiter auflösen |
| `soloSplitKey` | `0x17DE0` | gleiche Funktionsimplementierung mit anderem Binding | PLAUSIBEL; gleiche Adresse bedeutet nicht gleiche Bedeutung |
| `soloAttack` | `0x17D50` | u.a. eigener Zeit-/Koeffizientenfall und Tabelle→S+0x18 | PLAUSIBEL; vollständige piecewise Kurve offen |
| `soloRelease` | `0x17D00` | Tabelle→S+0x1A | PLAUSIBEL; UI-Sekunden offen |
| `effectAnimate` | `0x17CB0`→`0x262130` | +0x1370, +0x136C; Dirtyflag; Formungssteuerung | PLAUSIBEL; kein eigener Delay-/Pitchshifter allein daraus ableitbar |
| `effectPhaser` | `0x17C30` | piecewise Integerabbildung→+0x11D38; Verbraucher 0x265F40 | BESTÄTIGT lokale Verbindung; UI-Einheit offen |
| `effectReverb` | `0x17BE0`→`0x265C20` | globaler Reverbzustand +0x1BF0 | BESTÄTIGT lokale Verbindung; Wet-/Decay-Kennlinie offen |
| `transpose` | Binding im Konstruktor | Tonhöhen-/Eventabbildung | OFFEN vollständige Bindung und Grenzen |
| `bendRange` | Binding im Konstruktor | MIDI-Bend-Skalierung | OFFEN vollständige Bindung und Einheiten |
| `modwheel` | Binding im Konstruktor | MIDI-/Parameterpfad | OFFEN vollständiger Verbraucher |

Für die Ensemble-Gainwörter treten die Bitmuster der signed16-Werte
−32768 (aus), −24576, −20480, −26624 auf. Das ist **keine** fertig
nachgewiesene Dry/Wet-Prozentabbildung: später werden weitere Faktoren und
eine vorzeichenverändernde Ausgangskonvertierung angewendet. Die Modus-
Gainwörter müssen zusammen mit dem Gesamtpfad bewertet werden.

Für Animate lautet ein lokaler Teil der nativen Abbildung bei `0x262154`:
`v = 128·(u²+640)`. Weitere Zweige ändern Vorzeichen/Offsets der Tiefe,
schalten die Steuerung aus und setzen Dirtyflags. `u` ist dabei der
Bytewert des Callbacks, nicht automatisch ein normierter Hostwert.
Für Phaser gibt es mehrere affine Integerbereiche statt einer belegten
einfachen Hz-Abbildung. Diese Teilbeziehungen sind keine vollständigen
Parameterkurven.

Noch zu schließen: numerische VST3-ParamIDs (inklusive JUCE-ID-Konvention),
Registry-Range/Step/Default, Floatnormalisierung, Automationspunkte im Block,
Preset-/State-Persistenz, alle Smoothingkonstanten. UI-Prozent, Sekunden,
Hz und interne Integerwerte bleiben getrennte Größen.
