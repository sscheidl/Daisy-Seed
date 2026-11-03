#pragma once
// SPDX-License-Identifier: MIT
// Musical events of the Streichwurst instrument contract (MODULAR_ARCHITECTURE.md).
// The MIDI/control engine of the product translates MIDI into these events; the
// instrument has no MIDI parser of its own.
#include <cstdint>

namespace streichwurst {

enum class EventType : uint8_t { NoteOn, NoteOff, Sustain, PitchBend, AllNotesOff, AllSoundOff };

struct Event {
    EventType type = EventType::NoteOn;
    uint8_t channel = 0;  // 0..15; the router decides which channels reach the instrument
    uint8_t note = 0;     // 0..127
    uint8_t value = 0;    // NoteOn/NoteOff velocity (NoteOn 0 = NoteOff), Sustain 0..127 (>= 64 down)
    int16_t bend = 0;     // PitchBend -8192..8191
    uint16_t frame = 0;   // offset in the block; equal frames keep their input order
};

inline Event NoteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint16_t frame = 0) {
    Event e;
    e.type = EventType::NoteOn;
    e.channel = channel;
    e.note = note;
    e.value = velocity;
    e.frame = frame;
    return e;
}
inline Event NoteOff(uint8_t channel, uint8_t note, uint16_t frame = 0) {
    Event e;
    e.type = EventType::NoteOff;
    e.channel = channel;
    e.note = note;
    e.frame = frame;
    return e;
}
inline Event Sustain(uint8_t channel, bool down, uint16_t frame = 0) {
    Event e;
    e.type = EventType::Sustain;
    e.channel = channel;
    e.value = down ? 127 : 0;
    e.frame = frame;
    return e;
}
inline Event PitchBend(uint8_t channel, int16_t bend, uint16_t frame = 0) {
    Event e;
    e.type = EventType::PitchBend;
    e.channel = channel;
    e.bend = bend;
    e.frame = frame;
    return e;
}
inline Event AllNotesOff(uint16_t frame = 0) {
    Event e;
    e.type = EventType::AllNotesOff;
    e.frame = frame;
    return e;
}
inline Event AllSoundOff(uint16_t frame = 0) {
    Event e;
    e.type = EventType::AllSoundOff;
    e.frame = frame;
    return e;
}

} // namespace streichwurst
