// SPDX-License-Identifier: MIT
#include "resample.h"
#include <cmath>

namespace looper::resample {
namespace {

// Kaiser-windowed sinc, about 80 dB stopband. The -6 dB point sits at 20.3 kHz,
// so the transition band (about 3.5 kHz wide for 64 taps) ends near the 22.05 kHz
// Nyquist frequency of the input.
constexpr double kCutoff = 20300.0 / 44100.0;  // of the input rate
constexpr double kBeta = 7.86;
constexpr double kPi = 3.14159265358979323846;

double BesselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) {
        const double h = x / (2.0 * k);
        term *= h * h;
        sum += term;
        if (term < sum * 1e-12)
            break;
    }
    return sum;
}

// Impulse response at a distance of t input frames from the output position.
double Kernel(double t) {
    const double x = t / kHalf;
    if (x <= -1.0 || x >= 1.0)
        return 0.0;
    const double a = 2.0 * kCutoff * t;
    const double sinc = a == 0.0 ? 1.0 : std::sin(kPi * a) / (kPi * a);
    return 2.0 * kCutoff * sinc * BesselI0(kBeta * std::sqrt(1.0 - x * x)) / BesselI0(kBeta);
}

float Clamp(float x) { return x > 1.f ? 1.f : (x < -1.f ? -1.f : x); }

}  // namespace

// Phase p is the output position p / kUp input frames after input frame n0;
// tap k weighs input frame n0 - (kHalf - 1) + k. Every phase is normalized to a
// DC gain of exactly 1.
void BuildTable(float* table) {
    for (uint32_t p = 0; p < kUp; ++p) {
        double h[kTaps], sum = 0.0;
        for (uint32_t k = 0; k < kTaps; ++k) {
            h[k] = Kernel(double(p) / kUp + double(kHalf - 1) - double(k));
            sum += h[k];
        }
        for (uint32_t k = 0; k < kTaps; ++k)
            table[p * kTaps + k] = float(h[k] / sum);
    }
}

bool Upsampler::Start(const float* table, Frame* out, uint32_t inputFrames, const Frame* last,
                      uint32_t lastCount) {
    *this = Upsampler();
    if (table == nullptr || out == nullptr || inputFrames == 0 || last == nullptr || lastCount == 0 ||
        lastCount > kHalf || (inputFrames >= kHalf ? lastCount != kHalf : lastCount != inputFrames))
        return false;
    table_ = table;
    out_ = out;
    inputs_ = inputFrames;
    outputs_ = OutputFrames(inputFrames);
    // History before frame 0: the loop's end, repeated if the loop is shorter.
    for (uint32_t k = 0; k < kHalf; ++k) {
        const uint32_t j = (lastCount - kHalf % lastCount + k) % lastCount;
        ring_[k] = last[j];
    }
    next_ = 0;
    return true;
}

void Upsampler::Put(const Frame& f) {
    if (next_ < kHalf && next_ < inputs_)
        first_[next_] = f;
    ring_[(next_ + kHalf) % kRing] = f;
    ++next_;
}

// Output o needs input frames up to n0 + kHalf with n0 = floor(o * kDown / kUp).
void Upsampler::Produce() {
    while (produced_ < outputs_) {
        const uint64_t position = uint64_t(produced_) * kDown;
        const uint32_t n0 = uint32_t(position / kUp);
        if (uint64_t(n0) + kHalf >= next_)
            return;
        const float* h = table_ + (position % kUp) * kTaps;
        const uint32_t base = n0 + 1;  // ring slot of input n0 - (kHalf - 1)
        float left = 0.f, right = 0.f;
        for (uint32_t k = 0; k < kTaps; ++k) {
            const Frame& x = ring_[(base + k) % kRing];
            left += h[k] * x.left;
            right += h[k] * x.right;
        }
        out_[produced_++] = {Clamp(left), Clamp(right)};
    }
}

void Upsampler::Push(const Frame* in, uint32_t count) {
    if (table_ == nullptr)
        return;
    for (uint32_t i = 0; i < count && next_ < inputs_; ++i) {
        Put(in[i]);
        Produce();
    }
}

bool Upsampler::Finish() {
    if (table_ == nullptr || next_ != inputs_)
        return false;
    const uint32_t stored = inputs_ < kHalf ? inputs_ : kHalf;
    for (uint32_t j = 0; j < kHalf && produced_ < outputs_; ++j) {
        ring_[(next_ + kHalf) % kRing] = first_[j % stored];  // input index inputs_ + j
        ++next_;
        Produce();
    }
    return produced_ == outputs_;
}

}  // namespace looper::resample
