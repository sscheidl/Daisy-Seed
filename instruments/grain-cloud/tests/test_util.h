#pragma once
// SPDX-License-Identifier: MIT
// Shared helpers of the Grain Cloud host tests. All test signals are generated here.
#include "grain_cloud/engine.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)
#define CHECK_MSG(cond, ...)                                                        \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);              \
            std::printf(__VA_ARGS__);                                               \
            std::printf("\n");                                                      \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

inline bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

namespace testing {
using namespace grain_cloud;

// Constant level: the output then shows window and gain directly, without any signal.
inline std::vector<int16_t> DcSample(uint32_t frames, int16_t value = 16384) {
    return std::vector<int16_t>(frames, value);
}
inline std::vector<int16_t> NoiseSample(uint32_t frames, uint32_t seed = 12345u) {
    std::vector<int16_t> data(frames);
    uint32_t state = seed;
    for(int16_t& x : data) {
        state = state * 1103515245u + 12345u;
        x = int16_t(int32_t(state >> 16) - 32768) / 2;
    }
    return data;
}
inline std::vector<int16_t> RampSample(uint32_t frames) { // value = index, to read positions back
    std::vector<int16_t> data(frames);
    for(uint32_t i = 0; i < frames; ++i)
        data[i] = int16_t(i % 30000);
    return data;
}
inline SampleView ViewOf(const std::vector<int16_t>& data, uint32_t id = 1, bool loop = true) {
    SampleView view;
    view.data = data.data();
    view.frames = uint32_t(data.size());
    view.id = id;
    if(loop) {
        view.loop_start = 0;
        view.loop_end = view.frames;
    }
    return view;
}

// Engine plus a block loop that keeps the rendered output.
struct Rig {
    Engine engine;
    std::vector<float> left, right;
    float block_l[kMaxBlock], block_r[kMaxBlock];

    bool Start(const SampleView& view, Config config = Config{}) {
        if(!engine.Prepare(config))
            return false;
        engine.PublishSample(view);
        return true;
    }
    void Set(Param param, float value) { engine.Post(SetParam(param, value)); }
    void Block() {
        engine.Process(block_l, block_r, 48);
        left.insert(left.end(), block_l, block_l + 48);
        right.insert(right.end(), block_r, block_r + 48);
    }
    void Blocks(int count) {
        for(int i = 0; i < count; ++i)
            Block();
    }
    float MaxStep(std::size_t from = 1) const { // largest sample-to-sample change
        float worst = 0.f;
        for(std::size_t i = from < 1 ? 1 : from; i < left.size(); ++i) {
            worst = std::fmax(worst, std::fabs(left[i] - left[i - 1]));
            worst = std::fmax(worst, std::fabs(right[i] - right[i - 1]));
        }
        return worst;
    }
    float Rms(std::size_t from, std::size_t to) const {
        double sum = 0.0;
        for(std::size_t i = from; i < to; ++i)
            sum += double(left[i]) * left[i] + double(right[i]) * right[i];
        return float(std::sqrt(sum / double(2 * (to - from))));
    }
    std::size_t ActiveVoices() const {
        std::size_t n = 0;
        for(std::size_t i = 0; i < kVoiceSlots; ++i)
            n += engine.VoiceAt(i).active;
        return n;
    }
    template <class Fn> void EachGrain(Fn fn) const {
        for(std::size_t i = 0; i < kVoiceSlots; ++i) {
            const Voice& v = engine.VoiceAt(i);
            if(!v.active)
                continue;
            for(uint16_t g = v.head; g != kNoGrain; g = engine.Pool().At(g).next)
                fn(v, engine.Pool().At(g));
        }
    }
    // Pool and ownership invariants; true when all hold.
    bool Consistent() const {
        std::size_t listed = 0;
        bool ok = true;
        for(std::size_t i = 0; i < kVoiceSlots; ++i) {
            const Voice& v = engine.VoiceAt(i);
            if(!v.active) {
                ok = ok && v.count == 0 && v.head == kNoGrain;
                continue;
            }
            std::size_t own = 0;
            for(uint16_t g = v.head; g != kNoGrain; g = engine.Pool().At(g).next) {
                const Grain& grain = engine.Pool().At(g);
                ok = ok && grain.voice == i && grain.generation == v.generation;
                if(++own > kMaxGrains)
                    return false; // a cycle
            }
            ok = ok && own == v.count;
            listed += own;
        }
        return ok && listed == engine.Pool().Active() &&
               engine.Pool().Active() + engine.Pool().Free() == engine.Pool().Capacity();
    }
};
} // namespace testing
