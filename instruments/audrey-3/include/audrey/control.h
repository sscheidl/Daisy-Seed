#pragma once
// SPDX-License-Identifier: MIT
// Panel and MIDI control of Audrey 3 (README.md): eight pots with a second page on SW17,
// play modes on SW18, S1 for the echo warp, notes and CCs from MIDI. No hardware access;
// runs at the 1 kHz control rate in the audio callback.
#include "audrey/params.h"
#include <cmath>
#include <cstdint>

namespace audrey {

constexpr float kPickupDistance = 0.02f;
constexpr uint8_t kWarpCc = 31;
constexpr int kPagedPots = 3; // VR6-VR8

enum class PlayMode : uint8_t {
    Drone,      // SW18 up: always sounding, as the Audrey II; notes only set the pitch
    Play,       // SW18 middle: notes open the output, pitch glides
    PlayNoGlide // SW18 down: the same, pitch jumps
};

// Held notes, last note priority. A note beyond the capacity pushes out the oldest.
class NoteStack {
  public:
    void Push(uint8_t note) {
        Remove(note);
        if(size_ == kCapacity)
            Erase(0);
        notes_[size_++] = note;
    }
    void Remove(uint8_t note) {
        for(uint8_t i = 0; i < size_; ++i)
            if(notes_[i] == note) {
                Erase(i);
                return;
            }
    }
    void Clear() { size_ = 0; }
    bool Empty() const { return size_ == 0; }
    uint8_t Top() const { return notes_[size_ - 1]; }
    uint8_t Size() const { return size_; }

  private:
    static constexpr uint8_t kCapacity = 16;
    void Erase(uint8_t index) {
        for(uint8_t i = index; i + 1 < size_; ++i)
            notes_[i] = notes_[i + 1];
        --size_;
    }
    uint8_t notes_[kCapacity] = {};
    uint8_t size_ = 0;
};

struct PanelFrame {
    float pots[8] = {};
    bool page_b = false; // SW17
    int sw18 = 0;        // debounced: 0 up, 1 middle, 2 down
    uint16_t keys = 0;   // bit 0 = S1
};

class Control {
  public:
    void Init(float control_rate) {
        bank_.Init(control_rate);
        notes_.Clear();
        mode_ = PlayMode::Drone;
        started_ = false;
        held_ = 0;
    }

    void NoteOn(uint8_t note) {
        notes_.Push(note);
        bank_.SetNote(note, mode_ == PlayMode::PlayNoGlide);
    }
    void NoteOff(uint8_t note) {
        const bool was_top = !notes_.Empty() && notes_.Top() == note;
        notes_.Remove(note);
        if(was_top && !notes_.Empty())
            bank_.SetNote(notes_.Top(), mode_ == PlayMode::PlayNoGlide);
    }
    void AllNotesOff() { notes_.Clear(); }

    void ControlChange(uint8_t cc, uint8_t value) {
        if(cc >= kFirstParamCc && cc < kFirstParamCc + kParamCount)
            bank_.SetNorm(uint8_t(cc - kFirstParamCc), float(value) / 127.f);
        else if(cc == kWarpCc)
            bank_.SetWarp(value >= 64);
    }

    // Pots write only when they move, so a MIDI CC stays until then. After a page change
    // the paged pots wait until they reach the stored value: nothing jumps.
    void Panel(const PanelFrame& frame) {
        const bool first = !started_;
        if(!first && frame.page_b != page_b_)
            for(int i = 0; i < kPagedPots; ++i) {
                waiting_[i] = true;
                below_[i] = frame.pots[5 + i] < bank_.Norm(PagedParam(i, frame.page_b));
            }
        page_b_ = frame.page_b;
        for(int i = 0; i < 8; ++i) {
            const float v = frame.pots[i];
            const bool moved = first || v != last_[i];
            last_[i] = v;
            if(i < 5) {
                if(moved)
                    bank_.SetNorm(DirectParam(i), v);
                continue;
            }
            const int paged = i - 5;
            const uint8_t id = PagedParam(paged, page_b_);
            if(waiting_[paged]) {
                const float stored = bank_.Norm(id);
                if(std::fabs(v - stored) >= kPickupDistance && (v < stored) == below_[paged])
                    continue;
                waiting_[paged] = false;
                bank_.SetNorm(id, v);
            } else if(moved) {
                bank_.SetNorm(id, v);
            }
        }
        started_ = true;
        mode_ = frame.sw18 == 0 ? PlayMode::Drone : (frame.sw18 == 1 ? PlayMode::Play : PlayMode::PlayNoGlide);
        if((frame.keys & 1u) && !(held_ & 1u))
            bank_.SetWarp(!bank_.Warp());
        held_ = frame.keys;
    }

    float GateTarget() const { return mode_ == PlayMode::Drone || !notes_.Empty() ? 1.f : 0.f; }
    PlayMode Mode() const { return mode_; }
    bool Waiting(int paged_pot) const { return waiting_[paged_pot]; }
    ParamBank& Bank() { return bank_; }
    const ParamBank& Bank() const { return bank_; }

  private:
    static uint8_t DirectParam(int pot) {
        static const uint8_t ids[5] = {kFrequency, kFeedbackGain, kFeedbackBody, kFeedbackLpf, kFeedbackHpf};
        return ids[pot];
    }
    static uint8_t PagedParam(int paged_pot, bool page_b) {
        static const uint8_t ids[2][kPagedPots] = {{kReverbMix, kEchoSend, kOutputVolume},
                                                   {kReverbDecay, kEchoTime, kEchoFeedback}};
        return ids[page_b ? 1 : 0][paged_pot];
    }

    ParamBank bank_;
    NoteStack notes_;
    PlayMode mode_ = PlayMode::Drone;
    float last_[8] = {};
    bool waiting_[kPagedPots] = {}, below_[kPagedPots] = {};
    bool page_b_ = false, started_ = false;
    uint16_t held_ = 0;
};

} // namespace audrey
