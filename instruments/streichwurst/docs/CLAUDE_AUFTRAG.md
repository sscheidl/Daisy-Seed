# Arbeitsauftrag an Claude: Streichwurst auf Seed3 Desktop

## Ziel und Autorisierung

Der Nutzer beauftragt ein neues Instrument **Streichwurst** im bestehenden
Repository `sscheidl/Daisy-Seed`, Verzeichnis `instruments/streichwurst/`.
Zielhardware ist das **Seed3 Desktop Development Kit**. Bedienung am Waldorf
Streichfett orientiert, Klang möglichst originalgetreu. Eigenständiger Code,
eigene mathematisch erzeugte Daten; kein Herstellerport.

Dieser Auftrag erlaubt Implementierung, gezielte lokale Softwaretests und
lokale ARM-Builds. Stringsektion und Ensemble bilden den ersten vollständigen
Signalpfad; danach Solo und weitere FX ergänzen. Führe die Phasen innerhalb
dieses Umfangs ohne wiederholte Routinefreigaben aus. Dokumentiere technisch
offene Punkte und arbeite an unabhängig lösbaren Modulen weiter. Kein Flashen,
Gerätezugriff, Originalpluginbetrieb oder Release. Nach dem geprüften
Firmwareprototyp mit dokumentierten Grenzen endet dieser Auftrag.

## Zuerst lesen

* Repository: [AGENTS.md](../../../AGENTS.md), [CLAUDE.md](../../../CLAUDE.md),
  [HANDOVER.md](../../../HANDOVER.md), [BUILD.md](../../../docs/BUILD.md),
  [HARDWARE.md](../../../docs/HARDWARE.md),
  [gemeinsamer Build](../../../common/desktop.mk),
  [Bootloader-Einstieg](../../../common/bootloader/README.md).
* Projekt: [Regeln](../AGENTS.md), [Herkunft und Befundgrade](PROVENANCE.md),
  [Bedienentwurf](CONTROLS.md), [Prüfstatus](VALIDATION.md).
* Forschung, in dieser Reihenfolge:
  [Audio und Stimmen](research/audio-and-voices.md),
  [gezielte Vertiefung](research/focused-findings.md),
  [Ensemble](research/ensemble.md), [Parameter](research/parameters.md),
  [historischer Entwurf 0.2](research/daisy-dsp-draft-0.2.md),
  [Ressourcenannahmen](research/daisy-budget.md).

Die neueren SF-F-Befunde präzisieren ältere offene Einträge im selben Snapshot:
insbesondere Crescendo/Release, Oktavtransformation, Balance, Ensemblegain und
Bypass. Nicht die älteren OFFEN-Vermerke ungeprüft als letzten Stand übernehmen.

## Leitlinie für Klangtreue

Der vorhandene Entwurf ist keine fertige Streichfett-Nachbildung. Er verwendet
vier eigene Spektralfamilien statt acht Originalgewichten, eine eigene
Stringquelle, Soloersatz, vereinfachte Formung, eigene FX und Pegelnormierung.
Übernimm diese Entscheidungen nicht stillschweigend als Originalverfahren.

Erstelle `docs/FIDELITY.md` mit einer Zeile pro Modul: belegte Beziehung,
implementiertes Verfahren, Abweichung, erwartete Klangfolge, Test und offene
Referenzfrage. Bei fehlenden Originalbeziehungen die bestbegründete eigene
Annäherung bauen und austauschbar halten. Keine erfundene UI-Kennlinie und
keine Herstellerdaten nachgenerieren, als wären sie bekannt.

Besonders wichtig:

* String: gemeinsame frei laufende Tonklassenphasen, Hüllkurven-/Zieltrennung,
  Registrierung/Oktaven und spektrale Formung. Das belegte Original benutzt
  Ereigniskern plus Leckintegration; eine gewöhnliche Saw-Wavetable ist eine
  Annäherung. Die vollständigen Ereignisamplituden sind noch offen.
* Ensemble: eine gemeinsame Leitung der Stringsumme, Modustopologie 3/5/3
  Taps, parabolische LFOs, belegte Phasen-/Zeitbeziehungen und Stereokorrektur
  bevorzugen. Ein beliebiger DaisySP-Chorus erfüllt diese Anforderung nicht.
* Original-Fs 40 kHz/Hop16 und eigener Hardwareausgang 48 kHz sauber trennen.
  Vergleiche einen intern mit 40 kHz laufenden Kern samt selbst konstruiertem
  Resampler mit einer zeitskalierten 48-kHz-Fassung, soweit lokal sinnvoll.
  Auswahl samt Alias-/CPU-/Klangfolgen dokumentieren; keinen Fremdresampler kopieren.
* Originalgain greift nach Solo und Reverb. Eigene DC-Normierung, Limiter und
  weiterlaufender Bypass verändern das Verhalten. Native relative Gainfaktoren
  und Balance sind inzwischen bekannt; vollständige Ausgangsdynamik nicht.
* Phaser sitzt im gemeinsamen Ensemble-Summenarm vor den Stereo-Tapkorrekturen.
  Solo kommt danach hinzu; Reverb verarbeitet die spätere Gesamtmischung.
* Solo-Wellenbildung, Animate, Phaser und Reverb sind teilweise ungeklärt.
  Eigene Ersatzstufen als solche ausweisen; Dry-/Bypasswege müssen separat
  testbar sein. Das Ziel Klangtreue gilt weiterhin auch für diese Stufen.

## Phase 1: Portabler Kern und erster Klangpfad

1. C++17-DSP ohne libDaisy-/HAL-Abhängigkeit unter `include/streichwurst/`.
   Definierte `Prepare`, `Reset`, Parameter-, Event- und Stereo-Renderverträge.
   Keine Allokationen, Locks, Dateizugriffe oder blockierende I/O beim Rendern.
2. Note-on/off, Velocity-null-Off, Wiederholnoten, Sustain, Pitchbend,
   AllNotesOff und AllSoundOff implementieren. Kanalzuordnung explizit;
   gleiche Eventzeit behält Eingangsreihenfolge. Überläufe lösen keine
   hängenden Noten aus. NaN/Inf und ungültige Eingaben begrenzt behandeln.
3. Stringquelle mit eigenen bandbegrenzten Daten und nachvollziehbarem
   Spektralmodell; Register, Oktaven, Crescendo und Release. Maximalbelegung
   und Steal-/Auswahlregel deterministisch festlegen. Die bisher geplanten
   16 Tonlagen sind ein Startbudget, keine bestätigte Originalpolyphonie.
4. Ensemble samt Stereo, Bypass und Moduswechsel. Quantisierung/Blockreader
   sind im Original teilweise offen; die eigene Interpolation ausdrücklich
   benennen. Hüllkurven und Modulation müssen unabhängig von Hostblockgrenzen
   laufen. Pegel bei Moduswechsel und Monosumme prüfen.
5. Hosttests und eigene Offline-Testsignale. Das beigefügte Pythonmodell
   zeigt funktionale Teilbeziehungen; seine Signale sind keine Originalreferenz.

## Phase 2: Seed3-Firmware und Bedienung

* `src/main.cpp` mit `daisy::desktop_devkit::Hardware`, Stereoausgang und
  48 kHz/48 Frames als Startkonfiguration. Keine erfundenen GPIOs.
* Bestehenden offiziellen Desktopadapter, Parameterübergabe, MIDIqueues und
  Bootloader-Einstieg nach Lizenz-/Abhängigkeitsprüfung nutzen. Kein Toy-Piano-
  oder Physical-Modeling-DSP als Ersatzinstrument einbauen.
* USB MIDI über J7 zuerst; DIN nur bei tatsächlich geprüfter API und gezielten
  Tests ergänzen. Keine blockierenden MIDI-/LED-Aufrufe im Audiocallback.
* [Bedienentwurf](CONTROLS.md) implementieren und als eigene Adaption
  dokumentieren. Soft takeover bei Seitenwechsel; sichere Preset-/Resetlogik.
  Kein Flash-/QSPI-Schreiben. Presets zunächst nur im RAM bzw. als eigene Defaults.
* Echte Projekt-Makefile, eindeutige Firmwareversion und Zielname
  `Streichwurst-v<version>`. Include `../../common/desktop.mk`, Bibliothekspins
  erhalten. `make PROJECT=streichwurst` muss dieses Instrument bauen.

## Phase 3: Solo, Formung und FX

* Acht Soloobjekte als Startvertrag mit dokumentierter eigener Allokation,
  Hüllkurven, Timbre/Morph, Sustain und Tremolo. Split/Layer und Balance.
* Animate/Formung, Phaser und globalen Reverb ergänzen. Begründete eigene
  Algorithmen dort, wo die Referenz offen ist; Modulgrenzen erlauben spätere
  Präzisierungen. Lizenzgeprüfte vorhandene FX nur gezielt einsetzen und
  ihre Klangabweichung benennen. Kein Shimmer als vermeintlichen Streichfett-FX ableiten.
* Gain/Headroom, Wet/Dry, Pan und Ausgangsbegrenzung konsistent spezifizieren.
  Alle Stufen isoliert und zusammen prüfen, dann die Produktfassung dokumentieren.

## Abnahme und GitHub

* Befehl, Toolchain, Ergebnis und Grenzen in `docs/VALIDATION.md` eintragen.
  Testminimum: Phase/Tonhöhe/Bandgrenzen, Hüllkurvenzeit, Ereignisfolgen,
  Sustain/Retrigger/Steal, Queuegrenzen, variable/Null-Blöcke, Impuls/DC,
  Stereo/Mono, Modus-/Bypass-/Parameterwechsel und langes Rendern ohne NaN.
* Eigene Goldenwerte erst nach erklärtem Verhalten festschreiben; keine
  Originalgleichheit aus einem Goldenhash ableiten. Sanitizer und bei
  eigener nebenläufiger Queue passende Threadtests ausführen.
* ARM tatsächlich crosskompilieren, Version/Imageprüfung erweitern, Mapdatei
  auf DTCM/AXI/SDRAM/Stackreserve prüfen. Ziel Audio ≤60 % ist ungemessen.
  Keine M7-Last aus Python- oder Desktoplaufzeit behaupten.
* Erst danach `projects.mk` auf buildable setzen, Komponentenmanifest und
  gezielte CI-Testauswahl ergänzen. Keine breite automatische Buildmatrix,
  keine manuelle Actions-Ausführung oder Veröffentlichung starten.
* Bestehende Instrumente unverändert lassen. Bei Änderungen gemeinsam genutzter
  Quellen die tatsächlichen Verbraucher testen; keine Goldens beiläufig ändern.
* Arbeite auf `claude/streichwurst` und aktualisiere den vorbereiteten PR.
  Keine automatische Integration. Keine Binärdateien oder Originaldaten pushen.
  Firmware-/Audio-Buildausgaben bleiben lokal unter ignoriertem `build/`.
* Übergabe: Quellstand, reproduzierbare Befehle, Test-/Buildresultate,
  Ressourcen, Fidelity-Lücken, Bedienung und Hardware-Laufzettel. Hardware und
  Originalklangvergleich ausdrücklich **NICHT GEMESSEN**, bis real berichtet.

## Starttext

```text
Arbeite im vorbereiteten Daisy-Seed-Worktree auf Branch claude/streichwurst.
Lies AGENTS.md und instruments/streichwurst/CLAUDE.md. Führe anschließend
instruments/streichwurst/docs/CLAUDE_AUFTRAG.md vollständig aus.
Neues Instrument: Streichwurst, Ziel Seed3 Desktop Development Kit,
Klang möglichst originalgetreu, Bedienung am Streichfett orientiert.
```
