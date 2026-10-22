# Mod on Daisy — Architekturkonzept

**Stand:** 22. Oktober 2026 · **Phase:** 1 (Architekturkonzept) · **Ursprung:** Diskussion „Stardust on Daisy“, 19.10.2026
**Ziel:** Universelle, wiederverwendbare Modulationsmatrix für Instrumente und Effekte auf dem Seed3 Desktop Dev Kit.
**Status:** Konzept. Es gibt noch keinen Code, keine Messung und keine beschlossene Schnittstelle.

Dieses Dokument beantwortet die acht offenen Fragen der Konzeptphase und schlägt für jede
eine Lösung vor. Die Vorschläge sind Entscheidungsvorlagen. Was davon gilt, legt der
Projektinhaber fest (siehe [§ 13](#13-entscheidungsbedarf)). Das MIDI-Protokoll steht im
Detail in [PROTOCOL.md](PROTOCOL.md).

**Evidenzklassen** (wie in der Stardust-Machbarkeitsstudie):
`BELEGT` = im gepinnten libDaisy-Stand oder im Repository nachgelesen, mit Fundstelle;
`SCHÄTZUNG` = gerechnet, aber nicht gemessen; `VORSCHLAG` = Entwurfsentscheidung, noch nicht
beschlossen; `OFFEN` = nur durch Hardwaretest oder spätere Entscheidung zu klären.

---

## 0. Kurzfassung

| Frage | Vorschlag in einem Satz |
| --- | --- |
| Architektur | Header-only C++17-Kern ohne libDaisy-Abhängigkeit: **Quellen → kompiliertes Routing → normierte Ziel-Offsets**. Instrumente beschreiben ihre Ziele in einer `constexpr`-Tabelle. |
| DSP-Raten | **Control-Rate ≈ 1 kHz als Standard** mit Glättung im Ziel; Audio-Rate nur als Opt-in für einzeln markierte Ziele; diskrete Ziele mit Hysterese. |
| MIDI-Protokoll | **Selbstbeschreibendes SysEx-Protokoll** (Hersteller-ID `0x7D`) für die Konfiguration; normale CCs als Live-Modulationsquellen. |
| GUI | Zielansicht mit **Modulationsringen und Drag & Drop** plus **Routing-Tabelle**. Das Layout wird automatisch aus den Zielbeschreibungen des Geräts erzeugt. |
| VST3 | **JUCE**: Standalone und VST3 entstehen aus einer Codebasis. Der Protokoll-Codec ist derselbe C++-Code wie in der Firmware. |
| Integration | Etwa 50–100 Zeilen pro Instrument: Zieltabelle, `Tick()` im Audio-Callback, `Apply()` an der Stelle, an der heute der Basiswert gelesen wird, und MIDI-Weiterleitung im Main-Loop. |
| Ressourcen | `SCHÄTZUNG`: ca. 6 KB RAM, **< 0,5 % CPU** bei 32 Routen und 1 kHz. Die Kosten der Audio-Rate hängen fast nur davon ab, wie teuer das Ziel jeden Wert verarbeitet. |
| Speicherung | Versionierte Patches mit CRC-32 in einem reservierten **QSPI-Bereich** (8 Slots und ein Einschalt-Patch) plus JSON-Dateien im Editor. Gespeichert wird nur auf ausdrücklichen Befehl. |

---

## 1. Ausgangslage im Repository

Diese Fakten bestimmen den Entwurf. Alle sind `BELEGT` (Fundstellen in Klammern).

| Thema | Befund | Folge für Mod on Daisy |
| --- | --- | --- |
| CPU | Cortex-M7, 480 MHz, FPv5-D16, Hard-Float, kein NEON ([BUILD.md](../../../docs/BUILD.md), [FEASIBILITY](../../../instruments/looper/docs/FEASIBILITY_2026-10-19.md)) | Skalare Float-Arithmetik ist billig. SIMD gibt es nicht. Teuer sind `powf`/`expf` pro Wert. |
| Blockgrößen | Toy Piano / Looper / Physical Modeling 48, Reverb 32, Nebulae 256 Samples (`main.cpp` der Projekte) | Die Matrix darf **keine feste Blockgröße** voraussetzen. Der Takt muss unabhängig vom Block sein. |
| Interne Quanten | Physical Modeling rechnet in 12-Sample-Quanten (4 kHz) und glättet Parameter mit 5 ms ([ARCHITECTURE](../../../instruments/physical-modeling/docs/ARCHITECTURE.md), `parameter_manager.h`) | Viele Instrumente glätten bereits selbst. Die Matrix liefert dort nur Offsets und muss nicht noch einmal glätten. |
| CV-Eingänge | 2 × `AnalogControl::InitBipolarCv` (−1…+1), werden nur in `UpdateAllControls()` verarbeitet, also einmal pro Audio-Block (`libDaisy/src/desktop_devkit.cpp`) | **CV-Bandbreite ≤ halbe Blockrate**: 500 Hz bei Block 48, etwa 94 Hz bei Block 256. CV ist auf dieser Hardware **keine Audio-Rate-Quelle**. |
| Gate-Eingänge | 2 × `GateIn` (D0, D18). `Trig()` vergleicht nur mit dem Zustand beim letzten Aufruf (`gatein.cpp`) | Impulse, die kürzer als ein Blockintervall sind, können verloren gehen. Jitter bis zu einem Block. `OFFEN`: Ob EXTI-Interrupts auf den Gate-Pins nötig sind, zeigt erst der Hardwaretest. |
| CV-Ausgänge | 2 × 12-Bit-DAC, Polling-Modus (`SetDacOut`) | Später als **Modulationsziel nach außen** möglich, etwa für ein LFO an den Eurorack (Phase 6). |
| MIDI | USB-MIDI (J7) wird im USB-Interrupt geparst und im Main-Loop abgeholt. `SendMessage` kann blockieren und darf nur im Main-Loop laufen ([HARDWARE.md](../../../docs/HARDWARE.md)). DIN-MIDI über UART ist initialisiert. | Das Protokoll läuft vollständig im Main-Loop. Antworten sind begrenzt und rate-limitiert. |
| SysEx-Empfang | `SYSEX_BUFFER_LEN 128`. Längere Nachrichten werden **stillschweigend abgeschnitten** und trotzdem als vollständig gemeldet (`MidiEvent.h`, `midi_parser.cpp`) | Protokollrahmen **≤ 120 Byte** plus Prüfsumme, damit abgeschnittene Rahmen sicher erkannt werden. |
| SysEx-Senden | `MidiToUsb` trennt den Sendepuffer an jedem Statusbyte, also auch vor `F7`. Der letzte Datenblock bekommt dadurch den CIN „SysEx-Ende“ ohne `F7`, danach folgt `F7` als Einzelbyte-Paket (`usb_midi.cpp`) | Laut Code-Lesung nicht spezifikationskonform. Der zusammengesetzte Bytestrom kann trotzdem stimmen. `OFFEN`: Test mit Windows und macOS. Rückfallebene: eigenes USB-Paket-Framing wie beim lokalen Parser-Fix in Physical Modeling. Physical Modeling meidet SysEx deshalb bisher (`cpu_telemetry.h`). |
| CC-Belegung | Physical Modeling CC20–33 und Kanal 16 CC100–119 (Telemetrie). Nebulae CC20–27 | Die Standard-Quell-CCs von Mod on Daisy **liegen außerhalb dieser Bereiche** (CC1, CC16–19, siehe [PROTOCOL.md](PROTOCOL.md)). |
| Speicher | DTCM ist knapp: Toy Piano belegt 88,6 %, Nebulae 77 KB von 128 KB. AXI-SRAM und SDRAM sind reichlich vorhanden ([HARDWARE.md](../../../docs/HARDWARE.md), [MEMORY_BUDGET](../../../instruments/granular/docs/MEMORY_BUDGET.md)) | Nur der heiße Zustand des Audiopfads (etwa 3 KB) gehört in DTCM. Die Platzierung wählt das Instrument per Section-Makro. |
| QSPI | Linkerskript: Anwendung ab `0x90040000`, 7936 KiB. `PersistentStorage` ist vorhanden, `Save()` blockiert (`STM32H750IB_sram.lds`, `PersistentStorage.h`) | Presets in einem reservierten Bereich am oberen Ende des Flashs. Speichern nur auf Befehl und nur im Main-Loop. |
| Echtzeitregeln | Alle Projekte: kein Heap, keine Locks, keine USB- oder Dateizugriffe im Audio-IRQ, feste Schleifengrenzen, SPSC-Queues, Release/Acquire-Atomics | Mod on Daisy übernimmt diese Regeln unverändert (§ 6). |

---

## 2. Architektur (Frage 1)

### 2.1 Schichten und Zuständigkeiten

```text
                 ┌──────────────── Main-Loop (Thread-Kontext) ────────────────┐
  Editor/DAW ──► │ mod::Controller                                            │
  (USB/DIN-MIDI) │  · Protokoll-Codec (SysEx)    · Patch (Arbeitskopie)       │
                 │  · Compiler Patch → Program   · Storage (QSPI-Adapter)     │
                 └───────────────┬───────────────────────┬────────────────────┘
                     Triple-Buffer│(Program, lock-free)   │ atomare CC-Werte
                 ┌───────────────▼───────────────────────▼────────────────────┐
  CV/Gate ─────► │ mod::Engine (Audio-IRQ, einziger Schreiber seines Zustands)│
  (Adapter)      │  SourceBank ──► Routen (kompiliert) ──► Ziel-Akkumulatoren │
                 └───────────────────────────────────────────┬────────────────┘
                                                              │ Offset(target)
                 ┌────────────────────────────────────────────▼────────────────┐
                 │ Instrument-DSP: value = Apply(target, base) → eigene Abbildung│
                 └─────────────────────────────────────────────────────────────┘
```

| Komponente | Verantwortung | Läuft in | Abhängigkeiten |
| --- | --- | --- | --- |
| `SourceBank` | LFOs, Zufall, Hüllkurven, CV, Gate, MIDI-Werte → ein Wert pro Quelle und Tick | Audio-IRQ | keine |
| `Program` | Kompilierte, unveränderliche Routenliste und Quellparameter | wird im Main-Loop erzeugt und im Audio-IRQ gelesen | keine |
| `Engine` | Quellen berechnen, Routen summieren, Offsets je Ziel bereitstellen | Audio-IRQ | `SourceBank`, `Program` |
| `TargetInfo[]` | Instrumentenseitige `constexpr`-Beschreibung der modulierbaren Parameter | Flash/Image | keine |
| `Patch` | Editierbare Konfiguration: Slots, Quellparameter, Name, Version, CRC | Main-Loop | keine |
| `Controller` | SysEx-Protokoll, Transaktionen, Kompilieren, Veröffentlichen, Speichern | Main-Loop | Codec, Storage-Interface |
| `protocol/` | Kodierung und Dekodierung der Rahmen. **Gleicher Code in Firmware, Host-Tests und Editor** | überall | nur `<cstdint>` |
| `adapters/desktop_devkit.h` | Liest CV und Gate aus `daisy::desktop_devkit::Hardware`; QSPI-Backend | Audio-IRQ / Main | **einzige** libDaisy-Abhängigkeit |
| Editor / VST3 | GUI, Preset-Bibliothek, MIDI-Port | Desktop | JUCE + `protocol/` |

Daraus folgt die geforderte Trennung von Quellen, Matrix, Zielen, MIDI-Kommunikation,
GUI und instrumentspezifischer DSP. Der Kern lässt sich ohne Hardware auf dem Host testen,
wie alle anderen Projekte im Repository.

### 2.2 Modulationsziele: normierter Parameterraum

`VORSCHLAG`: Die Matrix moduliert **im normierten Raum des Instruments (0…1)** und
**vor** dessen eigener Abbildung auf physikalische Größen:

```text
value_norm = clamp(base_norm + Σ(route.depth · source · via), 0, 1)
physical   = instrument_mapping(value_norm)   // z. B. exp. Frequenz, Taper, Stufen
```

Begründung:

* Alle bestehenden Instrumente führen ihre Parameter bereits normiert. Beispiele sind
  `ParameterManager::target_` in Physical Modeling und `Parameters::primary` in Nebulae.
  Die Matrix muss deshalb keine Einheiten kennen.
* Musikalisch sinnvolle Kurven bleiben erhalten: Eine lineare Modulation im normierten
  Raum wirkt bei exponentieller Abbildung als Modulation in Oktaven.
* Die Summe wird **einmal pro Ziel** begrenzt, nicht pro Route. Zwei gegenläufige
  Routen heben sich also korrekt auf.

Ausnahme: Bipolare Ziele um eine Mitte, etwa Pitch-Offset in Halbtönen oder Pan, setzen
`bipolar` und `range`. Dann bedeutet Tiefe 1,0 zum Beispiel ±24 Halbtöne. Die Umrechnung
übernimmt das Instrument, die Matrix bleibt einheitenfrei.

Eine Zielbeschreibung (`VORSCHLAG`, vereinfacht):

```cpp
namespace mod {
enum class Rate : uint8_t { Control, Audio, Event };       // § 3
enum class Kind : uint8_t { Continuous, Stepped, Toggle };
struct TargetInfo {
    uint16_t    id;          // stabil über Firmwareversionen, nie wiederverwenden
    const char* key;         // stabiler Text, z. B. "brightness" (Preset-Migration)
    const char* name;        // Anzeigename im Editor, ≤ 16 Zeichen
    uint8_t     group, order;// automatisches Editor-Layout
    Kind        kind;  uint8_t steps;   // Stepped: Stufenzahl für Quantisierung
    Rate        max_rate;    // höchste Rate, die das Ziel sinnvoll verarbeitet
    bool        bipolar;
    float       display_min, display_max; uint8_t unit; // nur für die Anzeige
};
}
```

**Stabile IDs statt Indizes**: Patches referenzieren Ziele über `id`. Ein Firmware-Update
darf deshalb Ziele umsortieren oder ergänzen, ohne gespeicherte Patches zu zerstören.
Entfernte Ziele werden beim Kompilieren deaktiviert und dem Editor gemeldet.

### 2.3 Modulationsquellen

Jede Quelle liefert pro Tick genau einen Wert. Er ist entweder **bipolar (−1…+1)** oder
**unipolar (0…1)**, und die Quelle meldet ihre Polarität. Jede Route kann die Polarität
umrechnen (`uni→bi`, `bi→uni`). Damit wird zum Beispiel ein LFO zu einer reinen
Aufwärtsmodulation.

| Typ | Varianten / Parameter | Rate | Kosten pro Tick (`SCHÄTZUNG`) |
| --- | --- | --- | --- |
| **LFO** | Sinus, Dreieck, Sägezahn auf/ab, Rechteck mit Pulsbreite, S&H; Rate frei oder synchron (MIDI-Clock, Gate-Takt); Phase; Retrigger durch Note oder Gate | Control, Audio | 20–40 Zyklen |
| **Random** | Sample & Hold mit Rate, geglättetes Zufallssignal, Drift (langsamer Random Walk) | Control | 10–20 |
| **Hüllkurve** | AD / ADSR, Auslösung durch Gate oder Note | Control, Audio | 15–25 |
| **CV 1/2** | bipolar, mit Kalibrierung für Offset und Skalierung (wichtig für spätere 1 V/Okt) | Block | 5 |
| **Gate 1/2** | 0/1 als Wert, außerdem Trigger für LFO, Hüllkurve und S&H | Block | 5 |
| **MIDI** | Mod-Wheel, Aftertouch, Velocity, Keytrack, Pitch Bend, 4 frei zuweisbare CCs (7- oder 14-Bit) | Ereignis, gehalten | 5 |
| **Makro / Konstante** | fester Wert oder Poti des Instruments (wenn freigegeben) | Block | 2 |

`VORSCHLAG` für die Erstausstattung in Phase 2: 2 CV, 2 Gate, 4 LFO, 2 Random,
2 Hüllkurven und 9 MIDI-Quellen, zusammen 21 Quellen. Die Anzahl jeder Gattung ist ein
Template-Parameter. Ein Instrument ohne Gate-Nutzung kann also 0 Gates einkompilieren.

**Erweiterbarkeit**: Ein neuer Quellentyp ist eine Struktur mit `Reset()`,
`Process(n_samples, ctx)` und optional `ProcessAudio(out, n)`. Dazu kommt ein Eintrag in
der Typliste der `SourceBank`. Weder Matrix noch Protokoll ändern sich, weil der Editor
die Quellen über `GET_SOURCE_INFO` abfragt. Im Audiopfad gibt es **keine virtuellen
Aufrufe**. Jede Gattung wird als eigenes Array in einer festen Schleife verarbeitet.

**Nur benutzte Quellen rechnen**: Der Compiler erzeugt eine Bitmaske der Quellen, die
mindestens eine aktive Route speist. Unbenutzte LFOs kosten dann nur ihren
Phasen-Fortschritt (eine Addition). So bleiben sie beim späteren Aktivieren in Phase
mit einem Takt.

### 2.4 Routen, Slots und Kompilierung

Eine Route im Patch (Editor-Sicht, **8 Byte**):

| Feld | Typ | Bedeutung |
| --- | --- | --- |
| `source` | u8 | Quellindex (0xFF = leer) |
| `target_id` | u16 | stabile Ziel-ID |
| `via` | u8 | optionale zweite Quelle, die die Tiefe moduliert, z. B. Mod-Wheel regelt die LFO-Tiefe (0xFF = keine) |
| `depth` | s16 | Tiefe −1…+1 in Q15 (Protokoll: 14 Bit) |
| `flags` | u8 | aktiv, Polaritätsumrechnung, invertieren, Audio-Rate gewünscht |
| `reserved` | u8 | später: Kurve, Slew |

`VORSCHLAG`: **32 Slots** pro Patch, als Template-Parameter änderbar. 32 Slots reichen für
komplexe Patches und bleiben in einer Tabelle überschaubar.

Der Audio-IRQ arbeitet **nie direkt auf den Slots**. Nach jeder Änderung erzeugt der
Main-Loop daraus ein `Program`:

1. Leere und deaktivierte Slots entfernen. Ziel-IDs in Indizes auflösen; unbekannte IDs
   deaktivieren und melden.
2. Tiefe als `float` vorberechnen, Polarität als Offset und Faktor vorberechnen
   (`s' = a·s + b`). So entfallen Verzweigungen im Audiopfad.
3. Nach Ziel sortieren, damit jeder Akkumulator zusammenhängend geschrieben wird.
4. Quellmaske, Liste der aktiven Ziele und Audio-Rate-Liste erzeugen.
5. Quellparameter in Inkremente umrechnen, zum Beispiel LFO-Hz in Phase pro Sample.
   Damit bleiben `powf` und Divisionen aus dem Audiopfad.

Die Übergabe läuft über einen **lock-freien Triple-Buffer**. Der Main-Loop schreibt in
den hinteren Puffer und tauscht ihn atomar mit dem mittleren. Der Audio-IRQ holt sich zu
Beginn eines Ticks den mittleren, sofern er neu ist. Keine Seite wartet je auf die
andere, und bei schnellem Drehen im Editor gewinnt immer der letzte Stand. Quellzustände
wie die LFO-Phase liegen in der `Engine`, nicht im `Program`. Ein neues Patch setzt
laufende LFOs also nicht zurück.

### 2.5 Schnittstelle für Instrumente (Entwurf)

```cpp
// Audio-Seite: im Audio-Callback, einziger Besitzer.
template<size_t kTargets, size_t kSlots = 32, size_t kAudioTargets = 0>
class Engine {
  public:
    void Init(const TargetInfo (&targets)[kTargets], float sample_rate);
    void SetHardware(const HardwareFrame& hw);   // CV/Gate aus dem Adapter, einmal pro Block
    void Tick(size_t n_samples);                  // Quellen + Routen; n beliebig (12…256)
    float Offset(size_t target) const;            // Σ der Routen, ungeklemmt
    float Apply(size_t target, float base) const; // clamp(base + Offset) bzw. quantisiert
    const float* AudioOffsets(size_t audio_slot) const; // nur wenn kAudioTargets > 0
    void NoteOn(uint8_t note, uint8_t velocity);  // Velocity/Keytrack/Retrigger
    void NoteOff(uint8_t note);
};

// Main-Seite: im Main-Loop.
class Controller {
  public:
    template<class E> void Init(E& engine, Storage* storage /* nullptr = keine Persistenz */);
    bool HandleMidi(const MidiBytes& message); // true = von Mod on Daisy verbraucht
    void Service(uint32_t now_ms, MidiOut& out); // begrenzte Arbeit, ≤ 1 Antwort/ms
};
```

---

## 3. Audio-Rate, Control-Rate und Ereignisse (Frage 2)

### 3.1 Wo die Kosten tatsächlich entstehen

`SCHÄTZUNG`: Eine Route kostet pro Auswertung etwa 5–10 Zyklen (Laden, Multiplizieren,
Addieren). Selbst bei Audio-Rate sind das bei 48 kHz nur 0,05–0,1 % CPU pro Route. **Teuer
ist die Matrix selten. Teuer ist, was das Ziel mit jedem neuen Wert macht**:
Filterkoeffizienten mit `tanf`, exponentielle Frequenzabbildung, Neuberechnung von
Resonator-Moden, Grain-Scheduling. Deshalb ist die Rate **eine Eigenschaft des Ziels**.
Das Instrument legt mit `max_rate` fest, wie oft es einen Wert sinnvoll verarbeiten kann.

Hinzu kommt: Viele vorhandene DSP-Engines lesen Parameter ohnehin nur blockweise.
Torus in Physical Modeling liest pro 12-Sample-Quantum, Nebulae pro Block oder bei
Grain-Start. Dort wäre Audio-Rate-Modulation **wirkungslos**, solange die Engine nicht
selbst umgebaut wird.

### 3.2 Drei Ratenklassen

| Klasse | Takt | Typische Ziele | Glättung |
| --- | --- | --- | --- |
| **Control** (Standard) | Matrix-Tick ≈ 1 kHz | Cutoff, Brightness, Damping, Grain-Größe und -Dichte, Mix, Feedback, Varispeed, Pan | Das Ziel glättet bereits (z. B. 5 ms in Physical Modeling) oder liest `Offset()` als lineare Rampe über den Tick |
| **Audio** (Opt-in) | pro Sample, Puffer der Länge `n` | Pitch/FM eines eigenen Oszillators, VCA/Tremolo > 20 Hz, Ringmodulation, Delay-Zeit mit Interpolation | keine; Quelle muss bandbegrenzt sein |
| **Event** | Tick, mit Quantisierung und Hysterese | Modellwahl, Akkord, Polyphonie, Modi, Puffer-Längen | Hysterese ±½ Stufe; Übergänge (Crossfade) übernimmt das Instrument |

**Tick-Länge**: Die Engine bekommt die tatsächliche Sampleanzahl `n` des Ticks. Phasen
und Hüllkurvenzeiten stimmen deshalb bei jeder Blockgröße. `VORSCHLAG`: Ziel-Tick
**0,75–1,5 kHz**. Konkret:

| Instrument | Block | Empfohlener Tick |
| --- | --- | --- |
| Toy Piano, Looper | 48 | 1 Tick pro Block (1 kHz) |
| Physical Modeling | 48, Quanten à 12 | 1 Tick pro Block, Offsets in allen 4 Quanten lesen. Ein Tick pro Quantum ginge auch, kostet aber das Vierfache |
| Ambient Reverb | 32 | 1 Tick pro Block (1,5 kHz) |
| Nebulae | 256 | 4 Ticks à 64 Samples (750 Hz), sonst nur 187,5 Hz |

**Grenze der Control-Rate**: Mit linearer Interpolation über den Tick sind LFO-Raten bis
etwa Tick/20, also **~50 Hz bei 1 kHz**, frei von hörbaren Treppenstufen (`SCHÄTZUNG`,
in Phase 4 hören und messen). Schnellere Modulation ist Audio-Rate und nur auf Zielen mit
`max_rate = Audio` erlaubt. Der Editor begrenzt den LFO-Ratenbereich je nach Ziel.

**Audio-Rate-Pfad** (`VORSCHLAG`: API ab Phase 2 reservieren, Umsetzung in Phase 4): Für
jedes Ziel mit Audio-Rate-Routen schreibt die Engine pro Tick einen Puffer aus `n` Werten.
Er setzt sich zusammen aus dem Control-Anteil als lineare Rampe und dem pro Sample
berechneten Anteil der Audio-Quellen. Nur LFO und Hüllkurve sind Audio-Quellen; CV ist es
wegen der Blockabtastung nicht (§ 1). Die Anzahl der Audio-Ziele ist ein
Compile-Time-Parameter, Standard 0. Ohne Audio-Rate entsteht dann auch kein Code und kein
RAM-Verbrauch.

### 3.3 Sprünge und Zipper-Geräusche vermeiden (Frage 6.5)

1. **Glättung im Ziel**, nicht in der Matrix: Wer bereits glättet, braucht keine zweite
   Stufe. Wer nicht glättet, liest die Rampe von `Offset()`.
2. **Begrenzen nach der Summe**, mit Snap an den Endpunkten wie im bestehenden
   `ParameterManager`.
3. **Sprunghafte Quellen** (S&H, Gate, Rechteck) springen absichtlich. Optionaler Slew pro
   Route (`reserved`-Feld) für weiche Varianten.
4. **Stufenziele** mit Hysterese. Modellwechsel laufen über die vorhandenen Crossfades des
   Instruments; Physical Modeling blendet bereits 4 ms aus und wieder ein. Im Editor
   werden Stufenziele markiert.
5. **Patch-Wechsel**: Das neue `Program` greift am Tick-Anfang. Der Sprung des Offsets
   wird von der Zielglättung abgefangen. Quellphasen laufen weiter.
6. **Determinismus**: feste Schleifengrenzen, keine datenabhängigen Pfade mit Ausreißern,
   keine Allokation. Eine Spitzenlast im Tick wäre ein Fehler.

---

## 4. MIDI-Protokoll (Frage 3)

Details stehen in [PROTOCOL.md](PROTOCOL.md). Die Grundzüge:

* **Zwei Ebenen**:
  * **Live-Ebene** aus normalen MIDI-Nachrichten. CCs, Aftertouch und Velocity sind
    *Quellen*. Sie funktionieren mit jeder DAW auch ohne Editor.
  * **Konfigurationsebene** aus SysEx. Darüber laufen Quellen, Ziele, Slots, Presets und
    Monitoring.
* **Selbstbeschreibend**: Das Gerät meldet Instrument, Firmwareversion, Anzahl der Quellen
  und Ziele sowie einen **Schema-Hash** (CRC-32 der Beschreibungstabellen). Der Editor
  liest einmal alle Beschreibungen und speichert sie zwischen. **Ein neues Instrument
  braucht keinen Editor-Code.**
* **Robust trotz der gepinnten libDaisy-Grenzen**: Rahmen ≤ 120 Byte, 7-Bit-Prüfsumme,
  Sequenznummer, ACK/NAK und Wiederholung durch den Editor. Abgeschnittene Rahmen werden
  dadurch sicher verworfen.
* **Keine ungefragten Nachrichten**. Ausnahme ist ein abonniertes Monitoring, das ohne
  Keepalive nach 3 s endet. Das folgt dem Prinzip von Physical Modeling: keine
  Telemetrie ohne Anfrage.
* Hersteller-ID **`0x7D`**, im MIDI-Standard für nicht-kommerzielle und Entwicklungszwecke
  reserviert. Für ein veröffentlichtes Produkt wäre eine eigene ID nötig.

---

## 5. Editor, GUI und VST3 (Fragen 4 und 5)

### 5.1 Technologie

| Option | Vorteile | Nachteile |
| --- | --- | --- |
| **JUCE (C++)** — `VORSCHLAG` | Standalone **und** VST3/AU aus einem Projekt; derselbe C++-Codec wie in der Firmware; MIDI-Ports auf Windows und macOS; ausgereifte GUI | Lizenz prüfen (AGPLv3 oder kommerziell, mit kostenloser Einstiegsstufe); längere Einarbeitung als bei Web-Technik |
| Web-App (Web MIDI) | Keine Installation, schnelle Iteration | Kein VST3; SysEx nur nach Freigabe und nur in Chromium-Browsern zuverlässig; Codec doppelt (JS und C++) |
| Python/Qt | Schnell prototypisiert | Kein VST3-Weg; Codec doppelt |

Empfehlung: **JUCE** mit einer Codebasis und den Zielen *Standalone* und *VST3*.
Begonnen wird mit Standalone. Das beantwortet die offene Frage „VST3 direkt oder zuerst
eigenständig“: Beides entsteht aus demselben Code, VST3 ist ein zusätzliches Build-Ziel.
Das VST3-SDK steht laut Steinberg seit Version 3.8 unter MIT-Lizenz; das ist vor
Projektstart zu bestätigen.

### 5.2 Bedienkonzept

* **Zielansicht** (Hauptansicht): Die Parameter des Instruments erscheinen als Regler,
  gruppiert nach `group` und `order` aus den Zielbeschreibungen. Ein farbiger **Ring**
  zeigt je Quelle die Modulationstiefe, eine Live-Markierung den aktuellen Wert. Eine
  Route entsteht, indem man einen **Quell-Chip** per **Drag & Drop** auf einen Regler
  zieht; ziehen am Ring ändert die Tiefe. Das Muster ist aus Bitwig und Vital bekannt.
* **Routing-Tabelle** zum präzisen Arbeiten, eine Zeile pro Slot: `Quelle | Via | Tiefe |
  Ziel | Polarität | Aktiv`. Sortierbar und filterbar nach Quelle oder Ziel.
* **Quellen-Panel**: LFO-Editor mit Kurvenvorschau, Rate frei oder synchron, Phase und
  Retrigger. Dazu Random- und Hüllkurven-Editor, Kalibrierung der CVs und Live-Anzeige
  von CV und Gate.
* **Optionale Matrix-Übersicht** (Quellen × Ziele) als Gesamtbild.
* **Presets**: Bibliothek als JSON-Dateien, Speicher-Slots im Gerät, Vergleich zwischen
  Editor und Gerät.
* **Offline-Bearbeitung** ohne Gerät. Beim Verbinden fragt der Editor nach der Richtung:
  „Vom Gerät laden“ oder „Zum Gerät senden“. Undo/Redo gibt es nur im Editor.
* Warnungen bei Stufenzielen, bei CC-Konflikten mit den reservierten CCs des Instruments
  und bei LFO-Raten über der Control-Grenze.

### 5.3 VST3-Besonderheiten (`OFFEN`, früh prüfen)

1. **Portzugriff**: Unter Windows sind klassische WinMM-MIDI-Ports **nicht mehrfach
   nutzbar**. Hat Studio One „Daisy Seed External“ geöffnet, kann der Editor den Port nicht
   öffnen. Die Toy-Piano-Dokumentation beschreibt bereits konkurrierende Anwendungen.
   Mögliche Wege sind Windows MIDI Services (mehrfach nutzbar, Verfügbarkeit prüfen), DIN
   MIDI als zweiter physischer Pfad für den Editor, oder das Plugin öffnet den Port selbst
   und die DAW nicht.
2. **SysEx über die DAW**: VST3 kennt SysEx-Ereignisse, doch DAWs leiten SysEx aus Plugins
   unterschiedlich weiter. Studio One ist zu testen.
3. **Mehrwert im Plugin**: Das Patch wird im Projekt gespeichert und beim Laden des
   Projekts an das Gerät gesendet. Makro-Automation läuft über die Live-CCs.

→ Am Anfang von Phase 3 steht deshalb ein **kurzer technischer Test** dieser drei Punkte
unter Windows und Studio One, bevor die GUI entsteht.

---

## 6. Echtzeitregeln (gelten für jeden Code von Mod on Daisy)

Die Regeln entsprechen den bestehenden Projekten:

* Im Audio-IRQ: kein Heap, keine Locks, kein USB, kein QSPI, kein Logging, keine
  Division oder `powf` pro Route, feste Schleifengrenzen, keine virtuellen Aufrufe.
* Der Audio-IRQ ist alleiniger Besitzer des `Engine`-Zustands. Der Main-Loop ist
  alleiniger Besitzer von `Patch`, `Controller` und Storage.
* Datenaustausch nur über den Triple-Buffer (`Program`), atomare 32-Bit-Werte (MIDI-CC als
  Quelle) und die vorhandenen SPSC-Queues des Instruments (Noten).
* Alle Atomics sind `is_always_lock_free` und per `static_assert` geprüft.
* Platzierung im Speicher per Makro (`MOD_ON_DAISY_HOT` für DTCM, Standard AXI-SRAM).
  Das Instrument entscheidet nach seinem DTCM-Budget.

---

## 7. Integration in bestehende Instrumente (Frage 6)

### 7.1 Ablauf

1. **Zieltabelle** `constexpr mod::TargetInfo kModTargets[]` anlegen, IDs ab 1 vergeben
   und nie wiederverwenden.
2. **Einbinden**: im Makefile `C_INCLUDES += -I../../modules/mod-on-daisy/include`,
   Header-only, kein zusätzliches Archiv.
3. **Audio-Callback**: nach `hw.UpdateAllControls()` → `engine.SetHardware(adapter(hw))`
   → `engine.Tick(n)` → an den Stellen, an denen heute `target_[i]` gelesen wird,
   `engine.Apply(i, target_[i])` verwenden.
4. **Main-Loop**: MIDI-Ereignisse zuerst an `controller.HandleMidi()` geben. Was nicht
   verbraucht wird, verarbeitet das Instrument wie bisher. Danach `controller.Service()`.
5. **Optional**: QSPI-Storage, Gate- und CV-Freigabe, Poti-Makros.
6. **Host-Tests**: Die vorhandenen Fakes (`tests/fakes`) bekommen einen Hardware-Rahmen
   für CV und Gate.

### 7.2 Beispiel Physical Modeling

`ParameterManager::Advance()` glättet bereits mit 5 ms pro Quantum. Die Integration
besteht im Wesentlichen aus einer Zeile:

```cpp
// vorher:  current_[i] += k * (target_[i] - current_[i]);
// nachher: current_[i] += k * (mod.Apply(i, target_[i]) - current_[i]);
```

Model und Polyphony werden als Stufenziele mit Hysterese beschrieben; der vorhandene
4-ms-Modell-Crossfade bleibt zuständig. Für Pitch braucht es nichts Neues: `Frequency` ist
bereits normiert, `Transpose()` bildet 0…1 auf −24…+24 Halbtöne ab, und die Modulation
wirkt über den bestehenden `PitchOffset()`. Eine Tiefe von 1/48 entspricht einem Halbton.
Die Zielbeschreibung gibt dem Editor dafür die Anzeige „±24 st“ mit.

### 7.3 Besitz der Eingänge

CV- und Gate-Eingänge gehören **dem Instrument, wenn es sie direkt nutzt**, später zum
Beispiel als 1 V/Okt im Looper. Sonst gehören sie der Matrix. Das Instrument meldet die
Belegung in der Quellenbeschreibung, und der Editor zeigt belegte Eingänge gesperrt an.
Instrument-CCs bleiben unangetastet; das Instrument meldet seine reservierten CC-Bereiche
für die Konfliktprüfung im Editor.

### 7.4 Pilotinstrument

`VORSCHLAG`: **Physical Modeling** zuerst.

* Es hat USB-MIDI, einen zentralen normierten `ParameterManager`, eine CPU-Telemetrie
  über MIDI und Host-Tests mit Callback-Fakes. Der Aufwand ist hier am kleinsten.
* Der **Looper**, der Anlass der Diskussion war, kommt als zweites Instrument. Er hat
  bisher **kein MIDI** („no MIDI or effects“, `main.cpp`), deshalb müsste dort zuerst
  USB-MIDI entstehen.

---

## 8. Ressourcen (Frage 7)

### 8.1 RAM (`SCHÄTZUNG`, Standardausstattung: 21 Quellen, 32 Slots, 32 Ziele)

| Posten | Größe | Ort |
| --- | ---: | --- |
| Patch (32 × 8 B Slots, Quellparameter, Kopf, CRC) | ~0,6 KB | Main (AXI) |
| Schattenkopie für Transaktionen | ~0,6 KB | Main (AXI) |
| Program × 3 (Triple-Buffer) | ~2,1 KB | heiß (DTCM oder AXI) |
| Quellzustände und -ausgänge | ~0,4 KB | heiß |
| Ziel-Akkumulatoren und Rampen (32 × 12 B) | ~0,4 KB | heiß |
| Controller: Sende- und Empfangsrahmen, Preset-Chunks | ~1,2 KB | Main (AXI) |
| Zielbeschreibungen und Strings (`const`) | ~1,5–3 KB | Image |
| **Summe ohne Audio-Rate** | **~6–8 KB** | davon ~3 KB heiß |
| Audio-Rate-Puffer je Ziel (Block 256) | +1 KB | heiß |

Zum Vergleich: Die MIDI-FIFO von libDaisy allein belegt etwa 37 KB.

### 8.2 CPU (`SCHÄTZUNG`, vor Phase 4 nicht verlässlich)

Budget pro 48-Sample-Block bei 480 MHz: **480 000 Zyklen**.

| Posten pro Tick | Zyklen |
| --- | ---: |
| 4 LFO + 2 Random + 2 Hüllkurven + CV/Gate/MIDI | ~250 |
| 32 Routen à ~8 (mit Via ~12) | ~260–380 |
| 32 Ziele: Akkumulator, Begrenzung, Rampe | ~320 |
| Program-Prüfung, Verwaltung | ~20 |
| **Summe (mit Cache-Reserve aufgerundet)** | **≤ 2 000** |

Das ergibt **≤ 0,4 % CPU bei 1 kHz** und etwa 1,7 % bei 4 kHz. Daher die Empfehlung von
1 kHz auch für Physical Modeling. Audio-Rate kostet etwa 20 Zyklen pro Sample und Route:
**~0,2 % je Audio-Route** bei 48 kHz, zuzüglich der Kosten im Ziel.

**Realistische Grenze für die Zahl der Verbindungen (Frage 6.3)**: Die Matrix selbst
erlaubt weit über 100 Control-Routen im Prozentbereich. Die praktische Grenze setzen
Übersichtlichkeit (32 Slots) und die Ziele, die Werte teuer verarbeiten. Gemessen wird
das in Phase 4 mit einer Benchmark-Firmware.

### 8.3 Messung

Die Benchmark-Firmware `bench/` auf dem Desktop-Kit nutzt den bewährten
`daisy::CpuLoadMeter` mit Überlaufzählung und Abfrage über MIDI-CC, wie
`cpu_telemetry.h`. Gemessen werden 0, 8, 16, 32 und 64 Routen, Tick 1/2/4 kHz, mit und
ohne Audio-Rate und bei Blockgröße 48 und 256. Ergebnis ist eine Tabelle in
`VALIDATION.md`. `VORSCHLAG`: Eine **erste Messung schon am Ende von Phase 2**, weil die
DSP-Last die zentrale Unsicherheit ist und die Messung wenig kostet.

---

## 9. Konfiguration speichern und wiederherstellen (Frage 8)

| Ebene | Inhalt | Wann |
| --- | --- | --- |
| **Arbeitspatch** (RAM) | aktueller Zustand | jede Änderung |
| **QSPI-Slots** (Gerät) | 8 Patches + Index „Einschalt-Patch“ | nur auf Befehl `STORE` (Editor oder optional Tastenkombination) |
| **Editor-Bibliothek** | JSON-Dateien, beliebig viele | Speichern im Editor |
| **DAW-Projekt** (VST3) | Plugin-State | beim Speichern des Projekts |

* **Format**: Kopf mit Magic `MODD`, Formatversion, Instrument-Key, Schema-Hash, Länge und
  CRC-32, danach der Patch. Ungültige CRC, ein fremdes Instrument oder eine unbekannte
  Version führen zu Standardwerten und einer Meldung an den Editor. Nichts wird
  stillschweigend uminterpretiert.
* **Migration**: Ziele werden über ihre stabile `id` referenziert, in JSON zusätzlich über
  `key`. Bei geändertem Schema-Hash bildet der Editor über `key` ab und meldet, was nicht
  zugeordnet werden konnte.
* **QSPI-Bereich** (`VORSCHLAG`): die obersten 64 KiB des 8-MiB-Fensters, je Slot ein
  4-KiB-Sektor. Das liegt weit oberhalb jeder BOOT_SRAM-Anwendung, die auf die Größe des
  AXI-SRAM begrenzt ist. Weil jede Firmware dieselbe Region sieht, schützt der
  Instrument-Key vor fremden Patches. Vor dem Flashen eines anderen Instruments sichert
  der Editor die Slots als Dateien.
* **Blockierung**: Löschen und Schreiben eines Sektors dauert je nach Baustein mehrere
  zehn bis hundert Millisekunden (`OFFEN`: Baustein des Seed3 und Zeiten prüfen). Audio
  läuft weiter, weil der Code bei BOOT_SRAM aus dem SRAM ausgeführt wird. Der Main-Loop
  und damit der USB-Dienst steht in dieser Zeit. Deshalb gibt es **kein Autosave**; der
  Editor zeigt „Speichere…“. Sektoren nutzen sich ab (typisch ≥ 100 000 Zyklen), was bei
  ausdrücklichem Speichern unkritisch ist. Bedingung: Kein Instrument mit Mod on Daisy
  legt Code oder Daten in `.qspiflash_*`-Sektionen ab.
* **SD-Karte**: Looper und Nebulae haben bereits FatFS. Ein optionales SD-Backend hinter
  derselben `Storage`-Schnittstelle ist möglich, gehört aber nicht zum Kern.
* **Hardware ohne Editor** (Leitgedanke § 10 des Projektauftrags): Das Instrument kann
  optional eine Tastenkombination für „Patch 1–8 laden“ und „Modulation aus“ anbieten.
  Mod on Daisy selbst führt **keine Hardware-Menüs** ein.

---

## 10. Verzeichnisstruktur (Vorschlag für Phase 2 ff.)

```text
modules/mod-on-daisy/
├── README.md
├── docs/            ARCHITECTURE.md, PROTOCOL.md, später VALIDATION.md
├── include/mod_on_daisy/
│   ├── types.h      TargetInfo, SourceInfo, Rate, Kind
│   ├── sources/     lfo.h, random.h, envelope.h, cv_gate.h, midi_source.h
│   ├── patch.h      Slots, Quellparameter, CRC
│   ├── program.h    Kompilierung Patch → Program
│   ├── engine.h     Audio-Seite
│   ├── triple_buffer.h
│   ├── protocol.h   Codec (auch im Editor)
│   └── controller.h Main-Seite
├── adapters/        desktop_devkit.h, qspi_storage.h  (einzige libDaisy-Abhängigkeit)
├── tests/           Host-Tests (ASan/UBSan/TSan) wie in den übrigen Projekten
├── bench/           Benchmark-Firmware für Phase 4
└── editor/          JUCE-Projekt (CMake), bindet include/mod_on_daisy/protocol.h ein
```

`modules/` ist neu und steht für wiederverwendbare Komponenten ohne eigene Firmware.
`projects.mk` bleibt unverändert, bis `bench/` gebaut werden kann.

---

## 11. Phasen mit Abnahmekriterien

| Phase | Inhalt | Abnahme |
| --- | --- | --- |
| **1 Konzept** | dieses Dokument und PROTOCOL.md | Entscheidungen aus § 13 getroffen |
| **2 Prototyp** | Kern (Quellen, Program, Engine, Triple-Buffer, Codec) header-only, Host-Tests | LFO-Frequenz bei Tick 12/32/48/64/256 exakt; Summe und Begrenzung; Stufen-Hysterese; Codec-Round-Trip und Fuzzing gegen abgeschnittene Rahmen; TSan auf dem Triple-Buffer; CI-Job grün. **Erste Messung** mit `bench/` |
| **3 Editor** | technischer Test MIDI-Weg und Windows-Ports (§ 5.3), danach JUCE-Standalone | Verbinden, Schema lesen, Slots bearbeiten, Presets speichern und laden gegen eine Host-Simulation und gegen das Gerät |
| **4 Performance** | Benchmark-Matrix, Audio-Rate-Pfad, Gate-Timing, SysEx-Framing | Messtabelle; keine Überläufe bei der Standardausstattung; Entscheidung über EXTI für Gates |
| **5 Integration** | Physical Modeling (Pilot), dann Looper | Bestehende Tests unverändert grün; Instrument ohne Patch klanglich identisch (Offset 0); Hardwaretest-Checkliste |
| **6 Erweiterung** | weitere Quellen (Hüllkurvenfolger, MIDI-Clock-Sync, CV-Out als Ziel), VST3-Ziel, weitere Instrumente | je nach Bedarf |

---

## 12. Risiken

| Risiko | Wirkung | Gegenmaßnahme |
| --- | --- | --- |
| SysEx-Senden der gepinnten libDaisy falsch gerahmt | Editor erhält keine oder kaputte Antworten | Frühtest in Phase 2/3; eigenes Paket-Framing als lokale Kompatibilitätsschicht (Muster: `midi_parser.cpp` in Physical Modeling) |
| SysEx > 128 Byte abgeschnitten | stille Datenfehler | Rahmen ≤ 120 B, Prüfsumme, Längenfeld |
| MIDI-Port unter Windows exklusiv | Editor und DAW gleichzeitig nicht möglich | Technischer Test in Phase 3; DIN-MIDI oder Windows MIDI Services als Ausweg |
| Gate-Impulse < 1 Block gehen verloren | verpasste Trigger | Messen in Phase 4; bei Bedarf EXTI-Interrupt mit Flag |
| DTCM-Engpass (Toy Piano 88,6 %) | Linkerfehler oder Stack-Risiko | heißer Zustand ~3 KB, Platzierung wählbar |
| Ziele verarbeiten Werte zu teuer | CPU-Spitzen | `max_rate` pro Ziel, Event-Klasse, Messung |
| Modulation diskreter Parameter klickt | Artefakte | Hysterese und Crossfades im Instrument; Editor-Warnung |
| Schemaänderung zerstört Presets | Datenverlust | stabile IDs und Keys, CRC, Migration im Editor |

---

## 13. Entscheidungsbedarf

Diese Punkte sollten vor Phase 2 entschieden werden. Vorschläge sind fett markiert.

1. **Ort im Repository**: **`modules/mod-on-daisy/`** (neue Ebene für Komponenten) oder unter `common/`?
2. **Slot-Anzahl**: **32** oder 16/64?
3. **Erstausstattung der Quellen**: **2 CV, 2 Gate, 4 LFO, 2 Random, 2 Hüllkurven, 9 MIDI**?
4. **Audio-Rate**: **API in Phase 2 reservieren, Umsetzung in Phase 4** oder sofort?
5. **Editor-Technologie**: **JUCE (Standalone → VST3)** oder Web-App?
6. **Pilotinstrument**: **Physical Modeling**, danach Looper?
7. **Persistenz**: **QSPI, 8 Slots + Einschalt-Patch, nur ausdrückliches Speichern**?
8. **SysEx-Hersteller-ID**: **`0x7D` (nicht-kommerziell)** akzeptiert?
9. **Erste Messung schon in Phase 2** (Benchmark-Firmware) statt erst in Phase 4?
