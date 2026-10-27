#pragma once
// SPDX-License-Identifier: MIT
// Deterministic exp2/log2 and dB conversions for shared DSP (Drive, Stereo Delay): only
// + * / and bit operations, no libm, so host golden tests give the same bits with every
// C library and compiler. Relative error about 2e-7 (exp2) and 1e-6 (log2).
#include <cstdint>
#include <cstring>

#define SEED_FAST_MATH_VERSION "1.0.0"

namespace seed::dsp {

inline float Exp2(float x) {
    if(x < -126.f)
        return 0.f;
    if(x > 127.f)
        x = 127.f;
    int i = int(x + 0.5f); // nearest integer: the fraction stays in [-0.5, 0.5]
    if(float(i) > x + 0.5f)
        --i;
    const float f = x - float(i);
    const float p = 1.f + f * (0.693147182f + f * (0.240226507f + f * (0.0555041087f +
                    f * (0.00961812911f + f * (0.00133335581f + f * 0.000154034f)))));
    uint32_t bits;
    std::memcpy(&bits, &p, sizeof(bits));
    bits += uint32_t(i) << 23;
    float r;
    std::memcpy(&r, &bits, sizeof(r));
    return r;
}

// log2(x) for x > 0 (x <= 0 gives -126): exponent bits plus an atanh series of the mantissa.
inline float Log2(float x) {
    if(!(x > 1.2e-38f))
        return -126.f;
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    int e = int((bits >> 23) & 0xFF) - 127;
    bits = (bits & 0x007FFFFFu) | 0x3F800000u;
    float m;
    std::memcpy(&m, &bits, sizeof(m));
    if(m > 1.41421356f) { // centre the mantissa on 1 for a short series
        m *= 0.5f;
        ++e;
    }
    const float t = (m - 1.f) / (m + 1.f), t2 = t * t;
    const float ln = 2.f * t * (1.f + t2 * (1.f / 3.f + t2 * (1.f / 5.f + t2 * (1.f / 7.f + t2 * (1.f / 9.f)))));
    return float(e) + ln * 1.44269504f;
}

inline float DbToGain(float db) { return Exp2(db * 0.166096405f); }   // log2(10) / 20
inline float GainToDb(float gain) { return Log2(gain) * 6.02059991f; } // 20 / log2(10)
// One-pole coefficient that reaches 1/e after `ms` at `rate`.
inline float SmoothingCoefficient(float ms, float rate) {
    const float samples = ms * 0.001f * rate;
    return 1.f - Exp2(-1.44269504f / (samples < 1.f ? 1.f : samples));
}

} // namespace seed::dsp
