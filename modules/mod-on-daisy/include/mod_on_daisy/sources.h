#pragma once
// Building blocks of the modulation sources (ARCHITECTURE.md § 4.2-4.4).
// Audio-side code: no allocation, no libm calls except in the explicitly
// marked main-loop helpers.
#include "types.h"
#include <cmath>

namespace mod {
inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// sin(2*pi*p) for p in [0, 1) without a table; max. error about 0.1 %.
inline float SinePhase(float p) {
    const float u = 2.0f * p - 1.0f; // sin(2 pi p) = -sin(pi u)
    float y = 4.0f * u * (1.0f - std::fabs(u));
    y = 0.225f * (y * std::fabs(y) - y) + y;
    return -y;
}

inline float ShapeValue(uint8_t shape, float p) {
    switch(shape) {
        case Triangle: {
            float v = p + 0.25f;
            if(v >= 1.0f)
                v -= 1.0f;
            return 1.0f - 4.0f * std::fabs(v - 0.5f);
        }
        case SawUp: return 2.0f * p - 1.0f;
        case SawDown: return 1.0f - 2.0f * p;
        case Square: return p < 0.5f ? 1.0f : -1.0f;
        default: return SinePhase(p); // Sine; SampleHold is handled by the caller
    }
}

// xorshift32: deterministic, one multiply-free step per value.
class Rng {
  public:
    explicit Rng(uint32_t seed = 0x2545F491u) : state_(seed ? seed : 1u) {}
    float Bipolar() { // -1..+1
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return float(int32_t(state_)) * (1.0f / 2147483648.0f);
    }
  private:
    uint32_t state_;
};

// One-pole smoothing whose time constant does not depend on the tick length.
class Smoother {
  public:
    float Process(float target, float coeff) {
        y_ += coeff * (target - y_);
        if(std::fabs(target - y_) < 1e-6f)
            y_ = target; // exact convergence, no denormals
        return y_;
    }
    float Value() const { return y_; }
    void Reset(float value = 0.0f) { y_ = value; }
  private:
    float y_ = 0.0f;
};

// Main-loop/initialization helpers (call libm).
inline float SmoothingCoeff(float seconds, std::size_t samples, float sample_rate) {
    return 1.0f - std::exp(-float(samples) / (seconds * sample_rate));
}
inline float RateToHz(uint16_t rate) {
    return 0.02f * std::pow(1000.0f, float(rate > kRateMax ? kRateMax : rate) / float(kRateMax));
}
// Phase increment per sample in 1/2^32 cycles.
inline uint32_t PhaseIncrement(float hz, float sample_rate) {
    const double inc = double(hz) / double(sample_rate) * 4294967296.0;
    return inc >= 4294967295.0 ? 0xFFFFFFFFu : uint32_t(inc + 0.5);
}

// CV input: calibration offset, dead band against noise of open jacks, 2 ms.
constexpr float kCvDeadBand = 0.02f;
inline float CvDeadBand(float x) {
    const float a = std::fabs(x);
    if(a < kCvDeadBand)
        return 0.0f;
    const float y = (a - kCvDeadBand) / (1.0f - kCvDeadBand);
    return x < 0.0f ? -(y > 1.0f ? 1.0f : y) : (y > 1.0f ? 1.0f : y);
}

// Follows an external clock (MIDI clock pulses or gate edges) that is only
// observed once per tick. It counts pulses and estimates the period so a
// synced LFO moves smoothly between pulses; it never runs past the next
// expected pulse, so a stopped clock holds the LFO.
class ClockFollower {
  public:
    explicit ClockFollower(float period_alpha = 0.125f) : alpha_(period_alpha) {}
    void Advance(std::size_t samples) {
        if(since_ < 1.0e9f)
            since_ += float(samples);
    }
    // `pulses` arrived since the last call (assumed at the end of this tick).
    void Pulses(uint32_t pulses) {
        if(!pulses)
            return;
        if(have_reference_) {
            const float measured = since_ / float(pulses);
            period_ = period_ > 0.0f ? period_ + alpha_ * (measured - period_) : measured;
        }
        have_reference_ = true;
        since_ = 0.0f;
        if(waiting_) { // the first pulse after Start is tick 0
            waiting_ = false;
            --pulses;
        }
        count_ += pulses;
    }
    // MIDI Start: the next pulse is the beginning of the bar.
    void Restart() {
        count_ = 0;
        waiting_ = true;
    }
    float Fraction() const {
        if(waiting_ || period_ <= 0.0f)
            return 0.0f;
        const float f = since_ / period_;
        return f < 0.9999f ? f : 0.9999f;
    }
    // Phase in [0, 1) of a cycle that lasts den/num pulses.
    float Phase(Ratio ratio) const {
        const float pulses = float(count_ % ratio.den) + Fraction();
        const float cycles = pulses * float(ratio.num) / float(ratio.den);
        return cycles - float(uint32_t(cycles)); // cycles >= 0
    }
    float PeriodSamples() const { return period_; }
    uint32_t Count() const { return count_; }
  private:
    float alpha_;
    float since_ = 0.0f, period_ = 0.0f;
    uint32_t count_ = 0;
    bool have_reference_ = false, waiting_ = false;
};
}
