// MIT. Product load of the spike (README.md, "Product load"): the Mod on Daisy matrix with
// a fixed patch of eight connections, as a port would carry it. No libDaisy: host-tested.
#pragma once
#include "mod_on_daisy/patch.h"
#include "spike.h"

namespace spike {

static_assert(kNumTargets == mod::kTargets, "one matrix column per spike target");
constexpr std::size_t kTickFrames = 48; // 1 kHz control rate, as every Mod on Daisy product

inline mod::InstrumentInfo MakeModInfo() {
    mod::InstrumentInfo info;
    info.key = "texture-spike";
    info.version = "0.4.0"; // follows TEXTURE_SPIKE_VERSION
    info.targets[kModTime] = {"Time", "100%", 1.0f, false, false};
    info.targets[kModSize] = {"Size", "100%", 1.0f, false, false};
    info.targets[kModShape] = {"Shape", "100%", 1.0f, false, false};
    info.targets[kModPitch] = {"Pitch", "+-12 st", 0.25f, false, false}; // 0.25 * kPitchSpan = 12 st
    info.targets[kModDensity] = {"Density", "100%", 1.0f, false, false};
    info.targets[kModFeedback] = {"Feedback", "100%", 1.0f, false, false};
    info.targets[kModDryWet] = {"Dry/Wet", "100%", 1.0f, false, false};
    info.targets[kModReverb] = {"Reverb", "100%", 1.0f, false, false};
    return info;
}

inline mod::Patch Connect(mod::Patch p, mod::Source s, Target t, int depth) {
    p.depth[s][t] = int8_t(depth);
    p.SetEnabled(s, t, true);
    return p;
}

// Eight connections, the module's default limit (kMaxActive). SIZE stays unmodulated: case b
// must stay above 0.99 to remain in delay mode. Depths are moderate so the sweep points stay
// recognizable; unpatched CVs read about zero.
inline mod::Patch MakeProductPatch() {
    mod::Patch p;
    p = Connect(p, mod::Lfo1, kModTime, 20);
    p = Connect(p, mod::Lfo1, kModReverb, -20);
    p = Connect(p, mod::Lfo2, kModShape, 30);
    p = Connect(p, mod::Random, kModPitch, 10); // about +-1.2 semitones
    p = Connect(p, mod::Cv1, kModDensity, 10);
    p = Connect(p, mod::Cv2, kModShape, 10);
    p = Connect(p, mod::ModWheel, kModFeedback, 30);
    p = Connect(p, mod::ChannelAt, kModDryWet, -20);
    p.lfo[0].rate = 9000;  // about 0.9 Hz
    p.lfo[1].rate = 11000; // about 2.1 Hz
    p.lfo[1].shape = mod::Triangle;
    p.random.mode = mod::Smooth;
    return p;
}

struct OffsetFrame {
    float value[kNumTargets] = {};
};

} // namespace spike
