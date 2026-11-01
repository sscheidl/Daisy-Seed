# Transport and event contract (Acid Seq)

**As of:** 26 October 2026 · **Status:** contract from phase 0 for the sequencer, the voice
and tempo receivers (Delay). Basis: work order 3.2, 3.3, 4.2–4.6.
Test vectors: [`contracts/transport_vectors.json`](../contracts/transport_vectors.json).

## 1. Time base

* Sample rate 48 kHz, block size 48 frames. One block is one **tick** (1 ms).
* The sequencer counts in samples with an accumulator of integer and fractional part
  (64 bits, 32-bit fraction). At tempo *B* and swing *s*, a step lasts
  `2 · T · s` or `2 · T · (1 − s)` samples with `T = 48000 · 60 / (B · 4)`
  (sixteenths). The error is never carried forward rounded; the tempo does not drift.
* An event is assigned to the tick in which its point in time lies. All events
  of a tick have the timestamp of the block start.

## 2. Order within a tick

1. Gate-off of the old step (or end of a preview note)
2. Reset (Gate IN 2 in reset mode)
3. Step change and application of pattern, length and mode at the step boundary
4. Gate-on of the new step

If gate-off and gate-on fall on the same tick and the old step has no slide,
the gate-on is shifted by one tick (retrigger gap ≥ 1 ms). Gates are
at least 2 ms high and 1 ms low.

## 3. Sequencer events

The sequencer core produces at most a small, fixed number of events per tick.
It knows no hardware. The outputs (CV/gate, MIDI queue, internal voice) read
the same events.

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

* **Identifier (`tag`):** every note-on gets a new identifier. A note-off only acts
  on the note with the same identifier. A stale Off (e.g. the end of a
  150 ms preview note after a start) is ignored.
* **Legato:** a step with slide does not end with Off. The next step arrives as
  `Legato` (new identifier, replaces the old one). Same note with slide: no event,
  the note stays held. Same note without slide: Off, then On in the next tick.
* **Panic** produces an `Off` for every active identifier of the source `Sequencer` or
  `Preview`, and gate low. Open slides end.

## 4. Voice (`acid-303`)

```cpp
voice.NoteOn(note, velocity, accent, legato, source, tag);
voice.NoteOff(tag, source);         // ends only that note of that source
voice.AllNotesOff(source);          // Panic of one source
```

* Monophonic with a note stack (at most 8 entries per source, the oldest drops out).
* The most recently played note sounds, regardless of its source. If the sounding
  note ends, the most recent still-held note of both sources takes over (legato, without a new
  envelope).
* `Legato` glides to the new pitch with the slide time, without a new envelope.
* External MIDI notes on the voice channel have the source `MidiIn`. A panic of the
  sequencer does not affect them.

## 5. Tempo receivers (Delay)

* On every change, the host gives each tempo receiver a `TempoEvent` with
  `samples_per_quarter = 48000 · 60 / BPM`. With an external clock it is the
  period estimate (moving average of the last 4 periods).
* The delay converts sync note values from it itself (1/32 to 1/1, dotted × 1.5,
  triplet × 2/3). A tempo change glides like a time change (4.8).
* Without a sequencer (firmware `delay-fx`), the tempo of VR5 applies.

## 6. Transport states

STOP, WAITING, RUNNING and PAUSE with the transitions from 4.3 of the work order. `TransportEvent`
reports every change. The panic rules from 4.4 apply at every transition to STOP
and PAUSE and on the clock timeout.

## 7. Test vectors

`transport_vectors.json` contains the expected sequences from 4.2 (forward, reverse, length 1,
length and mode changes) and the order within a tick. Phase 1 replays
them in the core test.
