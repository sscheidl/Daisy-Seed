#pragma once
// SPDX-License-Identifier: MIT
// Host side of the Stereo Delay (FX contract: the host owns feed, dry/wet and transitions;
// ARBEITSAUFTRAG 4.8, R14). Switching off first fades the feed into the delay out over
// 10 ms; the tail keeps running with the set feedback until the wet level stays under
// -90 dBFS for 500 ms plus the delay time (no echo can still be on its way), at the latest
// 20 s after switching off it fades over 1 s. Once off, dry/wet fades to 0 % in 10 ms and
// the output is exactly dry (unity bypass, Codex Q1). Switching on during the tail fades
// the feed back in and keeps the lines; switching on after the tail starts with silent
// lines (Codex F02) without clearing them in the callback.
#include "delay.h"

namespace seed::fx {

class DelayHost {
  public:
    enum class State : uint8_t { On, Tail, Off };
    static constexpr unsigned kFeedFadeFrames = 480;      // 10 ms
    static constexpr unsigned kQuietFrames = 24000;       // 500 ms
    static constexpr unsigned kTailLimitFrames = 960000;  // 20 s
    static constexpr unsigned kTailFadeFrames = 48000;    // 1 s
    static constexpr float kQuiet = 3.16227766e-5f;       // -90 dBFS

    explicit DelayHost(StereoDelay& delay) : delay_(delay) {}

    void Reset(bool enabled) {
        state_ = enabled ? State::On : State::Off;
        feed_ = enabled ? 1.f : 0.f;
        tail_gain_ = 1.f;
        quiet_ = since_off_ = 0;
    }
    void SetEnabled(bool on) {
        if(on && state_ != State::On) {
            if(state_ == State::Off)
                delay_.Invalidate(); // nothing of an ended (or faded) tail comes back
            state_ = State::On; // during the tail the lines stay, the feed fades back in
            tail_gain_ = 1.f;
        } else if(!on && state_ == State::On) {
            state_ = State::Tail;
            quiet_ = since_off_ = 0;
        }
    }
    void SetMix(float percent) { mix_target_ = percent < 0.f ? 0.f : (percent > 100.f ? 1.f : percent * 0.01f); }
    State GetState() const { return state_; }
    float Feed() const { return feed_; }

    // dry in -> out (out may alias neither input). Scratch buffers hold feed and wet.
    void Process(const float* in_l, const float* in_r, float* out_l, float* out_r, std::size_t n) {
        float feed_l[48], feed_r[48], wet_l[48], wet_r[48];
        if(n > 48)
            n = 48;
        const float feed_target = state_ == State::On ? 1.f : 0.f;
        for(std::size_t i = 0; i < n; ++i) {
            feed_ = Step(feed_, feed_target, 1.f / float(kFeedFadeFrames));
            feed_l[i] = in_l[i] * feed_;
            feed_r[i] = in_r[i] * feed_;
        }
        if(state_ == State::Off) {
            for(std::size_t i = 0; i < n; ++i)
                wet_l[i] = wet_r[i] = 0.f;
        } else if(!delay_.Process({feed_l, feed_r, wet_l, wet_r, n})) {
            for(std::size_t i = 0; i < n; ++i)
                wet_l[i] = wet_r[i] = 0.f;
        }
        float peak = 0.f;
        for(std::size_t i = 0; i < n; ++i) {
            if(state_ == State::Tail && since_off_ >= kTailLimitFrames)
                tail_gain_ = Step(tail_gain_, 0.f, 1.f / float(kTailFadeFrames));
            wet_l[i] *= tail_gain_;
            wet_r[i] *= tail_gain_;
            mix_ = Step(mix_, state_ == State::Off ? 0.f : mix_target_, 1.f / float(kFeedFadeFrames));
            out_l[i] = in_l[i] * (1.f - mix_) + wet_l[i] * mix_;
            out_r[i] = in_r[i] * (1.f - mix_) + wet_r[i] * mix_;
            const float a = wet_l[i] < 0.f ? -wet_l[i] : wet_l[i], b = wet_r[i] < 0.f ? -wet_r[i] : wet_r[i];
            peak = a > peak ? a : peak;
            peak = b > peak ? b : peak;
        }
        if(state_ == State::Tail) {
            since_off_ += unsigned(n);
            quiet_ = peak < kQuiet ? quiet_ + unsigned(n) : 0;
            if(quiet_ >= kQuietFrames + unsigned(delay_.ReachSamples()) || tail_gain_ == 0.f)
                state_ = State::Off; // the tail has ended: no more delay processing
        }
    }

  private:
    static float Step(float v, float target, float step) {
        if(v < target)
            return v + step > target ? target : v + step;
        return v - step < target ? target : v - step;
    }
    StereoDelay& delay_;
    State state_ = State::On;
    float feed_ = 1.f, mix_ = 0.25f, mix_target_ = 0.25f, tail_gain_ = 1.f;
    unsigned quiet_ = 0, since_off_ = 0;
};

} // namespace seed::fx
