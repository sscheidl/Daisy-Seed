#pragma once
// SPDX-License-Identifier: MIT
// Parameters of the Grain Cloud core. Every value is normalized 0..1. The first eight
// are the Mod on Daisy columns (modulation.h). kParamInfo is the written birth-or-running
// table (rule R9): docs/BEHAVIOUR.md repeats it and a test exists per row.
#include <cstddef>
#include <cstdint>

namespace grain_cloud {

constexpr std::size_t kMaxVoices = 8;               // notes that sound at once
constexpr std::size_t kVoiceSlots = 2 * kMaxVoices; // the rest hold stolen notes while they fade
constexpr std::size_t kMaxGrains = 256;
constexpr std::size_t kMaxBlock = 48;

struct Config {
    float sample_rate = 48000.f;
    std::size_t block_frames = 48;
    std::size_t polyphony = 4;       // 1..kMaxVoices
    std::size_t pool_size = kMaxGrains;
    uint32_t tick_blocks = 1;        // control tick every n blocks: 1 = 1 kHz, 4 = 250 Hz at 48 frames
    uint32_t seed = 0x51ED2701u;
};

enum class StartMode : uint8_t { Grow, FullCloud, FullCloudAtStart, Keep };

enum class Param : uint8_t {
    Position, Spray, Size, Density, Pitch, Window, Direction, Level, // Mod on Daisy columns
    PanSpread, Scan, Attack, Decay, Sustain, Release, Start, ActiveLimit,
    kCount
};
constexpr std::size_t kParamCount = std::size_t(Param::kCount);
constexpr std::size_t kModTargets = 8;

// When a value reaches the sound (R9).
enum class Timing : uint8_t {
    Birth,   // read when a grain is born; running grains keep what they got
    Running, // acts on sounding grains, ramped or slew-limited
    Note,    // shapes the voice envelope or its start, see docs/BEHAVIOUR.md
    Control  // engine limit, read by the scheduler
};

struct ParamInfo {
    const char* name;
    float initial;
    Timing timing;
    bool one_sided; // bipolar modulation moves upward from the base value (B5)
};

constexpr ParamInfo kParamInfo[kParamCount] = {
    {"Position", 0.25f, Timing::Birth, false},
    {"Spray", 0.05f, Timing::Birth, true},
    {"Size", 0.60f, Timing::Birth, true},
    {"Density", 0.50f, Timing::Birth, true},
    {"Pitch", 0.50f, Timing::Birth, false},
    {"Window", 0.00f, Timing::Running, false},
    {"Direction", 0.00f, Timing::Birth, false},
    {"Level", 0.80f, Timing::Running, false},
    {"Pan Spread", 0.50f, Timing::Birth, false},
    {"Scan", 0.50f, Timing::Birth, false},
    {"Attack", 0.20f, Timing::Note, false},
    {"Decay", 0.40f, Timing::Note, false},
    {"Sustain", 1.00f, Timing::Note, false},
    {"Release", 0.45f, Timing::Note, false},
    {"Start", 0.00f, Timing::Note, false},
    {"Active Limit", 1.00f, Timing::Control, false},
};

inline StartMode StartModeFrom(float value) {
    const int mode = int(value * 3.f + 0.5f);
    return StartMode(mode < 0 ? 0 : (mode > 3 ? 3 : mode));
}
inline float StartModeValue(StartMode mode) { return float(uint8_t(mode)) * (1.f / 3.f); }

} // namespace grain_cloud
