// SPDX-License-Identifier: MIT
// Fixed band-limited 48 <-> 32 kHz conversion for the 32 kHz internal grain path. One Kaiser-
// windowed sinc prototype at the common 96 kHz rate: 72 taps, -6 dB at 16 kHz, pass band to about
// 13 kHz, stop band from about 19 kHz (aliases from 16..19 kHz fold above 13 kHz). Polyphase:
// 48 -> 32 kHz (up 2, down 3) uses 36 taps per output, 32 -> 48 kHz (up 3, down 2) 24 taps.
// Group delay 35.5 samples at 96 kHz = 0.37 ms per direction. Coefficients are computed in
// Init (startup only); processing is bounded and allocation-free.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cumulonimbus {

struct RatePrototype {
    static constexpr std::size_t kTaps = 72;
    static constexpr double kRate = 96000.0, kCutoff = 16000.0, kBeta = 7.0;
    float h[kTaps];
    void Init() {
        const double pi = 3.14159265358979323846;
        const double center = (kTaps - 1) / 2.0, fc = kCutoff / kRate;
        double sum = 0.0;
        for(std::size_t i = 0; i < kTaps; ++i) {
            const double x = double(i) - center;
            const double sinc = x == 0.0 ? 2.0 * fc : std::sin(2.0 * pi * fc * x) / (pi * x);
            const double r = x / center;
            const double w = I0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / I0(kBeta);
            h[i] = float(sinc * w);
            sum += sinc * w;
        }
        for(auto& c : h)
            c = float(c / sum); // unit DC gain of the prototype
    }
    static double I0(double x) {
        double sum = 1.0, term = 1.0;
        for(int k = 1; k < 40; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }
};

// Streaming rational resampler by L/M with the shared prototype. One instance per channel.
template <unsigned L, unsigned M> class Polyphase {
  public:
    static constexpr std::size_t kPhaseTaps = RatePrototype::kTaps / L;
    static_assert(RatePrototype::kTaps % L == 0, "prototype must split into L phases");

    void Init(const RatePrototype& prototype) {
        // phase p, tap i = h[p + i * L] * L (zero insertion lowers the gain by L)
        for(unsigned p = 0; p < L; ++p)
            for(std::size_t i = 0; i < kPhaseTaps; ++i)
                taps_[p][i] = prototype.h[p + i * L] * float(L);
        Reset();
    }
    void Reset() {
        for(auto& x : history_)
            x = 0.f;
        write_ = 0;
        next_ = 0;
    }
    // Consumes `frames` inputs, returns the number of outputs written. For the 48-frame host
    // block: 48 -> 32 (L=2, M=3) and 32 -> 48 (L=3, M=2) outputs every block.
    std::size_t Process(const float* in, std::size_t frames, float* out) {
        std::size_t written = 0;
        for(std::size_t n = 0; n < frames; ++n) {
            // Two copies keep the newest kPhaseTaps samples contiguous at history_[write_ ..].
            write_ = write_ ? write_ - 1 : kPhaseTaps - 1;
            history_[write_] = history_[write_ + kPhaseTaps] = in[n];
            while(next_ < L) {
                const float* x = &history_[write_];
                const float* c = taps_[next_];
                float acc = 0.f;
                for(std::size_t i = 0; i < kPhaseTaps; ++i)
                    acc += c[i] * x[i];
                out[written++] = acc;
                next_ += M;
            }
            next_ -= L;
        }
        return written;
    }

  private:
    float taps_[L][kPhaseTaps];
    float history_[2 * kPhaseTaps];
    std::size_t write_ = 0;
    unsigned next_ = 0;
};

using Down48To32 = Polyphase<2, 3>;
using Up32To48 = Polyphase<3, 2>;

} // namespace cumulonimbus
