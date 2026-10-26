#pragma once
// SPDX-License-Identifier: MIT
// Hardware-test control surface shared by `tape-fx` and `toy-piano-tape`:
// VR1..VR8 = Drive, Tone, Age, Mix, Wow, Flutter, Hiss, Dropout (TapeVibe's drumlogue
// pages in their order), SW17 = Tape on / Bypass, SW18 = character of hiss and dropouts.
// Board-independent: the firmware passes pot values and switch states in.
#include "tape_fx.h"
#include <array>
#include <cmath>

namespace seed::fx {
class TapePanel {
  public:
    enum Pot { kDrive, kTone, kAge, kMix, kWow, kFlutter, kHiss, kDropout, kNumPots };
    // libDaisy Switch3::Read() values.
    static constexpr int kSwitchCenter = 0, kSwitchUp = 1, kSwitchDown = 2;
    static constexpr unsigned kSwitchStableTicks = 15; // 15 ms at the 1 kHz control rate

    // Starting values: TapeVibe's init values, Tape character, not bypassed.
    void Reset() {
        percent_ = {35, 50, 40, 40, 20, 15, 20, 10};
        bypass_ = false;
        character_ = candidate_ = tape::Character::Tape;
        stable_ = kSwitchStableTicks;
        first_ = true;
    }
    // Control rate. Pots become integer percent with hysteresis: the exact drumlogue
    // parameter resolution and no ADC jitter. Returns true when a parameter changed.
    bool Update(const std::array<float, kNumPots>& pots, bool sw17_pressed, int sw18_position) {
        bool changed = first_;
        first_ = false;
        for(unsigned i = 0; i < kNumPots; ++i) {
            const float x = std::isfinite(pots[i]) ? pots[i] * 100.0f : float(percent_[i]);
            if(changed || std::fabs(x - float(percent_[i])) > kHysteresis) {
                int p = int(std::lround(x));
                p = p < 0 ? 0 : (p > 100 ? 100 : p);
                changed |= p != percent_[i];
                percent_[i] = p;
            }
        }
        changed |= sw17_pressed != bypass_;
        bypass_ = sw17_pressed; // SW17 position B (Pressed) = bypass
        const tape::Character request = sw18_position == kSwitchUp ? tape::Character::Studio
                                        : sw18_position == kSwitchDown ? tape::Character::Cassette
                                                                       : tape::Character::Tape;
        if(request != candidate_) {
            candidate_ = request;
            stable_ = 1;
        } else if(stable_ < kSwitchStableTicks) {
            ++stable_;
        }
        if(stable_ == kSwitchStableTicks && character_ != candidate_) {
            character_ = candidate_;
            changed = true;
        }
        return changed;
    }
    // TapeVibe fields from the panel; the MixForge fields of `base` are kept.
    tape::Parameters Apply(tape::Parameters base) const {
        base.drive = tape::Parameters::FromPercent(percent_[kDrive]);
        base.tone = tape::Parameters::FromPercent(percent_[kTone]);
        base.age = tape::Parameters::FromPercent(percent_[kAge]);
        base.wow = tape::Parameters::FromPercent(percent_[kWow]);
        base.flutter = tape::Parameters::FromPercent(percent_[kFlutter]);
        base.hiss = tape::Parameters::FromPercent(percent_[kHiss]);
        base.dropout = tape::Parameters::FromPercent(percent_[kDropout]);
        base.hiss_type = base.drop_type = character_;
        return base;
    }
    float Mix() const { return tape::Parameters::FromPercent(percent_[kMix]); }
    bool Bypass() const { return bypass_; }
    tape::Character Character() const { return character_; }
    int Percent(unsigned pot) const { return percent_[pot]; }
  private:
    static constexpr float kHysteresis = 0.6f; // percent: half a step plus margin
    std::array<int, kNumPots> percent_{35, 50, 40, 40, 20, 15, 20, 10};
    bool bypass_ = false, first_ = true;
    tape::Character character_ = tape::Character::Tape, candidate_ = tape::Character::Tape;
    unsigned stable_ = kSwitchStableTicks;
};
} // namespace seed::fx
