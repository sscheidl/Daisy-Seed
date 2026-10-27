# Arbeitsauftrag: Acid Seq

16-Step-Sequencer nach dem Vorbild KORG SQ-1 mit TB-303-Stimme, Drive und
Stereo-/Ping-Pong-Delay für das Seed3 Desktop Dev Kit.

* **Stand:** Fassung 2 vom 2026-10-26. Sie enthält die Befunde R01–R16 aus dem
  [Codex-Review](CODEX_REVIEW.md) (Urteil GO MIT KORREKTUREN, geprüft auf
  `3b119be`). Welcher Befund wo eingearbeitet ist, steht in Abschnitt 9.
  Phase 0 (Gerüst und Verträge) ist umgesetzt, siehe Abschnitt 5.
* **Basis:** `main` @ `1eb091c`, libDaisy `f715e487`, DaisySP `2c72eaf`,
  xPack GCC 13.2.1-1.1, JUCE 9.0.4. Diese Versionen werden nicht angehoben.
* **Herkunft:** Die Eckdaten wurden mit dem Benutzer am 2026-10-26 im Gespräch
  festgelegt. Abschnitt 6 listet, was entschieden ist und was noch offen ist.

## 1. Ziel

Ein eigenständiges Produkt `acid-seq` entsteht aus vier wiederverwendbaren
Komponenten. Jede wird zuerst einzeln entwickelt, getestet und als eigene
Firmware auf der Hardware abgenommen:

| Komponente | Zweck |
| --- | --- |
| Sequencer-Kern | 2 × 8 Steps wie beim SQ-1. Ausgabe über CV/Gate und MIDI, drei Abspielmodi |
| TB-303-Stimme | Monophon, Säge oder Rechteck, Ladder-Filter mit Selbstoszillation, Accent, Slide |
| Drive | Sättigung mit den Kurven aus Tape FX und automatischer Pegelanpassung |
| Stereo-Delay | Ping-Pong (Grundeinstellung) und Stereo L/R, Clock-Sync, Feedback, Dry/Wet, Dämpfung |

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
  ändern. Das betrifft vor allem Phase 3a, in der die Sättigung ausgelagert wird.
* **Echtzeit:** Im Audio-Callback gibt es keine Speicheranforderung, keine
  Sperren (auch keine HAL-Locks, siehe 3.3) und keine blockierenden Aufrufe.
  MIDI wird nur aus `main` gesendet:
  * **USB:** `SendMessage` kann bis zu 3 × 100 µs blockieren und meldet keinen
    Fehler.
  * **DIN:** `MidiUartTransport::Tx` sendet per `PollTx` mit 10 ms Timeout, also
    etwa 0,32 ms pro Byte bei 31 250 Baud. Die Sendearbeit pro Durchlauf von
    `main` wird deshalb begrenzt (4.4).
* **Kein Schreiben ins Flash** ohne ausdrückliche Freigabe des Benutzers.
* **Das Mod-on-Daisy-Protokoll (`7D 4D 44`) bleibt unverändert,** samt
  Echo-Schutz. Dieses Projekt nutzt einen eigenen Namensraum `7D 53 51` mit
  einem gemeinsamen Parametervertrag für alle Komponenten (4.8, D1). Für Drive
  oder Delay entsteht kein weiteres eigenes Protokoll.
* **Jede Firmware enthält den Bootloader-Einstieg** aus `common/bootloader`:
  S9 + S16 für 5 s oder die SysEx-Nachricht. Vor dem Eintritt werden Gates und
  Noten freigegeben (4.1).
* **Sprache:** Code und Code-Kommentare auf Englisch. Benutzerdokumentation,
  Panel-Texte und Hardware-Testpläne auf Deutsch.
* **Lizenzen:** MIT für eigenen Firmware- und DSP-Code, die AGPL-Grenze des
  JUCE-Editors bleibt bestehen. Fremde Lizenz- und Herkunftsvermerke (DaisySP,
  die von MixForge stammenden Tape-Kurven) bleiben erhalten (D8).
* **CI-Budget:** Automatisch laufen nur gezielte Host-Tests im einen Linux-Job
  von `quick.yml`. Dabei bleiben das 15-Minuten-Limit, der Abbruch überholter
  Läufe und die Ausschlüsse für Dokumente erhalten, und es gibt keine ARM-Matrix.
  Firmware-, Editor- und Release-Workflows startet nur der Benutzer. Neue
  Testschritte in `quick.yml` gibt der Benutzer frei (Phase 0).

## 3. Architektur

### 3.1 Komponenten und Orte

| Komponenten-ID | Ort | `kind` | Versionsquelle | Eigene Firmware (Projekt-ID) |
| --- | --- | --- | --- | --- |
| `step-seq` | `sequencers/step-seq/` (neue Rubrik) | `sequencer` (neu) | `src/version.h` | `step-seq` |
| `acid-303` | `instruments/acid-303/` | `instrument` | `src/version.h` | `acid-303` |
| `saturation` | `common/dsp/saturation.h` | `dsp` (neu) | Makro `SEED_SATURATION_VERSION` im Header | keine |
| `drive` | `effects/drive/` | `effect` | `src/version.h` | `drive-fx` |
| `stereo-delay` | `effects/stereo-delay/` | `effect` | `src/version.h` | `delay-fx` |
| Panel-Variante | `editors/mod-on-daisy/` (Web, JUCE, VST3) | `panel` | vorhandene Panel-Versionen | keine |
| Produkt `acid-seq` | `sequencers/acid-seq/` | Produkt | `composed_version.h` | `acid-seq` |

* `common/desktop.mk` setzt `DAISY_ROOT ?= ../..`. `sequencers/<id>` passt dazu.
* **Register (R12):** Komponenten werden eingetragen, sobald ihre Versionsquelle
  existiert. Produkte kommen erst mit dem ersten validierten Build dazu, weil
  `tests/test_components.py` verlangt, dass `BUILDABLE_PROJECTS` und die
  Produktliste genau übereinstimmen.
  * Die neuen Kinds `sequencer` und `dsp` sind eine kleine, kontrollierte
    Erweiterung von `scripts/component_manifest.py:51` samt Tests.
  * Zu jedem Produkt registriert werden: Projekt-ID, Komponenten,
    Image-Präfix, Marker, Gerätekennung in IDENTIFY und kompatible Panels (D9).
* **Firmware-Prüfer (R12):** `scripts/check-firmware.py` erkennt neue
  Image-Präfixe bisher nicht und behandelt sie wie Toy Piano. Vor dem ersten
  Image einer Phase wird der Prüfer für dieses Produkt erweitert, mit positiven
  und negativen Tests.

### 3.2 Schichten

```
Hardware-Adapter (src/main.cpp je Produkt): libDaisy, Tasten, Potis, LEDs,
eigener DAC-Schreibpfad, Gate/CV, USB-/DIN-MIDI, Bootloader-Einstieg, Callback-Diagnose
        │  normalisierte Werte, Ereignisse (SPSC-Queues, Snapshots)
Kerne (header-only, ohne libDaisy, auf dem Host testbar):
  step-seq::Engine ──Noten/Gates──► acid303::Voice ─► Drive ─► StereoDelay ─► Host-Mixer/Limiter
        └──► CV-/Gate-Werte, MIDI-Ereignisse (begrenzte Queue nach main, Notenverfolgung je Ausgang)
```

* **Kerne** kennen keine Hardware. Sie haben `Prepare` und `Process` bzw.
  `Tick` und normalisierte Parameter, wie Mod on Daisy und Tape FX.
* **Effekte** erfüllen den FX-Vertrag 1.0 (`common/audio/fx_contract.h`):
  * `Prepare` läuft nur beim Start, `Process` ist begrenzt und gibt nur das
    bearbeitete Signal aus (wet-only).
  * Den Speicher stellt der Host. Dry/Wet-Mischung, Überblendungen und der
    Ausgangsschutz liegen beim Host.
* **Datenübergabe aus `main` an den Callback (R10):** Patterns und Parameter
  werden nie direkt aus `main` in Daten geschrieben, die der Callback liest.
  * Änderungen gehen über eine SPSC-Queue oder einen Snapshot im
    Triple-Buffer, nach dem Muster von Mod on Daisy.
  * Übernommen werden Parameter am Blockrand und Patterns am Step-Rand.
  * Es gibt eine angenommene und eine angewandte Revision. Beide werden im
    Protokoll gemeldet.

### 3.3 Zeitbasis und DAC (R01, R04)

* **Zeitbasis:**
  * Der Sequencer rechnet mit einem Akkumulator in Samples (ganzzahlig mit
    Bruchteil), nicht mit gerundeten Millisekunden. Dadurch driftet das Tempo
    nicht.
  * Ereignisse werden auf den Block gelegt, in dem sie fällig sind. Bei
    48 Frames entspricht das 1 ms.
  * Gates und CV gehen damit auf 1 ms genau raus. MIDI wird in `main` gesendet
    und kann zusätzlich verzögert werden, was NICHT GEMESSEN ist.
* **Reihenfolge in einem Tick:** Gate-Off, dann Reset, dann Step-Wechsel, dann
  Gate-On. Fallen Gate-Off des alten Steps und Gate-On des neuen auf denselben
  Tick, liegt dazwischen eine Lücke von mindestens 1 ms (Retrigger-Lücke,
  außer bei Slide).
* **Gate-Grenzen:** mindestens 2 ms High und 1 ms Low. Eine Gate-Länge in
  Prozent wird auf diese Grenzen geklemmt. Ein 5-%-Gate bei 300 BPM
  (Sechzehntel = 50 ms) ergibt 2,5 ms und liegt damit innerhalb der Grenzen.
* **Clock-Eingang Gate IN 1:**
  * `GateIn::Trig` liest den Pin einmal pro Block. Pulse kürzer als ein Block
    können verloren gehen.
  * **Spezifikation:** High und Low je mindestens 2 ms. Höchstrate 24 PPQN bei
    300 BPM, das sind 120 Hz bzw. 8,3 ms Periode.
  * Kürzere Pulse sind nicht unterstützt. Ob eine Flankenerfassung mit
    Zeitstempel (EXTI) nötig ist, entscheidet der Hardwaretest (Risiko, Abschnitt 7).
* **Eigener DAC-Schreibpfad (R01):** `hw.SetDacOut` → `DacHandle::WriteValue`
  ruft für jeden Kanal `HAL_DAC_Start` mit `__HAL_LOCK` auf
  (`libDaisy/src/per/dac.cpp:208`). Den Rückgabewert `HAL_BUSY` ignoriert
  libDaisy. Das ist im Callback nicht zulässig. Deshalb gilt:
  * **Beim Start, vor dem Audiostart:** `hw.Init` konfiguriert den DAC
    (Polling, Software-Trigger, beide Kanäle). Danach wird jeder Kanal einmal
    aus `main` über `hw.SetDacOut` mit dem Ruhecode aktiviert.
  * **Im Callback** schreibt nur ein kleiner Adapter: `DAC1->DHR12R1` und
    `DHR12R2`, danach `SWTRIGR`. Er ist der einzige Besitzer des DAC, kein
    weiterer Aufruf von `SetDacOut` nach dem Start. Es gibt keinen HAL-Aufruf
    und keinen Lock. libDaisy bleibt unverändert.
  * **Prüfung:** ein Host-Test des Adapters mit gefälschten Registern, dazu der
    Hardwaretest von Timing und Spannung (Phase 1).

## 4. Eckdaten

### 4.1 Bedienung am Kit (Sequencer und Produkt `acid-seq`)

| Element | Funktion |
| --- | --- |
| S1–S8, S9–S16 | Steps A1–A8 und B1–B8. **Drücken** wählt den Step. **Lang drücken** (≥ 400 ms) schaltet beim Loslassen sein Gate um, sofern die Taste nicht durch einen Akkord markiert ist (siehe unten) |
| LEDs D1–D16 | Gate an: hell. Gate aus: dunkel. Lauflicht: kurzes Aufblitzen bzw. Abdunkeln. Gewählter Step: schnelles Blinken |
| VR1 | Oktave des gewählten Steps, 5 Stufen (0–4) |
| VR2 | Halbton des gewählten Steps, 12 Stufen (C–H) |
| VR3 | Gate-Länge des gewählten Steps, 5–100 % der Step-Dauer (Grenzen siehe 3.3) |
| VR4 | Accent und Slide des gewählten Steps: nichts, Accent, Slide, Accent + Slide |
| VR5 | Tempo, 30–300 BPM, global |
| VR6 | Swing 50–75 %, global (Formel in 4.3) |
| VR7 | Sequenzlänge 1–16, global (übernommen am Step-Rand, siehe 4.2) |
| VR8 | Transponierung ±12 Halbtöne, global |
| SW17 (2 Stellungen) | Start/Stop, siehe 4.2 |
| SW18 (3 Stellungen) | Abspielmodus: oben Vorwärts, Mitte Rückwärts, unten Zufall |

* **Rasterstufen der Potis:** Die Poti-Glättung von Toy Piano (20 ms, 0,1 %
  Hysterese, `toy_piano.cpp:13–24`) wird in den Sequencer kopiert. Toy Piano
  wird nicht umgebaut (D6). Jeder Stufen-Poti (VR1, VR2, VR4, VR7, VR8)
  bekommt zusätzlich eigene Schaltschwellen mit Hysterese, Vorschlag 25 % einer
  Stufenbreite. Getestet wird mit 1 kHz Abtastrate.
* **Übernahme bei Bewegung:** Nach dem Wechsel auf einen anderen Step ändert
  ein Poti dessen Wert erst, wenn er sich um mehr als 2 % gegenüber der Stellung
  beim Wechsel bewegt hat. Das gilt für VR1–VR4. VR5–VR8 wirken immer.
* **Vorhören (R06):** Bei gestopptem Sequencer spielt jede Änderung an VR1 oder
  VR2 die Note für 150 ms auf CV/Gate, MIDI und der internen Stimme.
  * Es gibt genau einen Vorhör-Platz. Eine neue Vorhör-Note beendet die alte
    zuerst.
  * Start bricht das Vorhören vor dem ersten Step ab.
  * Note-Offs tragen eine Kennung. Ein veraltetes Note-Off wirkt nie auf eine
    neuere Note.
* **SW18 (Switch3)** wird von `UpdateAllControls` nicht entprellt. Der Adapter
  übernimmt das 20-ms-Mapping von Nebulae (`instruments/granular/src/control.cpp:40–48`).
* **Akkord-Markierung und Bootloader (R07):** S9 und S16 sind normale
  Step-Tasten. Deshalb gilt:
  * Jede Taste merkt sich, ob während ihrer gesamten Haltezeit irgendeine
    andere Taste gehalten wurde, auch eine, die schon vorher gedrückt war.
  * Ist das der Fall, löst sie beim Loslassen keine Aktion für langen Druck
    aus. Das gilt in jeder Reihenfolge des Loslassens.
  * Die Auswahl beim Drücken bleibt.
  * Unmittelbar vor dem Bootloader-Eintritt gibt die Firmware Gates und Noten
    frei: Gate auf Low-Code, All Notes Off auf allen aktiven Ausgängen,
    interne Stimme stumm.
* **Zuordnung der Schalter** (vom Benutzer bestätigt am 2026-10-26): SW17 mit
  zwei Stellungen ist Start/Stop, SW18 mit drei Stellungen wählt den Modus.

### 4.2 Abspielmodi und Pattern-Regeln (R06)

| SW18 | Modus | Ablauf bei Länge L | Startpunkt nach Start oder Reset |
| --- | --- | --- | --- |
| oben | Vorwärts | 1, 2, …, L, 1, … (A1…A8 = 1…8, B1…B8 = 9…16) | Step 1 |
| Mitte | Rückwärts | L, L−1, …, 1, L, … | Step L |
| unten | Zufall | gleichverteilt über 1…L, direkte Wiederholung erlaubt | ein gezogener Step |

Beispielfolgen für die Tests:
* Vorwärts mit L = 3: 1 2 3 1 2 3.
* Rückwärts mit L = 3: 3 2 1 3 2 1.
* L = 1: in jedem Modus immer Step 1.

**Zufall:**
* Generator xorshift32. Beim Einschalten wird er mit einer festen Konstante
  ungleich null geseedet. Ein Seed von null wird durch diese Konstante ersetzt.
* Er läuft über Start und Stop hinweg weiter, ohne neu geseedet zu werden.
  Jede Wiedergabe klingt deshalb anders.
* Tests setzen den Seed selbst.

**Moduswechsel:**
* gilt ab dem nächsten Step.
* Die Position bleibt, nur die Richtung ändert sich. Bei Zufall wird ab dem
  nächsten Step gezogen.

**Längenänderung:**
* wird am nächsten Step-Rand übernommen.
* Liegt die aktuelle Position hinter der neuen Länge, ist der nächste Step bei
  Vorwärts Step 1 und bei Rückwärts Step L. Zufall zieht ab dann aus 1…L.

**Gate aus:**
* Ein Step mit Gate aus erzeugt keine Note. Die CV-Tonhöhe bleibt dabei stehen.

**Slide:**
* Ein Step mit Slide hält sein Gate bis zum nächsten Step offen. Die Tonhöhe
  gleitet zur nächsten Note (konstant, Vorschlag 60 ms), es gibt keine neue
  Hüllkurve und MIDI sendet Legato.
* Hat der nächste Step Gate aus, fällt das Gate am Ende des Slide-Steps.
* Folgt eine gleiche Note mit Slide, bleibt sie einfach gehalten.

**Gleiche Note ohne Slide:**
* Sie wird neu angeschlagen: Retrigger-Lücke im Gate (3.3), MIDI Note-Off vor
  Note-On.

**Notenbereich:**
* Die Step-Note ist Oktave × 12 + Halbton, also 0–59, bezogen auf C1
  (MIDI 24). Daraus ergibt sich MIDI 24–83.
* **Skala** (chromatisch, Dur, Moll) wird auf die Step-Note angewendet: Die
  Note wird auf die nächsttiefere Stufe der Skala gerundet, Grundton C.
* **Danach Transponierung:** VR8 plus CV IN 1. CV IN 1 wird auf Halbtöne
  quantisiert (mit Hysterese), auf ±24 begrenzt und am Step-Anfang
  übernommen, nicht mitten in der Note.
* **Ergebnis:** auf MIDI 0–127 und auf den kalibrierten CV-Bereich begrenzt.
* Die SQ-1-Skala „linear“ entfällt. Sie setzt einen stufenlosen Regler voraus,
  die Tonhöhe ist hier aber auf Halbtöne quantisiert (D11).

**Weitere Modi** wie Pendel, Brownian, Step Jump oder Parallel 2 × 8 kommen
vielleicht später über das Control Panel. Sie sind nicht Teil der Phasen 1–6.
Vorher muss geregelt sein, wie ein Panel-Modus sich zur Stellung von SW18
verhält (D3).

### 4.3 Transport und Clock (R03, R04)

**Freigabe:** `Freigabe = (SW17 auf Run) UND (Gate IN 2 im Run-Modus ? Gate IN 2 High : wahr)`.

* SW17 auf Stop gewinnt immer.
* Gate IN 2 im Run-Modus ist eine zusätzliche Freigabe, kein eigener
  Startbefehl.

**Zustände:**

| Zustand | Bedeutung |
| --- | --- |
| STOP | steht. Gate auf Low-Code, keine Noten aktiv. Die Position wird beim nächsten Start neu gesetzt |
| WARTET | freigegeben, aber eine externe Quelle läuft noch nicht. Keine Ausgabe |
| LÄUFT | spielt |
| PAUSE | angehalten durch Panel-Stop oder MIDI Stop bei weiterhin bestehender Freigabe. Die Position bleibt für Continue |

**Übergänge:**

| Ereignis | Quelle Intern | Quelle Gate IN 1 | Quelle MIDI (USB/DIN) |
| --- | --- | --- | --- |
| Freigabe steigt (auch beim Einschalten) | STOP → LÄUFT ab Startpunkt | STOP → WARTET | STOP → WARTET |
| Freigabe fällt | → STOP, Panic | → STOP, Panic | → STOP, Panic |
| erster Puls bzw. MIDI Start | — | WARTET → LÄUFT, der erste Puls spielt den Startpunkt | Start: → LÄUFT ab Startpunkt mit dem ersten folgenden Clock-Byte |
| MIDI Continue | — | — | PAUSE oder WARTET → LÄUFT ab der gemerkten Position (Song Position Pointer wird nicht ausgewertet) |
| MIDI Stop | — | — | LÄUFT → PAUSE, Panic |
| Panel Stop (nur bei Freigabe) | LÄUFT → PAUSE, Panic | LÄUFT → PAUSE, Panic | LÄUFT → PAUSE, Panic |
| Panel Start (nur bei Freigabe) | PAUSE → LÄUFT ab Startpunkt | PAUSE → WARTET | PAUSE → WARTET |
| Reset (Gate IN 2 im Reset-Modus, steigende Flanke) | der nächste Step ist der Startpunkt, auch in STOP, PAUSE und WARTET | wie Intern | wie Intern |
| Clock bleibt aus (Timeout, siehe unten) | — | LÄUFT → WARTET, Gate auf Low-Code, Noten aus, Position bleibt. Der nächste Puls spielt den nächsten Step | wie Gate IN 1 |

* **Panic** bedeutet: Gate auf Low-Code, Note-Offs aus der Notenverfolgung an
  alle aktiven Ausgänge (4.4), interne Stimme Release, offene Slides beenden.
  Kein Slide und kein Gate bleibt über Stop oder Timeout hinaus offen.
* **Einschalten mit SW17 auf Run (D2):** SW17 wird nach der Initialisierung
  stabil ausgewertet (entprellt), bevor Audio, MIDI oder Gates ausgegeben werden.
  * Bei interner Clock startet der Sequencer dann.
  * Bei externer Clock wartet er auf die Startbedingung seiner Quelle.
  * Eine fehlende CV-Freigabe (4.5) hält Audio und MIDI nicht an.

**Clock-Quelle:**
* Grundeinstellung ist „Intern“ (D10).
* **„Gate IN 1“** und **„MIDI“** sind fest gewählte Quellen. Bei „MIDI“ gilt
  der erste Port, der nach dem Scharfschalten Clock liefert (USB oder DIN). Er
  bleibt gesetzt bis Stop oder Timeout. Clock vom anderen Port wird ignoriert
  und gezählt.
* **„Auto“** ist eine Option:
  * Rangfolge: Gate IN 1, dann MIDI USB, dann MIDI DIN.
  * Die zuerst erkannte Quelle bleibt gesetzt, bis ihr Timeout abläuft.
  * Dann übernimmt die interne Clock phasenrichtig: mit dem zuletzt gemessenen
    Tempo, der nächste Step zur erwarteten Zeit.
  * Kehrt eine externe Quelle zurück, wird am nächsten Step-Rand auf ihren
    Puls umgeschaltet.
* **Timeout:** Ohne Puls für max(2 s, 4 × zuletzt gemessene Periode) gilt die
  Quelle als ausgefallen.
* **Teiler an Gate IN 1:** 1 Puls pro Step (Grundeinstellung), 2, 4 oder 24
  Pulse pro Viertel.
* **MIDI-Clock-Eingang:** 24 PPQN, das sind 6 Clock-Bytes pro Sechzehntel.
* **Clock-Ausgang:** MIDI-Clock (24 PPQN), Start, Stop und Continue werden nur
  gesendet, wenn die Quelle Intern oder Gate IN 1 ist. Empfangene MIDI-Clock
  wird nicht weitergesendet. Das verhindert Echo-Schleifen. Ein Clock-Thru
  gibt es in dieser Fassung nicht.
* **Periodenschätzung bei externer Clock:**
  * gleitender Mittelwert der letzten 4 Perioden.
  * Startwert ist das Tempo von VR5.
  * Prozent-Gates und Swing beziehen sich auf diese Schätzung.
  * Kommt ein Puls früher als erwartet, wird das Gate vorher geschlossen
    (Reihenfolge wie in 3.3).
* **Swing:**
  * Ein Step-Paar dauert 2T. Der erste Step dauert 2T · s, der zweite
    2T · (1 − s), mit s = 0,50 … 0,75. Bei 50 % sind beide gleich lang.
  * Bei Gate IN 1 mit 1 Puls pro Step wird der zweite Step anhand der
    Periodenschätzung verzögert. Die ersten zwei Steps nach dem Start sind
    deshalb gerade.

### 4.4 Buchsen und MIDI (R05)

| Buchse | Grundbelegung | Alternative (Panel) |
| --- | --- | --- |
| CV OUT 1 | Tonhöhe, 1V/Oct, Slide als Glide | — |
| CV OUT 2 | Gate (Low- und High-Code aus der Kalibrierung) | — (weitere Belegungen erst mit Parallel-Modi, D3) |
| Gate IN 1 | Clock-Eingang (4.3) | — |
| Gate IN 2 | Reset auf die steigende Flanke | Run als Freigabe (4.3) |
| CV IN 1 | Transponierung 1V/Oct (4.2) | aus |
| CV IN 2 | aus | Gate-Länge oder Swing |

* **Accent:** bei MIDI Anschlagstärke 127 statt 100 (Vorschlag), in der
  internen Stimme siehe 4.6. Auf CV gibt es keinen Accent.
* **USB:** `MidiUsbHandler` mit `EXTERNAL`, wie in den bestehenden Firmwares.
* **DIN:** `hw.midi`. Die Hardware-Klasse initialisiert USART1 bereits, es wird
  keine zweite UART-Instanz angelegt. Bisher nutzt keine Firmware im Repo DIN
  zum Senden oder Empfangen, das wird erst auf der Hardware geprüft.
* **Kanäle:** Sequencer-Noten auf einem einstellbaren Kanal (Vorschlag 1),
  Noten für die interne Stimme auf einem einstellbaren Kanal (Vorschlag 2).
  CCs steuern Stimme und Effekte, die Tabelle entsteht in Phase 2.
* **Ausgänge:** USB und DIN senden gleichzeitig, jeder ist im Panel abschaltbar.

**Notenverfolgung und Panic:**
* Für jeden Ausgang (USB, DIN) und jeden Kanal gibt es eine Tabelle der
  aktiven Noten (128 Bit).
* Note-Offs und Panic werden aus dieser Tabelle erzeugt. Sie liegen nicht in
  der allgemeinen Queue und können deshalb nicht verloren gehen.
* Ein Note-On wird nur gesendet, wenn es in die Tabelle eingetragen werden
  kann. Ist die Queue voll, wird das Note-On verworfen und gezählt. Ein
  Note-Off fehlt dann nie, weil die Note nie gesendet wurde.

**Panic wird ausgelöst bei:**
* Stop und Pause
* Timeout der Clock
* Wechsel des MIDI-Kanals
* Abschalten eines Ausgangs
* Überlauf der Queue
* Bootloader-Eintritt

**USB getrennt:**
* Die USB-Tabelle wird verworfen, weil dort keine Nachricht mehr ankommt.
* DIN, die Gates und die interne Stimme werden bereinigt.
* **Wiederanschluss:** zuerst All Notes Off über USB, dann den Zustand neu
  synchronisieren.

**Sendebudget in `main`:**
* höchstens 8 Nachrichten pro Port und Durchlauf.
* DIN höchstens 24 Bytes pro Durchlauf, das sind etwa 7,7 ms.
* Clock-Bytes haben Vorrang vor Noten. Note-Offs aus der Tabelle haben Vorrang
  vor Note-Ons.

**SysEx:** in dieser Fassung nur über USB. Für DIN gibt es dafür kein Budget.

### 4.5 CV-Kalibrierung und Freigabe (R08)

**Vor dem ersten Anschluss an einen externen Synth** misst der Benutzer im
Diagnose-Test H14:
* Spannungen bei mehreren DAC-Codes
* Polarität
* Verhalten beim Einschalten und beim Reset

Daraus wird ein bekannter Low- bzw. Ruhecode für das Gate festgehalten.

**„Gesperrt“ bedeutet:**
* keine dynamische Ausgabe.
* Der DAC bleibt auf dem Code, den die Initialisierung setzt. Welche Spannung
  das ist, ist bis H14 NICHT GEMESSEN.

**Kalibrierdaten pro Ausgang:**
* Offset und Skalierung (Code pro Volt)
* Gate-Low- und Gate-High-Code
* Klemmbereich in Codes

Alle Werte müssen endlich sein und innerhalb fester Grenzen liegen.
Ungültige Daten werden abgelehnt.

**Freigabe („CV aktiv“):**
* ist ein eigener Befehl, getrennt von den Kalibrierdaten.
* wird nie automatisch wiederhergestellt, weder nach einem Neustart noch nach
  einem Gerätewechsel.
* Wird sie bei offenem Gate entzogen, geht das Gate sofort auf den Low-Code.

**Gerätebindung:**
* Kalibrierdaten gehören zu einem Kit, erkannt an der 96-Bit-UID des STM32,
  die IDENTIFY meldet.
* Panel und `sq_probe.py` senden gespeicherte Kalibrierdaten nur an das Kit mit
  derselben UID.

**Ohne Panel nutzbar (Phase 1):** `sq_probe.py` kann bereits in Phase 1
kalibrieren, freigeben, sperren und auslesen. Die Panel-Seite folgt in Phase 5.

### 4.6 TB-303-Stimme (`acid-303`)

| Teil | Umsetzung |
| --- | --- |
| Oszillator | DaisySP `Oscillator`, `WAVE_POLYBLEP_SAW` oder `WAVE_POLYBLEP_SQUARE` |
| Filter | DaisySP `LadderFilter` (LP24, tanh-Eingangsstufe, laut Doku stabile Selbstoszillation, intern 4-fach oversampled, das gehört ins CPU-Budget). Das ist eine Annäherung, kein Nachweis für echtes 303-Verhalten. Ein Diode-Ladder bleibt spätere Option mit eigener Version und neuem Golden-Hash (D4) |
| Hüllkurven | Filterhüllkurve mit Env Mod und Decay. Accent verstärkt Filterhüllkurve und Lautstärke und verkürzt das Decay |
| Slide | Glide mit konstanter Zeit. Legato-Noten lösen keine neue Hüllkurve aus |
| Noten | Monophon, gemeinsamer Notenstapel mit Herkunftskennung (siehe unten) |
| Parameter | Wellenform, Tuning (±12 Halbtöne, fein ±100 Cent), Cutoff, Resonanz, Env Mod, Decay, Accent-Stärke, Lautstärke |
| Glättung | alle kontinuierlichen Parameter, Schwellwerte siehe Phase 2 |
| Ausgang | Mono, im Produkt auf beide Kanäle und den Kopfhörerausgang |

**Stimmenbesitz (R16):** Sequencer und externe MIDI-Noten (Kanal der Stimme)
teilen sich die monophone Stimme.
* Es gilt die zuletzt gespielte Note, egal aus welcher Quelle.
* Jede Note trägt ihre Quelle. Das Note-Off einer Quelle beendet nur deren Note.
* Ist die beendete Note die klingende, übernimmt die zuletzt gehaltene Note
  einer der beiden Quellen.
* Ein Panic des Sequencers entfernt nur Sequencer-Noten.

**Eigene Firmware `acid-303`** (zum Testen und als eigenständiges Instrument):
* **S1–S16:** chromatisch ab C2.
* **VR1–VR8:** Cutoff, Resonanz, Env Mod, Decay, Accent, Tuning, Lautstärke, Wellenform.
* **SW17:** Accent für die Tasten.
* **SW18:** Slide aus, Slide bei gehaltener Taste (Legato), Slide immer.
* MIDI über USB und DIN, CC wie im Produkt.

### 4.7 Drive (`saturation`, `drive`)

**Quelle der Kurven:**
* Soft, Hard, Tape und Fold, von MixForge (drumlogue) nach
  `effects/tape-fx/src/tape_fx.h:51–64` portiert. Die Tests vergleichen sie
  exakt mit MixForge (`test_tape_fx.cpp:158–168`).
* Sie werden ohne Klangänderung nach `common/dsp/saturation.h` ausgelagert.
  Die bisherigen Namen in `tape::` bleiben als Wrapper erhalten.
* Der Looper hat keinen wiederverwendbaren Drive mit diesen Kurven. Er hat nur
  ein `SoftLimit` in der Frippertronics-Rückkopplung (`looper_engine.cpp:35`,
  `:1008`) und bleibt unverändert. Die Stardust-Firmware ist nur analysiert und
  liefert keinen Code.

**Parameter:** Ein/Aus, Kurve, Drive, Tone, Level, Auto-Level an/aus,
Ausgleich 0–100 % (Grundeinstellung 100 %), Zielversatz −3…0 dB
(Grundeinstellung −1,5 dB).

**Auto-Level (R14),** gemessen nur um den Drive, also zwischen Eingang und
Ausgang des Drive-Kerns. Delay, Dry/Wet und Limiter liegen außerhalb.
1. **Feste Ausgleichskurve abhängig vom Drive.** Eine Tabelle, gemessen im
   Host-Test mit Referenz-Fixtures (Phase 0, R11) und als Konstante abgelegt.
2. **Langsame Nachregelung:**
   * misst den RMS-Pegel vor und nach dem Drive, Messfenster 300 ms.
   * auf ±3 dB begrenzt.
   * Unter −60 dBFS am Eingang wird der Wert gehalten.
3. **Zielpegel** = Eingangs-RMS + Zielversatz. Der Ausgleich skaliert die
   Korrektur linear in dB (0 % = keine Korrektur).

**Ein/Aus:** über 10 ms überblendet, das übernimmt der Host.

**Aliasing bei Hard und Fold:** 2-faches Oversampling ist optional und wird
erst nach der CPU-Messung entschieden. Bis dahin ohne, Kosten und Hörtest
werden dokumentiert (D5).

**Eigene Firmware `drive-fx`:** bearbeitet die Stereo-Audioeingänge, nach dem
Muster von `reverb-fx` und `tape-fx`.

### 4.8 Stereo-Delay (`stereo-delay`, R02, D7)

* **Modi:**
  * **Ping-Pong** (Grundeinstellung): Der Mono-Eingang (L + R) / 2 geht in die
    linke Leitung. Die Rückkopplung wechselt zwischen links und rechts.
  * **Stereo L/R:** zwei getrennte Leitungen mit eigener Rückkopplung.
* **Definition der Delay-Zeit:** Abstand zwischen zwei aufeinanderfolgenden
  Echos. Bei Ping-Pong ist das der Abstand von Seitenwechsel zu Seitenwechsel,
  bei Stereo L/R der Abstand je Kanal.
* **Parameter:** Ein/Aus, Modus, Delay-Zeit, Sync an/aus, Feedback, Dry/Wet,
  Dämpfung.
* **Delay-Zeit:**
  * frei von 10 ms bis 2 s.
  * Mit Sync ein Notenwert von 1/32 bis 1/1, auch punktiert und triolisch,
    bezogen auf das Tempo, das der Host vorgibt.
  * Der volle Bereich wird unterstützt: Bei 30 BPM ist eine punktierte ganze
    Note 12 s lang. Es wird nicht still auf 2 s geklemmt.
* **Speicher:** 2 Leitungen × 12 s × 48 000 Samples als Float =
  4 608 000 B, plus Rand für die Interpolation (Vorschlag 64 Samples je
  Leitung). Liegt im SDRAM (`DSY_SDRAM_BSS`), vom Host gestellt.
* **Lesezeiger:** Die Interpolation liest höchstens bis zur maximalen Zeit
  minus Rand. Längere Werte werden auf diese Grenze geklemmt und im Panel
  angezeigt.
* **Feedback:** 0–95 %. Ein Soft-Clip in der Schleife verhindert, dass sie sich
  aufschaukelt.
* **Dämpfung:** Tiefpass erster Ordnung in der Rückkopplung, 20 kHz bis 1 kHz.
* **Zeitänderungen:** gleiten mit begrenzter Rate. Bei Sprüngen ab 10 % wird
  über zwei Lesezeiger in 20 ms überblendet.
* **Ausschalten (R14), Reihenfolge:**
  1. Der Host blendet die Einspeisung ins Delay über 10 ms aus.
  2. Der Nachhall läuft mit dem eingestellten Feedback weiter.
  * **Ende:** wenn der Wet-Pegel 500 ms lang unter −90 dBFS liegt.
    Spätestens nach 20 s wird der Nachhall über 1 s ausgeblendet. Das schützt
    vor endlosem Nachhall.
  * **Wieder einschalten während des Nachhalls:** Die Einspeisung wird
    eingeblendet, die Leitungen werden nicht gelöscht.
* **Eigene Firmware `delay-fx`:** bearbeitet die Audioeingänge. Ohne externe
  Clock gilt das Tempo von VR5. `docs/FUTURE_EFFECT_PROGRAMS.md` wünscht ein
  Stereo-Delay, dieser Effekt erfüllt den Wunsch.

### 4.9 Control Panel und Protokoll (R09, R10)

**Ort:** Panel-Variante im vorhandenen Editor `editors/mod-on-daisy`: Browser,
JUCE-Standalone und VST3. Die Transporte werden mitgenutzt.
* **Dispatcher:** `Session` (C++, `src/core/Session.h`) und `mod_protocol.js`
  verarbeiten heute nur `7D 4D 44`. Beide bekommen einen expliziten Dispatcher
  für MD und SQ sowie eine passende Session-Strategie.
* **Geräte nur mit SQ:** Für sie wird keine MD-Matrix abgefragt.
* **MD v1** und sein Echo-Schutz bleiben unverändert. Die Matrix-Seiten
  erscheinen nur bei Firmwares mit Mod-Kern.

**Protokoll:** Namensraum `7D 53 51` („SQ“), spezifiziert in
`sequencers/step-seq/docs/PROTOCOL.md` (Deutsch). Das ist eine bewusste
Abweichung vom Audit (`docs/MODULARITY_AUDIT_2026-10-25.md` § 3/4). Das Audit
empfiehlt einen Parametervertrag, aber keinen zweiten Protokollkopf.
* **Begründung:** MD v1 bleibt unverändert und kompatibel.
* **Aufwand:** Firmware, `sq_probe.py`, C++- und JS-Session, gemeinsame Tests
  (D1).

**Rahmen:**
* Kopf, Protokollversion, Nachrichtentyp, Request-ID, Antwortbit, Länge,
  Nutzdaten, Prüfsumme (CRC-8).
* Höchstens 120 Byte Inhalt. Der Parser in libDaisy speichert nur 128 Bytes und
  schneidet darüber ab.
* **Fragmente:** Längere Daten (Deskriptorlisten) werden mit Index und Anzahl
  aufgeteilt. Ein Pattern mit 16 Steps passt in einen Rahmen.

**Zuverlässigkeit:**
* Timeout 200 ms, höchstens 3 Wiederholungen. Gleiche Request-IDs werden
  idempotent behandelt.
* Antworten tragen das Antwortbit und werden nie beantwortet.
* Fehlerhafte, zu lange, doppelte und verlorene Rahmen werden verworfen und
  gezählt. Eingestreute Echtzeit-Bytes (F8 usw.) trennt schon der libDaisy-Parser
  ab, der Rahmen bleibt gültig (Phase 0, begründet in `PROTOCOL.md` § 2). Verworfen
  werden Rahmen mit anderen Statusbytes.

**Inhalt:**
* **IDENTIFY:** Produkt, Version, Komponentenliste mit Schema-Version, Boot-ID
  (zufällig pro Start, zeigt einen Neustart an) und STM32-UID.
* **Parametervertrag** für alle Komponenten: Komponente, stabile Parameter-ID,
  Typ, Einheit, Min, Max, Standardwert, CC-Nummer.
* **Pattern** lesen und schreiben (atomar, Übernahme am Step-Rand),
  Kalibrierung und Freigabe (4.5), Transport (4.3).
* **Monitor:** ereignisbasiert. Step-Wechsel, Gate, Modus, Zustand aus 4.3,
  Schalterstellungen, Clock-Quelle, Tempo, angenommene und angewandte Revision,
  Fehlerzähler. Höchstens 50 Rahmen pro Sekunde.

**Testvektoren:** gemeinsame Vektoren für C++ (Firmware und Editor),
JavaScript und Python (`sequencers/step-seq/tools/sq_probe.py`, nach dem Muster
von `mod_probe.py`).

**Patch und Speichern (R16):**
* **Produkt-Patch:** versioniert. Enthält Pattern, Modus, Transport- und
  Clock-Einstellungen, MIDI-Einstellungen und alle Parameter von Stimme, Drive
  und Delay, jeweils mit Komponenten- und Schema-Version.
* **Laden ohne Gerät** ist möglich. Fehlende oder inkompatible Komponenten
  werden angezeigt und übersprungen.
* **Kalibrierung** gehört zum Gerät, nicht zum Song. Sie wird getrennt
  gespeichert (4.5).
* **Verbinden:** Das Panel liest zuerst den Gerätezustand, das Gerät hat
  Vorrang. Eine Übertragung vom Panel aufs Gerät gibt es nur auf ausdrückliche
  Aktion.
* **VST3:** Beim Laden eines Songs wird dessen Patch nach Bestätigung
  übertragen, mit der Option „immer“. Die Übernahme ist atomar.
* `.modpatch`-Dateien bleiben lesbar.
* **Firmware-Speicher:** Patterns liegen nur im RAM. SD kommt später, Flash nur
  mit Freigabe.

### 4.10 Produkt `acid-seq`

* **Signalweg:** Sequencer → Stimme → Drive → Delay → Host-Mixer (Dry/Wet je
  Effekt, Überblendungen) → Soft-Limiter → Ausgang.
* **Komponenten:** `step-seq`, `acid-303`, `saturation`, `drive`,
  `stereo-delay`, `fx-contract`, `bootloader-entry`, `callback-diagnostics`.
* **Version:** eine eigene Produktversion in `composed_version.h`, wie bei
  `physical-modeling-fx-*`.
* **Ohne angeschlossenen Audioausgang** laufen CV/Gate und MIDI unverändert.
* **Release:** Der vorhandene manuelle Release-Workflow ist auf Toy Piano
  festgelegt (`.github/workflows/release.yml:38`, `:56`). Für `acid-seq` muss
  ein eigener manueller Lieferweg später festgelegt werden. Dabei gibt es keine
  automatischen Releases und keinen ungewollten Toy-Piano-Release.

## 5. Phasen

**Abhängigkeiten (R11):**

```
0 Verträge ─► 1 Transport und Kern
                 ├─► 2 Stimme ───────────────┐
                 ├─► 3a Auslagern, 3b Drive-Kern ─► Auto-Level-Abnahme (braucht 2)
                 └─► 4 Delay ────────────────┤
                                             ▼
                                5 Panel ─► 6 Produkt
```

* Die Phasen 2, 3b (Kern) und 4 können parallel laufen. Sie bauen auf den
  Verträgen aus Phase 0 und dem Transport- und Ereignisvertrag aus Phase 1 auf.
* Die Auto-Level-Tabelle wird zuerst mit den Referenz-Fixtures aus Phase 0
  erstellt. Abgenommen wird sie erst mit der versionierten 303-Fixture aus
  Phase 2.

**Für jede Phase mit Firmware gilt:**
* Ein PR mit:
  * Host-Tests (`test`, `test-sanitize`)
  * ARM-Build
  * `python scripts/check-firmware.py <bin>` (zuvor für das Produkt erweitert,
    3.1)
  * `make components test-components`
  * ELF-Prüfung der Speicherplatzierung (R15)
  * einem deutschen Hardware-Testplan unter `docs/HARDWARE_TEST.md` der
    Komponente
* `test-thread` braucht Linux. Unter Windows läuft TSan nicht. Er läuft in WSL,
  falls vorhanden, oder in einem budgetierten Lauf, den der Benutzer startet.
  Er ist nicht Teil der automatischen CI.
* Die Phase gilt als abgeschlossen, wenn der Benutzer den Hardwaretest
  bestätigt hat oder ausdrücklich darauf verzichtet.

**Ressourcen-PASS auf der Hardware (R15),** ab dem ersten Image jeder
Firmware:
* keine Überschreitung der Callback-Frist im vereinbarten Stressfall
* erfasste Callback-Spitzen
* Hochwasserstand des Stacks
* größte Belegung jeder Queue

Der Stressfall umfasst MIDI- und Panel-Verkehr, Parameterfahrten und, je nach
Phase, Selbstoszillation und Feedback 95 %. CPU-Prozentwerte werden nicht aus
Host-Zeiten abgeleitet.

### Phase 0: Gerüst und Verträge

Ohne Firmware, deshalb ohne ARM-Image und ohne Hardwaretest.

**Stand 2026-10-26: umgesetzt** (Branch `claude/acid-seq-phase-0-f1c35b`). Verträge:
[PROTOCOL.md](../../sequencers/step-seq/docs/PROTOCOL.md),
[PARAMETERS.md](../../sequencers/step-seq/docs/PARAMETERS.md),
[TRANSPORT.md](../../sequencers/step-seq/docs/TRANSPORT.md), Testvektoren in
`sequencers/step-seq/contracts/`, Fixtures in `tests/fixtures/acid-seq/`, Prüfung
durch `tests/test_acid_seq_contracts.py`. Neben den Vektoren für SQ entstanden auch
Sollfolgen für den Transport (`transport_vectors.json`).

* **Struktur:**
  * `sequencers/README.md` (Rubrik)
  * Ordnergerüste für `step-seq`, `acid-303`, `drive` und `stereo-delay`, je
    mit README, `src/version.h`, Makefile und leerem Host-Test
  * `common/dsp/saturation.h` mit Versionsmakro (zunächst ohne Kurven)
  * `projects.mk`: Einträge mit Status `planned`, ohne `BUILDABLE_PROJECTS`
  * `README.md` (Projektliste)
* **Register:** Komponenten mit Versionsquelle eintragen, noch ohne Produkte
  (3.1). Die Kinds `sequencer` und `dsp` im Validator samt Tests ergänzen.
* **Verträge, als Dokumente mit Testvektoren:**
  * Entwurf von `sequencers/step-seq/docs/PROTOCOL.md`: Rahmen, IDENTIFY,
    Parametervertrag, Fragmente, Fehler (4.9)
  * stabile Parameter-IDs und CC-Plan für alle Komponenten
  * Transport- und Ereignisvertrag zwischen Sequencer, Stimme und
    Tempo-Empfängern
  * Referenz-Fixtures für Drive und Delay: bandbegrenzte Säge und Rechteck
    durch einen festen Tiefpass in drei Stufen, Resonanzspitze, Impuls,
    Stille. Versioniert unter `tests/fixtures/acid-seq/`
* **CI (Freigabe durch den Benutzer, R13):**
  * `sequencers/**` in die Pfadfilter von `quick.yml`.
  * `scripts/select_test_suites.py`, neue Verbraucherregeln:
    * `stepseq`, `acid303`, `drive`, `delay` und `acidseq` je für ihren Ordner.
    * Jede Änderung an einem Kern von `acid-seq` wählt `acidseq`.
    * `common/dsp/` wählt `tape`, `toy` (wegen `toy-piano-tape`), `drive`,
      `fx` und `acidseq`, aber nicht mehr alle Suites.
    * Neue Effekt-Adapter wählen `fx`.
    * Die Liste `submodules` bekommt die neuen ARM-abhängigen Suites.
  * Auswahltests in `tests/test_components.py`, positiv und negativ.
* **PASS:**
  * `make list` zeigt die neuen IDs.
  * `make components test-components` ist grün.
  * Die Auswahltests decken jede neue Regel ab.
  * Die bestehenden Suites sind unverändert grün.

### Phase 1: Sequencer-Kern, Transport und Firmware `step-seq`

**Stand 2026-10-27: Software umgesetzt** (Branch `claude/acid-seq-phase-1`), Version 0.1.0.
Host-, Sanitizer- und ARM-Prüfungen bestanden. Hardwaretest NICHT GEMESSEN, Plan in
[HARDWARE_TEST.md](../../sequencers/step-seq/docs/HARDWARE_TEST.md). Abweichungen stehen im
[CHANGELOG](../../sequencers/step-seq/CHANGELOG.md): Echo-Schutz im Auto-Modus, eigene
Callback-Diagnose statt MD-Adapter, Monitor-Ereignis 7 und Zähler 16. Nach D15 folgt
Phase 2 ohne Warten auf den Hardwaretest.

**Ziel:** ein vollständiger Sequencer ohne Audio, nur CV/Gate und MIDI. Dazu
SQ-Codecs, `sq_probe.py` und ein Handshake-Test im Editor-Kern.

* **Kern** `sequencers/step-seq/include/step_seq/`:
  * Pattern-Daten und Regeln aus 4.2
  * Zustandsmaschine aus 4.3
  * Zeitbasis aus 3.3
  * Notenverfolgung und Panic aus 4.4
  * Kalibrierung und Freigabe aus 4.5
  * Ereignisausgabe
* **Adapter `src/main.cpp`:**
  * Bedienung aus 4.1
  * eigener DAC-Schreibpfad (3.3)
  * Gate- und CV-Eingänge
  * USB- und DIN-MIDI mit Sendebudget
  * LED-Mailbox nach dem Toy-Piano-Muster (höchstens alle 10 ms)
  * Bootloader-Einstieg mit Freigabe von Gates und Noten
  * Callback-Diagnose
* **Protokoll:**
  * SQ-Codec für Firmware und `sq_probe.py`.
  * Im Editor nur der Dispatcher und ein Handshake-Test für C++ und JS.
    Die UI kommt in Phase 5.
* **Tests:**
  * **Abspielen:**
    * die Beispielfolgen aus 4.2 für alle drei Modi
    * Längen 1, 7, 8, 9 und 16
    * Längen- und Moduswechsel mitten im Lauf
    * Zufall mit gesetztem Seed und Seed null
  * **Transport:**
    * jede Zeile der Übergangstabelle aus 4.3
    * Einschalten mit SW17 auf Run
    * Timeout und Rückkehr bei fester Quelle und bei Auto
    * gleichzeitige Quellen
    * kein Weitersenden empfangener Clock (Echo-Test)
  * **Timing:**
    * Akkumulator ohne Drift über 10 000 Steps
    * Swing-Formel
    * Grenzen der Gates
    * Retrigger-Lücke
    * Reihenfolge bei gleichzeitigem Gate-Off, Step und Reset
    * Startwert der Periodenschätzung
    * früher Puls
  * **Noten:**
    * Slide in einen Step mit Gate aus
    * gleiche Note mit und ohne Slide
    * Skalen und Transponierung
    * Quantisierung und Begrenzung von CV IN 1
    * Klemmbereich MIDI 0–127
    * veraltete Vorhör-Note-Offs werden ignoriert
  * **Tasten:**
    * kurz und lang
    * Akkord-Markierung bei gleichzeitigem und versetztem Drücken, mit dritter
      Taste und in allen Reihenfolgen des Loslassens
    * Poti-Bewegung während des Akkords
    * S9 + S16 unter 5 s (kein Bootloader, keine Aktion)
    * S9 + S16 ab 5 s (Bootloader-Request nach Panic)
  * **MIDI:**
    * Notenverfolgung je Ausgang
    * Panic in jedem Fall aus 4.4
    * Queue-Überlauf ohne hängende Note
    * USB-Verlust und Wiederanschluss
    * Abschalten eines Ausgangs
    * Sendebudget
  * **CV:**
    * gesperrt, freigegeben, ungültige Kalibrierdaten
    * Freigabe-Entzug bei offenem Gate
    * Gerätebindung über die UID
    * DAC-Adapter mit gefälschten Registern
  * **Protokoll:**
    * Testvektoren für C++, JS und Python
    * kaputte, zu lange, doppelte und verlorene Rahmen und Rahmen mit
      eingestreuten Echtzeit-Bytes
    * Revisionen
    * Boot-ID
  * **Golden-Test:** FNV-1a über eine feste Ereignisfolge (Pattern, 64 Steps,
    alle drei Modi).
* **Hardwaretest (Benutzer):**
  1. **Diagnose H14 zuerst** (4.5): Volt pro Code, Polarität, Verhalten beim
     Einschalten und beim Reset, Ruhecode. Erst danach CV an einen externen
     Synth anschließen.
  2. Kalibrieren und freigeben mit `sq_probe.py`.
  3. Tasten, LEDs, Akkord-Fälle, die drei Modi an SW18, Start/Stop an SW17,
     Einschalten mit SW17 auf Run.
  4. Gate an einem externen Synth. 1V/Oct über 3 Oktaven mit einem Stimmgerät.
  5. Clock an Gate IN 1 mit Pulsbreiten ab 2 ms und bis 120 Hz,
     Reset an Gate IN 2.
  6. MIDI über USB in der DAW, DIN mit einem externen Gerät.
     Synchronisation in beide Richtungen, USB abziehen und wieder anstecken.
  7. Ressourcen-PASS (Abschnitt 5).

### Phase 2: TB-303-Stimme und Firmware `acid-303`

**Stand 2026-10-27: Software umgesetzt** (Branch `claude/acid-seq-phase-2`), Version 0.1.0.
Host-, Sanitizer- und ARM-Prüfungen bestanden. Golden-Hashes und die versionierte
303-Fixture liegen vor. Hardwaretest NICHT GEMESSEN
([HARDWARE_TEST.md](../../instruments/acid-303/docs/HARDWARE_TEST.md)). Festlegungen
(Accent ab Anschlag 101, omni in der eigenen Firmware, Bezugswert des Klick-Kriteriums)
stehen im [CHANGELOG](../../instruments/acid-303/CHANGELOG.md).

* **Ziel:** die Stimme aus 4.6 als header-only Kern
  `instruments/acid-303/include/acid303/` und als eigenständiges Instrument.
* **Umfang:**
  * Kern, Adapter und CC-Tabelle (`MIDI.md`)
  * Parameter-Deskriptoren nach dem Vertrag aus Phase 0
  * Stimmenbesitz mit Herkunftskennung
  * versionierte 303-Fixture für die Auto-Level-Abnahme (Phase 3)
* **Tests:**
  * **Golden-Test:** FNV-1a über 2 s Ausgabe einer festen Notenfolge mit Accent
    und Slide, für Säge und Rechteck.
  * **Grenzen:**
    * Selbstoszillation bei maximaler Resonanz ist immer endlich, ohne NaN
      oder Inf.
    * Spitzenpegel ≤ 0 dBFS vor dem Host-Limiter.
    * Gleichanteil: |Mittelwert| < 0,001 über 1 s.
  * **Klick-Kriterium bei Parametersprüngen:** Bei einem 100-Hz-Sinus mit
    −12 dBFS als Anregung darf die größte Abweichung von Sample zu Sample
    höchstens das 1,5-Fache des eingeschwungenen Werts betragen.
  * Slide-Zeit, Legato ohne neuen Hüllkurvenstart, Accent-Wirkung.
  * Notenstapel aus zwei Quellen: Note-Off und Panic wirken je Quelle.
  * Relative CPU-Kosten im Host-Benchmark.
* **Hardwaretest (Benutzer):**
  * Hörtest von Klang, Selbstoszillation, Accent und Slide.
  * MIDI über USB und DIN.
  * Ressourcen-PASS mit Selbstoszillation und Parameterfahrten.

### Phase 3: Sättigung auslagern und Effekt `drive`

**Stand 2026-10-27: Software umgesetzt** (Branch `claude/acid-seq-phase-3`), Drive 0.1.0.
3a als eigener Commit: Tape-FX-BINs bytegleich, Hashes und Referenztests unverändert.
3b: Pegeltest ±1 dB bestanden (größte Abweichung 0,41 dB), mit zwei begründeten
Abweichungen: Die feste Kurve hängt vom Pegel in die Kurve ab, Fold auf Rechtecken bleibt
leiser ([CHANGELOG](../../effects/drive/CHANGELOG.md)). Hardwaretest NICHT GEMESSEN
([HARDWARE_TEST.md](../../effects/drive/docs/HARDWARE_TEST.md)).

* **Ziel:** die Kurven aus `tape-fx` ohne Klangänderung nach
  `common/dsp/saturation.h` verschieben und darauf den Effekt `drive` bauen.
* **Schritt 3a, Auslagern (eigener Commit):**
  * **Vorher:** Baselines erfassen. Gleicher Compiler, gleiche Flags, gleiche
    Fixtures. Ausgabe-Hashes von `tape-fx` und `toy-piano-tape`, Ergebnis der
    Referenztests, ARM-BINs.
  * **Umbau:** `tape::Saturate*` werden zu Wrappern um die gemeinsame Datei. Der
    Herkunftsvermerk für MixForge bleibt erhalten.
  * **Nachher:** Tests, Ausgabe-Hashes und BIN-Vergleich wiederholen. Ist das
    BIN nicht bytegleich, wird der Grund dokumentiert. Die Versionen bleiben
    unverändert.
* **Schritt 3b, Effekt `drive`:**
  * Kern nach FX-Vertrag 1.0, Auto-Level aus 4.7.
  * Firmware `drive-fx` für die Audioeingänge.
  * Adapter in `tests/fx/`, damit `make test-fx` ihn abdeckt.
* **Tests:**
  * **Pegeltest:** Drive 0–100 %, alle Kurven, Ausgleich 100 %.
    * Zielversatz 0 dB: Ausgangs-RMS innerhalb von ±1 dB um den Eingangs-RMS.
    * Zielversatz −1,5 dB: innerhalb von ±1 dB um Eingangs-RMS − 1,5 dB.
    * Erst mit den Fixtures aus Phase 0, zur Abnahme mit der 303-Fixture aus
      Phase 2.
  * **Nachregelung:** innerhalb von ±3 dB, hält in der Stille.
  * Überblenden ohne Sprung nach dem Klick-Kriterium aus Phase 2.
  * Gleichanteil, Endlichkeit, Golden-Test, FX-Vertragstests.
* **Hardwaretest (Benutzer):**
  * Hörtest mit Auto-Level an und aus, Zielversatz −1,5 dB.
  * Ein/Aus ohne Knacken.
  * Ressourcen-PASS.

### Phase 4: Effekt `stereo-delay`

**Stand 2026-10-27: Software umgesetzt** (Branch `claude/acid-seq-phase-4`), Version 0.1.0.
Alle Tests aus der Liste bestanden, Speicher laut ELF im SDRAM (4 608 512 Byte). Hardwaretest
NICHT GEMESSEN ([HARDWARE_TEST.md](../../effects/stereo-delay/docs/HARDWARE_TEST.md)).

* **Ziel:** das Delay aus 4.8 (Ping-Pong und Stereo L/R) als Kern nach
  FX-Vertrag 1.0 und als Firmware `delay-fx`.
* **Tests:**
  * **Impulsantwort für beide Modi:** Abstände nach der Zeit-Definition,
    Ping-Pong abwechselnd links und rechts, Stereo L/R getrennt, Abklingen
    gemäß Feedback.
  * Sync-Notenwerte bei 30–300 BPM, auch punktierte 1/1 bei 30 BPM (12 s).
  * Interpolation und Klemmung am maximalen Zeitpunkt.
  * Zeitänderung ohne Klick nach dem Klick-Kriterium.
  * Feedback 95 % mit lauter Eingabe bleibt begrenzt.
  * Dämpfung als Frequenzgang.
  * **Ausschalten:**
    * Einspeisung zuerst ausblenden, Nachhall weiter
    * Ende-Erkennung
    * Grenze von 20 s
    * Wiedereinschalten während des Nachhalls
  * Golden-Test und FX-Vertragstests.
* **Hardwaretest (Benutzer):**
  * Hörtest beider Modi, Sync mit externer Clock.
  * SDRAM-Platzierung laut ELF-Prüfung.
  * Ressourcen-PASS mit Feedback 95 %.

### Phase 5: Panel-Variante (Web, JUCE, VST3)

**Stand 2026-10-27: Software umgesetzt** (Branch `claude/acid-seq-phase-5`), Panels 0.2.0.
* **Kern:** SQ-Client, `.acidpatch` und SQ-Simulator, getestet gegen die Geräteseite der
  Firmware (C++ und JavaScript).
* **Browser:** Seiten und Offline-Ansicht, im eingebauten Browser geprüft.
* **JUCE und VST3:** Seiten und Songzustand mit Bestätigung, Selbsttest. Lokal nur gegen
  JUCE 9.0.0 gebaut. Der Build mit der gepinnten Version 9.0.4 und der Playwright-e2e-Test
  laufen lokal nicht, sie sind NICHT AUSGEFÜHRT (PR).
* Hardwaretest NICHT GEMESSEN.

* **Ziel:** Seiten für Sequencer, Stimme, Drive und Delay in allen drei
  Editor-Varianten. Sie erscheinen je nach Komponentenliste in IDENTIFY.
* **Umfang:**
  * Pattern-Raster mit 16 Steps und Notennamen
  * Lauflicht
  * Modus, Transportzustand aus 4.3 und Schalterstellungen
  * Start- und Stop-Taste (wirksam bei Freigabe)
  * Clock-Quelle und Tempo
  * Kalibrierung und Freigabe nach 4.5
  * MIDI-Einstellungen
  * Parameterseiten aus den Deskriptoren
  * Produkt-Patch laden und speichern (Datei, VST3-Songzustand) mit der
    Verbindungsregel aus 4.9
  * Fehlerzähler
* **Tests:**
  * `make -C editors/mod-on-daisy test test-sanitize`.
  * Browser-e2e (Playwright) mit simuliertem SQ-Gerät und mit einem MD-Gerät.
  * Der Selbsttest der JUCE-App läuft weiter.
  * Gemeinsame Testvektoren.
  * Die Mod-on-Daisy-Seiten und ihre Tests bleiben unverändert grün.
  * `.modpatch` bleibt lesbar.
* **Hardwaretest (Benutzer):** Panel am Kit über USB, in der DAW als VST3.
  Songwechsel und Wiederverbinden.

### Phase 6: Produkt `acid-seq`

* **Ziel:** alle Kerne in `sequencers/acid-seq/` zusammenführen, nach dem
  Muster der zusammengesetzten Produkte.
* **Umfang:**
  * Produktadapter mit Signalweg aus 4.10.
  * Tempo-Übergabe an das Delay.
  * Stimmenbesitz zwischen Sequencer und MIDI-Kanal 2.
  * Registereintrag, Erweiterung des Firmware-Prüfers, Manifest
    (`make PROJECT=acid-seq manifest`), README und CHANGELOG.
* **Tests:**
  * End-to-End-Golden: Pattern → Audio mit allen FX.
  * Die Golden-Hashes der Einzelkomponenten bleiben unverändert.
  * ELF-Prüfung mit dem Budget für das Endprodukt.
* **Hardwaretest (Benutzer):**
  * Gesamttest: drei Modi, Start/Stop, CV/Gate und MIDI gleichzeitig mit Audio.
  * Ressourcen-PASS im vollen Stressfall, Langzeitlauf von 1 h.
* **Release:** Nur der Benutzer startet einen Release, und erst nach der
  Hardware-Abnahme. Der Lieferweg wird erst noch festgelegt (4.10).

## 6. Entscheidungen

| Nr. | Thema | Stand |
| --- | --- | --- |
| D1 | SysEx-Namensraum | **Entschieden 2026-10-26 (vom Benutzer bestätigt, von Codex unterstützt):** eigener Namensraum `7D 53 51` mit gemeinsamem Parametervertrag für alle Komponenten. Bewusste Abweichung vom Audit, Aufwand und Dispatcher siehe 4.9. Kein weiteres eigenes Protokoll für Drive oder Delay |
| D2 | Start/Stop und Einschalten | **Entschieden 2026-10-26:** Start/Stop auf SW17, die Modi auf SW18. Steht SW17 beim Einschalten auf Run, startet die interne Clock nach der Initialisierung. Bei externer Clock wird scharfgeschaltet (4.3). |
| D3 | Weitere Modi | **Entschieden 2026-10-26:** zunächst drei Modi. Weitere nach Phase 6 und nur mit festgelegtem Vorrang zwischen Panel und SW18. CV/Duty entfällt, weil die Gate-Länge pro Step ihn abdeckt |
| D4 | Filter | **Entschieden 2026-10-26 (vom Benutzer bestätigt, von Codex unterstützt):** zuerst `LadderFilter`. Diode-Ladder später als absichtliche Klangänderung mit eigener Version und neuem Golden-Hash |
| D5 | Oversampling für Drive | **Entschieden 2026-10-26 (vom Benutzer bestätigt, von Codex unterstützt):** zuerst ohne. Entschieden wird nach der CPU-Messung und dem Hörtest auf der Hardware |
| D6 | Poti-Glättung | **Entschieden 2026-10-26 (vom Benutzer bestätigt, von Codex unterstützt):** in den Sequencer kopieren, mit eigenem Test und eigenen Stufen-Hysteresen. Toy Piano bleibt unverändert |
| D7 | Delay-Umfang | **Entschieden 2026-10-26 (vom Benutzer bestätigt, von Codex unterstützt):** Stereo-Delay mit den Modi Ping-Pong (Grundeinstellung) und Stereo L/R, beide in Phase 4 (4.8) |
| D8 | Lizenzen | **Entschieden 2026-10-26 (vom Benutzer bestätigt, von Codex unterstützt):** MIT für eigenen Code, AGPL für den Editor. Herkunftsvermerke bleiben |
| D9 | Namen | **Entschieden 2026-10-26:** `acid-seq`, `step-seq`, `acid-303`, `drive`, `stereo-delay` (vorher `ping-pong-delay`, wegen D7 umbenannt). Projekt-IDs `delay-fx` und `drive-fx`. |
| D10 | Clock-Grundeinstellung | **Entschieden 2026-10-26:** „Intern“. „Auto“ ist nur eine Option, mit festgelegter Rangfolge und phasenrichtigem Wechsel (4.3). |
| D11 | Skala „linear“ | **Entschieden 2026-10-26:** entfällt, weil die Tonhöhe auf Halbtöne quantisiert ist. Skalen: chromatisch, Dur, Moll. |
| D12 | Delay-Speicher | **Entschieden 2026-10-26:** volle Sync-Spanne, 12 s je Leitung, rund 4,6 MB im SDRAM, statt die Notenwerte zu begrenzen. |
| D13 | Verbinden des Panels | **Entschieden 2026-10-26:** Das Gerät hat Vorrang. Vom Panel aufs Gerät nur auf ausdrückliche Aktion. Im VST3 beim Songladen nach Bestätigung, mit der Option „immer“. |
| D14 | CI-Änderungen aus Phase 0 | **Freigegeben 2026-10-26 vom Benutzer:** Pfadfilter `sequencers/**` in `quick.yml` und neue Verbraucherregeln in `scripts/select_test_suites.py` samt Testschritten der neuen Suites im bestehenden Linux-Job (R13). Ein Job, 15-Minuten-Limit, Abbruch überholter Läufe und Doku-Ausschlüsse bleiben. |
| D15 | Ablauf der Phasen | **Entschieden 2026-10-26 vom Benutzer:** „Ziehe alle Phasen bis zur letzten durch. Dann macht Codex Fehlersuche.“ Die Phasen 1–6 werden nacheinander umgesetzt, ohne nach jeder Phase auf den Hardwaretest zu warten. Jede Phase behält ihren eigenen Branch und PR. Alle Hardwaretests bleiben NICHT GEMESSEN, bis der Benutzer sie durchführt; danach prüft Codex auf Fehler. |

## 7. Risiken

* **CV OUT unbekannt.** Spannungsbereich, Polarität, Linearität und Verhalten
  beim Einschalten sind nicht gemessen. Ohne H14 gibt es kein 1V/Oct und keinen
  sicheren Anschluss an externe Synths.
* **DIN-MIDI ist neu im Repo.** Senden und Empfangen sind nicht geprüft.
  `PollTx` blockiert `main`, das Sendebudget aus 4.4 begrenzt das.
  * Speicher: Die MIDI-Queue von libDaisy belegt 256 × 144 B = 36 KiB (von
    Codex mit der gepinnten Toolchain ermittelt). Die Hardware-Klasse enthält
    den UART-Handler bereits, es wird keine zweite Instanz angelegt.
  * DTCM-Reserve: Die 88,6 % in `docs/HARDWARE.md` gelten für Toy Piano und
    sind nicht für dieses Projekt gemessen.
* **Clock-Pulse unter 2 ms** gehen bei Abtastung pro Block verloren. Falls der
  Hardwaretest das zeigt, kommt eine Flankenerfassung per EXTI mit Zeitstempel
  in Frage.
* **Schwankung der MIDI-Ausgabe** aus `main` ist NICHT GEMESSEN.
* **CPU-Reserve** mit Ladder-Filter (4-fach oversampled), Drive, Delay und
  Sequencer ist NICHT GEMESSEN. Der Aufbau ist plausibel, aber nicht zugesagt.
* **Die Auslagerung der Sättigung** könnte den Klang von Tape FX ändern.
  Abgesichert durch die Baselines und Vergleiche in Schritt 3a.
* **SW17 startet sofort.** Steht der Schalter beim Einschalten auf Run, laufen
  Sequencer und MIDI-Ausgabe nach der Initialisierung los (D2). Die
  CV-Ausgänge bleiben gesperrt, bis sie freigegeben werden (4.5).

## 8. Liste NICHT GEMESSEN (wird in den Phasen fortgeschrieben)

| Größe | Phase | Status |
| --- | --- | --- |
| CV OUT 1/2: Volt pro Code, Polarität, Linearität, Verhalten beim Einschalten und Reset, Ruhecode | 1 (H14) | NICHT GEMESSEN |
| CV IN 1/2 und Gate IN 1/2: Bereich, Schwelle, kürzeste erkannte Pulsbreite | 1 | NICHT GEMESSEN |
| 1V/Oct-Genauigkeit nach Kalibrierung | 1 | NICHT GEMESSEN |
| DIN-MIDI senden und empfangen | 1 | NICHT GEMESSEN |
| Schwankung der MIDI-Clock (Ein- und Ausgang) | 1 | NICHT GEMESSEN |
| Klang der Stimme, Selbstoszillation, Accent, Slide | 2 | NICHT GEMESSEN |
| Auto-Level im Hörtest, Zielversatz | 3 | NICHT GEMESSEN |
| Delay im Hörtest, Sync | 4 | NICHT GEMESSEN |
| Callback-Spitzen, Stack-Hochwasserstand, Queue-Belegung je Firmware | 1–6 | NICHT GEMESSEN |

## 9. Einarbeitung des Codex-Reviews

| Befund | Schwere | Eingearbeitet in |
| --- | --- | --- |
| R01 DAC-HAL-Lock im Callback | P1 | 2, 3.3 (eigener DAC-Schreibpfad), Phase 1 |
| R02 Delay-Speicher für Sync | P1 | 4.8, D12, Phase 4 |
| R03 Transport-Zustände, Clock-Ausfall, Auto | P2 | 4.3, D2, D10, Phase 1 |
| R04 Zeitbasis, Gate-Grenzen, Swing, Clock-Pulse | P2 | 3.3, 4.3, Phase 1, Risiken |
| R05 MIDI-Ausgabe, Panic, DIN-Timeout | P2 | 2, 4.4, Phase 1 |
| R06 Pattern-Regeln, Notenbereich, Vorhören | P2 | 4.1, 4.2, D11, Phase 1 |
| R07 Akkorde und Bootloader-Geste | P2 | 4.1, Phase 1 |
| R08 Kalibrierung und Freigabe | P2 | 4.5, Phase 1 |
| R09 Session und Dispatcher, D1 | P2 | 4.9, D1, Phase 0/1 |
| R10 Rahmen, Fragmente, atomare Übernahme | P2 | 3.2, 4.9, Phase 1 |
| R11 Abhängigkeiten der Phasen, Fixtures | P2 | 5 (Abhängigkeiten), Phase 0, Phase 3 |
| R12 Register, Versionsorte, Firmware-Prüfer | P2 | 3.1, Phase 0, Phasen 1–6 |
| R13 CI-Verbraucherregeln | P2 | 2, Phase 0 |
| R14 Spillover-Reihenfolge, Auto-Level-Messort, Schwellwerte | P2 | 4.7, 4.8, Phasen 2–4 |
| R15 Ressourcen-PASS | P2 | 5, Risiken |
| R16 Produkt-Patch, Verbinden, Stimmenbesitz | P2 | 4.6, 4.9, D13, Phase 6 |
| P3 Aussage zum Looper | P3 | 4.7 |
| P3 Lieferweg für Releases | P3 | 4.10, Phase 6 |
