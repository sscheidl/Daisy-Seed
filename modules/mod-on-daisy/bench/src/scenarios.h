#pragma once
// Bench instrument and measurement scenarios (TEST_PLAN_PHASE2.md § 3).
// No libDaisy dependency: the host tests check every scenario patch.
#include "mod_on_daisy/patch.h"

namespace bench {
using namespace mod;

constexpr std::size_t kMaxVoices = 32;
enum Target : uint8_t { Pitch, Level, Harmonic, Tone, Pan, Master, Width, Drive };

inline InstrumentInfo MakeInfo() {
    InstrumentInfo info;
    info.key = "mod-bench";
    info.version = "0.1.1"; // IDENTIFY, follows MOD_BENCH_VERSION
    info.targets[Pitch] = {"Pitch", "+-1 st", 1.0f / 24.0f, true, false}; // 0..1 = +-12 st
    info.targets[Level] = {"Level", "100%", 1.0f, true, false};
    info.targets[Harmonic] = {"Harmonic", "100%", 1.0f, true, false};
    info.targets[Tone] = {"Tone", "100%", 1.0f, true, false};
    info.targets[Pan] = {"Pan", "+-50%", 0.5f, true, false};
    info.targets[Master] = {"Master", "100%", 1.0f, false, false};
    info.targets[Width] = {"Width", "100%", 1.0f, false, false};
    info.targets[Drive] = {"Drive", "100%", 1.0f, false, false};
    return info;
}

// Base values the bench instrument modulates around.
constexpr float kBase[kTargets] = {0.5f, 0.6f, 0.3f, 0.2f, 0.5f, 0.7f, 0.5f, 0.2f};

struct Scenario {
    Patch patch;
    std::size_t voices = 4;
    bool bypass = false; // S0: matrix not called at all (instrument baseline)
};

inline Patch Cell(Patch p, Source s, Target t, int depth) {
    p.depth[s][t] = int8_t(depth);
    p.SetEnabled(s, t, true);
    return p;
}

inline Patch EightConnections() { // S4: each source once, Poly AT per voice
    Patch p;
    p = Cell(p, Lfo1, Pitch, 20);
    p = Cell(p, Lfo2, Harmonic, -30);
    p = Cell(p, Random, Tone, 40);
    p = Cell(p, Cv1, Pan, 50);
    p = Cell(p, Cv2, Width, 50);
    p = Cell(p, ModWheel, Master, 50);
    p = Cell(p, ChannelAt, Drive, 60);
    p = Cell(p, PolyAt, Level, 80);
    p.lfo[0].rate = 9000;  // ~0.9 Hz
    p.lfo[1].rate = 11000; // ~2.1 Hz
    p.lfo[1].shape = Triangle;
    p.random.mode = Smooth;
    return p;
}

constexpr uint8_t kScenarioCount = 9;

// false: scenario not available in this build (S6 needs MOD_ON_DAISY_MAX_ACTIVE=64).
inline bool MakeScenario(uint8_t number, Scenario& s) {
    s = Scenario{};
    switch(number) {
        case 0: s.bypass = true; return true;
        case 1: return true; // module called, empty patch
        case 2:
            s.patch = Cell(Patch{}, Lfo1, Master, 30);
            return true;
        case 3: {
            Patch p;
            p = Cell(p, Lfo1, Pitch, 20);
            p = Cell(p, Lfo2, Level, -30);
            p = Cell(p, Random, Tone, 40);
            p = Cell(p, ModWheel, Master, 50);
            s.patch = p;
            return true;
        }
        case 4: s.patch = EightConnections(); return true;
        case 5:
            s.patch = EightConnections();
            s.voices = kMaxVoices;
            return true;
        case 6: {
            if(kMaxActive < kCells)
                return false;
            Patch p;
            for(std::size_t src = 0; src < kSources; ++src)
                for(std::size_t t = 0; t < kTargets; ++t)
                    p = Cell(p, Source(src), Target(t), 10 + int((src * kTargets + t) % 30)); // never 0
            s.patch = p;
            s.voices = kMaxVoices;
            return true;
        }
        case 7: // S4 + editor load from mod_probe.py
        case 8: // S4 + MIDI flood from mod_probe.py
            s.patch = EightConnections();
            return true;
        default: return false;
    }
}
}
