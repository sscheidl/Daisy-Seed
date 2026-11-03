#pragma once
// SPDX-License-Identifier: MIT
// Parameters of the Streichwurst FX component in physical/normalized units.
#include <cmath>

namespace streichwurst_fx {

enum EnsembleMode : int { Mode0 = 0, Mode1 = 1, Mode2 = 2 }; // native values (SF-D03), no product labels

struct FxParams {
    float animate_amount = 0.f;     // 0..1
    float animate_rate_hz = 0.5f;   // 0.05..5 (own parameter)
    bool ensemble_on = true;
    int ensemble_mode = Mode1;
    float phaser_amount = 0.f;      // 0..1
    float phaser_rate_hz = 0.4f;    // 0.05..5 (own parameter)
    float reverb_wet = 0.2f;        // 0..1
    float reverb_decay = 0.5f;      // 0..1 (own parameter)
};

namespace detail {
inline float Clamp(float v, float lo, float hi, float fallback) {
    if(!std::isfinite(v))
        return fallback;
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace detail

inline FxParams Sanitize(const FxParams& in, const FxParams& previous) {
    using detail::Clamp;
    FxParams p;
    p.animate_amount = Clamp(in.animate_amount, 0.f, 1.f, previous.animate_amount);
    p.animate_rate_hz = Clamp(in.animate_rate_hz, 0.05f, 5.f, previous.animate_rate_hz);
    p.ensemble_on = in.ensemble_on;
    p.ensemble_mode = in.ensemble_mode < 0 ? 0 : (in.ensemble_mode > 2 ? 2 : in.ensemble_mode);
    p.phaser_amount = Clamp(in.phaser_amount, 0.f, 1.f, previous.phaser_amount);
    p.phaser_rate_hz = Clamp(in.phaser_rate_hz, 0.05f, 5.f, previous.phaser_rate_hz);
    p.reverb_wet = Clamp(in.reverb_wet, 0.f, 1.f, previous.reverb_wet);
    p.reverb_decay = Clamp(in.reverb_decay, 0.f, 1.f, previous.reverb_decay);
    return p;
}

// Piecewise parabola of SF-D04 in real form: v = signed phase in [-1, 1), h = 4v(1-|v|).
inline float Parabola(float phase) {
    const float v = phase < 0.5f ? 2.f * phase : 2.f * phase - 2.f;
    return 4.f * v * (1.f - std::fabs(v));
}
inline float Wrap(float phase) {
    phase -= std::floor(phase);
    return phase >= 1.f ? 0.f : phase;
}

} // namespace streichwurst_fx
