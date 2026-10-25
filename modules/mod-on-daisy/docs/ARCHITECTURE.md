# Mod on Daisy — Architektur und technische Spezifikation v0.1

**Stand:** 23. Oktober 2026 · **Status:** Spezifikation v0.1. Phase 2 ist als Software
umgesetzt und auf dem Host geprüft. Die Messungen auf der Hardware stehen aus
([VALIDATION.md](VALIDATION.md)).
Grundlage sind die [Architekturentscheidungen v0.1](DECISIONS.md) (D1–D11, Nachtrag
N1–N6) und die dort benannten Einwände (E1–E12).

Evidenzklassen: `BELEGT` (Code oder libDaisy nachgelesen), `GEMESSEN` (Build oder Test),
`SCHÄTZUNG`, `VORSCHLAG`, `OFFEN`.

**Grundsatz:** eine schlanke Komponente:

* feste Größen: 8 Quellen, 8 Ziele, 8 aktive Verbindungen;
* kein Heap, keine virtuellen Aufrufe im Audiopfad, keine Audio-Rate;
* das Modul belegt in der Firmware **1 936 Byte RAM** (`GEMESSEN`), davon etwa 1 KiB im
  Audiopfad;
* Schätzung: weniger als 0,3 % CPU (§ 8).

---

## 1. Modulstruktur

```text
modules/mod-on-daisy/
├── Makefile              Host-Tests: make test | test-sanitize | test-thread
├── docs/                 DECISIONS, ARCHITECTURE, PROTOCOL, STORAGE, TEST_PLAN_PHASE2, VALIDATION
├── include/mod_on_daisy/ header-only, C++17, nur Standardbibliothek
│   ├── types.h           Konstanten, Source/Shape/Sync, Teilungen, TargetInfo, InstrumentInfo, AllowedCells
│   ├── patch.h           Patch (Raster + Quellparameter), Validate, Serialize (109 B, Format 2: 111 B), CRC-32, SchemaHash
│   ├── sources.h         Sinus/Formen, Rng, Smoother, CV-Totzone, ClockFollower; Rate → Inkrement (Main)
│   ├── program.h         Program und Compile(Patch) → Program (Main)
│   ├── mailbox.h         lock-freier Triple-Buffer
│   ├── notes.h           LiveState: CC1, Aftertouch, Drucktabelle, Last-Note, Clock-/Start-/Note-On-Zähler
│   ├── engine.h          Engine (Audio-IRQ) und VoicePressure
│   ├── protocol.h        SysEx-Codec (Writer/Reader, Rahmen, Prüfsumme) — auch für den Editor
│   ├── controller.h      Controller (Main-Loop): MIDI-Eingang, Protokoll, Antworten, Monitor
│   └── module.h          Module = LiveState + Engine + Controller
├── adapters/mod_on_daisy_desktop.h   einzige libDaisy-Abhängigkeit: CV/Gate, MidiEvent, USB-Senden
├── tests/                test_mod.cpp, test_thread.cpp, test_bench.cpp
├── bench/                Messfirmware ModBench (projects.mk: mod-bench)
└── tools/mod_probe.py    Kommandozeilen-Client für SysEx-Tests und Messabläufe

editors/mod-on-daisy/     (Phase 3) JUCE-Editor, bindet protocol.h und patch.h ein
```

**Abhängigkeitsregeln**

* `include/` hängt weder von libDaisy noch von DaisySP ab. Es läuft auf dem Host, in
  Tests und im Editor.
* `adapters/` ist die einzige Stelle mit `daisy::`.
* Editor und Testskript nutzen dieselbe Serialisierung. Der Prüfwert
  `CRC(Testpatch) = E2340CC6` wird in C++ (`test_mod.cpp`) und Python
  (`mod_probe.py selftest`) gleich berechnet.
* Instrumente binden das Modul mit
  `C_INCLUDES += -I../../modules/mod-on-daisy/include -I../../modules/mod-on-daisy/adapters`
  ein. Ein zusätzliches Archiv gibt es nicht.

## 2. Ausführungskontexte und Besitz

```text
 USB-IRQ (libDaisy)      Main-Loop                                 Audio-IRQ
 ──────────────────      ─────────────────────────────────         ───────────────────────────
 Paket → MidiEvent  ──►  usb_midi.PopEvent() → adapter::ToMessage  hw.UpdateAllControls()
 (FIFO 256)              ├─► controller.HandleMidi(m) ───────┐     adapter::ReadHardware(engine, hw)
                         │    · SysEx "MD": Protokoll         │     engine.Tick(n)
                         │    · CC1, ChAT, PolyAT → LiveState │      ├ Program holen (Triple-Buffer)
                         │    · Noten, Clock, Start: mitlesen │      ├ Clocks, Gates, Retrigger
                         ├─► false: Instrument verarbeitet    │      ├ Quellen rechnen
                         └─► controller.Service(ms, sender)   │      └ globale Offsets
                              · ≤ 1 Antwort/ms, Monitor       │   Instrument-DSP:
                              · Patch → Compile → Publish ────┘     Apply / ApplyVoice
```

| Zustand | Schreiber | Leser | Mechanismus |
| --- | --- | --- | --- |
| `Patch`, Antwortpuffer | Main | Main | keiner nötig |
| `Program` | Main (`Compile`) | Audio | Triple-Buffer, acq_rel |
| CC1, Channel AT, Last-Note-Druck, `pressure[128]` | Main | Audio | `atomic<uint8_t>`, relaxed |
| Note-On-, Clock-, Start-Zähler, Clock-Stand bei Start | Main | Audio | `atomic<uint32_t>`; Start mit Release/Acquire |
| Engine-Zustand (Phasen, Glättung, Clock-Folger, Offsets) | Audio | Audio | Alleinbesitz |
| Monitor-Schnappschuss | Audio | Main | Triple-Buffer, nur wenn der Monitor aktiv ist |

Alle Atomics werden per `static_assert(…::is_always_lock_free)` geprüft. ThreadSanitizer
läuft auf dem Zusammenspiel beider Threads (`test_thread.cpp`).

## 3. Datenstrukturen

### 3.1 Quellen (Zeilen, feste Reihenfolge D3)

| Zeile | Quelle | Bereich | Rohdaten | Aufbereitung |
| --- | --- | --- | --- | --- |
| 0 | LFO 1 | −1…+1 | intern | Form, Rate oder Sync (§ 4.2) |
| 1 | LFO 2 | −1…+1 | intern | wie LFO 1 |
| 2 | Random / S&H | −1…+1 | intern | Stufe oder geglättet; Rate oder Sync |
| 3 | CV 1 | −1…+1 | `hw.cv[0].Value()` | Kalibrierung, Totzone 0,02, 2 ms |
| 4 | CV 2 | −1…+1 | `hw.cv[1].Value()` | wie CV 1 |
| 5 | Modwheel | 0…1 | CC1, alle Kanäle | 10 ms |
| 6 | Channel AT | 0…1 | Channel Pressure, alle Kanäle | 10 ms |
| 7 | Poly AT | 0…1 | Poly Key Pressure pro Note | Stimmenziele: pro Stimme 10 ms (`VoicePressure`); globale Ziele: Last-Note, 10 ms |

### 3.2 Zielbeschreibung des Instruments

```cpp
struct TargetInfo {
    const char* name = nullptr;      // Spaltenkopf, ≤ 12 Zeichen ASCII; nullptr = keine Spalte
    const char* span_text = nullptr; // Bedeutung von Tiefe 100, ≤ 8 Zeichen ASCII, z. B. "+-12 st"
    float span = 1.0f;               // normierter Hub bei Tiefe 100 (N2), 0 < span ≤ 1
    bool per_voice = false;          // Instrument nutzt ApplyVoice (Poly AT pro Stimme)
    bool latched = false;            // nur beim Anschlag gelesen → Poly-AT-Zelle gesperrt
};
struct InstrumentInfo {
    const char* key; const char* version;   // ≤ 24 (kMaxKeyLength) bzw. ≤ 12 Zeichen
    TargetInfo targets[8];
    uint8_t unavailable_sources = 0;        // Bit s: Quelle vom Instrument belegt
};
```

* Die **Spaltennummer ist die Ziel-ID**. `SchemaHash(info)` ist eine CRC-32 über Key,
  alle Spalten und die Poly-AT-Regel. Gespeicherte Patches und Editor-Caches werden
  daran gebunden.
* **Erlaubte Zellen** (`AllowedCells`): Ziel vorhanden, Quelle verfügbar, und nicht Poly
  AT auf einem `latched`-Ziel. Poly AT auf globalen Zielen ist erlaubt und nutzt den
  Last-Note-Druck (N1).
* Strings sind 7-Bit-ASCII, weil SysEx nur 7-Bit-Bytes überträgt. Das Zeichen „±“
  schreibt das Instrument als `+-`, der Editor zeigt es als ±.

### 3.3 Patch (editierbare Konfiguration)

```cpp
struct LfoParams    { uint16_t rate = 6000; uint8_t shape, sync, gate, division = 4, retrigger; };
struct RandomParams { uint16_t rate = 8000; uint8_t mode,  sync, gate, division = 4; };
struct Patch {
    uint8_t format = 1;
    int8_t  depth[8][8];   // −100…+100
    uint8_t enabled[8];    // Bit t = Zelle (s, t) eingeschaltet
    LfoParams lfo[2]; RandomParams random;
    char name[16];
};   // serialisiert: 1 + 64 + 8 + 2·7 + 6 + 16 = 109 Byte, Little Endian, ohne Padding
```

Mit `MOD_ON_DAISY_EXTENSIONS=1` hat `LfoParams` zusätzlich `int8_t fm` (−100…+100,
LFO-Quermodulation). Ein Patch mit `format = 2` hängt `fm` von LFO 1 und 2 an
(111 Byte); `format = 1` muss `fm = 0` haben. Ein Gerät mit eingeschalteter
Quermodulation arbeitet mit Format 2 (PROTOCOL § 3.6).

* **Aktiv** ist eine Zelle, wenn sie eingeschaltet ist und `depth ≠ 0`. Eine
  ausgeschaltete Zelle behält ihre Tiefe.
* **`Validate(patch, allowed)`** lehnt einen Patch als Ganzes ab, wenn:
  * Format oder Tiefen ungültig sind (`Range`);
  * ein Parameter außerhalb des Bereichs liegt, auch eine Teilung, die zur Sync-Quelle
    nicht passt (`Range`);
  * eine aktive Zelle nicht erlaubt ist (`NotAllowed`);
  * mehr als `kMaxActive` Zellen aktiv sind (`Limit`).
  Teilweise übernommen wird nie.

**Quellparameter** (N4/N5; Parameternummern wie in `SET_SOURCE`):

| Nr. | Parameter | LFO 1/2 | Random |
| --- | --- | --- | --- |
| 0 | Rate | 0…16383 → 0,02…20 Hz logarithmisch, `f = 0,02 · 1000^(r/16383)`; gilt bei Sync = frei | wie LFO: Häufigkeit neuer Zufallswerte |
| 1 | Form / Modus | 0 Sinus, 1 Dreieck, 2 Sägezahn steigend, 3 Sägezahn fallend, 4 Rechteck, 5 Sample & Hold | 0 Stufe (S&H), 1 geglättet |
| 2 | Sync | 0 frei, 1 MIDI-Clock, 2 Gate | wie LFO |
| 3 | Gate | 0 Gate 1, 1 Gate 2 (bei Sync = Gate) | wie LFO |
| 4 | Teilung | MIDI: 0 4 Takte, 1 2 Takte, 2 1 Takt, 3 1/2, 4 1/4, 5 1/4T, 6 1/8, 7 1/8T, 8 1/16, 9 1/16T, 10 1/32. Gate: 0 1 Zyklus pro 4 Impulse, 1 pro 2, 2 pro Impuls, 3 zwei pro Impuls, 4 vier pro Impuls | wie LFO |
| 5 | Retrigger | 0 aus, 1 Neustart bei jedem Note On (nur bei Sync = frei) | — |
| 6 | fm (Erweiterung) | −100…+100, übertragen als fm + 100: Rate × 2^(fm/50 · Wert des anderen LFOs), also bis ±2 Oktaven; nur bei Sync = frei | — |

Wechselt die Sync-Art, setzt der Controller die Teilung auf den Standard der neuen Art:
MIDI-Clock 1/4, Gate 1 Zyklus pro Impuls. Derselbe Index bedeutet je nach Art etwas
anderes; in der MIDI-Tabelle ist 4 zum Beispiel 1/4, in der Gate-Tabelle 4/1.

### 3.4 Program (kompilierte Form für den Audio-IRQ)

```cpp
struct Connection   { uint8_t source, target; float gain; };      // gain = depth/100 · span
struct SourceConfig { uint32_t inc; Ratio ratio; uint8_t shape, sync, gate, retrigger; };
struct Program {
    uint8_t count; bool active;          // active == false → alle Offsets exakt +0.0f
    Connection conn[kMaxActive];
    float voice_gain[8];                 // Poly AT auf per_voice-Zielen
    SourceConfig lfo[2], random;
    float cv_offset[2];                  // Gerätekalibrierung, nicht Teil des Patches
};
```

`Compile()` läuft nur im Main-Loop und erledigt dort folgende Schritte:

* Poly AT auf einem `per_voice`-Ziel → `voice_gain[t]`.
* Alles andere, auch Poly AT auf globalen Zielen, wird zu einer `Connection`.
* Rate → Phaseninkrement (`powf`) und Teilung → `Ratio`.

### 3.5 Übergabe Main → Audio

* Ein **Triple-Buffer** (`mailbox.h`) mit drei `Program`-Instanzen: Main schreibt in
  `Back()` und veröffentlicht mit `Publish()`; Audio holt zu Beginn von `Tick()` mit
  `Fetch()` ab. Keine Seite wartet, und es gewinnt immer der neueste Stand.
* Phasen und Glättungszustände liegen in der Engine. Ein neues Program setzt sie nicht
  zurück (Test `TestProgramSwapKeepsPhase`).

## 4. Modulationsverarbeitung

### 4.1 Ablauf eines Ticks

`Tick(n)` läuft einmal pro Audio-Block mit der tatsächlichen Sampleanzahl `n`:

```text
1. Program aus dem Triple-Buffer holen, falls neu; bei geändertem n die Koeffizienten neu berechnen.
2. Clock-Folger fortschreiben: MIDI-Clock-Impulse, Start, Gate-Flanken; Note-On-Zähler für Retrigger.
3. !program.active → beim Übergang Offsets auf +0.0f setzen; freie Phasen fortschreiben; Ende.
4. LFO 1/2, Random, CV 1/2, Modwheel, Channel AT, Poly AT (Last-Note) berechnen → source[8].
5. offset[0..7] = 0; für jede Verbindung c: offset[c.target] += c.gain · source[c.source].
```

Bei aktiver Matrix werden immer alle acht Quellen gerechnet. Das kostet nur wenige
hundert Zyklen und hält jede Quelle aktuell, sobald eine neue Verbindung hinzukommt.

Das Instrument liest:

```cpp
float Apply(t, base)            = Clamp01(base + offset[t])
float ApplyVoice(t, base, p)    = Clamp01(base + offset[t] + voice_gain[t] · p)
```

* **Bitgenau neutral**: Ohne aktive Verbindung ist `offset[t] == +0.0f` und
  `voice_gain[t] == 0`. Für jedes `base` in [0, 1] liefert die Matrix dann genau `base`
  (Test `TestNeutral`, auch nachdem eine Verbindung entfernt wurde).
* Begrenzt wird **nach** der Summe.

### 4.2 LFO, Random und Synchronisation

* **Frei**: `uint32_t`-Phasenakkumulator `phase += inc · n`. Die Phase ist nach gleicher
  Sampleanzahl für alle Tick-Längen bitgleich (Test bei 12, 32, 48, 64 und 256 Samples).
* **Formen** aus `p ∈ [0, 1)`:
  * Sinus über ein Polynom, Fehler 0,11 % (`GEMESSEN`);
  * Dreieck, Sägezahn und Rechteck direkt aus der Phase;
  * Sample & Hold zieht bei jedem Periodenwechsel einen neuen Wert (xorshift32, fester
    Startwert).
* **Random**: neuer Wert bei jedem Periodenwechsel. Im Modus geglättet läuft der Wert
  linear vom vorigen zum neuen.
* **Synchronisation** (`ClockFollower`):
  * Der Folger zählt Impulse: MIDI-Clock mit 24 ppqn, oder Gate-Flanken, die einmal pro
    Block abgefragt werden.
  * Er schätzt die Periode (MIDI: Glättung 1/8, Gate: 1/2).
  * Die Phase ist `((Impulse mod den) + Bruchteil) · num / den`; der Bruchteil
    interpoliert zwischen den Impulsen.
* **Verhalten der Synchronisation**:
  * **MIDI Start** (`FA`): Der nächste Clock-Impuls ist Taktanfang, die Phase steht bis
    dahin auf 0. Continue und Stop setzen nichts zurück.
  * **Clock oder Gate stoppt**: Der Bruchteil läuft höchstens bis kurz vor den nächsten
    erwarteten Impuls. Der LFO **hält** und läuft nicht davon (Test `TestMidiClockSync`).
  * **Auflösung**: MIDI-Clock-Impulse und Gate-Flanken werden mit Blockauflösung
    erfasst, bei Block 48 also 1 ms. Der Jitter wird durch die Periodenschätzung
    geglättet. Gate-Impulse kürzer als ein Block können verloren gehen (E8, Messung H8).
* **Retrigger** (nur frei laufend): Jedes Note On setzt die Phase auf 0.
* **Quermodulation** (Erweiterung, nur frei laufend): Das Inkrement von LFO i wird pro
  Tick mit `2^(fm_i/50 · LFO_(1−i))` multipliziert. Der Wert des anderen LFOs stammt aus
  dem vorigen Tick, damit die Reihenfolge der Berechnung keine Rolle spielt. `Exp2Small`
  ist ein kubisches Polynom mit Tabellenstufen, begrenzt auf ±2 Oktaven. Bei `fm = 0`
  bleibt der Code-Pfad bitgleich zum freien Lauf ohne Quermodulation.

### 4.3 CV-Aufbereitung

```text
x = cv − cal_offset[i]                              // Kalibrierung (SET_CALIBRATION, RAM in v0.1)
x = |x| < 0,02 ? 0 : sign(x) · (|x| − 0,02) / 0,98   // Totzone gegen Rauschen offener Buchsen
y += a · (x − y)                                    // 2 ms
```

Totzone und Zeitkonstante sind `VORSCHLAG` und werden in H5 am Gerät überprüft. Der
Monitor zeigt dafür auch die Rohwerte.

### 4.4 MIDI-Quellen und -Ereignisse (`Controller::HandleMidi`)

| Nachricht | Wirkung | an das Instrument weiter? |
| --- | --- | --- |
| CC1 | Modwheel | nein |
| Channel Pressure | Channel AT | nein |
| Poly Key Pressure | `pressure[note]`, nur bei gehaltener Note; Last-Note | nein |
| Note On (v > 0) | Druck der Note = 0, Note gehalten, ans Ende der Last-Note-Liste, Retrigger-Zähler + 1 | ja |
| Note Off, Note On v = 0 | Druck = 0, Note entfernt | ja |
| CC120, CC123 | alle Noten und Drücke löschen | ja |
| Clock `F8`, Start `FA` | Clock-Zähler bzw. Taktanfang | ja |
| SysEx `7D 4D 44 …` | Protokoll | nein |
| alles andere | — | ja |

Die Last-Note-Liste fasst 16 Noten. Bei Überlauf fällt die älteste heraus; das betrifft
nur die Last-Note-Regel.

### 4.5 Parametersprünge vermeiden

| Ursache | Maßnahme |
| --- | --- |
| 7-Bit-MIDI-Stufen | 10 ms Glättung (global in der Engine, pro Stimme in `VoicePressure`) |
| CV-Rauschen | Totzone + 2 ms |
| Tiefenänderung im Editor | Offset springt einmal pro Tick; die eigene Parameterglättung des Instruments fängt das ab (Physical Modeling 5 ms; Toy Piano v0.3 bekommt eine) |
| S&H, Random-Stufe, Rechteck | gewollt; Random geglättet als Alternative |
| Neues Program | Phasen und Glättung laufen weiter |
| Lastspitzen | feste Schleifengrenzen, kein Heap (Test `TestNoAllocation`), kein libm im Tick außer `fabs` |

Stufenziele wie die Modellwahl sind in v0.1 keine Ziele.

## 5. Globale und stimmenbezogene Modulation

| Weg | Quellen | Ergebnis | Kosten |
| --- | --- | --- | --- |
| **Global** | alle 8, Poly AT als Last-Note | `offset[t]`, einmal pro Tick | ≤ `kMaxActive` Multiplikationen und Additionen |
| **Stimme** | nur Poly AT | `voice_gain[t] · VoicePressure` beim Lesen pro Stimme | 1 Multiplikation und Addition pro Stimme und Ziel |

* Nur Poly AT ist pro Stimme verschieden. Die Engine kennt deshalb keine Stimmen; die
  Zuordnung Stimme → Note bleibt beim Instrument (E3).
* `VoicePressure::Process(engine, note)` glättet den Druck der Note einer Stimme
  (`note < 0` = stumm → 0).

| Fall | Verhalten |
| --- | --- |
| Voice-Stealing | Die neue Note beginnt mit Druck 0; der Helfer gleitet in 10 ms dorthin, mit `Reset()` sofort |
| Gleiche Note in zwei Stimmen | gleicher Druck (MIDI-1.0-Grenze) |
| Note Off, Stimme klingt weiter | Druck → 0, Modulation gleitet zum Grundwert |
| Poly AT ohne gehaltene Note | ignoriert |
| Monophones Instrument (Nebulae) | alle Ziele global → Last-Note |

## 6. Instrumentenschnittstelle

### 6.1 API

```cpp
namespace mod {
struct Module {                                       // ein statisches Objekt pro Instrument
    LiveState live; Engine engine; Controller controller;
    void Init(const InstrumentInfo&, float sample_rate, std::size_t block_size,
              uint8_t device_id = 0, Diagnostics* diagnostics = nullptr);
};
class Engine {                                        // Audio-IRQ
    void  SetHardware(float cv1, float cv2, bool gate1, bool gate2);   // via adapter::ReadHardware
    void  Tick(std::size_t samples);
    float Apply(std::size_t target, float base) const;
    float ApplyVoice(std::size_t target, float base, float voice_pressure) const;
    float NotePressure(uint8_t note) const;  float PressureCoeff() const;  bool Active() const;
};
class Controller {                                    // Main-Loop
    bool   HandleMidi(const MidiMessage&);           // true = verbraucht
    void   Service(uint32_t now_ms, MidiSender&);
    Status LoadPatch(const Patch&);                  // lokal, z. B. Tastenkombination
    void   SetCalibration(float cv1, float cv2);
};
namespace adapter {                                   // libDaisy
    void ReadHardware(Engine&, daisy::desktop_devkit::Hardware&);
    bool ToMessage(const daisy::MidiEvent&, MidiMessage&);
    class UsbMidiSender : public MidiSender;          // prüft USBD_STATE_CONFIGURED wie Toy Piano
}
}
```

### 6.2 Einbindung (Muster: `bench/src/main.cpp`)

```cpp
mod::Module modulation;                    // optional MOD_ON_DAISY_HOT für eine andere Section
modulation.Init(kInfo, hw.seed.AudioSampleRate(), kBlock);
// Audio-Callback
hw.UpdateAllControls();
mod::adapter::ReadHardware(modulation.engine, hw);
modulation.engine.Tick(size);
value = modulation.engine.Apply(t, base);  // bzw. ApplyVoice(t, base, voice.pressure.Process(engine, note))
// Main-Loop
while(usb_midi.HasEvents()) {
    const daisy::MidiEvent e = usb_midi.PopEvent();
    mod::MidiMessage m;
    if(mod::adapter::ToMessage(e, m) && !modulation.controller.HandleMidi(m))
        InstrumentMidi(e);
}
modulation.controller.Service(daisy::System::GetNow(), sender);
```

### 6.3 Zieltabellen

**Toy Piano v0.3** (Entwicklungsumgebung, N3):

| Spalte | Ziel | Weg | span | Hinweis |
| --- | --- | --- | --- | --- |
| 0 | Pitch | Stimme | `+-1 st` | Vibrato |
| 1 | Level | Stimme | 100 % | Tremolo, Poly-AT-Lautstärke |
| 2 | Harmonic | Stimme | 100 % | Anteil des 2. Obertons |
| 3 | Attack | global, `latched` | 100 % | wirkt beim nächsten Anschlag |
| 4 | Decay | global, `latched` | 100 % | wirkt beim nächsten Anschlag |
| 5–7 | frei für Versuche | — | — | sonst ausgegraut |

**Rings (Physical Modeling)** (DSP-Änderung für den Stimmenweg nötig, E1):

| Spalte | Ziel | Parameter heute | Weg | span |
| --- | --- | --- | --- | --- |
| 0 | Pitch | `Frequency` (0…1 = ±24 st) | Stimme | 0,25 = `+-12 st` |
| 1 | Structure | `Structure` | Stimme¹ | 100 % |
| 2 | Brightness | `Brightness` | Stimme | 100 % |
| 3 | Damping | `Damping` | Stimme | 100 % |
| 4 | Position | `Position` | Stimme | 100 % |
| 5 | Excitation | `Excitation` | global, `latched` | 100 % |
| 6 | Level | Ausgangspegel (Grundwert 0,5 = Verstärkung 1) | global | `+-100%` (0…2); ersetzt seit dem Hardwaretest 23.10.2026 Reverb/`Decay` |
| 7 | Dry/Wet | `Wet` | global | 100 % |

¹ `OFFEN`: In Akkordmodellen wählt Structure den Akkord für den ganzen Part
(`ChordSelector`). Ob eine stimmenbezogene Structure dort sinnvoll ist, wird bei der
Integration geprüft.

**Nebulae**: Start, Speed, Size, Density, Pitch, Overlap, Blend und Window, alle global.
Poly AT kommt dort über die Last-Note-Regel; die effektive Rate liegt bei 187,5 Hz (E7).

## 7. Echtzeitregeln

* **Audio-IRQ**: kein Heap, keine Locks, kein USB, kein QSPI, kein Logging, kein `powf`
  oder `exp` (Koeffizienten nur bei geänderter Tick-Länge), keine virtuellen Aufrufe,
  feste Schleifengrenzen.
* **Main-Loop**: Protokoll, Validierung, `Compile()`, CRC. Antworten gehen in eine
  Warteschlange mit **2 Rahmen**, weil der Editor höchstens eine Anfrage offen hat.
  `Service()` sendet höchstens einen Rahmen pro Millisekunde.
* **Flash**: keine Schreibfunktion in v0.1 (D8). `STORE`/`RECALL` → `NAK 9`.

## 8. Speicher und Performance

### 8.1 RAM und Code (`GEMESSEN`, ARM-Build der Messfirmware, `arm-none-eabi-nm`)

| Posten | Bytes |
| --- | ---: |
| `mod::Module` gesamt (kMaxActive 8) | **1 936** |
| `mod::Module` gesamt (kMaxActive 64, Lasttest) | 3 280 |
| davon Controller (Patch, Antwort, Wiederholungscache, 2 Sende-Rahmen) | ≈ 870 (`SCHÄTZUNG` aus den Feldgrößen) |
| davon Engine (3 Programs, 3 Monitor-Schnappschüsse, Zustand) | ≈ 850 |
| davon LiveState (Drucktabelle 128 B, Noten, Zähler) | ≈ 190 |
| Stack `Engine::Tick` (`-fstack-usage`) | 40 |
| Code `Engine::Tick` / `Controller::Execute` | 3,3 KiB / 8,4 KiB |

Das Modul liegt standardmäßig im `.bss` und damit bei BOOT_SRAM im DTCM. Toy Piano hat
dort noch etwa 14,9 KiB frei. 1,9 KiB sind tragbar, werden bei der Integration aber im
ELF-Check mitgeführt.

### 8.2 CPU (`SCHÄTZUNG`; Messung: TEST_PLAN § 3, VALIDATION.md)

Das Budget pro 48-Sample-Block beträgt 1 ms bzw. **480 000 Zyklen**.

| Szenario | Zyklen pro Tick | Anteil bei 1 kHz |
| --- | ---: | ---: |
| Matrix inaktiv | ~30 + Apply | ≈ 0,015 % |
| 1–4 Verbindungen (alle Quellen gerechnet) | ~200–300 | ≈ 0,05 % |
| 8 Verbindungen | ~300 | ≈ 0,06 % |
| + Stimmenweg 4 Stimmen × 5 Ziele | +~200 | ≈ 0,1 % |
| + Stimmenweg 32 Stimmen × 5 Ziele | +~1 300 | ≈ 0,3 % |
| Lasttest: 64 aktive Zellen, 32 Stimmen | ~2 000 | ≈ 0,4 % |

Die Messfirmware misst genau diesen Anteil mit dem DWT-Zykluszähler (Tick plus alle
Apply-Aufrufe), getrennt vom restlichen Callback.

## 9. Stand der Phasen

| Phase | Stand |
| --- | --- |
| 1 Spezifikation | abgeschlossen; Entscheidungen in DECISIONS.md |
| 2 Prototyp und Messung | **Software umgesetzt**: Kern, Host-Tests (ASan/UBSan/TSan, 1 Mio. Fuzz-Rahmen), Messfirmware (3 Varianten), `mod_probe.py`, CI. **Offen**: Messungen S0–S8 und Hardwaretests H1–H8 auf dem Desktop-Kit |
| 3 Editor | nach den Ergebnissen von H3 und H4 |
| 4 Performance | mit integrierten Instrumenten |
| 5 Integration | **Toy Piano v0.3 umgesetzt**; **Physical Modeling als getrennter Build (`MOD=1`) mit Stimmenweg umgesetzt**; Hardwaretests ausstehend; danach Nebulae |
| 6 Erweiterung | QSPI-Speicherung (STORAGE.md, eigene Freigabe), VST3, weitere Quellen |
