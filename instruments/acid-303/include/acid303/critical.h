#pragma once
// SPDX-License-Identifier: MIT
// Hand-over for the inputs that must never be lost (Codex F01): Note-Off, All-Off and Mute.
// They normally travel in the command queue with everything else; when that queue is full,
// main records them here instead and the callback applies them right after draining the
// queue. Every queued command is older than a fallback entry (only the callback makes room
// in the queue), so the order of events is kept. Lock-free, no allocation.
#include "voice.h"
#include <atomic>
#include <cstdint>

namespace acid303 {

class CriticalInputs {
  public:
    // Main side.
    void NoteOff(uint8_t note) {
        note &= 0x7F;
        notes_off_[note >> 5].fetch_or(1u << (note & 31), std::memory_order_release);
    }
    void AllOff() { all_off_.store(true, std::memory_order_release); }
    void Mute() { mute_.store(true, std::memory_order_release); }

    // Callback side, after the queue: Note-Offs and All-Off for `source`, then Mute.
    // Returns true when a Mute was applied.
    bool Apply(Voice& voice, Source source) {
        for(int w = 0; w < 4; ++w) {
            uint32_t bits = notes_off_[w].exchange(0, std::memory_order_acq_rel);
            for(int b = 0; bits; ++b, bits >>= 1)
                if(bits & 1u)
                    voice.NoteOffNote(uint8_t(w * 32 + b), source);
        }
        if(all_off_.exchange(false, std::memory_order_acq_rel))
            voice.AllNotesOff(source);
        if(!mute_.exchange(false, std::memory_order_acq_rel))
            return false;
        voice.Mute();
        return true;
    }

  private:
    std::atomic<uint32_t> notes_off_[4] = {};
    std::atomic<bool> all_off_{false}, mute_{false};
};

} // namespace acid303
