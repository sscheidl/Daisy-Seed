#pragma once
// SPDX-License-Identifier: MIT
// The eight-pot Cumulonimbus control map, shared by the standalone firmware and the FX page of
// Toy Piano on Cumulonimbus: VR1 Position, VR2 Grain Length (Clouds SIZE), VR3 Density, VR4 Grain
// Pitch, VR5 Grain Mix, VR6 Stereo Spread, VR7 Hall Decay, VR8 Hall Mix. SW18: up Freeze, centre
// Run, down FX Bypass (daisy::Switch3 positions; the physical polarity is a board-run check).
#include "chain.h"
#include <array>
#include <cmath>

namespace seed::fx {

constexpr int kSwitch3Up = 1, kSwitch3Down = 2; // daisy::Switch3::POS_UP / POS_DOWN

struct FxControls {
    CumulonimbusParameters grains{};
    ChainSettings chain{};
};

// x: eight 0..1 positions. The pitch has a centre detent of +-1 % at 0 semitones.
inline FxControls MapFxPots(const std::array<float, 8>& x) {
    FxControls c;
    c.grains.position = x[0];
    c.grains.size = x[1];
    c.grains.density = x[2];
    const float pitch = (x[3] - .5f) * 48.f; // -24 .. +24 semitones
    c.grains.pitch = std::fabs(x[3] - .5f) < .01f ? 0.f : pitch;
    c.chain.grain_mix = x[4];
    c.grains.stereo_spread = x[5];
    c.chain.hall_decay = x[6];
    c.chain.hall_mix = x[7];
    c.grains.texture = .5f; // fixed window shape in Stage 1 (Clouds TEXTURE 0.5: mostly Hann)
    return c;
}

// Safe low-wet start (playable images): moderate random grains, little wet signal.
inline std::array<float, 8> FxDefaultPots() { return {.1f, .45f, .65f, .5f, .2f, .7f, .5f, .2f}; }

inline ChainMode ModeFromSwitch(int sw18) {
    return sw18 == kSwitch3Up ? ChainMode::Freeze : (sw18 == kSwitch3Down ? ChainMode::Bypass : ChainMode::Run);
}

} // namespace seed::fx
