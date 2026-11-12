// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Single grain synthesis: the shared grain renderer (reader, window, pan, accumulation).
//
// Derived from clouds/dsp/grain.h at pichenettes/eurorack
// f4cfa7697095a28d292bff758beb51deb49b5a86 (see ../../UPSTREAM.md). Local changes:
// * OverlapAdd is templated on any PCM source with Clouds' Read<method>(integral, fractional)
//   interface, so the live ring and a future immutable sample share this renderer. Scheduling
//   policy lives in live_scheduler.h, not here.
// * The window table is passed in (tables.h) instead of the global lut_window.
// * first_sample wraps negative starts correctly (C++ % keeps the sign).
// * Accessors for tests and telemetry.
#pragma once
#include "pcm_ring.h"
#include "tables.h"
#include <cstddef>
#include <cstdint>

namespace cumulonimbus::clouds {

enum GrainQuality { GRAIN_QUALITY_LOW, GRAIN_QUALITY_MEDIUM, GRAIN_QUALITY_HIGH };

class Grain {
  public:
    void Init() {
        active_ = false;
        envelope_phase_ = 2.0f;
    }

    void Start(int32_t pre_delay, int32_t buffer_size, int32_t start, int32_t width, int32_t phase_increment,
               float window_shape, float gain_l, float gain_r, GrainQuality recommended_quality) {
        pre_delay_ = pre_delay;
        width_ = width;
        int32_t first = start % buffer_size;
        if(first < 0) first += buffer_size;
        first_sample_ = first;
        phase_increment_ = phase_increment;
        phase_ = 0;
        envelope_phase_ = 0.0f;
        envelope_phase_increment_ = 2.0f / static_cast<float>(width);
        if(window_shape >= 0.5f) {
            envelope_smoothness_ = (window_shape - 0.5f) * 2.0f;
            envelope_slope_ = 0.0f;
        } else {
            envelope_smoothness_ = 0.0f;
            envelope_slope_ = 0.5f / (window_shape + 0.01f);
        }
        active_ = true;
        gain_l_ = gain_l;
        gain_r_ = gain_r;
        recommended_quality_ = recommended_quality;
    }

    template <bool use_lut_for_envelope, GrainQuality quality>
    inline void RenderEnvelope(const float* window, float* destination, std::size_t size) {
        const float increment = envelope_phase_increment_;
        const float smoothness = envelope_smoothness_;
        const float slope = envelope_slope_;
        float phase = envelope_phase_;
        while(size--) {
            float gain = phase;
            gain = gain >= 1.0f ? 2.0f - gain : gain;
            if(use_lut_for_envelope) {
                if(quality == GRAIN_QUALITY_HIGH) {
                    const float w = Interpolate(window, gain, 4096.0f);
                    gain += smoothness * (w - gain);
                }
            } else {
                if(quality >= GRAIN_QUALITY_MEDIUM) {
                    gain *= slope;
                    if(gain >= 1.0f) gain = 1.0f;
                }
            }
            phase += increment;
            if(phase >= 2.0f) {
                *destination = -1.0f;
                break;
            }
            *destination++ = gain;
        }
        envelope_phase_ = phase;
    }

    // Adds `size` interleaved stereo frames to `destination`. `source` points at one source per
    // channel. `envelope` is scratch of at least `size` floats.
    template <int32_t num_channels, GrainQuality quality, class Source>
    inline void OverlapAdd(const Source* source, const float* window, float* destination, float* envelope,
                           std::size_t size) {
        if(!active_)
            return;
        // The pre-delay lets grains start at arbitrary samples within a block.
        while(pre_delay_ && size) {
            destination += 2;
            --size;
            --pre_delay_;
        }
        if(envelope_smoothness_ == 0.0f)
            RenderEnvelope<false, quality>(window, envelope, size);
        else
            RenderEnvelope<true, quality>(window, envelope, size);

        const int32_t phase_increment = phase_increment_;
        const int32_t first_sample = first_sample_;
        const float gain_l = gain_l_;
        const float gain_r = gain_r_;
        int32_t phase = phase_;
        while(size--) {
            const int32_t sample_index = first_sample + (phase >> 16);
            const float gain = *envelope++;
            if(gain == -1.0f) {
                active_ = false;
                break;
            }
            const float l = source[0].template Read<InterpolationMethod(quality)>(sample_index, phase & 65535) * gain;
            if(num_channels == 1) {
                *destination++ += l * gain_l;
                *destination++ += l * gain_r;
            } else {
                const float r =
                    source[1].template Read<InterpolationMethod(quality)>(sample_index, phase & 65535) * gain;
                *destination++ += l * gain_l + r * (1.0f - gain_r);
                *destination++ += r * gain_r + l * (1.0f - gain_l);
            }
            phase += phase_increment;
        }
        phase_ = phase;
    }

    inline bool active() const { return active_; }
    inline GrainQuality recommended_quality() const { return recommended_quality_; }
    int32_t first_sample() const { return first_sample_; }
    int32_t width() const { return width_; }
    int32_t phase_increment() const { return phase_increment_; }
    int32_t pre_delay() const { return pre_delay_; }
    float gain_l() const { return gain_l_; }
    float gain_r() const { return gain_r_; }

  private:
    int32_t first_sample_ = 0;
    int32_t width_ = 2;
    int32_t phase_ = 0;
    int32_t phase_increment_ = 65536;
    int32_t pre_delay_ = 0;
    float envelope_smoothness_ = 0.f;
    float envelope_slope_ = 0.f;
    float envelope_phase_ = 2.f;
    float envelope_phase_increment_ = 0.f;
    float gain_l_ = 0.f;
    float gain_r_ = 0.f;
    bool active_ = false;
    GrainQuality recommended_quality_ = GRAIN_QUALITY_HIGH;
};

} // namespace cumulonimbus::clouds
