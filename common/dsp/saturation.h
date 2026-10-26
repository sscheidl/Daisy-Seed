#pragma once
// SPDX-License-Identifier: MIT
// Shared waveshaping curves (Soft, Hard, Tape, Fold) for Tape FX and Drive
// (docs/acid-seq/ARBEITSAUFTRAG.md 4.7, phase 3a).
//
// Provenance: ported from the drumlogue master effects MixForge (saturateSoft,
// saturateHard, saturateTape, saturateFold) in sscheidl/drumlogue_fx_units @ 0db9c37;
// the verbatim originals are kept in effects/tape-fx/tests/reference/ (UPSTREAM.md).
// Moved here from effects/tape-fx/src/tape_fx.h without any change to the arithmetic:
// effects/tape-fx/tests/test_tape_fx.cpp still compares every curve bit for bit with
// MixForge, and tape::Saturate* remain as wrappers.
#include <cmath>

#define SEED_SATURATION_VERSION "1.0.0"

namespace seed::dsp {

inline float SaturateClamp(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}
// MixForge SOFT (= the TapeVibe drive curve): x (27 + x^2) / (27 + 9 x^2).
inline float SaturateSoft(float x) {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
// MixForge HARD: clip to +-1.
inline float SaturateHard(float x) { return SaturateClamp(x, -1.0f, 1.0f); }
// MixForge TAPE: the soft curve with a small bias for even harmonics.
inline float SaturateTape(float x) {
    const float biased = x + 0.055f;
    return SaturateSoft(biased) - 0.052f;
}
// MixForge FOLD: triangle wavefolding outside +-1.
inline float SaturateFold(float x) {
    if(x > 1.0f || x < -1.0f)
        x = 2.0f * std::fabs(0.5f * x + 0.5f - std::floor(0.5f * x + 1.0f)) - 1.0f;
    return x;
}

} // namespace seed::dsp
