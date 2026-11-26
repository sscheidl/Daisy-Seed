# Performance-Optimierungen und Vergleich — 27.10.2026

Nachtrag: Der spätere [A/B-Hardwarelauf vom 27.10.](HARDWARE_RESULTS_2026-10-27.md)
hat CPU-Gewinne gemessen. ARM-Render-Hashes und Hörprüfung bleiben NICHT GEMESSEN.
Die folgenden Umsetzungsnotizen halten den Stand vor dieser Messung fest.

Die Optimierungen verändern weder Stimmenzahl, Resonanzmoden, Abtastrate noch
Hallstruktur. Die vorhandenen Golden-Prüfwerte werden nicht angepasst.
**CPU-Gewinn, ARM-Render-Hashes und Hörprüfung: NICHT GEMESSEN.**
Es wurde keine Hardware angesprochen oder geflasht.

## Umgesetzte Änderungen

| Änderung | Einstellung | Abgrenzung |
| --- | --- | --- |
| Schneller FM-Phasenwortpfad | `PM_OPTIMIZE=1` (Standard) | Direkte Konvertierung nur innerhalb des gültigen uint32-Bereichs; bisheriger int64-Pfad an der Obergrenze und bei negativen Werten |
| Gemeinsame FM-Konstanten | ebenfalls | Bitmuster des tatsächlichen Damping-Werts wird je Stimme geprüft; individuelles MOD-Damping bleibt individuell |
| Resonator-Koeffizientencache | ebenfalls | Frequenz, Structure, quadriertes Brightness, Damping, Auflösung und Modenzahl; Invalidierung bei Init |
| Saitenschleife | ebenfalls | Konstanter Kehrwert außerhalb der Schleife, lokale Kopien der SRC-/Dispersionszustände mit abschließendem Zurückschreiben |
| AXI-Speicher | `PM_MEMORY=axi` | Eigene NOLOAD-Sektion hinter dem vollständigen Image; explizite Initialisierung; Standard bleibt SDRAM |
| Schleifenoptimierung | `PM_LOOP_INVARIANTS=1` | Nur Anwendungsquellen; SDK und andere Projekte unverändert; standardmäßig noch ausgeschaltet |
| Abschnittsmessung | `PM_PROFILE=1` | DWT-Zyklen, ausschließlich in Diagnose-Builds |
| ARM-Vergleichsrender | zusätzlich `PM_RENDER_HASH=1` | Deterministischer Korpus vor dem Audiostart; kein Klang wird dabei ausgegeben |

`PM_OPTIMIZE=0` erhält einen Referenzpfad für direkte Vergleiche.
Saitenfilter-Interpolation, LFOs, Glide, Zufallsfolge und laufende Filterzustände
werden nicht zwischengespeichert. LTO ist nach der Zweitprüfung zurückgestellt.
Der Mischpfad wird zunächst gemessen, nicht vorsorglich umgebaut.

Die Einstellungen erhalten getrennte Objektverzeichnisse. Beim Wechsel eines
Schalters werden keine Objekte einer anderen Variante wiederverwendet.
Versionen, Release-Workflows und die Zahl von vier DSP-Stimmen bleiben unverändert.

## Speicher und Grenzen

Das lokale Linkerskript `ld/physical_modeling_axi.lds` basiert auf dem gepinnten
SDK-Skript. Es reserviert `synth_storage` und `lut_sine` in `.axi_bss`
hinter der Ladeadresse der initialisierten Daten. Das SDK wird nicht verändert.

Der ELF-Checker prüft zusätzlich Sektionen und Symbolgrößen, die Trennung von
BIN/Image und DSP-Daten, die Lage des D2-Heaps und die vollständige Sinustabelle.
Mindestens 8 KiB Reserve im DTCM sowie bei AXI im verbleibenden Imagebereich
werden verlangt. Die bisherige pauschale SDRAM-Annahme ist durch Objektprüfungen
ersetzt. Negative Tests verwerfen verschobene DSP-Objekte, verkürzte LUTs,
Imageüberlappung und einen in AXI verschobenen Heap.

**GEMESSEN (ELF, 27.10.2026):** Standard, MOD und COMPOSED/Torus passen in AXI.
COMPOSED/Ambient belegt rund 1,26 MB DSP-Speicher und bleibt im SDRAM.
Eine vollständige AXI-Platzierung für diese Variante wird mit einer verständlichen
Buildfehlermeldung abgelehnt; die größere Effekt-Engine wird nicht verkleinert.
Die konkreten Größen und Prüfsummen sind den lokal gebauten ELF-/MAP-Dateien
zu entnehmen. Statische Reserven sind keine Stack-Hochwassermessung.

## Lokale Prüfungen

**GEMESSEN (Host, 27.10.2026):** Der neue Vergleichskorpus rendert je Variante
2.304.000 Samples (24 Sekunden Stereo über sechs Modelle). Referenz und
Optimierung werden mit demselben Compiler gebaut und Sample für Sample verglichen.
Standard und MOD sind dabei exakt gleich zur jeweiligen Referenz, auch mit
unterschiedlichen Parametern pro Stimme. Geprüft werden Stillstand, zwei bewegte
Regler, acht automatisierte Regler, Ausklingen und legale MIDI-Grenzfälle.
Dies belegt die getesteten Abläufe, nicht jede mögliche Eingabefolge.

ASan, UBSan und float-cast-overflow prüfen beide Pfade zusätzlich. Der Profiler
wird mit synthetischer Uhr auf Wrap, Sättigung, Anfragen, Paketformat,
IRQ/Main-Trennung und fehlende USB-Verbindung geprüft. Python-Tests prüfen
64-Bit-Grenzen, beschädigte Nachrichten und erneute Synchronisierung.

Der bestehende feste Golden-Hash wurde für Linux/GCC aufgezeichnet. Er schlägt
unter dem hier vorhandenen Windows/Clang bereits vor diesen Änderungen fehl.
Er wird deshalb nicht ersetzt oder gelockert: Der unveränderte Linux-Golden
bleibt Pflicht; der neue Referenzvergleich ergänzt die lokale Prüfung.

```sh
make -C instruments/physical-modeling test-performance HOST_CXX=clang++ HOST_PYTHON=python
make -C instruments/physical-modeling test-performance HOST_CXX=clang++ HOST_PYTHON=python HOST_FLAGS="-std=c++17 -O1 -g -pthread -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all"
make PROJECT=physical-modeling test
make PROJECT=physical-modeling-fx-torus test
make PROJECT=physical-modeling-fx-ambient test
```

Die letzten drei Befehle gehören für den festen Golden-Hash auf die passende
Linux/GCC-Plattform. Es wird kein teurer ARM-CI-Lauf automatisch ausgelöst.

## Vergleichsimages für die menschliche Hardwareprüfung

Beispiel für Standard; für MOD zusätzlich `MOD=1`:

```sh
# Referenz mit denselben Messwerkzeugen:
make -C instruments/physical-modeling PM_OPTIMIZE=0 PM_PROFILE=1 PM_RENDER_HASH=1 -j4
# Zuerst nur Rechenänderungen:
make -C instruments/physical-modeling PM_PROFILE=1 PM_RENDER_HASH=1 -j4
# Danach Speicherplatzierung:
make -C instruments/physical-modeling PM_MEMORY=axi PM_PROFILE=1 PM_RENDER_HASH=1 -j4
# Zuletzt separates Compilerexperiment:
make -C instruments/physical-modeling PM_MEMORY=axi PM_LOOP_INVARIANTS=1 PM_PROFILE=1 PM_RENDER_HASH=1 -j4
```

Für jede ELF-Datei `scripts/check-physical-elf.py` und für die zugehörige BIN
`scripts/check-firmware.py` ausführen. Negative Speicherprüfungen:
`python instruments/physical-modeling/tests/test_elf_layout.py PFAD_ZUR_ELF`.

Der Benutzer entscheidet über Flash und Hörprüfung. Der ARM-Korpus läuft vor
Audiostart/USB-MIDI-Initialisierung; nach dem Einschalten entsprechend warten.
Er benutzt nur Arbeitsbuffer, danach werden Synth und Zufallszustand für das
normale Spiel wiederhergestellt. Sein Hash gilt für den Synth-Korpus, nicht
für ADC, USB-Verkehr oder alle denkbaren Spielsituationen.

Nach bestätigtem Flash, vorhandene MIDI-Abhängigkeiten vorausgesetzt:

```sh
python instruments/physical-modeling/tools/performance_probe.py --input "EINGANG" --output "AUSGANG" --seconds 60 --reset --csv messung.csv
```

Das Werkzeug beobachtet nur, es sendet keine Noten und verändert keine Klangregler.
Es liest Gerätetakt und ARM-Korpus-Hash und speichert Abschnittszähler als CSV.
Ein Hash mit Korpusversion 0 bedeutet: kein ARM-Render ausgeführt.

Für einen aussagekräftigen Vergleich dasselbe Modell, dieselben Parameter und
denselben Spielablauf benutzen. Zunächst unveränderte Regler, anschließend zwei
bewegte Regler beziehungsweise acht MIDI-Automationsspuren. Normale Akkorde,
keine Eventflut. Bei Bedarf zusätzlich die vorhandenen Callback-Messungen mit
`cpu_probe.py` erfassen: Mittelwert, Spitze und Fristüberschreitungen.
Modellwechsel bleiben separat dokumentiert und sind kein Optimierungsziel.

Den vollständigen Ausgangspfad aktiv lassen; extern leise abhören oder stummschalten.
Master=0 würde den nichtlinearen Ausgangspfad aus der Messung nehmen.
Nur Varianten mit passendem ARM-Referenzhash und unauffälligem Hör-/Stabilitätstest
übernehmen. Profiling selbst kostet Zeit; daher identisch instrumentierte Builds
vergleichen. Prozentgewinne dürfen nicht aus Hostlaufzeiten abgeleitet werden.

## Separates Diagnoseformat, nur PM_PROFILE

Das Mod-on-Daisy-SysEx-Protokoll 0x7D und die bisherige CPU-Antwort bleiben unverändert.
Anfrage: Kanal 16, CC119, Wert 16–21 für Voices/Filters/Reverb/Output/Hash/Clock.
Wert 22 setzt die Abschnittszähler zurück und liefert Voices nach dem nächsten Block.
Nur ein Auftrag ist gleichzeitig ausstehend; der IRQ publiziert einen kohärenten
SPSC-Snapshot, USB-Ausgabe erfolgt ausschließlich in der Hauptschleife.

Antwort: Kanal 16, fortlaufende CC70–98, Werte als Base-128-Ziffern, niedrigste zuerst:
70=Version 1, 71=Abschnitt, 72–76=Aufrufe (uint32), 77–86=Zyklensumme (uint64),
87–91=Maximum (uint32), 92–96=Blöcke (uint32), 97=0, 98=Abschluss 64.
Bei Hash enthält die Summe den Hash und Aufrufe die Korpusversion (0 oder 1).
Bei Clock enthält die Summe den tatsächlichen Gerätetakt in Hz.
Filters liegt innerhalb von Voices: Diese Zeiten nicht zusammenaddieren.
