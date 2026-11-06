#pragma once
// SPDX-License-Identifier: MIT
// Typed events into the audio callback, on the shared SPSC queue of Step Seq. Note-offs and
// other releases have their own queue, so a flood of notes cannot hang a note: if even that
// queue is full, a sticky all-notes-off request takes over. It carries the sequence number
// of the lost release, because it must act after every older event and before every newer
// one. Sequence numbers also keep the input order between the two queues. The MIDI/control
// engine of a product translates MIDI into these events; the core has no parser.
#include "params.h"
#include "step_seq/spsc.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace grain_cloud {

enum class EventType : uint8_t { NoteOn, NoteOff, Sustain, PitchBend, SetParam, AllNotesOff, AllSoundOff };

struct Event {
    EventType type = EventType::NoteOn;
    uint8_t note = 0, velocity = 0; // velocity also carries Sustain (>= 64 down)
    Param param = Param::Position;
    float value = 0.f;              // SetParam 0..1, PitchBend in semitones
    uint32_t sequence = 0;
};

inline Event NoteOn(uint8_t note, uint8_t velocity) {
    Event e;
    e.note = note;
    e.velocity = velocity;
    return e;
}
inline Event NoteOff(uint8_t note) {
    Event e;
    e.type = EventType::NoteOff;
    e.note = note;
    return e;
}
inline Event Sustain(bool down) {
    Event e;
    e.type = EventType::Sustain;
    e.velocity = down ? 127 : 0;
    return e;
}
inline Event PitchBend(float semitones) {
    Event e;
    e.type = EventType::PitchBend;
    e.value = semitones;
    return e;
}
inline Event SetParam(Param param, float value) {
    Event e;
    e.type = EventType::SetParam;
    e.param = param;
    e.value = value;
    return e;
}
inline Event AllNotesOff() {
    Event e;
    e.type = EventType::AllNotesOff;
    return e;
}
inline Event AllSoundOff() {
    Event e;
    e.type = EventType::AllSoundOff;
    return e;
}

// Everything that ends a note. A note-on with velocity 0 is a note-off.
inline bool IsRelease(const Event& e) {
    return e.type == EventType::NoteOff || e.type == EventType::AllNotesOff || e.type == EventType::AllSoundOff ||
           (e.type == EventType::NoteOn && e.velocity == 0) || (e.type == EventType::Sustain && e.velocity < 64);
}

// Queue is a template so a test can stop the producer exactly between two queue reads.
template <template <class, std::size_t> class Queue>
class BasicControlBridge {
  public:
    static constexpr std::size_t kEvents = 128, kReleases = 64;

    // Main loop only. false: the event was lost; a lost release falls back to all notes off.
    bool Post(Event event) {
        if(++sequence_ == 0)
            ++sequence_; // 0 means "no lost release"
        event.sequence = sequence_;
        const bool release = IsRelease(event);
        if(release ? releases_.Push(event) : events_.Push(event))
            return true;
        overflows_.fetch_add(1, std::memory_order_relaxed);
        if(release)
            lost_release_.store(sequence_, std::memory_order_release); // the newest covers older ones
        return false;
    }

    // Audio only: the oldest event of both queues, or nullptr. The producer may post during
    // this call, so the read order matters. Seeing an item also makes every older item of the
    // other queue visible. A release read first therefore cannot pass an older event, and an
    // event found with no release seen needs one more look for an older release.
    const Event* Peek() {
        const Event* release = releases_.Peek();
        const Event* event = events_.Peek();
        if(event && !release)
            release = releases_.Peek();
        const bool take_event = event && (!release || int32_t(event->sequence - release->sequence) < 0);
        selected_ = take_event ? Selected::Event : (release ? Selected::Release : Selected::None);
        return take_event ? event : release;
    }
    // Audio only: removes exactly the item the last Peek() returned, whatever arrived since.
    void Drop() {
        Event ignored;
        if(selected_ == Selected::Event)
            events_.Pop(ignored);
        else if(selected_ == Selected::Release)
            releases_.Pop(ignored);
        selected_ = Selected::None;
    }
    // Audio only. Sequence of the newest lost release, 0 if none. Read this before Peek():
    // the acquire makes every older event visible there.
    uint32_t LostRelease() const { return lost_release_.load(std::memory_order_acquire); }
    // Keeps a newer loss that arrived after `sequence` was read.
    void ClearLostRelease(uint32_t sequence) {
        lost_release_.compare_exchange_strong(sequence, 0, std::memory_order_acq_rel);
    }
    uint32_t Overflows() const { return overflows_.load(std::memory_order_relaxed); }

  private:
    enum class Selected : uint8_t { None, Event, Release };

    Queue<Event, kEvents> events_;
    Queue<Event, kReleases> releases_;
    Selected selected_ = Selected::None; // audio only
    std::atomic<uint32_t> lost_release_{0};
    std::atomic<uint32_t> overflows_{0};
    uint32_t sequence_ = 0; // main loop only
};

using ControlBridge = BasicControlBridge<step_seq::SpscQueue>;

} // namespace grain_cloud
