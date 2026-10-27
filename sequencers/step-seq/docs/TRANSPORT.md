# Transport- und Ereignisvertrag (Acid Seq)

**Stand:** 26. Oktober 2026 · **Status:** Vertrag aus Phase 0 für Sequencer, Stimme
und Tempo-Empfänger (Delay). Grundlage: Arbeitsauftrag 3.2, 3.3, 4.2–4.6.
Testvektoren: [`contracts/transport_vectors.json`](../contracts/transport_vectors.json).

## 1. Zeitbasis

* Abtastrate 48 kHz, Blockgröße 48 Frames. Ein Block ist ein **Tick** (1 ms).
* Der Sequencer zählt in Samples mit einem Akkumulator aus ganzem Teil und Bruchteil
  (64 Bit, 32 Bit Bruchteil). Ein Step dauert bei Tempo *B* und Swing *s*
  `2 · T · s` bzw. `2 · T · (1 − s)` Samples mit `T = 48000 · 60 / (B · 4)`
  (Sechzehntel). Der Fehler wird nie gerundet weitergetragen, das Tempo driftet nicht.
* Ein Ereignis wird dem Tick zugeordnet, in dem sein Zeitpunkt liegt. Alle Ereignisse
  eines Ticks haben den Zeitstempel des Blockanfangs.

## 2. Reihenfolge innerhalb eines Ticks

1. Gate-Off des alten Steps (bzw. Ende einer Vorhör-Note)
2. Reset (Gate IN 2 im Reset-Modus)
3. Step-Wechsel und Übernahme von Pattern, Länge und Modus am Step-Rand
4. Gate-On des neuen Steps

Fallen Gate-Off und Gate-On auf denselben Tick und hat der alte Step kein Slide,
verschiebt sich das Gate-On um einen Tick (Retrigger-Lücke ≥ 1 ms). Gates sind
mindestens 2 ms High und 1 ms Low.

## 3. Ereignisse des Sequencers

Der Sequencer-Kern erzeugt pro Tick höchstens eine kleine, feste Anzahl Ereignisse.
Er kennt keine Hardware. Die Ausgänge (CV/Gate, MIDI-Queue, interne Stimme) lesen
dieselben Ereignisse.

```cpp
namespace step_seq {
enum class Source : uint8_t { Sequencer, Preview, MidiIn };

struct NoteEvent {
    enum class Kind : uint8_t { On, Off, Legato };   // Legato = slide into this note
    Kind kind;
    uint8_t note;        // MIDI 0..127 after scale, transpose and clamp
    uint8_t velocity;    // 100 or accent velocity (127), 0 for Off
    bool accent;
    Source source;
    uint32_t tag;        // identifies the note; an Off only ends the On with the same tag
};

struct GateEvent  { bool high; };                      // CV OUT 2
struct PitchEvent { uint8_t note; bool glide; };       // CV OUT 1, glide = slide
struct StepEvent  { uint8_t step; bool gate; };        // display, monitor
struct TempoEvent { float bpm; double samples_per_quarter; bool external; };
struct TransportEvent { uint8_t state; uint8_t position; };  // 4.3 states
}
```

* **Kennung (`tag`):** Jede Note-On bekommt eine neue Kennung. Ein Note-Off wirkt nur
  auf die Note mit derselben Kennung. Ein veraltetes Off (z. B. das Ende einer
  150-ms-Vorhör-Note nach einem Start) wird ignoriert.
* **Legato:** Ein Step mit Slide endet nicht mit Off. Der nächste Step kommt als
  `Legato` (neue Kennung, ersetzt die alte). Gleiche Note mit Slide: kein Ereignis,
  die Note bleibt gehalten. Gleiche Note ohne Slide: Off, dann On im nächsten Tick.
* **Panic** erzeugt ein `Off` für jede aktive Kennung der Quelle `Sequencer` bzw.
  `Preview` und Gate-Low. Offene Slides enden.

## 4. Stimme (`acid-303`)

```cpp
voice.NoteOn(note, velocity, accent, legato, source, tag);
voice.NoteOff(tag, source);         // ends only that note of that source
voice.AllNotesOff(source);          // Panic of one source
```

* Monophon mit Notenstapel (höchstens 8 Einträge je Quelle, ältester fällt heraus).
* Die zuletzt gespielte Note klingt, gleich aus welcher Quelle. Endet die klingende
  Note, übernimmt die jüngste noch gehaltene Note beider Quellen (Legato, ohne neue
  Hüllkurve).
* `Legato` gleitet mit der Slide-Zeit zur neuen Tonhöhe, ohne neue Hüllkurve.
* Externe MIDI-Noten auf dem Stimmen-Kanal haben die Quelle `MidiIn`. Ein Panic des
  Sequencers berührt sie nicht.

## 5. Tempo-Empfänger (Delay)

* Der Host gibt jedem Tempo-Empfänger bei jeder Änderung `TempoEvent` mit
  `samples_per_quarter = 48000 · 60 / BPM`. Bei externer Clock ist es die
  Periodenschätzung (gleitender Mittelwert der letzten 4 Perioden).
* Das Delay rechnet Sync-Notenwerte daraus selbst um (1/32 bis 1/1, punktiert × 1,5,
  triolisch × 2/3). Eine Tempoänderung gleitet wie eine Zeitänderung (4.8).
* Ohne Sequencer (Firmware `delay-fx`) gilt das Tempo von VR5.

## 6. Transport-Zustände

STOP, WARTET, LÄUFT und PAUSE mit den Übergängen aus 4.3 des Auftrags. `TransportEvent`
meldet jeden Wechsel. Die Panic-Regeln aus 4.4 gelten bei jedem Übergang nach STOP
und PAUSE und beim Timeout der Clock.

## 7. Testvektoren

`transport_vectors.json` enthält die Sollfolgen aus 4.2 (Vorwärts, Rückwärts, Länge 1,
Längen- und Moduswechsel) und die Reihenfolge innerhalb eines Ticks. Phase 1 spielt
sie im Kern-Test nach.
