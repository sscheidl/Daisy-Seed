# Mod on Daisy — Architektur und technische Spezifikation v0.1

**Stand:** 23. Oktober 2026 · **Phase:** 1 · **Status:** Spezifikation, kein Code.
Grundlage sind die [Architekturentscheidungen v0.1](DECISIONS.md) (D1–D11) und die dort
benannten Einwände (E1–E12). Diese Fassung ersetzt das Konzept vom 22. Oktober; der
alte Stand bleibt in der Git-Historie.

Evidenzklassen wie in DECISIONS.md: `BELEGT`, `SCHÄTZUNG`, `VORSCHLAG`, `OFFEN`.

**Grundsatz:** eine schlanke Komponente. Feste Größen (8 Quellen, 8 Ziele, 8 aktive
Verbindungen), kein Heap, keine virtuellen Aufrufe im Audiopfad, keine Audio-Rate.
Der Audiopfad braucht etwa 1 KiB RAM und weniger als 0,3 % CPU (§ 8).

---

## 1. Modulstruktur

```text
modules/mod-on-daisy/
├── README.md
├── docs/                 DECISIONS, ARCHITECTURE, PROTOCOL, STORAGE, TEST_PLAN_PHASE2
├── include/mod_on_daisy/ (Phase 2) header-only, C++17, nur <cstdint>/<atomic>/<array>
│   ├── types.h           Source, TargetInfo, InstrumentInfo, Konstanten
│   ├── patch.h           Patch (Raster + Quellparameter), Validierung, CRC-32, Serialisierung
│   ├── sources.h         Lfo, RandomSource, Smoother, CvConditioner
│   ├── program.h         Program, Compile(Patch, InstrumentInfo) → Program
│   ├── mailbox.h         lock-freier Triple-Buffer für Program
│   ├── engine.h          Audio-Seite
│   ├── notes.h           Drucktabelle, gehaltene Noten, Last-Note (Main schreibt, Audio liest)
│   ├── protocol.h        SysEx-Codec (auch vom Editor eingebunden)
│   └── controller.h      Main-Seite: MIDI-Eingang, Protokoll, Antworten
├── adapters/             (Phase 2) desktop_devkit.h — einzige libDaisy-Abhängigkeit
├── tests/                (Phase 2) Host-Tests, ASan/UBSan/TSan
├── bench/                (Phase 2) Messfirmware, kein Instrument
└── tools/                (Phase 2) kleines Python-Testskript für SysEx unter Windows

editors/mod-on-daisy/     (Phase 3) JUCE-Editor, bindet include/mod_on_daisy/protocol.h und patch.h ein
```

**Abhängigkeitsregeln**

* `include/` hängt weder von libDaisy noch von DaisySP ab. Dadurch läuft es auf dem Host
  in Tests und im Editor.
* `adapters/` ist die einzige Stelle, an der `daisy::` vorkommt: CV aus
  `desktop_devkit::Hardware`, `MidiEvent` → `mod::MidiMessage`, Senden über
  `MidiUsbHandler`.
* Der Editor nutzt **dieselben** Dateien `protocol.h` und `patch.h`. Serialisierung und
  CRC sind dadurch auf beiden Seiten garantiert identisch.
* Instrumente binden das Modul per `C_INCLUDES += -I../../modules/mod-on-daisy/include`
  (und `adapters`) ein. Es gibt kein zusätzliches Archiv und keine Änderung an
  `common/desktop.mk`.

## 2. Ausführungskontexte und Besitz

```text
 USB-IRQ (libDaisy)      Main-Loop                                 Audio-IRQ
 ──────────────────      ─────────────────────────────────         ───────────────────────────
 Paket → MidiEvent  ──►  Instrument: PopEvent()                    hw.UpdateAllControls()
 (FIFO 256)              ├─► mod.controller.HandleMidi(ev) ──┐     mod.engine.SetCv(cv1, cv2)
                         │    · SysEx (eigener Kopf): Protokoll│    mod.engine.Tick(n)
                         │    · CC1, ChAT → atomare Rohwerte   │     ├ Program holen (Mailbox)
                         │    · PolyAT, NoteOn/Off → notes.h   │     ├ Quellen rechnen
                         │    · Rückgabe false für Noten       │     └ globale Offsets
                         ├─► Instrument verarbeitet Rest       │   Instrument-DSP:
                         └─► mod.controller.Service(ms, out)   │     Apply(t, base)
                              · ≤ 1 Antwort/ms                 │     ApplyVoice(t, base, p)
                              · Patch → Compile → Mailbox ─────┘
```

| Zustand | Schreiber | Leser | Mechanismus |
| --- | --- | --- | --- |
| `Patch` (Arbeitskopie) | Main | Main | keiner nötig |
| `Program` | Main (Compile) | Audio | Triple-Buffer, Release/Acquire |
| Rohwerte CC1, ChAT, Last-Note-Druck | Main | Audio | `std::atomic<uint8_t>`, relaxed |
| Drucktabelle `pressure[128]` | Main | Audio | `std::atomic<uint8_t>`, relaxed |
| Retrigger-Zähler (Note On) | Main | Audio | `std::atomic<uint32_t>` |
| Engine-Zustand (Phasen, Glättung, Offsets) | Audio | Audio | Alleinbesitz |
| Monitor-Schnappschuss (optional) | Audio | Main | SPSC-Queue wie `CpuSnapshot` in Physical Modeling |

Alle verwendeten Atomics werden per `static_assert(…::is_always_lock_free)` geprüft, wie
in den vorhandenen Queues.

## 3. Datenstrukturen

### 3.1 Konstanten und Quellen

```cpp
namespace mod {
constexpr size_t kSources = 8, kTargets = 8;
constexpr size_t kMaxActive = 8;           // D2: Richtwert, nur in der Validierung (E6)
enum Source : uint8_t { Lfo1, Lfo2, Random, Cv1, Cv2, ModWheel, ChannelAt, PolyAt };
}
```

| Zeile | Quelle | Wertebereich | Rohdaten | Glättung |
| --- | --- | --- | --- | --- |
| 0 | LFO 1 | −1…+1 | intern | keine (stetig) |
| 1 | LFO 2 | −1…+1 | intern | keine |
| 2 | Random / S&H | −1…+1 | intern | Modus „geglättet“: linear zwischen Zufallswerten |
| 3 | CV 1 | −1…+1 | `hw.cv[0].Value()` | Totzone + 2 ms |
| 4 | CV 2 | −1…+1 | `hw.cv[1].Value()` | Totzone + 2 ms |
| 5 | Modwheel | 0…1 | CC1 (alle Kanäle) | 10 ms |
| 6 | Channel AT | 0…1 | Channel Pressure (alle Kanäle) | 10 ms |
| 7 | Poly AT | 0…1 | Poly Key Pressure pro Note | 10 ms (pro Stimme im Instrument bzw. Last-Note in der Engine) |

Unipolare Quellen mit negativer Tiefe senken das Ziel ab. Polaritätsoptionen gibt es in
v0.1 nicht.

### 3.2 Zielbeschreibung des Instruments

```cpp
struct TargetInfo {
    const char* name;       // Spaltenkopf, ≤ 12 Zeichen; nullptr = Spalte nicht vorhanden
    const char* span_text;  // Anzeige für Tiefe 100, z. B. "±12 st", ≤ 8 Zeichen
    float       span;       // normierter Hub bei Tiefe 100, 0 < span ≤ 1 (E5)
    bool        per_voice;  // Instrument kann das Ziel pro Stimme modulieren
    bool        latched;    // Wert wird nur beim Anschlag gelesen (z. B. Excitation)
};
struct InstrumentInfo {
    const char* key;                  // "toy-piano", "physical-modeling", "granular"
    const char* version;              // Firmwareversion des Instruments
    TargetInfo  targets[kTargets];
    uint8_t     unavailable_sources;  // Bit s = Quelle s nicht nutzbar (z. B. CV vom Instrument belegt)
};
```

Die **Spaltennummer ist die Ziel-ID**. Ihre Bedeutung gehört zur Schnittstelle des
Instruments. Wer sie ändert, ändert den **Schema-Hash**, eine CRC-32 über Key und alle
Zielbeschreibungen (PROTOCOL § 3.1). Gespeicherte Patches mit anderem Hash werden
nicht stillschweigend geladen (STORAGE § 4.4).

**Erlaubte Zellen** (`allowed_mask`, 64 Bit, beim `Init` berechnet und an den Editor
gemeldet; nicht erlaubte Zellen sind ausgegraut):

```text
allowed(s, t) = target[t].name != nullptr
              ∧ (unavailable_sources & (1 << s)) == 0
              ∧ ¬(s == PolyAt ∧ target[t].latched)          // Druck entsteht nach dem Anschlag
              ∧ ¬(s == PolyAt ∧ ¬target[t].per_voice ∧ poly_global == Grey)   // DECISIONS § 4.1
```

### 3.3 Patch (editierbare Konfiguration, 104 Byte)

```cpp
struct LfoParams    { uint16_t rate; uint8_t shape; uint8_t retrigger; };  // rate: 0…16383
struct RandomParams { uint16_t rate; uint8_t mode;  uint8_t reserved;  };
struct Patch {
    uint8_t      format = 1;        // Patch-Formatversion
    uint8_t      reserved = 0;
    int8_t       depth[kSources][kTargets]; // −100…+100, 0 = keine Wirkung
    uint8_t      enabled[kSources];         // Bit t = Zelle (s, t) eingeschaltet
    LfoParams    lfo[2];
    RandomParams random;
    char         name[16];                  // 7-Bit-ASCII, mit Nullen aufgefüllt
};                                          // 2 + 64 + 8 + 8 + 4 + 16 = 102 Byte (+2 Ausrichtung)
```

* Eine Zelle ist **aktiv**, wenn sie eingeschaltet ist und `depth ≠ 0`. Eine
  ausgeschaltete Zelle behält ihre Tiefe; die Verbindung lässt sich also ein- und
  ausschalten, ohne den Wert zu verlieren.
* **Validierung** (`Validate(patch, info)`): alle Tiefen im Bereich, höchstens
  `kMaxActive` aktive Zellen, nur erlaubte Zellen aktiv, Parameter im Bereich, Name
  7-Bit. Ein ungültiger Patch wird **als Ganzes abgelehnt**, nie teilweise übernommen.
* **Serialisierung**: feste Byte-Reihenfolge (Little Endian, ohne Padding) und
  `Crc32(serialize(patch))` (CRC-32/ISO-HDLC, wie zlib). Dieselbe Funktion nutzen
  Firmware, Editor und Speicher.

**Quellparameter v0.1** (`VORSCHLAG`, DECISIONS § 4.4–4.5):

| Quelle | Parameter | Werte |
| --- | --- | --- |
| LFO 1/2 | Rate | 0…16383 → 0,02…20 Hz logarithmisch: `f = 0,02 · 1000^(r/16383)` |
| | Wellenform | 0 Sinus, 1 Dreieck, 2 Sägezahn auf, 3 Sägezahn ab, 4 Rechteck |
| | Retrigger | 0 frei laufend, 1 Neustart bei jedem Note On |
| Random | Rate | wie LFO-Rate; Häufigkeit neuer Zufallswerte |
| | Modus | 0 Stufe (S&H), 1 geglättet (linear zum nächsten Wert) |

### 3.4 Program (kompilierte Form für den Audio-IRQ)

```cpp
struct Connection { uint8_t source, target; uint16_t pad; float gain; }; // gain = depth/100 · span[t]
struct Program {
    uint8_t    count;                 // aktive globale Verbindungen
    uint8_t    source_mask;           // benutzte Quellen (nur diese werden gerechnet)
    bool       active;                // count > 0 oder Poly-AT-Stimmenweg benutzt
    Connection conn[kMaxActive];      // 8 × 8 Byte
    float      voice_gain[kTargets];  // Poly AT pro Stimme: depth/100 · span, sonst 0
    uint32_t   lfo_inc[2], random_inc;// Phaseninkrement pro Sample (2^32 · f / fs)
    uint8_t    lfo_shape[2], lfo_retrigger[2], random_mode;
};                                    // ≈ 120 Byte
```

Der Main-Loop führt `Compile()` nach jeder angenommenen Änderung aus. Dabei passiert
Folgendes:

* Aktive Zellen werden in `conn[]` oder `voice_gain[]` einsortiert.
* Poly AT auf einem `per_voice`-Ziel geht in `voice_gain[t]`. Poly AT auf einem
  globalen Ziel wird zu einer normalen Verbindung mit der Last-Note-Quelle.
* Die Rate wird in ein Phaseninkrement umgerechnet. `powf` läuft damit nur im
  Main-Loop.

### 3.5 Übergabe Main → Audio

Ein **Triple-Buffer** mit drei `Program`-Instanzen, insgesamt ≈ 360 Byte. Main schreibt in
den freien Puffer und tauscht ihn atomar (Release) mit dem mittleren, Audio holt ihn zu
Beginn von `Tick()` ab (Acquire). Keine Seite wartet, und es gewinnt der neueste Stand.
LFO-Phasen und Glättungszustände liegen in der Engine. Ein neues Program setzt sie
nicht zurück, Rasteränderungen beim Spielen sind deshalb nahtlos.

## 4. Modulationsverarbeitung

### 4.1 Ablauf eines Ticks

`Tick(n)` wird vom Instrument einmal pro Audio-Block aufgerufen, mit der tatsächlichen
Sampleanzahl `n`: 48 bei Toy Piano und Physical Modeling, 256 bei Nebulae.

```text
1. Program aus der Mailbox holen, falls neu.
2. Wenn !program.active: Phasen fortschreiben (2 Additionen), Offsets bleiben +0.0f, Ende.
3. Für jede Quelle in source_mask: Wert berechnen (§ 4.2–4.4) → src[8].
4. offset[0..7] = 0; für jede Verbindung c: offset[c.target] += c.gain · src[c.source].
```

Danach liest das Instrument, wann immer es einen Parameter braucht:

```cpp
float Apply(size_t t, float base) const     { return Clamp01(base + offset_[t]); }
float ApplyVoice(size_t t, float base, float voice_pressure) const
    { return Clamp01(base + offset_[t] + voice_gain_[t] * voice_pressure); }
```

**Bitgenau neutral**: Ohne aktive Verbindung ist `offset_[t] == +0.0f`, und
`Clamp01(base + 0.0f) == base` gilt für jedes `base` in [0, 1]. Ein Instrument mit
eingebundener, aber leerer Matrix verhält sich dadurch exakt wie vorher. Das wird
getestet (TEST_PLAN § 2.3).

**Begrenzung erst nach der Summe**: Gegenläufige Verbindungen heben sich korrekt auf.
Die Begrenzung auf [0, 1] verhindert ungültige Werte für die Abbildung des Instruments.

### 4.2 LFO und Random

* **Phase** als `uint32_t`-Akkumulator: `phase += inc · n`. Der Überlauf ist der
  Periodenwechsel, ohne `fmod` und ohne Drift. Die Frequenz stimmt bei jeder
  Blockgröße (E7).
* **Formen** aus `p = phase / 2^32`: Dreieck, Sägezahn und Rechteck direkt; Sinus über
  ein Polynom ohne Tabelle (Fehler < 0,1 %, `SCHÄTZUNG`).
* **Retrigger**: Ändert sich der Note-On-Zähler, setzt die Engine die Phase auf 0.
* **Random**: eigener Phasenakkumulator. Bei Überlauf gilt `prev = cur` und
  `cur = xorshift32() → −1…+1`. Modus Stufe gibt `cur` aus, Modus geglättet
  `prev + (cur − prev) · p`. Der Startwert ist fest, Tests sind reproduzierbar.

### 4.3 CV-Aufbereitung

```text
x = cv − cal_offset[i]                 // Gerätekalibrierung (STORAGE § 3), Standard 0
x = |x| < 0,02 ? 0 : (x − sign(x)·0,02) / 0,98    // Totzone gegen Rauschen offener Buchsen (E8)
y += a_cv · (x − y)                    // ≈ 2 ms
```

Die Werte für Totzone und Zeitkonstante sind `VORSCHLAG`. Sie werden in Phase 2 am
Gerät festgelegt.

### 4.4 MIDI-Quellen

* **Main** (`HandleMidi`):
  * CC1 → `mw_raw`; Channel Pressure → `chat_raw`.
  * Note On (Velocity > 0) → `pressure[n] = 0`, Note gehalten, ans Ende der Last-Note-Liste,
    Retrigger-Zähler + 1.
  * Poly Pressure → `pressure[n] = v`, nur wenn die Note gehalten ist.
  * Note Off oder Note On mit Velocity 0 → `pressure[n] = 0`, Note aus der Liste.
  * `last_pressure = pressure[letzte gehaltene Note]`, oder 0, wenn keine gehalten ist.
* Noten werden **mitgelesen und an das Instrument weitergegeben** (`HandleMidi` gibt
  `false` zurück). CC1 und Aftertouch werden verbraucht. Das ist unkritisch, weil keines
  der vorhandenen Instrumente diese Nachrichten nutzt (`BELEGT`: Toy Piano verarbeitet
  nur Note On, Physical Modeling nur CC20–33, Note On und Pitch Bend).
* **Audio**: 7-Bit-Rohwert / 127, anschließend Glättung mit 10 ms gegen Stufen der
  7-Bit-Werte.
* Die Last-Note-Liste fasst 16 Einträge. Läuft sie über, fällt die älteste Note heraus,
  was nur die Last-Note-Regel betrifft. `All Notes Off` (CC123) leert Tabelle und Liste.

### 4.5 Parametersprünge vermeiden

| Ursache | Maßnahme |
| --- | --- |
| 7-Bit-MIDI-Stufen | 10 ms Glättung in der Engine (global) bzw. im Stimmen-Helfer |
| CV-Rauschen | Totzone + 2 ms |
| Tiefenänderung im Editor | Offset springt einmal pro Tick. Instrumente mit eigener Parameterglättung (Physical Modeling 5 ms) fangen das ab. Bei Toy Piano v0.3 bekommen die neuen Ziele eine eigene 5-ms-Glättung (Phase 5). |
| Random im Modus Stufe | gewollt; Modus geglättet als Alternative |
| Neues Program | keine Phasen-Rücksetzung, Offsets laufen stetig weiter |
| Lastspitzen | feste Schleifengrenzen (≤ 8 Quellen, ≤ 8 Verbindungen), kein Heap, kein `powf`, keine Datenabhängigkeit |

Stufenziele wie Modellwahl sind in v0.1 **keine** Ziele. Damit entfallen Hysterese und
Crossfade-Fragen; sie lassen sich später ergänzen.

## 5. Globale und stimmenbezogene Modulation

### 5.1 Zwei Wege

| Weg | Quellen | Ergebnis | Kosten |
| --- | --- | --- | --- |
| **Global** | alle 8, Poly AT als Last-Note | `offset_[t]`, einmal pro Tick | ≤ 8 Multiplikationen und Additionen |
| **Stimme** | nur Poly AT | `voice_gain_[t] · voice_pressure` beim Lesen pro Stimme | 1 Multiplikation und Addition pro Stimme und Ziel |

Alle übrigen Quellen sind für jede Stimme gleich. Pro Stimme muss deshalb **nur der
Poly-AT-Anteil** gerechnet werden; das hält den Stimmenweg billig. Die Engine kennt
keine Stimmen. Die Zuordnung Stimme → Note bleibt beim Instrument, das sie ohnehin
führt (E3).

### 5.2 Helfer für Instrumente

```cpp
// Pro Stimme ein Objekt (4 Byte), gehört zum Instrument, läuft im Audio-IRQ.
class VoicePressure {
  public:
    float Process(const Engine& e, int note) {   // note < 0: Stimme stumm
        const float target = note >= 0 ? e.NotePressure(uint8_t(note)) : 0.0f;
        y_ += e.PressureCoeff() * (target - y_); // 10 ms, Koeffizient passend zu n
        return y_;
    }
    void Reset() { y_ = 0.0f; }                  // bei Voice-Stealing optional
  private:
    float y_ = 0.0f;
};
```

### 5.3 Verhalten in Grenzfällen

| Fall | Verhalten |
| --- | --- |
| Voice-Stealing | Die Stimme spielt eine neue Note, deren Druck mit 0 beginnt. Der Helfer gleitet in 10 ms vom alten Wert dorthin; mit `Reset()` springt er sofort. |
| Gleiche Note in zwei Stimmen | gleicher Druck (MIDI-1.0-Grenze, E3) |
| Note Off bei weiterklingender Stimme (Rings ignoriert Note Off) | Druck → 0, die Modulation gleitet zum Grundwert zurück |
| Poly AT ohne gehaltene Note | ignoriert |
| Keyboard sendet nur Channel AT | Die Poly-AT-Zeile bleibt wirkungslos; der Editor zeigt die Live-Aktivität (Monitor) |
| Monophones Instrument (Nebulae) | alle Ziele global → Last-Note-Druck |

## 6. Instrumentenschnittstelle

### 6.1 API (Entwurf)

```cpp
namespace mod {
class Engine {                                   // Audio-IRQ
  public:
    void  Init(const InstrumentInfo& info, float sample_rate);
    void  SetCv(float cv1, float cv2);           // nach hw.UpdateAllControls()
    void  Tick(size_t samples);
    float Apply(size_t target, float base) const;
    float ApplyVoice(size_t target, float base, float voice_pressure) const;
    float NotePressure(uint8_t note) const;      // 0…1, ungeglättet
    float PressureCoeff() const;
    bool  Active() const;
};
class Controller {                               // Main-Loop
  public:
    void Init(Engine& engine, const InstrumentInfo& info, uint8_t device_id);
    bool HandleMidi(const MidiMessage& m);       // true = verbraucht
    void Service(uint32_t now_ms, MidiSender& out);
};
struct Module { Engine engine; Controller controller; };  // statisch, Platzierung per MOD_ON_DAISY_HOT
}
```

`MidiMessage` ist eine kleine libDaisy-freie Struktur aus Typ, Kanal, zwei Datenbytes und
einem Zeiger auf SysEx-Daten mit Länge. `MidiSender` ist ein Interface mit
`bool Send(const uint8_t*, size_t)`. Es wird nur im Main-Loop aufgerufen, ein virtueller
Aufruf ist dort unkritisch. Der Adapter implementiert es mit `MidiUsbHandler::SendMessage`
und prüft vorher `dev_state == USBD_STATE_CONFIGURED`, wie Toy Piano.

### 6.2 Einbindung in fünf Schritten (Phase 5, je Instrument)

1. Eine `constexpr InstrumentInfo` mit bis zu 8 Zielen anlegen.
2. Im Makefile die Include-Pfade ergänzen und `mod::Module` statisch anlegen.
3. Im Audio-Callback nach `UpdateAllControls()` `SetCv(hw.cv[0].Value(), hw.cv[1].Value())`
   und `Tick(size)` aufrufen. An den Lesestellen der Parameter `Apply` bzw. `ApplyVoice`.
4. Im Main-Loop jedes `MidiEvent` zuerst an `HandleMidi()` geben und danach
   `Service()` aufrufen.
5. Host-Test: Die vorhandenen Callback-Fakes zeigen mit leerer Matrix bitgenau dieselbe
   Ausgabe, mit Matrix die erwartete Modulation.

### 6.3 Vorschläge für Zieltabellen

**Toy Piano v0.3** (technischer Integrationstest; neue Ziele nötig, E4):

| Spalte | Ziel | Weg | span | Hinweis |
| --- | --- | --- | --- | --- |
| 0 | Pitch | Stimme | ±1 st | Vibrato; pro Stimme über Phaseninkrement × 2^(x/12) |
| 1 | Level | Stimme | 100 % | Tremolo, Poly-AT-Lautstärke |
| 2 | Harmonic | Stimme | 100 % | Anteil des 2. Obertons |
| 3 | Attack | global, `latched` | 100 % | wirkt beim nächsten Anschlag |
| 4 | Decay | global, `latched` | 100 % | wirkt beim nächsten Anschlag |
| 5–7 | — | — | — | ausgegraut (prüft D4) |

**Rings (Physical Modeling)** (DSP-Änderung für den Stimmenweg nötig, E1):

| Spalte | Ziel | Parameter heute | Weg | span |
| --- | --- | --- | --- | --- |
| 0 | Pitch | `Frequency` (0…1 = ±24 st) | Stimme | 0,25 = ±12 st |
| 1 | Structure | `Structure` | Stimme¹ | 100 % |
| 2 | Brightness | `Brightness` | Stimme | 100 % |
| 3 | Damping | `Damping` | Stimme | 100 % |
| 4 | Position | `Position` | Stimme | 100 % |
| 5 | Excitation | `Excitation` | global, `latched` | 100 % |
| 6 | Reverb | `Decay` | global | 100 % |
| 7 | Dry/Wet | `Wet` | global | 100 % |

¹ `OFFEN`: In Akkordmodellen wählt Structure den Akkord über `ChordSelector` für den
ganzen Part. Ob eine stimmenbezogene Structure dort sinnvoll ist, wird in Phase 5
geprüft.

Integration: `ParameterManager::Advance()` glättet bereits mit 5 ms. Die globalen Ziele
brauchen nur `current_[i] += k · (Apply(i, target_[i]) − current_[i])`. Der Stimmenweg
übergibt 4 Patches und 4 Pitch-Offsets an `Part::Process`.

**Nebulae** (alles global, Poly AT als Last-Note; effektiv 187,5 Hz, E7): Start, Speed,
Size, Density, Pitch, Overlap, Blend, Window, abgebildet auf `Parameters::primary[0..7]`.

## 7. Echtzeitregeln

Die Regeln entsprechen denen der vorhandenen Projekte und sind verbindlich:

* **Audio-IRQ**: kein Heap, keine Locks, kein USB, kein QSPI, kein Logging, kein `powf`,
  keine Division pro Verbindung, keine virtuellen Aufrufe, feste Schleifengrenzen.
* **Main-Loop**: Protokoll, Validierung, `Compile()`, CRC. Pro Aufruf von `Service()`
  höchstens ein Rahmen verarbeiten und höchstens eine Antwort senden.
* **Speicherort**: Standard ist `.bss`, bei BOOT_SRAM also DTCM (E12). Mit dem Makro
  `MOD_ON_DAISY_HOT` kann ein Instrument den Zustand in den AXI-SRAM legen.
* **Flash**: In v0.1 gibt es keine Flash-Schreibfunktion (D8). `STORE`-Befehle beantwortet
  die Firmware mit `NAK 9 = nicht implementiert`.

## 8. Speicher- und Performanceabschätzung

### 8.1 RAM (`SCHÄTZUNG`)

| Posten | Bytes | Kontext |
| --- | ---: | --- |
| Program × 3 (Triple-Buffer) | 360 | Audio/Main |
| Engine: Phasen, Random, 5 Glättungen, `src[8]`, `offset[8]`, Koeffizienten | 150 | Audio |
| Drucktabelle 128 + Rohwerte + Zähler | 140 | Main → Audio |
| Last-Note-Liste (16) + gehaltene Noten (128 Bit) | 32 | Main |
| Patch (Arbeitskopie) | 104 | Main |
| Empfangsrahmen, Senderahmen, Antwortcache für Wiederholungen | 3 × 120 | Main |
| `VoicePressure` pro Stimme (Instrument) | 4 je Stimme | Audio |
| **Summe Modul** | **≈ 1,15 KiB** | |
| Konstanten (`InstrumentInfo`, Namen) | ≈ 0,2 KiB | Image |
| Code | ≈ 4–8 KiB | Image (BOOT_SRAM-Grenze 480 KiB) |

### 8.2 CPU pro Tick (`SCHÄTZUNG`, Cortex-M7 480 MHz)

Das Budget pro 48-Sample-Block beträgt 1 ms bzw. **480 000 Zyklen**.

| Szenario | Zyklen pro Tick | Anteil bei 1 kHz |
| --- | ---: | ---: |
| Matrix inaktiv (eingebunden, keine Verbindung) | ~30 + 8 × ~5 Apply | ≈ 0,015 % |
| 1 Verbindung (LFO → 1 Ziel) | ~100–150 | ≈ 0,03 % |
| 4 Verbindungen | ~150–250 | ≈ 0,05 % |
| 8 Verbindungen, alle Quellen benutzt | ~250 | ≈ 0,05 % |
| + Stimmenweg Rings (4 Stimmen × 5 Ziele) | +~200 | ≈ 0,1 % gesamt |
| + Stimmenweg Toy Piano (32 Stimmen × 3 Ziele) | +~900 | ≈ 0,25 % gesamt |
| Lasttest: 64 aktive Zellen (über der v0.1-Grenze) | ~600 | ≈ 0,13 % |

Die Unsicherheit liegt bei etwa Faktor 2–3 (Inlining, Cache, Aufrufkosten). Selbst
dann bleibt die Matrix **unter 1 % CPU**. Main-Loop-Kosten pro Rasteränderung:
Validierung, Compile und CRC-32 über 104 Byte, zusammen etwa 5 000 Zyklen (≈ 10 µs).

Was die Matrix nicht abdeckt: Kosten, die ein Ziel im Instrument auslöst. Bei Torus
entstehen keine (Filter werden ohnehin pro Quantum berechnet, E1). Bei Toy Piano
kostet das neue Pitch-Ziel pro Stimme ein `exp2`. Das wird in Phase 5 gemessen.

Wie gemessen wird, steht in [TEST_PLAN_PHASE2.md](TEST_PLAN_PHASE2.md) § 3.

## 9. Weiteres Vorgehen

| Phase | Inhalt |
| --- | --- |
| 1 | diese Spezifikation; Entscheidungen DECISIONS § 4 |
| 2 | `include/` + Host-Tests + Messfirmware `bench/` + SysEx-Hardwaretest (TEST_PLAN) |
| 3 | JUCE-Editor Standalone Windows (editors/mod-on-daisy/docs/GUI.md) |
| 4 | Performanceprüfung mit integrierten Instrumenten |
| 5 | Toy Piano v0.3, danach Rings mit Stimmenweg, danach Nebulae |
| 6 | QSPI-Speicherung umsetzen (STORAGE.md), VST3, weitere Quellen |

Die Speicherung ist in Phase 6 eingeplant. Ihre Spezifikation liegt vor; die Umsetzung
braucht nach D8 eine eigene Freigabe und kann bei Bedarf früher erfolgen, frühestens nach
Phase 2.
