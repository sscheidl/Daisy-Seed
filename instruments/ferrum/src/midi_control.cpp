// SPDX-License-Identifier: MIT
#include "midi_control.h"
#include "looper_engine.h"
#include <cmath>

namespace looper::midi {

bool Receiver::ControlChange(uint8_t controller, uint8_t value) {
    value &= 0x7F;
    if (controller >= kFirstKnobCc && controller < kFirstKnobCc + kKnobs) {
        const unsigned k = controller - kFirstKnobCc;
        sequence_[k] = (sequence_[k] + 1) & 0x1FFFFFFu;
        knobs_[k].store((sequence_[k] << 7) | value, std::memory_order_release);
        return true;
    }
    if (controller >= kFirstButtonCc && controller < kFirstButtonCc + kButtons) {
        const unsigned b = controller - kFirstButtonCc;
        const bool high = value >= kPressThreshold;
        if (high && !high_[b])
            presses_[b].fetch_add(1, std::memory_order_release);
        high_[b] = high;
        return true;
    }
    return false;
}

uint32_t Receiver::TakePresses() {
    uint32_t mask = 0;
    for (unsigned b = 0; b < kButtons; ++b) {
        const uint32_t count = presses_[b].load(std::memory_order_acquire);
        if (count != seenPresses_[b]) {
            seenPresses_[b] = count;
            mask |= 1u << b;
        }
    }
    return mask;
}

bool Receiver::TakeKnob(unsigned knob, uint8_t& value) {
    if (knob >= kKnobs)
        return false;
    const uint32_t packed = knobs_[knob].load(std::memory_order_acquire);
    if (packed == seenKnobs_[knob])
        return false;
    seenKnobs_[knob] = packed;
    value = uint8_t(packed & 0x7F);
    return true;
}

bool Takeover::UseMidi(float pot, bool fresh) {
    if (fresh) {
        midi = true;
        anchor = pot;
    } else if (midi && std::fabs(pot - anchor) > kTakeoverMove) {
        midi = false;
    }
    return midi;
}

float SemitonesFromCc(uint8_t value) {
    const float s = float(int(value & 0x7F) - int(kUnityCc));
    return s < kMinSemitones ? kMinSemitones : (s > kMaxSemitones ? kMaxSemitones : s);
}

float UnitFromCc(uint8_t value) { return float(value & 0x7F) / 127.f; }

}  // namespace looper::midi
