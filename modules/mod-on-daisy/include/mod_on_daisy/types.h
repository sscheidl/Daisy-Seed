#pragma once
// Mod on Daisy: fixed sizes, source and target descriptions (ARCHITECTURE.md § 3).
// Header-only, no libDaisy dependency: shared by firmware, host tests and the editor.
#include <cstddef>
#include <cstdint>

// Optional extensions, compiled in only with -DMOD_ON_DAISY_EXTENSIONS=1 and then
// switched on per instrument at run time (Module::EnableExtensions):
//   * extended monitor: column base values, source minimum/maximum per monitor
//     interval, clock activity and the last received aftertouch (PROTOCOL.md § 3.3)
//   * LFO cross modulation: each LFO's rate modulated by the other (patch format 2)
//   * instrument modes: names via GET_MODE, the active mode in the monitor (§ 3.6)
// Without the switch the code is absent, so images built without it stay unchanged.
#ifndef MOD_ON_DAISY_EXTENSIONS
#define MOD_ON_DAISY_EXTENSIONS 0
#endif

namespace mod {
constexpr uint8_t kVersionMajor = 0, kVersionMinor = 1, kVersionPatch = 0;

constexpr std::size_t kSources = 8, kTargets = 8, kCells = kSources * kTargets;
// v0.1 limit of active connections (D2/E6). The grid always stores all 64 cells,
// so the bench stress build raises this with -DMOD_ON_DAISY_MAX_ACTIVE=64.
#ifndef MOD_ON_DAISY_MAX_ACTIVE
#define MOD_ON_DAISY_MAX_ACTIVE 8
#endif
constexpr std::size_t kMaxActive = MOD_ON_DAISY_MAX_ACTIVE;
static_assert(kMaxActive >= 1 && kMaxActive <= kCells, "MOD_ON_DAISY_MAX_ACTIVE must be 1..64");

// Grid rows in this fixed order (D3).
enum Source : uint8_t { Lfo1, Lfo2, Random, Cv1, Cv2, ModWheel, ChannelAt, PolyAt };
constexpr int kMaxDepth = 100;

// LFO waveforms. SampleHold draws a new random value once per cycle.
enum Shape : uint8_t { Sine, Triangle, SawUp, SawDown, Square, SampleHold, kShapeCount };
// Free running, locked to MIDI clock (24 ppqn), or locked to a gate input.
enum Sync : uint8_t { Free, MidiClock, GateClock, kSyncCount };
enum RandomMode : uint8_t { Stepped, Smooth, kRandomModeCount };

// Clock divisions. MIDI: clock ticks per cycle. Gate: cycles per gate pulse
// as num/den. The patch stores the index.
struct Ratio { uint16_t num, den; };
constexpr Ratio kMidiDivisions[] = {
    {1, 384}, {1, 192}, {1, 96}, {1, 48}, {1, 24}, {1, 16}, // 4 bars … 1/4T
    {1, 12}, {1, 8}, {1, 6}, {1, 4}, {1, 3}};               // 1/8 … 1/32
constexpr const char* kMidiDivisionNames[] = {
    "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
constexpr Ratio kGateDivisions[] = {{1, 4}, {1, 2}, {1, 1}, {2, 1}, {4, 1}};
constexpr const char* kGateDivisionNames[] = {"1/4", "1/2", "1/1", "2/1", "4/1"};
constexpr uint8_t kMidiDivisionCount = sizeof(kMidiDivisions) / sizeof(kMidiDivisions[0]);
constexpr uint8_t kGateDivisionCount = sizeof(kGateDivisions) / sizeof(kGateDivisions[0]);
constexpr uint8_t kDefaultMidiDivision = 4; // 1/4
constexpr uint8_t kDefaultGateDivision = 2; // one cycle per pulse
constexpr uint16_t kRateMax = 16383;        // 0.02 … 20 Hz, logarithmic
constexpr std::size_t kMaxKeyLength = 24;   // "physical-modeling" has 17

// One grid column, described by the instrument (ARCHITECTURE.md § 3.2).
struct TargetInfo {
    const char* name = nullptr;      // column header, <= 12 characters; nullptr = no target
    const char* span_text = nullptr; // what depth 100 means, e.g. "±12 st", <= 8 characters
    float span = 1.0f;               // normalized change at depth 100, 0 < span <= 1
    bool per_voice = false;          // instrument applies Poly AT per voice (ApplyVoice)
    bool latched = false;            // read only when a note starts (Poly AT has no effect)
};

#if MOD_ON_DAISY_EXTENSIONS
// One selectable instrument mode (PROTOCOL.md § 3.6), e.g. a Physical Modeling model.
struct ModeInfo {
    const char* name = nullptr; // <= kMaxModeText characters
    const char* hint = nullptr; // how to select it on the device, <= kMaxModeText characters
};
constexpr std::size_t kMaxModeText = 24, kMaxModes = 32;
#endif

struct InstrumentInfo {
    const char* key = nullptr;       // stable instrument key, <= kMaxKeyLength characters
    const char* version = nullptr;   // instrument firmware version, <= 12 characters
    TargetInfo targets[kTargets]{};
    uint8_t unavailable_sources = 0; // bit s: source s is owned by the instrument
#if MOD_ON_DAISY_EXTENSIONS
    const ModeInfo* modes = nullptr; // optional, reported with Extensions::modes
    uint8_t mode_count = 0;          // <= kMaxModes
#endif
};

constexpr std::size_t CellIndex(std::size_t source, std::size_t target) {
    return source * kTargets + target;
}
constexpr uint64_t CellBit(std::size_t source, std::size_t target) {
    return uint64_t{1} << CellIndex(source, target);
}

// Cells the instrument supports; the editor greys out all others (D4).
// Poly AT on a global target uses the most recent held note (decision 1).
inline uint64_t AllowedCells(const InstrumentInfo& info) {
    uint64_t mask = 0;
    for(std::size_t t = 0; t < kTargets; ++t) {
        const TargetInfo& target = info.targets[t];
        if(!target.name || !(target.span > 0.0f && target.span <= 1.0f))
            continue;
        for(std::size_t s = 0; s < kSources; ++s) {
            if(info.unavailable_sources & (1u << s))
                continue;
            if(s == PolyAt && target.latched)
                continue; // pressure arrives after the note has latched the value
            mask |= CellBit(s, t);
        }
    }
    return mask;
}
}
