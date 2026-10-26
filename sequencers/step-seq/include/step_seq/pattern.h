#pragma once
// SPDX-License-Identifier: MIT
// Pattern data and playback rules (ARBEITSAUFTRAG 4.2, TRANSPORT.md, transport_vectors.json).
#include <array>
#include <cstdint>

namespace step_seq {

constexpr int kSteps = 16;
constexpr int kOctaves = 5;          // VR1: 0..4
constexpr int kMaxStepNote = kOctaves * 12 - 1; // 59
constexpr int kBaseNote = 24;        // step note 0 = C1 (MIDI 24)
constexpr int kMinGatePercent = 5, kMaxGatePercent = 100;

struct Step {
    uint8_t note = 24;      // octave * 12 + semitone, 0..59 (default C3 = octave 2)
    uint8_t gate_percent = 50;
    bool gate = true;
    bool accent = false;
    bool slide = false;

    bool Valid() const {
        return note <= kMaxStepNote && gate_percent >= kMinGatePercent && gate_percent <= kMaxGatePercent;
    }
    uint8_t Bits() const { return uint8_t((gate ? 1 : 0) | (accent ? 2 : 0) | (slide ? 4 : 0)); }
    bool operator==(const Step& o) const {
        return note == o.note && gate_percent == o.gate_percent && gate == o.gate && accent == o.accent &&
               slide == o.slide;
    }
};

using Pattern = std::array<Step, kSteps>;

enum class Mode : uint8_t { Forward, Reverse, Random };
enum class Scale : uint8_t { Chromatic, Major, Minor };

// Round a step note down to the nearest degree of the scale, root C (4.2).
inline int ApplyScale(int note, Scale scale) {
    static constexpr uint8_t kMajor[12] = {0, 0, 2, 2, 4, 5, 5, 7, 7, 9, 9, 11};
    static constexpr uint8_t kMinor[12] = {0, 0, 2, 3, 3, 5, 5, 7, 8, 8, 10, 10};
    if(scale == Scale::Chromatic || note < 0)
        return note;
    const int octave = note / 12, degree = note % 12;
    return octave * 12 + (scale == Scale::Major ? kMajor[degree] : kMinor[degree]);
}

// MIDI note of a step: scale, then VR8 + CV IN 1 transpose, clamped to 0..127 and to the
// calibrated CV range [low, high] (MIDI numbers).
inline uint8_t StepMidiNote(const Step& step, Scale scale, int transpose, int low = 0, int high = 127) {
    int note = kBaseNote + ApplyScale(step.note, scale) + transpose;
    if(low < 0)
        low = 0;
    if(high > 127)
        high = 127;
    if(note < low)
        note = low;
    if(note > high)
        note = high;
    return uint8_t(note);
}

// xorshift32, seeded once at power-up; runs on across start and stop (4.2).
class Xorshift32 {
  public:
    static constexpr uint32_t kDefaultSeed = 0x2545F491u;
    explicit Xorshift32(uint32_t seed = kDefaultSeed) { Seed(seed); }
    void Seed(uint32_t seed) { state_ = seed ? seed : kDefaultSeed; }
    uint32_t Next() {
        uint32_t x = state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return state_ = x;
    }
    // 1..length
    int Draw(int length) { return 1 + int(Next() % uint32_t(length)); }

  private:
    uint32_t state_ = kDefaultSeed;
};

// Play position (1-based step numbers as in the vectors; 0 = none yet).
class Playhead {
  public:
    // The step that follows `position` with the current mode and length.
    int Next(Mode mode, int length, Xorshift32& rng) {
        if(length < 1)
            length = 1;
        if(length > kSteps)
            length = kSteps;
        int next;
        if(restart_ || position_ == 0) {
            next = mode == Mode::Forward ? 1 : (mode == Mode::Reverse ? length : rng.Draw(length));
        } else if(mode == Mode::Forward) {
            next = position_ >= length ? 1 : position_ + 1;
        } else if(mode == Mode::Reverse) {
            next = position_ > length || position_ <= 1 ? length : position_ - 1;
        } else {
            next = rng.Draw(length);
        }
        restart_ = false;
        return position_ = next;
    }
    // The next step will be the start point (start, reset).
    void Restart() { restart_ = true; }
    void Clear() {
        position_ = 0;
        restart_ = false;
    }
    int Position() const { return position_; }

  private:
    int position_ = 0;
    bool restart_ = false;
};

} // namespace step_seq
