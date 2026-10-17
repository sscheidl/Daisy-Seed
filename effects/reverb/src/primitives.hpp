#pragma once
// SPDX-License-Identifier: MIT
// Original native DSP, 2026. Mathematical techniques informed by the FV-1 study.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace ambient {
constexpr float kPi = 3.14159265358979323846f;
inline float Bounded(float x, float lo, float hi, float fallback) {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}
inline float Input(float x) {
    return Bounded(x, -4.f, 4.f, 0.f);
}
inline float Quiet(float x) {
    return std::abs(x) < 1.e-25f ? 0.f : x;
}
struct Stereo {
    float left = 0, right = 0;
};
struct Ramp {
    float value = 0, target = 0, step = 0;
    unsigned remaining = 0;
    void Set(float x, unsigned samples) {
        if (samples != 0 && x == target && remaining)
            return;
        target = x;
        if (samples == 0 || x == value) {
            value = x;
            step = 0;
            remaining = 0;
        } else if (x != value) {
            remaining = samples;
            step = (x - value) / float(samples);
        }
    }
    float Next() {
        if (remaining) {
            value += step;
            if (--remaining == 0)
                value = target;
        }
        return value;
    }
};
template <std::size_t N> class Delay {
    static_assert((N & (N - 1)) == 0, "power-of-two delay capacity required");
    float *data_ = nullptr;
    std::size_t write_ = 0;

  public:
    void Bind(std::array<float, N> &data) {
        data_ = data.data();
        Reset();
    }
    void Reset() {
        if (data_)
            std::fill_n(data_, N, 0.f);
        write_ = 0;
    }
    void Push(float x) {
        data_[write_] = Quiet(x);
        write_ = (write_ + 1) & (N - 1);
    }
    // Before Push: delay=1 is previous sample; after Push it is the current sample.
    float Read(float delay) const {
        unsigned n = unsigned(delay);
        float f = delay - float(n);
        const float a = data_[(write_ - n) & (N - 1)], b = data_[(write_ - n - 1) & (N - 1)];
        return a + f * (b - a);
    }
    float Allpass(float x, unsigned length, float g) {
        float d = Read(float(length));
        float v = x + g * d;
        Push(v);
        return d - g * v;
    }
};
struct Lowpass {
    float state = 0;
    float Process(float x, float a) {
        state = Quiet(state + a * (x - state));
        return state;
    }
};
struct Highpass {
    float lastIn = 0, lastOut = 0;
    float Process(float x, float pole) {
        float y = (1 + pole) * .5f * (x - lastIn) + pole * lastOut;
        lastIn = x;
        lastOut = Quiet(y);
        return lastOut;
    }
};
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void Lowpass(float sampleRate, float frequency, float q) {
        const float w = 2 * kPi * frequency / sampleRate, c = std::cos(w),
                    a = std::sin(w) / (2 * q), inv = 1 / (1 + a);
        b0 = (1 - c) * .5f * inv;
        b1 = (1 - c) * inv;
        b2 = b0;
        a1 = -2 * c * inv;
        a2 = (1 - a) * inv;
        z1 = z2 = 0;
    }
    float Process(float x) {
        float y = b0 * x + z1;
        z1 = Quiet(b1 * x - a1 * y + z2);
        z2 = Quiet(b2 * x - a2 * y);
        return y;
    }
};
struct AntiAlias {
    Biquad a, b;
    void Prepare(float sampleRate, float cutoff) {
        a.Lowpass(sampleRate, cutoff, .5411961f);
        b.Lowpass(sampleRate, cutoff, 1.306563f);
    }
    float Process(float x) {
        return b.Process(a.Process(x));
    }
};
// Shared phase per stereo shifter; Hann windows form a partition of unity.
// Independent native redesign of the two-read-head principle, not rom_pitch bit emulation.
class Octave {
    Delay<8192> left_, right_;
    float position_ = 0, window_ = 4096;
    AntiAlias aa_[2];
    Lowpass post_[2];
    float postCoefficient_ = 1, ratio_ = 2;
    std::array<float, 1025> hann_{};
    float Weight(float phase) const {
        float t = phase / window_ * 1024;
        unsigned i = unsigned(t);
        float f = t - float(i);
        return hann_[i] + f * (hann_[i + 1] - hann_[i]);
    }

  public:
    void Prepare(float sampleRate, std::array<float, 8192> &left, std::array<float, 8192> &right,
                 float ratio) {
        left_.Bind(left);
        right_.Bind(right);
        position_ = 0;
        ratio_ = ratio;
        window_ = std::round(.0853333333f * sampleRate); // ~85ms at every supported host rate
        window_ = std::min(window_, 8190.f);
        for (unsigned i = 0; i <= 1024; ++i)
            hann_[i] = .5f - .5f * std::cos(2 * kPi * float(i) / 1024);
        float cutoff =
            ratio > 1 ? std::min(6000.f, .12f * sampleRate) : std::min(9000.f, .25f * sampleRate);
        for (auto &f : aa_)
            f.Prepare(sampleRate, cutoff);
        for (auto &f : post_)
            f = {};
        postCoefficient_ = 1 - std::exp(-2 * kPi * (ratio < 1 ? 3500.f : 11000.f) / sampleRate);
    }
    Stereo Process(Stereo x) {
        left_.Push(aa_[0].Process(x.left));
        right_.Push(aa_[1].Process(x.right));
        float other = position_ + window_ * .5f;
        if (other >= window_)
            other -= window_;
        float w = Weight(position_);
        Stereo y{w * left_.Read(position_ + 1) + (1 - w) * left_.Read(other + 1),
                 w * right_.Read(position_ + 1) + (1 - w) * right_.Read(other + 1)};
        y.left = post_[0].Process(y.left, postCoefficient_);
        y.right = post_[1].Process(y.right, postCoefficient_);
        position_ += 1 - ratio_;
        if (position_ < 0)
            position_ += window_;
        else if (position_ >= window_)
            position_ -= window_;
        return y;
    }
};
enum class Shape : unsigned { Sine, Warp, Sink, Step, Random };
struct Oscillator {
    float sine = 0, cosine = 1, wave = 0, random = .3f;
    uint32_t seed = 1;
    unsigned count = 0;
    void Seed(unsigned index) {
        float phase = 2 * kPi * (float(index) + .13f) / 8;
        sine = std::sin(phase);
        cosine = std::cos(phase);
        wave = sine;
        seed = 0x9e3779b9u + index * 31u;
        count = 0;
    }
    float Process(float step, Shape shape, float smoothing) {
        const float old = sine;
        cosine -= step * sine;
        sine += step * cosine;
        if (++count == 1024) {
            float norm = 1.5f - .5f * (sine * sine + cosine * cosine);
            sine *= norm;
            cosine *= norm;
            count = 0;
        }
        if (old < 0 && sine >= 0) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            random = float(seed >> 8) * (2.f / 16777215.f) - 1;
        }
        float x = sine;
        switch (shape) {
        case Shape::Warp:
            x = (sine + .4f * (sine * sine - .5f)) / 1.2f;
            break;
        case Shape::Sink:
            x = (sine - .4f * (sine * sine - .5f)) / 1.2f;
            break;
        case Shape::Step:
            x = sine >= 0 ? 1.f : -1.f;
            break;
        case Shape::Random:
            x = random;
            break;
        default:
            break;
        }
        wave += smoothing * (x - wave);
        return wave;
    }
};
// Normalized 8x8 Walsh-Hadamard transform: H^T H = I.
inline void Scatter(std::array<float, 8> &x) {
    for (unsigned stride = 1; stride < 8; stride *= 2)
        for (unsigned start = 0; start < 8; start += 2 * stride)
            for (unsigned j = 0; j < stride; ++j) {
                float a = x[start + j], b = x[start + j + stride];
                x[start + j] = a + b;
                x[start + j + stride] = a - b;
            }
    for (float &v : x)
        v *= .3535533905932738f;
}
} // namespace ambient
