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
// Granular playback of a live recording ring: grain scheduling relative to the write head.
//
// Derived from clouds/dsp/granular_sample_player.h at pichenettes/eurorack
// f4cfa7697095a28d292bff758beb51deb49b5a86 (see ../../UPSTREAM.md). Local changes:
// * Rate-aware: grain sizes come from tables.h at the internal rate, and the two per-call
//   smoothers (active-grain count, gain normalization) are converted from Clouds' 32 kHz /
//   32-frame calls to the configured rate and call size, so their times stay the same.
// * The Clouds grain count (32 stereo / 40 mono) stays the density reference. A separate
//   runtime cap bounds the total number of active grains: a start needs active < cap, over all
//   slots, so a lowered cap admits no new grain until the running ones have finished (PR #108
//   review R3). Quality and normalization use the total active count. With cap == reference the
//   scheduling is Clouds'.
// * Random is a per-instance stmlib LCG (same recurrence), seeded explicitly for repeatable tests.
// * Counters for telemetry: started grains per quality, rejected starts, peak active grains.
//   A refused trigger or random birth counts once; an overdue regular (deterministic) grain
//   counts once per overdue period, not once per sample while it waits.
// * Grains longer than half the ring are shortened (only reachable with small test rings).
// * The one-shot trigger is taken from the call's argument, so a host can apply it once.
#pragma once
#include "grain.h"
#include "tables.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cumulonimbus::clouds {

constexpr int32_t kMaxNumGrains = 64;
constexpr std::size_t kMaxBlockSize = 32; // Clouds' ceiling (frame.h), kept for every call

struct GrainParameters {
    float position = 0.f;      // 0..1, Clouds POSITION
    float size = .5f;          // 0..1, Clouds SIZE
    float pitch = 0.f;         // semitones
    float stereo_spread = 0.f; // 0..1
    float overlap = 0.f;       // Clouds' derived granular.overlap
    float window_shape = 0.f;  // Clouds' derived granular.window_shape
    bool use_deterministic_seed = false;
};

struct SchedulerCounters {
    uint32_t started_high = 0, started_medium = 0, rejected = 0;
    int32_t active = 0, peak_active = 0;
};

class Random {
  public:
    void Seed(uint32_t seed) { state_ = seed; }
    inline uint32_t GetWord() {
        state_ = state_ * 1664525L + 1013904223L;
        return state_;
    }
    inline float GetFloat() { return static_cast<float>(GetWord()) / 4294967296.0f; }

  private:
    uint32_t state_ = 0x21;
};

inline float SemitonesToRatio(float semitones) { return std::exp2(semitones * (1.0f / 12.0f)); }

// Quake's rsqrt, as stmlib's fast_rsqrt_carmack.
inline float FastRsqrtCarmack(float x) {
    uint32_t i;
    float x2 = x * 0.5f, y = x;
    std::memcpy(&i, &y, sizeof(i));
    i = 0x5f3759df - (i >> 1);
    std::memcpy(&y, &i, sizeof(y));
    return y * (1.5f - (x2 * y * y));
}

class LiveScheduler {
  public:
    // `reference_grains`: Clouds' pool size for the channel count. `rate`, `call_frames`: the
    // internal sample rate and the frames per Play call.
    void Init(const Tables* tables, int32_t num_channels, int32_t reference_grains, float rate,
              std::size_t call_frames, uint32_t seed) {
        tables_ = tables;
        reference_grains_ = std::min(reference_grains, kMaxNumGrains);
        cap_ = reference_grains_;
        num_midfi_grains_ = 3 * reference_grains_ / 4;
        gain_normalization_ = 1.0f;
        for(auto& g : grains_)
            g.Init();
        num_grains_ = 0.0f;
        num_channels_ = num_channels;
        grain_size_hint_ = 1024.0f * rate / kCloudsRate;
        grain_rate_phasor_ = 0.0f;
        random_.Seed(seed);
        // Clouds: SLOPE(0.9, 0.2) once per 32-frame call at 32 kHz; ONE_POLE 0.01 and the hint
        // ONE_POLE 0.1 per sample / per grain at 32 kHz.
        const double calls_ratio = (double(kCloudsRate) / 32.0) / (double(rate) / double(call_frames));
        slope_up_ = float(1.0 - std::pow(1.0 - 0.9, calls_ratio));
        slope_down_ = float(1.0 - std::pow(1.0 - 0.2, calls_ratio));
        gain_coefficient_ = float(1.0 - std::pow(1.0 - 0.01, double(kCloudsRate) / double(rate)));
        counters_ = {};
        overdue_ = false;
    }

    // Runtime pool bound, 0..kMaxNumGrains. Running grains above a lowered cap finish normally.
    void SetCap(int32_t cap) { cap_ = std::max<int32_t>(0, std::min(cap, kMaxNumGrains)); }
    int32_t Cap() const { return cap_; }

    // `out`: `size` interleaved stereo frames, overwritten. `size` <= kMaxBlockSize.
    void Play(const PcmRing* buffer, const GrainParameters& parameters, bool trigger, float* out,
              std::size_t size) {
        if(size > kMaxBlockSize)
            size = kMaxBlockSize;
        float overlap = parameters.overlap;
        overlap = overlap * overlap * overlap;
        const float target_num_grains = reference_grains_ * overlap;
        float p = target_num_grains / static_cast<float>(grain_size_hint_);
        const float space_between_grains = grain_size_hint_ / target_num_grains;
        if(parameters.use_deterministic_seed)
            p = -1.0f;
        else
            grain_rate_phasor_ = -1000.0f;

        int32_t active = 0;
        const int32_t num_free = FillAvailableGrainsList(active);
        int32_t num_available_grains = std::min(num_free, std::max<int32_t>(0, cap_ - active));

        bool seed_trigger = trigger;
        for(std::size_t t = 0; t < size; ++t) {
            grain_rate_phasor_ += 1.0f;
            const bool seed_probabilistic = random_.GetFloat() < p && target_num_grains > num_grains_;
            const bool seed_deterministic = grain_rate_phasor_ >= space_between_grains;
            const bool seed = seed_probabilistic || seed_deterministic || seed_trigger;
            if(seed && !num_available_grains) {
                const bool waiting = seed_deterministic && !seed_probabilistic && !seed_trigger;
                if((!waiting || !overdue_) && counters_.rejected != UINT32_MAX) ++counters_.rejected;
                overdue_ = overdue_ || seed_deterministic;
                seed_trigger = false;
            }
            if(num_available_grains && seed) {
                --num_available_grains;
                const int32_t index = available_grains_[num_available_grains];
                ++active;
                overdue_ = false;
                // Quality follows Clouds' occupancy rule (free slots of the reference pool).
                const GrainQuality quality =
                    reference_grains_ - active < num_midfi_grains_ ? GRAIN_QUALITY_MEDIUM : GRAIN_QUALITY_HIGH;
                ScheduleGrain(&grains_[index], parameters, int32_t(t), buffer->size(),
                              buffer->head() - int32_t(size) + int32_t(t), quality);
                if(quality == GRAIN_QUALITY_HIGH) {
                    if(counters_.started_high != UINT32_MAX) ++counters_.started_high;
                } else if(counters_.started_medium != UINT32_MAX) {
                    ++counters_.started_medium;
                }
                grain_rate_phasor_ = 0.0f;
                seed_trigger = false;
            }
        }

        std::fill(&out[0], &out[size * 2], 0.0f);
        float* e = envelope_buffer_;
        const float* window = tables_->window;
        for(int32_t i = 0; i < kMaxNumGrains; ++i) {
            Grain* g = &grains_[i];
            if(!g->active())
                continue;
            if(g->recommended_quality() == GRAIN_QUALITY_HIGH) {
                if(num_channels_ == 1)
                    g->OverlapAdd<1, GRAIN_QUALITY_HIGH>(buffer, window, out, e, size);
                else
                    g->OverlapAdd<2, GRAIN_QUALITY_HIGH>(buffer, window, out, e, size);
            } else if(g->recommended_quality() == GRAIN_QUALITY_MEDIUM) {
                if(num_channels_ == 1)
                    g->OverlapAdd<1, GRAIN_QUALITY_MEDIUM>(buffer, window, out, e, size);
                else
                    g->OverlapAdd<2, GRAIN_QUALITY_MEDIUM>(buffer, window, out, e, size);
            } else {
                if(num_channels_ == 1)
                    g->OverlapAdd<1, GRAIN_QUALITY_LOW>(buffer, window, out, e, size);
                else
                    g->OverlapAdd<2, GRAIN_QUALITY_LOW>(buffer, window, out, e, size);
            }
        }

        // Normalization uses the grains active after scheduling, before rendering (as Clouds).
        const int32_t active_grains = active;
        counters_.active = ActiveGrains();
        counters_.peak_active = std::max(counters_.peak_active, std::max(active_grains, counters_.active));
        {
            const float error = static_cast<float>(active_grains) - num_grains_;
            num_grains_ += (error > 0 ? slope_up_ : slope_down_) * error;
        }
        float gain_normalization = num_grains_ > 2.0f ? FastRsqrtCarmack(num_grains_ - 1.0f) : 1.0f;
        float window_gain = 1.0f + 2.0f * parameters.window_shape;
        window_gain = std::min(2.0f, std::max(1.0f, window_gain));
        gain_normalization *= 1.0f + (window_gain - 1.0f) * parameters.overlap;
        for(std::size_t t = 0; t < size; ++t) {
            gain_normalization_ += gain_coefficient_ * (gain_normalization - gain_normalization_);
            *out++ *= gain_normalization_;
            *out++ *= gain_normalization_;
        }
    }

    int32_t ActiveGrains() const {
        int32_t n = 0;
        for(const auto& g : grains_)
            n += g.active() ? 1 : 0;
        return n;
    }
    const SchedulerCounters& Counters() const { return counters_; }
    void ResetCounters() { counters_ = {}; }
    const Grain& grain(int32_t i) const { return grains_[i]; }
    float GainNormalization() const { return gain_normalization_; }

  private:
    // All free slots; `active` receives the number of grains still playing (in any slot).
    int32_t FillAvailableGrainsList(int32_t& active) {
        int32_t n = 0;
        active = 0;
        for(int32_t i = 0; i < kMaxNumGrains; ++i) {
            if(grains_[i].active())
                ++active;
            else
                available_grains_[n++] = i;
        }
        return n;
    }

    void ScheduleGrain(Grain* grain, const GrainParameters& parameters, int32_t pre_delay, int32_t buffer_size,
                       int32_t buffer_head, GrainQuality quality) {
        const float position = parameters.position;
        const float pitch = parameters.pitch;
        const float window_shape = parameters.window_shape;
        float grain_size = Interpolate(tables_->grain_size, parameters.size, 256.0f);
        const float pitch_ratio = SemitonesToRatio(pitch);
        const float inv_pitch_ratio = SemitonesToRatio(-pitch);
        const float pan = 0.5f + parameters.stereo_spread * (random_.GetFloat() - 0.5f);
        float gain_l, gain_r;
        if(num_channels_ == 1) {
            gain_l = Interpolate(tables_->sin, pan, 256.0f);
            gain_r = Interpolate(tables_->sin + 256, pan, 256.0f);
        } else if(pan < 0.5f) {
            gain_l = 1.0f;
            gain_r = 2.0f * pan;
        } else {
            gain_r = 1.0f;
            gain_l = 2.0f * (1.0f - pan);
        }
        if(pitch_ratio > 1.0f) {
            // The grain's play-head moves faster than the record-head: do not consume too much.
            grain_size = std::min(grain_size, buffer_size * 0.25f * inv_pitch_ratio);
        }
        grain_size = std::min(grain_size, buffer_size * 0.5f); // local: small rings only
        const float eaten_by_play_head = grain_size * pitch_ratio;
        const float eaten_by_recording_head = grain_size;
        float available = 0.0f;
        available += static_cast<float>(buffer_size);
        available -= eaten_by_play_head;
        available -= eaten_by_recording_head;
        int32_t size = static_cast<int32_t>(grain_size) & ~1;
        if(size < 2) size = 2;
        const int32_t start = buffer_head - static_cast<int32_t>(position * available + eaten_by_play_head);
        grain->Start(pre_delay, buffer_size, start, size, static_cast<int32_t>(pitch_ratio * 65536.0f),
                     window_shape, gain_l, gain_r, quality);
        grain_size_hint_ += 0.1f * (grain_size - grain_size_hint_);
    }

    const Tables* tables_ = nullptr;
    int32_t reference_grains_ = 32, cap_ = 32;
    bool overdue_ = false; // a regular grain is due but the pool is full
    int32_t num_midfi_grains_ = 24;
    int32_t num_channels_ = 2;
    float num_grains_ = 0.f;
    float gain_normalization_ = 1.f;
    float grain_size_hint_ = 1024.f;
    float grain_rate_phasor_ = 0.f;
    float slope_up_ = .9f, slope_down_ = .2f, gain_coefficient_ = .01f;
    Random random_;
    SchedulerCounters counters_;
    Grain grains_[kMaxNumGrains];
    int32_t available_grains_[kMaxNumGrains] = {};
    float envelope_buffer_[kMaxBlockSize] = {};
};

} // namespace cumulonimbus::clouds
