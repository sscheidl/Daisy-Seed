// SPDX-License-Identifier: MIT
// Streaming 44.1 kHz -> 48 kHz conversion for loop imports: a polyphase
// windowed-sinc interpolator (ratio 160/147). The input is treated as one
// period of a loop, so the converted loop wraps without a seam.
#pragma once
#include "looper_engine.h"
#include <cstdint>

namespace looper::resample {

constexpr uint32_t kUp = 160, kDown = 147;  // 48000 / 44100 = 160 / 147
constexpr uint32_t kTaps = 64;              // input frames per output frame
constexpr uint32_t kHalf = kTaps / 2;
constexpr uint32_t kTableSize = kUp * kTaps;  // floats, 40 KB

// Frames at 48 kHz for `input` frames at 44.1 kHz (rounded down).
constexpr uint32_t OutputFrames(uint32_t input) {
    return uint32_t(uint64_t(input) * kUp / kDown);
}

// Fills `table` (kTableSize floats) with the filter phases. Run once.
void BuildTable(float* table);

class Upsampler {
  public:
    // `last` holds the final `lastCount` (1...kHalf) input frames of the loop,
    // which precede its first frame when the loop wraps. Output goes to
    // out[0...OutputFrames(inputFrames)).
    bool Start(const float* table, Frame* out, uint32_t inputFrames, const Frame* last,
               uint32_t lastCount);
    // Feeds the next input frames in order; produces every output they allow.
    void Push(const Frame* in, uint32_t count);
    // After all input frames: wraps around to the first frames and produces
    // the rest. True if exactly OutputFrames(inputFrames) frames were written.
    bool Finish();
    uint32_t Produced() const { return produced_; }

  private:
    void Put(const Frame& f);
    void Produce();

    static constexpr uint32_t kRing = 128;  // power of two, > kTaps
    const float* table_ = nullptr;
    Frame* out_ = nullptr;
    uint32_t inputs_ = 0, outputs_ = 0, produced_ = 0;
    uint32_t next_ = 0;  // next input index (index -kHalf is the oldest history)
    Frame ring_[kRing] = {};
    Frame first_[kHalf] = {};  // the first frames, needed again at the end
};

}  // namespace looper::resample
