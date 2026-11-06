#pragma once
// SPDX-License-Identifier: MIT
// A prepared mono PCM16 sample the engine may read. The owner keeps the data valid until
// Engine::ConsumeSampleRetired() confirms that no grain reads it any more.
#include <cstdint>

namespace grain_cloud {

struct SampleView {
    const int16_t* data = nullptr;
    uint32_t frames = 0;
    float sample_rate = 48000.f;
    uint32_t loop_start = 0, loop_end = 0; // at least two frames apart: grains wrap inside
    uint32_t id = 0;                       // generation handle, non-zero

    bool Valid() const { return data && frames > 0 && frames < 0x7FFFFFFFu && id != 0; }
    bool Loops() const { return loop_end <= frames && loop_end >= loop_start + 2; }
    uint32_t RangeStart() const { return Loops() ? loop_start : 0; }
    uint32_t RangeFrames() const { return Loops() ? loop_end - loop_start : frames; }
};

} // namespace grain_cloud
