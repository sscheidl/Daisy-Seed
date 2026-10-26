# Arbeitsauftrag: Acid Seq

16-Step-Sequencer nach dem Vorbild KORG SQ-1 mit TB-303-Stimme, Drive und
Ping-Pong-Delay für das Seed3 Desktop Dev Kit.

* **Stand:** 2026-10-26, Entwurf zur Gegenprüfung durch Codex
  ([Review-Auftrag](CODEX_REVIEW_BRIEF.md)). Es gibt noch keinen Code.
* **Basis:** `main` @ `1eb091c`, libDaisy `f715e487`, DaisySP `2c72eaf`,
  xPack GCC 13.2.1-1.1, JUCE 9.0.4. Diese Versionen werden nicht angehoben.
* **Herkunft:** Die Eckdaten wurden mit dem Benutzer am 2026-10-26 im Gespräch
  festgelegt. Abschnitt 6 listet, was noch offen ist.

## 1. Ziel

Ein eigenständiges Produkt `acid-seq` entsteht aus vier wiederverwendbaren
Komponenten. Jede wird zuerst einzeln entwickelt, getestet und als eigene
Firmware auf der Hardware abgenommen:

| Komponente | Zweck |
| --- | --- |
| Sequencer-Kern | 2 × 8 Steps wie beim SQ-1. Ausgabe über CV/Gate und MIDI, mit eigenen Abspielmodi |
| TB-303-Stimme | Monophon, Säge oder Rechteck, Ladder-Filter mit Selbstoszillation, Accent, Slide |
| Drive | Sättigung mit den Kurven aus Tape FX und automatischer Pegelanpassung |
| Ping-Pong-Delay | Stereo-Delay mit Clock-Sync, Feedback, Dry/Wet und Dämpfung |

Die Hardware-Bedienelemente des fertigen Produkts gehören allein dem
Sequencer. Stimme und Effekte werden über das Control Panel (Web, JUCE, VST3)
und MIDI CC gesteuert. Wer die interne Stimme nicht braucht, schließt den
Audioausgang nicht an und nutzt CV/Gate oder MIDI.

## 2. Verbindliche Regeln

Es gilt [AGENTS.md](../../AGENTS.md). Für dieses Projekt heißt das besonders:

* **Hardware gehört dem Benutzer.** Der Agent flasht nicht und führt weder
  `program-dfu` noch `program-boot` aus. Hörtests, Messungen und das Patchen von
  Kabeln macht der Benutzer, der Agent trägt seine Rückmeldungen ein.
  Ergebnisse stehen als `GEMESSEN` (mit Datum) oder `NICHT GEMESSEN` im Dokument.
* **Jede Phase bekommt einen eigenen Branch und einen eigenen PR.** Im PR
  stehen die ausgeführten Kommandos, ihre Ergebnisse und was auf der Hardware
  noch offen ist.
* **Bestehender Klang bleibt unverändert.** Die Golden-Hashes von Toy Piano und
  Physical Modeling sowie die Referenztests von Tape FX dürfen sich nicht
  ändern. Das betrifft vor allem Phase 3, in der die Sättigung ausgelagert wird.
* **Echtzeit:** Im Audio-Callback gibt es keine Speicheranforderung, keine
  Sperren und keine blockierenden Aufrufe. MIDI wird nur aus `main` gesendet
  (`SendMessage` kann bis zu 3 × 100 µs blockieren).
* **Kein Schreiben ins Flash** ohne ausdrückliche Freigabe des Benutzers.
* **Das Mod-on-Daisy-Protokoll (`7D 4D 44`) bleibt unverändert.** Die
  Kommunikation dieses Projekts bekommt einen eigenen Kopf, siehe 4.7 und D1.
* **Jede Firmware enthält den Bootloader-Einstieg** aus `common/bootloader`:
  S9 + S16 für 5 s oder die SysEx-Nachricht. Keine Bediengeste dieses Projekts
  darf ihn versehentlich auslösen.
* **Sprache:** Code und Code-Kommentare auf Englisch. Benutzerdokumentation,
  Panel-Texte und Hardware-Testpläne auf Deutsch.
* **CI-Budget:** Automatisch laufen nur gezielte Host-Tests in `quick.yml`.
  Firmware-, Editor- und Release-Workflows startet nur der Benutzer.
  Neue Testschritte in `quick.yml` gibt der Benutzer frei (Phase 0).

## 3. Architektur

### 3.1 Komponenten und Orte

| Komponenten-ID | Ort | `kind` | Eigene Firmware (Projekt-ID) |
| --- | --- | --- | --- |
| `step-seq` | `sequencers/step-seq/` (neue Rubrik) | `sequencer` (neu) | `step-seq` |
| `acid-303` | `instruments/acid-303/` | `instrument` | `acid-303` |
| `saturation` | `common/dsp/saturation.h` | `dsp` (neu) | keine |
| `drive` | `effects/drive/` | `effect` | `drive-fx` |
| `ping-pong-delay` | `effects/ping-pong-delay/` | `effect` | `delay-fx` |
| Panel-Variante | `editors/mod-on-daisy/` (Web, JUCE, VST3) | `panel` | keine |
| Produkt `acid-seq` | `sequencers/acid-seq/` | Produkt | `acid-seq` |

* `common/desktop.mk` erwartet Projekte zwei Ebenen unter dem Repo-Wurzelverzeichnis.
  `sequencers/<id>` passt dazu.
* Jede Komponente hat einen eigenen `src/version.h` (SemVer, Start `0.1.0`) und
  einen Eintrag in `components/registry.json`. Jede Firmware ist dort als Produkt
  eingetragen, mit Image, Marker, Komponenten und Panels.

### 3.2 Schichten

```
Hardware-Adapter (src/main.cpp je Produkt): libDaisy, Tasten, Potis, LEDs,
DAC, Gate/CV, USB-/DIN-MIDI, Bootloader-Einstieg, Callback-Diagnose
        │  normalisierte Werte, Ereignisse
Kerne (header-only, ohne libDaisy, auf dem Host testbar):
  step-seq::Engine ──Noten/Gates──► acid303::Voice ─► Drive ─► PingPongDelay ─► Host-Mixer/Limiter
        └──► CV-/Gate-Werte, MIDI-Ereignisse (Lock-freie Queue nach main)
```

* **Kerne** kennen keine Hardware. Sie haben `Prepare` und `Process` bzw.
  `Tick` und normalisierte Parameter, wie Mod on Daisy und Tape FX.
* **Effekte** erfüllen den FX-Vertrag 1.0 (`common/audio/fx_contract.h`): Sie
  geben nur das bearbeitete Signal aus (wet-only). Den Speicher stellt der Host,
  Dry/Wet-Mischung, Überblendungen und der Ausgangsschutz liegen beim Host.
* **Sequencer-Zeitbasis:** Sie läuft im Audio-Callback. Mit 48 Frames bei
  48 kHz ergibt das eine Auflösung von 1 ms. Clock-Flanken an Gate IN 1 werden
  in diesem Raster abgetastet.
* **CV OUT:** wird im Audio-Callback über `hw.SetDacOut` (DAC im Polling-Modus,
  ein reiner Registerzugriff) geschrieben, damit Gates genau im Takt kommen.
  Die Diagnose-Firmware schreibt den DAC bisher nur aus `main`. Der Reviewer
  soll prüfen, ob der Aufruf im Callback zulässig ist.
* **MIDI-Ereignisse** gehen über eine Lock-freie Queue mit fester Größe an
  `main`. Läuft sie über, wird das gezählt und gemeldet. Note-Offs haben dabei
  Vorrang, damit keine Noten hängen bleiben.

## 4. Eckdaten

### 4.1 Bedienung am Kit (Sequencer und Produkt `acid-seq`)

| Element | Funktion |
| --- | --- |
| S1–S8, S9–S16 | Steps A1–A8 und B1–B8. **Drücken** wählt den Step. **Lang drücken** (≥ 400 ms) schaltet sein Gate um. Ausgewertet wird das beim Loslassen und nur, wenn währenddessen keine andere Taste gedrückt wurde. Mehrtastengesten (D2) schalten so kein Gate |
| LEDs D1–D16 | Gate an: hell. Gate aus: dunkel. Lauflicht: kurzes Aufblitzen bzw. Abdunkeln. Gewählter Step: schnelles Blinken |
| VR1 | Oktave des gewählten Steps (5 Stufen) |
| VR2 | Halbton des gewählten Steps (12 Stufen) |
| VR3 | Gate-Länge des gewählten Steps (5–100 % der Step-Dauer) |
| VR4 | Accent und Slide des gewählten Steps: nichts, Accent, Slide, Accent + Slide |
| VR5 | Tempo, 30–300 BPM, global |
| VR6 | Swing, global (gerade Sechzehntel um 50–75 % verschoben) |
| VR7 | Sequenzlänge 1–16, global (in den Parallel-Modi: Länge der Reihe des gewählten Steps) |
| VR8 | Transponierung ±12 Halbtöne, global |
| SW17 × SW18 | Sechs Modus-Plätze, siehe 4.2 |
| Start/Stop | Offen, siehe D2 |

* **Rasterstufen der Potis:** Hysterese an jeder Stufengrenze, damit Werte
  nicht springen. Die Poti-Glättung von Toy Piano (20 ms, 0,1 % Hysterese) dient
  als Vorlage, siehe D6.
* **Übernahme bei Bewegung:** Nach dem Wechsel auf einen anderen Step ändert
  ein Poti dessen Wert erst, wenn er sich um mehr als 2 % gegenüber der Stellung
  beim Wechsel bewegt hat. Das gilt für VR1–VR4. VR5–VR8 wirken immer.
* **Vorhören:** Bei gestopptem Sequencer gibt jede Änderung an VR1 oder VR2 die
  Note für 150 ms auf CV/Gate, MIDI und die interne Stimme aus.
* **SW18 (Switch3)** wird von `UpdateAllControls` nicht entprellt. Der Adapter
  übernimmt das 20-ms-Mapping, das Nebulae dafür schon verwendet.
* **Bootloader:** S9 und S16 gleichzeitig 5 s gehalten startet ihn. Gewählt wird
  ein Step schon beim Drücken. Deshalb darf keine Geste verlangen, beide Tasten
  lange zu halten.

### 4.2 Abspielmodi (SW17 × SW18 = 6 Plätze, im Editor frei belegbar)

| | SW18 oben | SW18 Mitte | SW18 unten |
| --- | --- | --- | --- |
| **SW17 A** | Vorwärts 16 | Rückwärts 16 | Zufall 16 |
| **SW17 B** | Pendel 16 | Parallel 2 × 8 | Polyrhythmus (A und B mit eigener Länge) |

* **Vorwärts 16 und Rückwärts 16:** A1…A8, B1…B8 und umgekehrt, begrenzt auf die
  Sequenzlänge.
* **Pendel:** läuft hin und zurück, ohne die Endpunkte doppelt zu spielen.
* **Zufall:** gleichverteilt über die aktive Länge. Der Zufallsgenerator
  (xorshift32) hat einen festen Seed, damit Tests reproduzierbar sind.
* **Parallel und Polyrhythmus:**
  * Reihe A wird zu Kanal A (CV OUT 1/2, MIDI-Kanal A, interne Stimme).
  * Reihe B wird zu MIDI-Kanal B, und zu CV OUT 2, wenn die CV-Belegung auf
    „Tonhöhe A + Tonhöhe B“ steht.
* **Weitere Modi** stehen im Editor für die Plätze zur Wahl: Brownian
  (Zufallsschritt −1/0/+1/+2) und Step Jump. Den SQ-1-Modus CV/Duty ersetzt die
  Gate-Länge pro Step, siehe D3.
* **Modus-Wechsel:** Der Schalter gilt. Ein Modus, den der Editor setzt, gilt bis
  zur nächsten Schalterbewegung.
* **Modus-Wechsel während des Spielens:** wird zum nächsten Step übernommen.
  Die Position wird dabei sinnvoll abgebildet, zum Beispiel bleibt bei Vorwärts
  nach Rückwärts der Index erhalten.

### 4.3 Buchsen, Clock und Ausgabe

| Buchse | Grundbelegung | Alternative (Editor) |
| --- | --- | --- |
| CV OUT 1 | Tonhöhe Kanal A, 1V/Oct, mit Slide als Glide | — |
| CV OUT 2 | Gate Kanal A | Tonhöhe Kanal B |
| Gate IN 1 | Clock-Eingang, 1 Puls pro Step | 2, 4 oder 24 Pulse pro Viertel |
| Gate IN 2 | Reset auf die steigende Flanke | Run (läuft, solange das Signal anliegt) |
| CV IN 1 | Transponierung 1V/Oct, zu VR8 addiert | aus |
| CV IN 2 | aus | Gate-Länge, Swing oder Step-Auswahl |

* **Clock-Quelle:** intern, Gate IN 1 oder MIDI-Clock (USB und DIN). „Auto“
  folgt einer externen Quelle, solange innerhalb von 2 s Pulse kommen.
* **Slide:** Der Gate bleibt zum nächsten Step hin offen, die Tonhöhe gleitet
  in konstanter Zeit (Vorschlag 60 ms) auf CV OUT 1 und in der internen Stimme.
  MIDI sendet Slide als überlappende Noten (Legato).
* **Accent:** Bei MIDI eine höhere Anschlagstärke (Vorschlag 127 statt 100), in
  der internen Stimme siehe 4.5. Auf CV gibt es keinen Accent, weil der dritte
  Ausgang fehlt.
* **Skalen:** chromatisch, Dur, Moll, linear (wie beim SQ-1). Sie werden vor
  Transponierung und Ausgabe angewendet.
* **Sicherheit der CV-Ausgänge:** Spannungsbereich und Skalierung von CV OUT
  sind **NICHT GEMESSEN** (Diagnose H14). Bis Kalibrierdaten vorliegen
  (Offset und Skalierung pro Ausgang, Gate-Pegel low/high als DAC-Codes),
  bleiben beide Ausgänge auf einem neutralen Code, und die Firmware gibt keine
  Tonhöhe oder Gates aus. Freigeschaltet werden sie erst im Editor
  („CV aktiv“). Ob der Ausgang unipolar oder bipolar ist, steht noch nicht fest.
  Deshalb sind auch die Gate-Codes einstellbar.

### 4.4 MIDI (USB über J7 und DIN)

* **USB:** `MidiUsbHandler` mit `EXTERNAL`, wie in den bestehenden Firmwares.
* **DIN:** `hw.midi` (`MidiUartHandler`). In diesem Repo nutzt bisher keine
  Firmware DIN-MIDI. Das ist neu und muss auf der Hardware geprüft werden.
* **Ausgang:** Noten von Kanal A und B auf einstellbaren MIDI-Kanälen
  (Vorschlag 1 und 2), Clock 24 PPQN, Start, Stop, Continue. Ausgegeben wird
  auf USB und DIN gleichzeitig, jeder Ausgang ist im Editor abschaltbar.
* **Eingang:** Clock, Start, Stop, Continue. Noten auf dem Kanal der Stimme
  (Vorschlag 3) spielen die interne Stimme. CCs steuern Stimme und Effekte,
  die Tabelle wird in Phase 2 festgelegt.
* **Panic:** All Notes Off beim Stoppen, beim Moduswechsel mit Kanaländerung
  und beim Trennen der USB-Verbindung.

### 4.5 TB-303-Stimme (`acid-303`)

| Teil | Umsetzung |
| --- | --- |
| Oszillator | DaisySP `Oscillator`, `WAVE_POLYBLEP_SAW` oder `WAVE_POLYBLEP_SQUARE` |
| Filter | DaisySP `LadderFilter` (LP24, tanh-Eingangsstufe, stabile Selbstoszillation laut Code). Ein Diode-Ladder wie in der echten 303 bleibt spätere Option, siehe D4 |
| Hüllkurven | Filterhüllkurve mit Env Mod und Decay. Accent verstärkt Filterhüllkurve und Lautstärke und verkürzt das Decay |
| Slide | Glide mit konstanter Zeit. Legato-Noten lösen keine neue Hüllkurve aus |
| Noten | Monophon, die zuletzt gespielte Note hat Vorrang |
| Parameter | Wellenform, Tuning (±12 Halbtöne, fein ±100 Cent), Cutoff, Resonanz, Env Mod, Decay, Accent-Stärke, Lautstärke |
| Glättung | Alle kontinuierlichen Parameter, damit beim Verstellen nichts knackt |
| Ausgang | Mono, im Produkt auf beide Kanäle und den Kopfhörerausgang |

**Eigene Firmware `acid-303`** (nur zum Testen und als eigenständiges Instrument):
* **S1–S16:** chromatisch ab C2.
* **VR1–VR8:** Cutoff, Resonanz, Env Mod, Decay, Accent, Tuning, Lautstärke, Wellenform.
* **SW17:** Accent für die Tasten.
* **SW18:** Slide aus, Slide bei gehaltener Taste (Legato), Slide immer.
* MIDI über USB und DIN, CC wie im Produkt.

### 4.6 Drive (`saturation`, `drive`)

* **Quelle der Kurven:** Soft, Hard, Tape und Fold, von MixForge (drumlogue) nach
  `effects/tape-fx/src/tape_fx.h` portiert (`tape::Saturate*`). Sie werden
  ohne Klangänderung nach `common/dsp/saturation.h` ausgelagert. Der Looper
  enthält keine Sättigung, die Stardust-Firmware ist nur analysiert und
  liefert keinen Code.
* **Parameter:** Ein/Aus, Kurve, Drive, Tone, Level, Auto-Level an/aus,
  Ausgleich 0–100 %.
* **Auto-Level:**
  1. **Feste Ausgleichskurve abhängig vom Drive.** Die Tabelle wird im
     Host-Test mit 303-Referenzsignalen gemessen und als Konstante abgelegt
     (Säge und Rechteck, Cutoff niedrig, mittel und hoch, Resonanz 0, 50 und 90 %).
  2. **Langsame Nachregelung** über den RMS-Pegel vor und nach der Kette, auf
     ±3 dB begrenzt, Zeitkonstante etwa 300 ms. Unter −60 dBFS am Eingang wird
     der Wert gehalten.
  3. **Etwas weniger als voller Ausgleich,** Vorschlag 1,5 dB, weil Verzerrung
     lauter klingt. Der Wert wird im Hörtest bestätigt.
* **Ein/Aus:** überblendet über 10 ms. Das übernimmt der Host, wie im
  FX-Vertrag vorgesehen.
* **Aliasing bei Hard und Fold:** 2-faches Oversampling ist optional, siehe D5.
* **Eigene Firmware `drive-fx`:** bearbeitet die Stereo-Audioeingänge, nach dem
  Muster von `reverb-fx` und `tape-fx`.

### 4.7 Ping-Pong-Delay (`ping-pong-delay`)

* **Parameter:** Ein/Aus, Delay-Zeit, Sync an/aus, Feedback, Dry/Wet, Dämpfung.
* **Delay-Zeit:** frei 10 ms bis 2 s, oder mit Sync ein Notenwert von 1/32 bis
  1/1, auch punktiert und triolisch, bezogen auf das Tempo, das der Host vorgibt.
* **Ping-Pong:** Der Mono-Eingang (L + R) / 2 geht in die linke Leitung. Die
  Rückkopplung wechselt zwischen links und rechts.
* **Feedback:** 0–95 %. Ein Soft-Clip in der Schleife verhindert, dass sie sich
  aufschaukelt.
* **Dämpfung:** Tiefpass erster Ordnung in der Rückkopplung, 20 kHz bis 1 kHz.
* **Zeitänderungen** gleiten mit einer Slew-Rate. Bei Sprüngen ab 10 % wird
  über zwei Lesezeiger überblendet, damit es nicht knackt.
* **Ausschalten:** Das Echo klingt aus (Spillover), danach ist der Eingang stumm.
* **Speicher:** 2 × 96 000 Floats ≈ 768 KB im SDRAM (`DSY_SDRAM_BSS`), vom
  Host gestellt.
* **Eigene Firmware `delay-fx`:** bearbeitet die Audioeingänge. Ohne externe
  Clock gilt das Tempo von VR5.
  `docs/FUTURE_EFFECT_PROGRAMS.md` wünscht ein Stereo-Delay. Das hier ist ein
  Ping-Pong-Delay, siehe D7.

### 4.8 Control Panel und Protokoll

* **Ort:** Panel-Variante im vorhandenen Editor `editors/mod-on-daisy`: Browser,
  JUCE-Standalone und VST3. Session, Transporte und UI-Bausteine werden
  mitgenutzt. Die Matrix-Seiten erscheinen nur bei Firmwares mit Mod-Kern.
* **Protokoll:** eigener SysEx-Kopf `7D 53 51` („SQ“), spezifiziert in
  `sequencers/step-seq/docs/PROTOCOL.md` (Deutsch). Inhalt:
  * **IDENTIFY:** Produkt, Version und Komponentenliste. Das Panel zeigt nur die
    Seiten der vorhandenen Komponenten.
  * **Versionierter Parametervertrag:** Komponente, stabile Parameter-ID, Typ,
    Einheit, Min, Max, Standardwert, CC-Nummer. So empfiehlt es
    `docs/MODULARITY_AUDIT_2026-10-25.md` § 3.
  * **Pattern lesen und schreiben** (alle 16 Steps), Modus-Plätze, Kalibrierung.
  * **Monitor ereignisbasiert:** Step-Wechsel, Gate, Modus, Schalterstellungen,
    Clock-Quelle und Tempo, höchstens 50 Rahmen pro Sekunde.
* **Speichern:** Firmware-Patterns liegen nur im RAM. Das Panel hält Patterns,
  Modus-Plätze und Kalibrierung und sendet sie beim Verbinden ans Kit.
  Im VST3 sind sie Teil des Songzustands. SD folgt später, Flash nur mit
  Freigabe.
* **Werkzeug für Tests:** `sequencers/step-seq/tools/sq_probe.py`, nach dem
  Muster von `mod_probe.py`. Gemeinsame Testvektoren prüfen, dass C++ und
  JavaScript gleich kodieren.

### 4.9 Produkt `acid-seq`

* **Signalweg:** Sequencer → Stimme → Drive → Delay → Host-Mixer → Soft-Limiter
  → Ausgang.
* **Komponenten:** `step-seq`, `acid-303`, `saturation`, `drive`,
  `ping-pong-delay`, `fx-contract`, `bootloader-entry`, `callback-diagnostics`.
* **Version:** eine eigene Produktversion in `composed_version.h`, wie bei
  `physical-modeling-fx-*`.
* Ohne angeschlossenen Audioausgang laufen CV/Gate und MIDI unverändert.

## 5. Phasen

Reihenfolge: 0 → 1 → (2, 3, 4 unabhängig, parallel möglich) → 5 → 6.

Für jede Phase gilt:
* Ein PR mit Host-Tests (`test`, `test-sanitize`), ARM-Build,
  `python scripts/check-firmware.py <bin>`, `make components test-components`
  und einem deutschen Hardware-Testplan unter `docs/HARDWARE_TEST.md` der
  Komponente.
* `test-thread` läuft unter Linux bzw. in CI, weil TSan unter Windows nicht
  verfügbar ist.
* Die Phase gilt als abgeschlossen, wenn der Benutzer den Hardwaretest
  bestätigt hat oder ausdrücklich darauf verzichtet.

### Phase 0: Gerüst und Verträge

* **Ziel:** Struktur, Register und CI für die neuen Orte. Noch keine DSP und
  keine Bedienlogik.
* **Umfang:**
  * `sequencers/README.md` (Rubrik)
  * Ordnergerüste für `step-seq`, `acid-303`, `drive` und `ping-pong-delay`,
    jeweils mit README, `src/version.h`, Makefile und leerem Test
  * `projects.mk` mit Status `planned`
  * `components/registry.json` mit neuen `kind`s `sequencer` und `dsp`.
    `scripts/component_manifest.py:51` lässt bisher nur `instrument`, `effect`,
    `control`, `panel` und `contract` zu. Die Liste und ihre Tests werden
    deshalb erweitert. Die Alternative ist, die vorhandenen Kinds zu nutzen
    (`control` für den Sequencer, `contract` oder `effect` für `saturation`).
    Der Reviewer soll dazu Stellung nehmen.
  * `README.md` (Projektliste)
* **CI (Freigabe durch den Benutzer):**
  * `sequencers/**` in die Pfadfilter von `quick.yml`.
  * `scripts/select_test_suites.py`: neue Suites `stepseq`, `acid303`, `drive`
    und `delay`.
  * `common/dsp/` bekommt eine eigene Verbraucherregel. Bisher wählt jede
    Änderung unter `common/` außer `audio/` und `sd/` alle Suites aus.
  * Die neuen Steps laufen nur, wenn ihre Pfade betroffen sind.
* **Dokumente:** Dieser Auftrag wird nach dem Review zur verbindlichen
  Fassung. Offene Punkte aus Abschnitt 6 werden mit dem Benutzer entschieden.
* **PASS:** `make list` zeigt die neuen IDs. `make components test-components`
  und `python3 scripts/select_test_suites.py` liefern die erwartete Auswahl.
  Bestehende Suites sind unverändert grün.

### Phase 1: Sequencer-Kern und Firmware `step-seq`

* **Ziel:** ein vollständiger Sequencer ohne Audio, nur CV/Gate und MIDI. Dazu
  das SQ-Protokoll mit Pattern- und Parameterteil und `sq_probe.py`.
* **Kern** `sequencers/step-seq/include/step_seq/`:
  * Pattern-Daten
  * Abspielmodi aus 4.2
  * Clock: intern, extern, MIDI
  * Swing, Reset und Run
  * Gate-Länge, Accent und Slide
  * Skalen und Transponierung
  * Ereignisausgabe für Noten, Gates, CV-Werte und MIDI
* **Adapter `src/main.cpp`:**
  * Tasten, LEDs und Potis aus 4.1
  * DAC-Ausgabe
  * Gate- und CV-Eingänge
  * USB- und DIN-MIDI
  * LED-Mailbox nach dem Toy-Piano-Muster (höchstens alle 10 ms)
  * Bootloader-Einstieg und Callback-Diagnose
* **Tests:**
  * Jeder Modus mit erwarteter Step-Folge, auch bei Längen 1, 7, 8, 9 und 16 und
    bei Moduswechsel mitten im Lauf.
  * Swing-Timing auf 1 ms genau.
  * Clock-Teiler, Reset und Run, Auto-Umschaltung der Clock-Quelle.
  * Lange und kurze Tastendrücke, Übernahme bei Bewegung, Hysterese.
  * Bootloader-Geste nur bei S9 + S16 für 5 s.
  * Slide und Legato bei MIDI, Panic, Überlauf der MIDI-Queue mit Vorrang für
    Note-Off.
  * CV-Werte nach Kalibrierung, gesperrte Ausgänge ohne Kalibrierung.
  * Kodierung des SQ-Protokolls mit Testvektoren.
  * Golden-Test: FNV-1a über eine feste Ereignisfolge (Pattern, 64 Steps, alle
    Modi).
* **Hardwaretest (Benutzer):**
  * **Zuerst Diagnose H14:** Spannungen von CV OUT 1/2 bei mehreren DAC-Codes
    mit dem Multimeter messen. Daraus werden die Kalibrierwerte abgeleitet.
  * Tasten und LEDs, alle sechs Modus-Plätze.
  * Gate an einem externen Synth.
  * 1V/Oct über 3 Oktaven mit einem Stimmgerät.
  * Clock an Gate IN 1, Reset an Gate IN 2.
  * MIDI über USB in der DAW, DIN mit einem externen Gerät.
  * Synchronisation mit MIDI-Clock in beide Richtungen.

### Phase 2: TB-303-Stimme und Firmware `acid-303`

* **Ziel:** die Stimme aus 4.5 als header-only Kern
  `instruments/acid-303/include/acid303/` und als eigenständiges Instrument.
* **Umfang:**
  * Kern, Adapter und CC-Tabelle (Dokument `MIDI.md`).
  * Die Parameterbeschreibungen der Stimme nach dem Parametervertrag aus 4.8.
    Sind die Nachrichten bereits in Phase 1 entstanden, werden sie hier
    wiederverwendet.
* **Tests:**
  * Golden-Test: FNV-1a über 2 s Ausgabe einer festen Notenfolge mit Accent und
    Slide, für Säge und Rechteck.
  * Selbstoszillation bei maximaler Resonanz bleibt endlich und ohne NaN oder
    Inf. Pegelobergrenze und Gleichanteil werden geprüft.
  * Slide-Zeit, Legato ohne neuen Hüllkurvenstart, Accent-Wirkung.
  * Glättung: kein Sprung über einem festgelegten Schwellwert bei
    Parametersprüngen.
  * Relative CPU-Kosten im Host-Benchmark.
* **Hardwaretest (Benutzer):**
  * Hörtest von Klang, Selbstoszillation, Accent und Slide.
  * MIDI über USB und DIN.
  * CPU-Last mit Callback-Diagnose (NICHT GEMESSEN bis dahin).

### Phase 3: Sättigung auslagern und Effekt `drive`

* **Ziel:** die Kurven aus `tape-fx` ohne Klangänderung nach
  `common/dsp/saturation.h` verschieben und darauf den Effekt `drive` mit
  Auto-Level bauen.
* **Schritt 3a, Auslagern (eigener Commit):**
  * `tape-fx` und `toy-piano-tape` nutzen danach die gemeinsame Datei.
  * Nachweis: Die Tape-FX-Tests gegen die drumlogue-Referenz bleiben grün.
  * Hash über die Ausgabe beider Produkte vorher und nachher identisch.
  * Das ARM-BIN von `tape-fx` vor und nach dem Umbau vergleichen. Ist es nicht
    bytegleich, den Grund festhalten. Die Versionen bleiben unverändert.
* **Schritt 3b, Effekt `drive`:**
  * Kern nach FX-Vertrag 1.0, Auto-Level aus 4.6.
  * Firmware `drive-fx` für die Audioeingänge.
  * Adapter in `tests/fx/`, damit `make test-fx` ihn abdeckt.
* **Tests:**
  * Pegeltest: Drive 0–100 % mit allen Kurven, der RMS-Pegel bleibt für die
    Referenzsignale innerhalb von ±1 dB.
  * Die Nachregelung bleibt innerhalb von ±3 dB und hält in der Stille.
  * Überblenden ohne Sprung, Golden-Test, FX-Vertragstests.
* **Hardwaretest (Benutzer):**
  * Hörtest von Auto-Level an und aus, Ausgleich-Vorschlag 1,5 dB.
  * Ein/Aus ohne Knacken.

### Phase 4: Effekt `ping-pong-delay`

* **Ziel:** das Delay aus 4.7 als Kern nach FX-Vertrag 1.0 und als Firmware
  `delay-fx`.
* **Tests:**
  * Impulsantwort: Abstände, abwechselnd links und rechts, Abklingen gemäß
    Feedback.
  * Sync-Notenwerte bei 30–300 BPM.
  * Zeitänderung ohne Klick, gemessen als maximale Abweichung von Sample zu
    Sample.
  * Feedback 95 % mit lauter Eingabe bleibt begrenzt.
  * Dämpfung als Frequenzgang.
  * Spillover beim Ausschalten.
  * Golden-Test und FX-Vertragstests.
* **Hardwaretest (Benutzer):**
  * Hörtest, Sync mit externer Clock.
  * SDRAM-Belegung im MAP-File. CPU-Last NICHT GEMESSEN bis dahin.

### Phase 5: Panel-Variante (Web, JUCE, VST3)

* **Ziel:** Seiten für Sequencer, Stimme, Drive und Delay in allen drei
  Editor-Varianten. Sie erscheinen je nach Komponentenliste in IDENTIFY.
* **Umfang:**
  * Pattern-Raster mit 16 Steps und Notennamen
  * Lauflicht
  * Modus-Plätze und Schalterstellungen
  * Clock-Quelle und Tempo
  * Kalibrierung und „CV aktiv“
  * MIDI-Einstellungen
  * Parameterseiten aus den Parameterbeschreibungen der Firmware
  * Patterns, Modus-Plätze und Kalibrierung speichern und laden (Datei und
    VST3-Songzustand) und beim Verbinden senden
* **Tests:**
  * `make -C editors/mod-on-daisy test test-sanitize`.
  * Browser-e2e (Playwright) mit simuliertem Gerät.
  * Der Selbsttest der JUCE-App läuft weiter.
  * Gemeinsame Protokoll-Testvektoren für C++ und JavaScript.
  * Die Mod-on-Daisy-Seiten und ihre Tests bleiben unverändert grün.
* **Hardwaretest (Benutzer):** Panel am Kit über USB, in der DAW als VST3.

### Phase 6: Produkt `acid-seq`

* **Ziel:** alle Kerne in `sequencers/acid-seq/` zusammenführen, nach dem
  Muster der zusammengesetzten Produkte.
* **Umfang:**
  * Produktadapter mit Signalweg aus 4.9, Host-Mixer mit Dry/Wet je Effekt,
    Überblendungen, Soft-Limiter.
  * Tempo-Übergabe an das Delay.
  * Stimme folgt Kanal A und MIDI-Kanal 3.
  * Registereintrag, Manifest (`make PROJECT=acid-seq manifest`), README und
    CHANGELOG.
* **Tests:**
  * End-to-End-Golden: Pattern → Audio mit allen FX.
  * Die Golden-Hashes der Einzelkomponenten bleiben unverändert.
  * Ressourcen: Speichergrenzen per ELF-Prüfskript nach dem Muster
    `check-tape-elf.py`.
* **Hardwaretest (Benutzer):**
  * Gesamttest: alle Modi, CV/Gate und MIDI gleichzeitig mit Audio.
  * CPU-Spitzen mit Callback-Diagnose, Langzeitlauf von 1 h.
* **Release:** Nur der Benutzer startet den Release-Workflow und veröffentlicht
  erst nach der Hardware-Abnahme.

## 6. Offene Entscheidungen

| Nr. | Frage | Vorschlag |
| --- | --- | --- |
| D1 | Eigener SysEx-Kopf `7D 53 51` oder Erweiterung des Mod-on-Daisy-Protokolls? | Eigener Kopf. Das MD-Protokoll bleibt dann unverändert, und Firmware, `mod_probe.py`, beide Editoren und deren Tests müssen nicht gemeinsam geändert werden |
| D2 | Start/Stop am Kit: Die Schalter wählen jetzt Modi | S1 halten und S16 kurz tippen. Bewusst nicht S9 + S16 wegen des Bootloaders. Außerdem MIDI Start/Stop, Gate IN 2 im Run-Modus und das Panel |
| D3 | SQ-1-Modus CV/Duty übernehmen? | Nein, die Gate-Länge pro Step deckt ihn ab. Der Platz geht an Polyrhythmus |
| D4 | Filter: Moog-Ladder (DaisySP) oder eigener Diode-Ladder? | Mit `LadderFilter` beginnen. Diode-Ladder als spätere Version mit neuem Golden-Hash |
| D5 | 2-faches Oversampling für Hard und Fold? | Erst nach der CPU-Messung auf der Hardware entscheiden. Bis dahin ohne Oversampling |
| D6 | Poti-Glättung von Toy Piano mitnutzen? | In den Sequencer kopieren, mit eigenem Test. Kein Umbau von Toy Piano, damit dessen Image unverändert bleibt |
| D7 | Erfüllt das Ping-Pong-Delay den Wunsch „Stereo-Delay“ in `FUTURE_EFFECT_PROGRAMS.md`? | Ping-Pong als Modus eines Stereo-Delays anlegen (Ping-Pong und Stereo L/R getrennt). Die Grundeinstellung ist Ping-Pong |
| D8 | Lizenzkopf der neuen Firmware-Dateien? | Wie bei `common/audio` (MIT). Der Editor bleibt AGPL |
| D9 | Name des Produkts und der Komponenten | Arbeitstitel `acid-seq`, `step-seq`, `acid-303` |

## 7. Risiken

* **CV OUT unbekannt.** Spannungsbereich, Polarität und Linearität des DAC-Wegs
  sind nicht gemessen. Ohne die Messung aus H14 ist kein 1V/Oct möglich.
* **DIN-MIDI ist neu im Repo.** UART-Empfang und -Senden, Puffergrößen und
  DTCM-Bedarf müssen geprüft werden.
* **MIDI-Clock-Ausgabe aus `main` kann schwanken.** Wie stark, ist NICHT
  GEMESSEN. Falls nötig, Zeitstempel aus dem Callback mitgeben und die Werte
  dokumentieren.
* **CPU-Reserve des Produkts** mit Ladder-Filter, Drive, Delay und
  Sequencer-Logik ist NICHT GEMESSEN. Der Host-Benchmark liefert nur relative
  Werte.
* **Die Auslagerung der Sättigung** könnte den Klang von Tape FX unbemerkt
  ändern. Abgesichert durch Schritt 3a mit Hash- und Referenztests.
* **Kaum freie Hardware-Bedienelemente.** Start/Stop und Bootloader-Geste
  dürfen nicht kollidieren (D2).

## 8. Liste NICHT GEMESSEN (wird in den Phasen fortgeschrieben)

| Größe | Phase | Status |
| --- | --- | --- |
| CV OUT 1/2: Volt pro DAC-Code, Polarität, Linearität | 1 (H14) | NICHT GEMESSEN |
| CV IN 1/2 und Gate IN 1/2: Bereich und Schwelle | 1 | NICHT GEMESSEN |
| 1V/Oct-Genauigkeit nach Kalibrierung | 1 | NICHT GEMESSEN |
| DIN-MIDI senden und empfangen | 1 | NICHT GEMESSEN |
| Schwankung der MIDI-Clock (Ein- und Ausgang) | 1 | NICHT GEMESSEN |
| Klang der Stimme, Selbstoszillation, Accent, Slide | 2 | NICHT GEMESSEN |
| Auto-Level im Hörtest, Ausgleich-Vorschlag | 3 | NICHT GEMESSEN |
| Delay im Hörtest, Sync | 4 | NICHT GEMESSEN |
| CPU-Last und Callback-Spitzen je Firmware | 1–6 | NICHT GEMESSEN |
