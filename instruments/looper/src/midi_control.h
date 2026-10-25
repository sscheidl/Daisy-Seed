// SPDX-License-Identifier: MIT
// MIDI control change mapping for the looper (USB-MIDI on J7, every channel).
// The main loop receives the messages and the audio callback applies them,
// without locks: key presses are counted, knob values carry a sequence number.
#pragma once
#include <atomic>
#include <cstdint>

namespace looper::midi {

// In the order of the keys S1...S12, so a press bit is the key's bit.
enum Button : uint8_t {
    Record, Play, Undo, Reverse, Erase, NextMode, Seam, Import,
    SoundOnSound, Replace, Frippertronics, Resample, kButtons
};
// In the order of the pots 0...7.
enum Knob : uint8_t { Varispeed, Inertia, Slope, Start, Size, Slice, Skip, Decay, kKnobs };

constexpr uint8_t kFirstKnobCc = 20;     // CC20...27 in Knob order
constexpr uint8_t kFirstButtonCc = 102;  // CC102...113 in Button order
constexpr uint8_t kPressThreshold = 64;  // a press is a step from below 64 to 64 or more
constexpr uint8_t kUnityCc = 64;         // CC20: value - 64 semitones, clamped to -24...+36
constexpr float kTakeoverMove = .02f;    // a pot takes its knob back after moving 2 %

// Single producer (main loop), single consumer (audio callback).
class Receiver {
  public:
    // Main loop. True if the controller belongs to the looper.
    bool ControlChange(uint8_t controller, uint8_t value);
    // Audio callback: one bit per Button pressed since the last call.
    uint32_t TakePresses();
    // Audio callback: true and the value if the knob's CC arrived since the last call.
    bool TakeKnob(unsigned knob, uint8_t& value);

  private:
    std::atomic<uint32_t> presses_[kButtons] = {};  // counters
    std::atomic<uint32_t> knobs_[kKnobs] = {};      // sequence << 7 | value
    uint32_t seenPresses_[kButtons] = {}, seenKnobs_[kKnobs] = {};  // audio callback
    uint32_t sequence_[kKnobs] = {};                                // main loop
    bool high_[kButtons] = {};                                      // main loop
};

// The control moved last wins: a new CC takes the knob over, and moving the pot
// by kTakeoverMove gives it back. Without MIDI the pot always wins.
struct Takeover {
    bool midi = false;
    float anchor = 0.f;
    bool UseMidi(float pot, bool fresh);
};

float SemitonesFromCc(uint8_t value);  // -24...+36, 64 = unity
float UnitFromCc(uint8_t value);       // 0...1

}  // namespace looper::midi
