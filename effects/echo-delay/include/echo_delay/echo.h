#pragma once
// SPDX-License-Identifier: MIT
// Echo Delay: the tape-like echo of the Audrey II (third_party/audrey-ii/EchoDelay.h), two
// independent lines of up to 5 s. Band-pass and soft clip in the feedback, so repeats get
// narrower and feedback above 1 saturates instead of growing. Wet-only under the FX contract
// 1.0. The host provides the memory: the lines are constructed in it by Prepare(), so it may
// be SDRAM that only works after hardware start-up.
#include "../../../../common/audio/fx_contract.h"
#include "EchoDelay.h"
#include <cstddef>
#include <new>

namespace seed::fx {
namespace echo {
inline constexpr unsigned kSampleRate = 48000;
inline constexpr float kMinSeconds = 0.01f, kMaxSeconds = 5.f;
inline constexpr std::size_t kLineFrames = 5 * kSampleRate;
inline constexpr float kMaxFeedback = 1.5f;
inline constexpr float kMaxLagSeconds = 10.f;

struct Parameters {
    float time_s = 5.f;   // 0.01..5, glides by lag_s
    float feedback = 0.5f; // 0..1.5, not smoothed
    float lag_s = 0.5f;   // 0..10, time constant of the glide
};
} // namespace echo

class StereoEcho {
  public:
    using Parameters = echo::Parameters;
    using Line = infrasonic::EchoDelay<echo::kLineFrames>;
    // Raw memory for both lines, about 1.9 MB. The host places it (SDRAM).
    struct Storage {
        alignas(Line) unsigned char line[2][sizeof(Line)];
    };

    // Startup only: clears the lines. The time starts at its value without a glide.
    bool Prepare(audio::Config config, Storage& storage) {
        if(config.sample_rate != echo::kSampleRate || !config.max_block)
            return false;
        config_ = config;
        requested_ = Parameters();
        for(int i = 0; i < 2; ++i) {
            line_[i] = new(storage.line[i]) Line;
            line_[i]->Init(float(config.sample_rate));
            line_[i]->SetDelayTime(requested_.time_s, true);
        }
        Apply();
        return true;
    }

    void SetParameters(const Parameters& p) {
        requested_.time_s = Limit(p.time_s, echo::kMinSeconds, echo::kMaxSeconds, requested_.time_s);
        requested_.feedback = Limit(p.feedback, 0.f, echo::kMaxFeedback, requested_.feedback);
        requested_.lag_s = Limit(p.lag_s, 0.f, echo::kMaxLagSeconds, requested_.lag_s);
        if(line_[0])
            Apply();
    }
    const Parameters& Requested() const { return requested_; }

    void Process(const audio::StereoBlock& block) {
        if(!line_[0] || !audio::Valid(config_, block))
            return;
        for(std::size_t i = 0; i < block.frames; ++i) {
            const float left = block.left[i], right = block.right[i];
            block.out_left[i] = line_[0]->Process(left);
            block.out_right[i] = line_[1]->Process(right);
        }
    }

  private:
    // NaN keeps the previous value.
    static float Limit(float value, float low, float high, float previous) {
        if(value != value)
            return previous;
        return value < low ? low : (value > high ? high : value);
    }
    void Apply() {
        for(Line* line : line_) {
            line->SetDelayTime(requested_.time_s);
            line->SetFeedback(requested_.feedback);
            line->SetLagTime(requested_.lag_s);
        }
    }

    audio::Config config_;
    Parameters requested_;
    Line* line_[2] = {nullptr, nullptr};
};

} // namespace seed::fx
