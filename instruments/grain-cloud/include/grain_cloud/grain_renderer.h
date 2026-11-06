#pragma once
// SPDX-License-Identifier: MIT
// The per-sample path. Rule R3: no transcendental function and no division in this file
// (make test scans it). Reads stay inside the sample by rule, see docs/BEHAVIOUR.md: a
// looping grain wraps in the loop and its right neighbour may be the loop start; any other
// grain holds the first or last frame. Grains that stay in range for a whole block take the
// fast loop, which has no bounds checks.
#include "grain_pool.h"
#include "sample_view.h"
#include "window_table.h"
#include <cstddef>
#include <cstdint>

namespace grain_cloud {

constexpr float kQ24 = 1.f / 16777216.f;

inline float ReadWrapped(const SampleView& s, int64_t pos) {
    const uint32_t index = uint32_t(pos >> 32);
    const uint32_t next = index + 1 == s.loop_end ? s.loop_start : index + 1;
    const float a = float(s.data[index]);
    return a + float(uint32_t(pos) >> 8) * kQ24 * (float(s.data[next]) - a);
}

inline float ReadClamped(const SampleView& s, int64_t pos) {
    const int64_t index = pos >> 32;
    if(index < 0)
        return float(s.data[0]);
    if(index + 1 >= int64_t(s.frames))
        return float(s.data[s.frames - 1]);
    const float a = float(s.data[index]);
    return a + float(uint32_t(pos) >> 8) * kQ24 * (float(s.data[index + 1]) - a);
}

// Adds one grain to the voice bus. false: the grain reached the end of its window.
inline bool RenderGrain(Grain& g, const SampleView& s, const float* window, float* left, float* right,
                        std::size_t frames) {
    std::size_t i = g.delay;
    if(i >= frames) {
        g.delay = uint16_t(i - frames);
        return true;
    }
    g.delay = 0;
    const bool wraps = g.flags & kGrainWraps;
    const int64_t low = int64_t(wraps ? s.loop_start : 0u) << 32;
    const int64_t high = int64_t((wraps ? s.loop_end : s.frames) - 1u) << 32; // needs a right neighbour
    const int64_t last = g.pos + g.inc * int64_t(frames - i);
    int64_t pos = g.pos;
    const int64_t inc = g.inc;
    uint32_t phase = g.win_phase;
    const uint32_t step = g.win_step;
    const float gain_l = g.gain_l, gain_r = g.gain_r;
    if(pos >= low && pos < high && last >= low && last < high) {
        const int16_t* data = s.data;
        for(; i < frames; ++i) {
            const uint32_t index = uint32_t(pos >> 32);
            const float a = float(data[index]);
            const float value = (a + float(uint32_t(pos) >> 8) * kQ24 * (float(data[index + 1]) - a)) *
                                WindowAt(window, phase);
            left[i] += value * gain_l;
            right[i] += value * gain_r;
            pos += inc;
            const uint32_t next = phase + step;
            if(next < phase)
                return false;
            phase = next;
        }
    } else {
        const int64_t end = int64_t(s.loop_end) << 32, span = end - low;
        for(; i < frames; ++i) {
            const float value = (wraps ? ReadWrapped(s, pos) : ReadClamped(s, pos)) * WindowAt(window, phase);
            left[i] += value * gain_l;
            right[i] += value * gain_r;
            pos += inc;
            if(wraps) { // birth guarantees |inc| < span: one correction is enough
                if(pos >= end)
                    pos -= span;
                else if(pos < low)
                    pos += span;
            }
            const uint32_t next = phase + step;
            if(next < phase)
                return false;
            phase = next;
        }
    }
    g.pos = pos;
    g.win_phase = phase;
    return true;
}

} // namespace grain_cloud
