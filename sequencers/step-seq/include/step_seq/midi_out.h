#pragma once
// SPDX-License-Identifier: MIT
// MIDI output with note tracking (ARBEITSAUFTRAG 4.4, R05). The callback feeds events
// into a bounded queue and keeps the set of notes that should sound ("wanted"); main
// sends with a budget per pass and derives every Note-Off from its own table of sent
// notes, so a full queue can drop a Note-On but never leave a note hanging.
#include "engine.h"
#include "spsc.h"
#include <atomic>
#include <cstdint>

namespace step_seq {

class NoteSet {
  public:
    bool Has(uint8_t n) const { return (bits_[n >> 5] >> (n & 31)) & 1u; }
    void Set(uint8_t n) { bits_[n >> 5] |= 1u << (n & 31); }
    void Clear(uint8_t n) { bits_[n >> 5] &= ~(1u << (n & 31)); }
    void ClearAll() { bits_[0] = bits_[1] = bits_[2] = bits_[3] = 0; }
    bool Any() const { return bits_[0] | bits_[1] | bits_[2] | bits_[3]; }
    uint32_t Word(int i) const { return bits_[i]; }

  private:
    uint32_t bits_[4] = {};
};

struct MidiMsg {
    enum class Kind : uint8_t { NoteOn, NoteOff, Clock, Start, Stop, Continue, AllNotesOff };
    Kind kind = Kind::NoteOn;
    uint8_t note = 0, velocity = 0;
};

// Callback side.
class NoteFeed {
  public:
    using Queue = SpscQueue<MidiMsg, 64>;
    Queue queue;
    std::atomic<uint32_t> dropped_on{0}, overflow{0};

    // Translates one tick of engine events (Sequencer and Preview notes, clock).
    void Feed(const TickOutput& out) {
        for(int i = 0; i < out.count; ++i) {
            const OutEvent& e = out.events[i];
            switch(e.kind) {
                case OutEvent::Kind::NoteOn: On(e.note, e.velocity); break;
                case OutEvent::Kind::NoteOff: Off(e.note); break;
                case OutEvent::Kind::Legato:
                    // New note first, then the old one ends: legato on the receiver.
                    On(e.note, e.velocity);
                    if(e.prev_note != e.note)
                        Off(e.prev_note);
                    break;
                case OutEvent::Kind::Clock: Rt(MidiMsg::Kind::Clock); break;
                case OutEvent::Kind::Start: Rt(MidiMsg::Kind::Start); break;
                case OutEvent::Kind::Stop: Rt(MidiMsg::Kind::Stop); break;
                case OutEvent::Kind::Continue: Rt(MidiMsg::Kind::Continue); break;
                case OutEvent::Kind::Panic: AllOff(); break;
            }
        }
    }
    void AllOff() {
        for(auto& w : wanted_)
            w.store(0, std::memory_order_release);
        MidiMsg m;
        m.kind = MidiMsg::Kind::AllNotesOff;
        if(!queue.Push(m))
            overflow.fetch_add(1, std::memory_order_relaxed);
    }
    bool Wanted(uint8_t note) const {
        return (wanted_[note >> 5].load(std::memory_order_acquire) >> (note & 31)) & 1u;
    }

  private:
    void On(uint8_t note, uint8_t velocity) {
        MidiMsg m;
        m.kind = MidiMsg::Kind::NoteOn;
        m.note = note;
        m.velocity = velocity;
        // Only a queued Note-On becomes wanted: a dropped one is never sent, so no Off is owed.
        if(queue.Push(m))
            wanted_[note >> 5].fetch_or(1u << (note & 31), std::memory_order_release);
        else
            dropped_on.fetch_add(1, std::memory_order_relaxed);
    }
    void Off(uint8_t note) {
        wanted_[note >> 5].fetch_and(~(1u << (note & 31)), std::memory_order_release);
        MidiMsg m;
        m.kind = MidiMsg::Kind::NoteOff;
        m.note = note;
        if(!queue.Push(m))
            overflow.fetch_add(1, std::memory_order_relaxed); // main reconciles from `wanted`
    }
    void Rt(MidiMsg::Kind kind) {
        MidiMsg m;
        m.kind = kind;
        if(!queue.Push(m))
            overflow.fetch_add(1, std::memory_order_relaxed);
    }
    std::atomic<uint32_t> wanted_[4] = {};
};

// Main side: one output port (USB or DIN) with its own table of sent notes per channel.
struct PortBudget {
    int messages = 8;   // note messages per pass (4.4)
    int bytes = 1 << 30; // DIN: 24 bytes per pass (~7.7 ms at 31 250 baud)
};

template <class SendFn>
class MidiPort {
  public:
    explicit MidiPort(SendFn send, int byte_budget) : send_(send), byte_budget_(byte_budget) {}

    bool enabled = true;
    uint32_t sent_messages = 0;

    void BeginPass() {
        messages_ = 8;
        bytes_ = byte_budget_;
    }
    bool CanNote(int count = 1) const { return messages_ >= count && bytes_ >= 3 * count; }
    bool CanByte() const { return bytes_ >= 1; }
    bool Has(uint8_t channel, uint8_t note) const { return sent_[channel & 15].Has(note); }
    bool AnySent(uint8_t channel) const { return sent_[channel & 15].Any(); }

    void Rt(uint8_t status) {
        if(!enabled)
            return;
        uint8_t b[1] = {status};
        Emit(b, 1, false);
    }
    void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        if(!enabled)
            return;
        uint8_t b[3] = {uint8_t(0x90 | ((channel - 1) & 15)), uint8_t(note & 0x7F), uint8_t(velocity & 0x7F)};
        Emit(b, 3, true);
        sent_[channel & 15].Set(note);
    }
    void NoteOff(uint8_t channel, uint8_t note) {
        if(!sent_[channel & 15].Has(note))
            return;
        sent_[channel & 15].Clear(note);
        if(!enabled)
            return;
        uint8_t b[3] = {uint8_t(0x80 | ((channel - 1) & 15)), uint8_t(note & 0x7F), 0};
        Emit(b, 3, true);
    }
    // Note-Offs for everything still sent on a channel, then CC 123 (All Notes Off).
    void Panic(uint8_t channel) {
        for(int n = 0; n < 128; ++n)
            NoteOff(channel, uint8_t(n));
        AllNotesOff(channel);
    }
    void AllNotesOff(uint8_t channel) {
        if(!enabled)
            return;
        uint8_t b[3] = {uint8_t(0xB0 | ((channel - 1) & 15)), 123, 0};
        Emit(b, 3, true);
    }
    // USB unplugged: nothing arrives any more, the table is void.
    void Forget() {
        for(auto& s : sent_)
            s.ClearAll();
    }

  private:
    void Emit(uint8_t* bytes, int size, bool note) {
        send_(bytes, size);
        ++sent_messages;
        bytes_ -= size;
        if(note)
            --messages_;
    }
    SendFn send_;
    NoteSet sent_[16];
    int byte_budget_;
    int messages_ = 8, bytes_ = 0;
};

// One main-loop pass: reconcile Note-Offs from `wanted` first, then queued events within
// the budget. Clock bytes go before notes; events wait in the queue when a port is full.
template <class Usb, class Din>
inline void ServiceMidi(NoteFeed& feed, uint8_t channel, bool clock_out, Usb& usb, Din& din) {
    usb.BeginPass();
    din.BeginPass();
    for(int n = 0; n < 128; ++n) {
        const uint8_t note = uint8_t(n);
        if(feed.Wanted(note))
            continue;
        if(usb.Has(channel, note) && usb.CanNote())
            usb.NoteOff(channel, note);
        if(din.Has(channel, note) && din.CanNote())
            din.NoteOff(channel, note);
    }
    while(const MidiMsg* m = feed.queue.Peek()) {
        const bool rt = m->kind == MidiMsg::Kind::Clock || m->kind == MidiMsg::Kind::Start ||
                        m->kind == MidiMsg::Kind::Stop || m->kind == MidiMsg::Kind::Continue;
        if(rt) {
            if((usb.enabled && !usb.CanByte()) || (din.enabled && !din.CanByte()))
                break;
            if(clock_out) {
                const uint8_t status = m->kind == MidiMsg::Kind::Clock   ? 0xF8
                                       : m->kind == MidiMsg::Kind::Start ? 0xFA
                                       : m->kind == MidiMsg::Kind::Stop  ? 0xFC
                                                                         : 0xFB;
                usb.Rt(status);
                din.Rt(status);
            }
        } else if(m->kind == MidiMsg::Kind::AllNotesOff) {
            // Bounded by the number of sent notes; panic ignores the note budget.
            usb.Panic(channel);
            din.Panic(channel);
        } else {
            // A Note-On may need an Off first (repeated note): room for two messages.
            const int need = m->kind == MidiMsg::Kind::NoteOn ? 2 : 1;
            const auto room = [&](auto& port) { return !port.enabled || port.CanNote(need); };
            if(!room(usb) || !room(din))
                break;
            if(m->kind == MidiMsg::Kind::NoteOn) {
                // A repeated note: Note-Off before Note-On (4.2).
                usb.NoteOff(channel, m->note);
                din.NoteOff(channel, m->note);
                usb.NoteOn(channel, m->note, m->velocity);
                din.NoteOn(channel, m->note, m->velocity);
            } else {
                usb.NoteOff(channel, m->note);
                din.NoteOff(channel, m->note);
            }
        }
        MidiMsg done;
        feed.queue.Pop(done);
    }
}

} // namespace step_seq
