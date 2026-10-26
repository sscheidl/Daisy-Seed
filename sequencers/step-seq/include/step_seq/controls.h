#pragma once
// SPDX-License-Identifier: MIT
// Kit controls of Step Seq (ARBEITSAUFTRAG 4.1): keys with long press and chord marking,
// pots with smoothing, stepped hysteresis and pick-up, SW18 debounce. Runs at the 1 kHz
// control rate in the audio callback; no hardware access.
#include "bootloader_entry.h"
#include "engine.h"
#include <cmath>
#include <cstdint>

namespace step_seq {

constexpr uint32_t kLongPressMs = 400;
constexpr float kPickupDistance = 0.02f;   // VR1-VR4 after a step change
constexpr float kStepHysteresis = 0.25f;   // of one step width
constexpr uint32_t kSwitchDebounceMs = 20; // SW18, as Nebulae

// Copied from Toy Piano (toy_piano.cpp, D6): 20 ms one-pole at 1 kHz, 0.1 % hysteresis.
class PotFilter {
  public:
    float Process(float input) {
        input = input < 0.f ? 0.f : (input > 1.f ? 1.f : input);
        if(!initialized_) {
            smoothed_ = stable_ = input;
            initialized_ = true;
        }
        smoothed_ += 0.0487705755f * (input - smoothed_); // 1 - exp(-1 ms / 20 ms)
        if(std::fabs(smoothed_ - stable_) > 0.001f)
            stable_ = smoothed_;
        return stable_ < 0.003f ? 0.f : (stable_ > 0.997f ? 1.f : stable_);
    }

  private:
    float smoothed_ = 0.f, stable_ = 0.f;
    bool initialized_ = false;
};

// A pot with n detents: leaves a level only 25 % of a step width beyond its edges.
class Stepper {
  public:
    explicit Stepper(int levels) : levels_(levels) {}
    int Update(float x) {
        const float width = 1.f / float(levels_);
        if(level_ < 0 || x < (float(level_) - kStepHysteresis) * width ||
           x > (float(level_ + 1) + kStepHysteresis) * width) {
            int level = int(x * float(levels_));
            level_ = level < 0 ? 0 : (level >= levels_ ? levels_ - 1 : level);
        }
        return level_;
    }
    int Level() const { return level_; }

  private:
    int levels_;
    int level_ = -1;
};

// Raw SW18 position, 0 up, 1 middle, 2 down; stable after 20 ms (Nebulae's mapping).
class Switch3Debounce {
  public:
    int Process(int raw, uint32_t ms) {
        if(!initialized_) {
            stable_ = candidate_ = raw;
            initialized_ = true;
        }
        if(raw != candidate_) {
            candidate_ = raw;
            since_ = ms;
        }
        if(candidate_ != stable_ && ms - since_ >= kSwitchDebounceMs)
            stable_ = candidate_;
        return stable_;
    }
    int Stable() const { return stable_; }

  private:
    int stable_ = 1, candidate_ = 1;
    uint32_t since_ = 0;
    bool initialized_ = false;
};

struct ControlFrame {
    float pots[8] = {};
    uint16_t keys = 0;  // debounced, bit 0 = S1
    int sw18 = 1;       // raw: 0 up, 1 middle, 2 down
    uint32_t ms = 0;
};

class Controls {
  public:
    // Applies one control tick to the engine. Returns true when the bootloader gesture
    // (S9 + S16 alone for 5 s) completed.
    bool Process(const ControlFrame& f, Engine& engine) {
        bool boot = false;
        Keys(f, engine);
        if(boot_keys_.Process(f.keys, f.ms))
            boot = true;
        Pots(f, engine);
        const int sw = sw18_.Process(f.sw18, f.ms);
        engine.settings.mode = sw == 0 ? Mode::Forward : (sw == 1 ? Mode::Reverse : Mode::Random);
        return boot;
    }
    int Selected() const { return selected_; }
    uint16_t Held() const { return held_; }
    bool BootHolding() const { return boot_keys_.Holding(); }
    int Sw18() const { return sw18_.Stable(); }
    // Panel or MIDI wrote a global value: the pot writes again only after it moves.
    void ReleaseGlobal(int pot) {
        if(pot >= 4 && pot < 8)
            global_hold_[pot - 4] = last_[pot];
    }

  private:
    void Select(int index) {
        selected_ = index;
        for(int k = 0; k < 4; ++k) {
            anchor_[k] = last_[k];
            armed_[k] = false;
        }
    }
    void Keys(const ControlFrame& f, Engine& engine) {
        const uint16_t rise = f.keys & ~held_, fall = held_ & ~f.keys;
        for(int i = 0; i < kSteps; ++i) {
            const uint16_t bit = uint16_t(1u << i);
            if(rise & bit) {
                // Any other key held during this key's hold marks both (R07), in any order.
                if(f.keys & ~bit & held_) {
                    chord_ |= bit;
                    chord_ |= uint16_t(held_ & f.keys);
                }
                if(f.keys & ~bit & rise)
                    chord_ |= rise;
                down_at_[i] = f.ms;
                Select(i);
            }
        }
        for(int i = 0; i < kSteps; ++i) {
            const uint16_t bit = uint16_t(1u << i);
            if(!(fall & bit))
                continue;
            if(!(chord_ & bit) && f.ms - down_at_[i] >= kLongPressMs) {
                Step& s = engine.pattern[std::size_t(i)];
                s.gate = !s.gate;
                ++edits_;
            }
            chord_ &= uint16_t(~bit);
        }
        held_ = f.keys;
    }
    void Pots(const ControlFrame& f, Engine& engine) {
        float v[8];
        for(int i = 0; i < 8; ++i)
            v[i] = filter_[i].Process(f.pots[i]);
        const bool first = !started_;
        if(first) {
            for(int i = 0; i < 8; ++i)
                last_[i] = v[i];
            for(int k = 0; k < 4; ++k)
                anchor_[k] = v[k];
            for(int k = 0; k < 4; ++k)
                global_hold_[k] = -1.f;
            started_ = true;
        }
        // VR1-VR4 edit the selected step after moving 2 % from the position at selection.
        Step& step = engine.pattern[std::size_t(selected_)];
        const uint8_t old_note = step.note;
        const int octave = octave_.Update(v[0]), semitone = semitone_.Update(v[1]);
        const int flags = flags_.Update(v[3]);
        for(int k = 0; k < 4; ++k)
            if(!armed_[k] && std::fabs(v[k] - anchor_[k]) > kPickupDistance)
                armed_[k] = true;
        if(armed_[0])
            step.note = uint8_t(octave * 12 + step.note % 12);
        if(armed_[1])
            step.note = uint8_t((step.note / 12) * 12 + semitone);
        if(armed_[2]) {
            const int percent = int(std::lround(kMinGatePercent + v[2] * (kMaxGatePercent - kMinGatePercent)));
            step.gate_percent = uint8_t(percent);
        }
        if(armed_[3]) {
            step.accent = flags & 1;
            step.slide = flags & 2;
        }
        if(step.note != old_note) {
            ++edits_;
            if(engine.GetState() != State::Running)
                engine.PreviewStep(selected_); // 4.1: VR1/VR2 sound the note when stopped
        }
        // VR5-VR8 always act, but only when they move (last change wins against the panel).
        const int length = length_.Update(v[6]), transpose = transpose_.Update(v[7]);
        Settings& s = engine.settings;
        if(Moved(4, v[4], first))
            s.tempo = 30.f + v[4] * 270.f;
        if(Moved(5, v[5], first))
            s.swing = 50.f + v[5] * 25.f;
        if(Moved(6, v[6], first) && length + 1 != last_length_)
            s.length = length + 1;
        last_length_ = length + 1;
        if(Moved(7, v[7], first) && transpose - 12 != last_transpose_)
            s.transpose = transpose - 12;
        last_transpose_ = transpose - 12;
        for(int i = 0; i < 8; ++i)
            last_[i] = v[i];
    }
    bool Moved(int pot, float value, bool first) {
        float& hold = global_hold_[pot - 4];
        if(first)
            return true;
        if(hold >= 0.f) {
            if(std::fabs(value - hold) <= kPickupDistance)
                return false;
            hold = -1.f;
            return true;
        }
        return value != last_[pot];
    }

    PotFilter filter_[8];
    Stepper octave_{kOctaves}, semitone_{12}, flags_{4}, length_{kSteps}, transpose_{25};
    Switch3Debounce sw18_;
    bootloader::KeyCombo boot_keys_;
    float last_[8] = {}, anchor_[4] = {}, global_hold_[4] = {-1.f, -1.f, -1.f, -1.f};
    bool armed_[4] = {};
    bool started_ = false;
    int last_length_ = -1, last_transpose_ = 100;
    int selected_ = 0;
    uint16_t held_ = 0, chord_ = 0;
    uint32_t down_at_[kSteps] = {};

  public:
    uint32_t edits_ = 0; // pattern edits on the kit (for the monitor)
};

} // namespace step_seq
