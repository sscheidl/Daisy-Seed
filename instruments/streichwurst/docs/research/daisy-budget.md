> Forschungssnapshot vom 01.11.2026. Links für diese Übergabe angepasst;
> historische Aussagen und eigene Ersatzverfahren beachten. Siehe [Herkunft](../PROVENANCE.md).

# Daisy: Annahmen und Ressourcen einschließlich Ensemble

01.11.2026. Ausschließlich lokale Bestandsunterlagen, keine neue
Hardware-/Internetprüfung. [Eigenständiger Entwurf](daisy-dsp-draft-0.2.md).

Die vorhandene [Tulip-/Daisy-Studie](../PROVENANCE.md)
plant Daisy Seed3/M7, 480 MHz, 64 MB SDRAM, 48 kHz Float32 Stereo,
48 Frames pro Callback und höchstens 60 % Gesamt-Audiolast als Ziel.
**Zielhardware laut Nutzer:** Desktop-Board des Daisy Seed 3.
Diese Angabe konkretisiert das Trägerboard; Revision, Schaltplan,
Bedienelemente und Pinbelegung sind noch nicht geprüft. Die Bedienung
soll sich an der Waldorf-Streichfett-Hardware orientieren; Klangtreue
hat Vorrang bei der weiteren DSP-Ausarbeitung.
**OFFEN:** genaue Boardrevision, libDaisy-Version, Linker-
und Cachekonfiguration, Audioformat und tatsächliche Blockgröße dieses
Streichwurst-Geräts. Die Studienwerte werden als Planungsannahmen übernommen,
nicht als festgestellte Hardwareausstattung oder Leistungszusage.

## Speicherrechnung des eigenen Entwurfs

| Zustand | Rechnung | Nutzdaten |
| --- | --- | --- |
| String-Notenverwaltung | 128×32 Byte Planstruktur | 4096 Byte |
| kanalweise Tastenzähler | 16×128×uint16 | 4096 Byte |
| zwölf Grundphasen/-inkremente | 12×2×Float64 | 192 Byte |
| Solo | 8×128 Byte Planstruktur | 1024 Byte |
| Ensembleleitung | (ceil(512×48000/40000)+2)×Float32 | **2468 Byte** |
| Ensemble-LFO/Smoothing/Metadaten | Planreserve | 256 Byte |
| Tap-/Mischscratch | 2×48×Float32 | 384 Byte |
| Phaser/Filter/Smoothing | Planreserve | 1024 Byte |
| eigener Vierzweig-Reverb | (1426+1781+1973+2098)×Float32 | 29.112 Byte |
| Eventqueue | 256×24 Byte | 6144 Byte |
| doppelt gepufferte Parameter | Planreserve | 2048 Byte |
| Audiopuffer | 4×48×Float32 | 768 Byte |
| eigene Wavebanks | 4×8×1025×Float32 | 131.200 Byte |

Summe dieser Planpositionen **182.812 Byte**. Kein `sizeof` eines
implementierten Programms: Alignment, Allocator, Stack, DMA, Codec,
Bibliotheks-/GUIzustände und Sicherheitsreserven kommen hinzu.
Wavebanks für SDRAM; heiße Phasen, Gates, Mixer, kleine Effektpuffer
nach tatsächlichem Linker-/RAMbudget in geeigneten schnellen Speicher.
Bei Modusüberblendung zusätzlich ein zweiter Ensemblezustand:
mindestens 2724 Byte plus Scratch und temporär doppelte Effektlast.

Die eigene Reverbwahl ist kleiner als der vorgefundene Originalring
von 65.536 Byte. Das ist eine bewusste Topologie-/Klangabweichung,
keine Optimierung eines vollständig verstandenen Originalreverbs.
Der Original-Engine-Feldraum reicht über E+0x11Dxx hinaus; dessen
Desktop-Objektgröße ist kein Mindestbedarf eines eigenen Nachbaus.

## Rechenbudget als Schätzung

480 MHz / 48 kHz ergibt 10.000 CPU-Zyklen pro Frame. Bei 48 Frames
sind 480.000 Zyklen/Callback verfügbar, Planungsziel Audio ≤288.000.
Das ist eine Deadline-Rechnung, kein gemessener Daisy-Benchmark.

Für einen ersten **eigenen Active-Limit-Versuch mit 16 String-Tonlagen**
(eine Lage je Taste, bei beiden Oktaven zwei und damit zunächst bis zu
acht Tasten im vollen Doppelregister):
vier Klangfarben und zwei mip-Stufen benötigen bis zu 128 lineare
Table-Interpolationen pro Frame (je zwei Speicherlesevorgänge),
also 12,288 Mio. Samplelesevorgänge/s, ungefähr 49,2 MB/s Floatnutzdaten.
Cache- und SDRAMverhalten können diesen Ansatz begrenzen. Phasen-Sharing
beseitigt nicht automatisch die Kosten jeder Noten-/Registermischung.

Grobe Zielkorridore für optimierten C++-Code, ausdrücklich ungemessen:
Quelle/Envelopes/Mix/acht Soloobjekte 1500..3500 Zyklen/Frame,
Ensemble mit bis zu fünf Taps 150..450, übrige Filter und Reverb
800..1800. Summe 2450..5750 vor Peripherie und Wechselspitzen.
Die Spannweiten sind Engineering-Annahmen, keine aus Pythonlaufzeit
oder Desktop-SIMD abgeleiteten M7-Messwerte. Der obere Rand lässt kaum
Reserve innerhalb des 60-%-Ziels; deshalb 16 nicht als zugesicherte
Polyphonie veröffentlichen. 128 logische Gates sind kein 128-Noten-CPUversprechen.

Bei späterer Implementierung: Compiler/Flags, Board, Fs, Callbackgröße,
Cachezustand, maximale Belegung, MIDIbursts, Moduswechsel und reale
Peripherielast protokollieren. Maximalzeit statt bloß Durchschnitt messen.
Danach Active-Limit und Qualitätsstufen festlegen. Keine Übertragung
von Desktop-Threading/SIMD auf den einzelnen M7 ohne Messung.

Fremdbibliotheken sind nicht importiert. Ein späterer Einsatz von DaisySP,
libDaisy oder anderen Implementierungen braucht genaue Revision und
Lizenzprüfung. Crema/Protein und Iridium bleiben getrennte Referenzen.
