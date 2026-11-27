// Host test of the S1 load spike: the sweep is complete, reaches the load conditions it is
// meant to reach (32 grains, delay mode, 48 kHz with 2:1 conversion) and is reproducible.
// 0.2.0: 48-frame callbacks give the same output as 12-frame ones, zero offsets change nothing,
// and the product patch loads and modulates what it should.
// PC timings are printed for orientation only; they say nothing about the kit.
//
// usage: test_spike [point_blocks]   (default 4000 = 1 s per point, as on the kit)
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <vector>
// The matrix needs no private access: included before the macro.
#include "mod_on_daisy/module.h"
#include "mod_on_daisy/patch.h"
// Test only: read the processor's grain pools and delay-mode state. All standard headers the
// upstream code uses are included above, so the macro reaches only the Beads and stmlib headers.
#define private public
#define protected public
#include "spike.h"
#undef private
#undef protected
#include "product.h"

namespace {
int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if(!(cond)) {                                                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while(0)

const char* kNames[spike::kNumCases] = {"a granular", "b delay", "c dirty tape", "d synth"};

int ActiveGrains(spike::Engine& e) {
    beads::Processor& p = e.processor();
    switch(e.current()) {
    case spike::kGranular: return p.processor_digital_.granular_player_.CountActiveGrains();
    case spike::kDirtyTape: return p.processor_dirty_tape_.granular_player_.CountActiveGrains();
    case spike::kSynth: return e.synth().wavetable_granular_player_.CountActiveGrains();
    default: return 0;
    }
}

uint64_t Fnv(uint64_t h, const void* data, std::size_t size) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    for(std::size_t i = 0; i < size; ++i) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

struct Run {
    uint64_t hash = 1469598103934665603ull;
    double rms = 0;
    bool finite = true, always_delay = true;
    std::vector<int> max_grains;
    std::vector<double> seconds;
};

Run RunCase(spike::Engine& e, spike::Case c) {
    Run r;
    CHECK(e.Start(c));
    const std::size_t points = spike::PointCount(c);
    r.max_grains.assign(points, 0);
    r.seconds.assign(points, 0.0);
    double sum = 0;
    uint64_t frames = 0;
    int last = spike::kWarmup;
    while(!e.done()) {
        beads::FloatFrame out[spike::kBlock];
        const auto t0 = std::chrono::steady_clock::now();
        const int point = e.Process(out);
        const auto t1 = std::chrono::steady_clock::now();
        CHECK(point == last || point == last + 1); // points in order, none skipped
        last = point;
        for(const auto& f : out) {
            r.finite = r.finite && std::isfinite(f.l) && std::isfinite(f.r);
            sum += double(f.l) * f.l + double(f.r) * f.r;
        }
        frames += spike::kBlock;
        r.hash = Fnv(r.hash, out, sizeof(out));
        if(point < 0)
            continue;
        r.seconds[point] += std::chrono::duration<double>(t1 - t0).count();
        r.max_grains[point] = std::max(r.max_grains[point], ActiveGrains(e));
        if(c == spike::kDelay)
            r.always_delay = r.always_delay && e.processor().delay_mode_transition_ > 0.0f;
    }
    CHECK(last == int(points) - 1); // every point ran
    r.rms = std::sqrt(sum / double(2 * frames));
    return r;
}

// A whole case through RenderCallback with `size`-frame callbacks; hashed like RunCase
// (interleaved left/right floats), so the hashes are comparable.
struct Rendered {
    uint64_t hash = 1469598103934665603ull;
    bool finite = true, always_delay = true;
};
Rendered RenderCase(spike::Engine& e, spike::Case c, std::size_t size, mod::Module* modulation = nullptr,
                    const float* fixed_offset = nullptr) {
    Rendered r;
    CHECK(e.Start(c));
    float left[48], right[48];
    spike::OffsetFrame frame;
    std::size_t frames_since_tick = 0;
    while(!e.done()) {
        const float* offset = fixed_offset;
        if(modulation) { // one matrix tick per 48 frames, as the 1 kHz timer
            if(frames_since_tick == 0) {
                modulation->engine.Tick(spike::kTickFrames);
                for(std::size_t t = 0; t < spike::kNumTargets; ++t)
                    frame.value[t] = modulation->engine.Offset(t);
            }
            frames_since_tick = (frames_since_tick + size) % spike::kTickFrames;
            offset = frame.value;
        }
        const int point = spike::RenderCallback(e, left, right, size, offset, 1.0f);
        for(std::size_t i = 0; i < size; ++i) {
            r.finite = r.finite && std::isfinite(left[i]) && std::isfinite(right[i]);
            const float pair[2] = {left[i], right[i]};
            r.hash = Fnv(r.hash, pair, sizeof(pair));
        }
        if(c == spike::kDelay && point >= 0)
            r.always_delay = r.always_delay && e.processor().delay_mode_transition_ > 0.0f;
    }
    return r;
}

// The controller keeps a pointer to the instrument description: it must outlive the module.
const mod::InstrumentInfo kModInfo = spike::MakeModInfo();

void TestProductPatch() {
    static mod::Module m;
    m.Init(kModInfo, spike::kSampleRate, spike::kTickFrames);
    CHECK(m.controller.LoadPatch(spike::MakeProductPatch()) == mod::Status::Ok);
    m.engine.SetHardware(0.0f, 0.0f, false, false); // nothing patched
    float lo[spike::kNumTargets], hi[spike::kNumTargets];
    for(std::size_t t = 0; t < spike::kNumTargets; ++t)
        lo[t] = hi[t] = 0.0f;
    auto run = [&](int ticks) {
        for(int i = 0; i < ticks; ++i) {
            m.engine.Tick(spike::kTickFrames);
            for(std::size_t t = 0; t < spike::kNumTargets; ++t) {
                lo[t] = std::min(lo[t], m.engine.Offset(t));
                hi[t] = std::max(hi[t], m.engine.Offset(t));
            }
        }
    };
    run(5000); // 5 s: both LFOs pass several cycles
    CHECK(hi[spike::kModTime] > 0.15f && lo[spike::kModTime] < -0.15f && hi[spike::kModTime] <= 0.2001f);
    CHECK(hi[spike::kModShape] > 0.2f);
    CHECK(hi[spike::kModPitch] <= 0.0251f && lo[spike::kModPitch] >= -0.0251f && hi[spike::kModPitch] > 0.0f);
    CHECK(hi[spike::kModSize] == 0.0f && lo[spike::kModSize] == 0.0f); // case b must stay in delay mode
    CHECK(hi[spike::kModFeedback] == 0.0f);                             // mod wheel still at 0
    mod::MidiMessage wheel;
    wheel.status = 0xB0;
    wheel.data0 = 1;
    wheel.data1 = 127;
    m.controller.HandleMidi(wheel);
    run(200);
    CHECK(hi[spike::kModFeedback] > 0.25f && hi[spike::kModFeedback] <= 0.3001f);
}

void TestTables() {
    CHECK(spike::PointCount(spike::kGranular) == 72);
    CHECK(spike::PointCount(spike::kDelay) == 48);
    CHECK(spike::kMaxPoints >= spike::PointCount(spike::kDelay));
    for(uint8_t c = 0; c < spike::kNumCases; ++c) {
        const spike::Case cs = spike::Case(c);
        const std::size_t n = spike::PointCount(cs);
        for(std::size_t i = 0; i < n; ++i) {
            const spike::Point a = spike::GetPoint(cs, i);
            CHECK(a.density < 0.48f || a.density > 0.52f); // never the manual dead zone
            CHECK(cs == spike::kDelay ? a.size > 0.99f : a.size < 0.98f);
            for(std::size_t j = 0; j < i; ++j) { // all points distinct
                const spike::Point b = spike::GetPoint(cs, j);
                CHECK(a.density != b.density || a.size != b.size || a.pitch != b.pitch || a.time != b.time
                      || a.time_amount != b.time_amount);
            }
        }
    }
    CHECK(spike::TotalBlocks(spike::kGranular, 4000) == 8000 + 72 * 4000);
}

void TestResult() {
    spike::Result r;
    r.Add(spike::kWarmup, 500, 100); // warm-up: overrun counted, peak not
    CHECK(r.overruns == 1 && r.peak == 0 && r.blocks == 0 && r.warmup_peak == 500);
    r.Add(3, 60, 100);
    r.Add(5, 90, 100);
    r.Add(3, 70, 100);
    CHECK(r.blocks == 3 && r.peak == 90 && r.worst_point == 5 && r.average() == 73);
    CHECK(r.point_peak[3] == 70 && r.point_peak[5] == 90 && r.overruns == 1);
    r.Add(7, 101, 100);
    CHECK(r.overruns == 2 && r.worst_point == 7);
    CHECK(spike::Load(60000, 120000) == 5000 && spike::Load(120000, 120000) == 10000 && spike::Load(1, 0) == 0);
}
} // namespace

int main(int argc, char** argv) {
    const uint32_t point_blocks = argc > 1 ? uint32_t(std::strtoul(argv[1], nullptr, 10)) : spike::kPointBlocks;
    CHECK(point_blocks >= 100);
    TestTables();
    TestResult();
    TestProductPatch();

    std::vector<uint8_t> memory(spike::kNumBanks * spike::kBankSize);
    std::vector<uint16_t> reverb(spike::kReverbWords);
    uint8_t* banks[spike::kNumBanks] = {&memory[0], &memory[spike::kBankSize], &memory[2 * spike::kBankSize]};
    static spike::Engine engine; // large: static, as on the kit
    engine.Init(banks, reverb.data(), point_blocks);

    for(uint8_t c = 0; c < spike::kNumCases; ++c) {
        const spike::Case cs = spike::Case(c);
        const Run r = RunCase(engine, cs);
        CHECK(r.finite);
        CHECK(r.rms > 1e-3);
        if(cs == spike::kDelay) {
            CHECK(r.always_delay);
        } else if(point_blocks >= spike::kPointBlocks) { // 4 s grains need the full second per point
            // The pool of 32 fills up. At the highest rate with the longest grains the scheduler
            // steals continuously and fades the oldest grains out quickly (granular_player.h
            // SortGrainList), so about 30 stay audible: MEASURED 2026-11-15 on the host, 30.
            const int full = *std::max_element(r.max_grains.begin(), r.max_grains.end());
            CHECK(full == 32);
            for(std::size_t i = 0; i < r.max_grains.size(); ++i) {
                const spike::Point p = spike::GetPoint(cs, i);
                if(p.density == 0.0f && p.size != 0.5f && r.max_grains[i] < 28) {
                    std::fprintf(stderr, "  point %zu (density %.1f size %.2f pitch %+.0f amount %.0f): %d grains\n", i,
                                 p.density, p.size, p.pitch, p.time_amount, r.max_grains[i]);
                    CHECK(r.max_grains[i] >= 28);
                }
            }
        }
        if(cs == spike::kDirtyTape)
            CHECK(engine.processor().sample_rate() == 48000.0f && engine.processor().ratio() == 2);
        std::size_t worst = 0;
        for(std::size_t i = 1; i < r.seconds.size(); ++i)
            worst = r.seconds[i] > r.seconds[worst] ? i : worst;
        const spike::Point w = spike::GetPoint(cs, worst);
        std::fprintf(stderr,
                     "%-13s rms %.3f, hash %016" PRIx64 ", PC-heaviest point %zu (density %.1f size %.2f pitch %+.0f "
                     "time %.1f amount %.0f) %.1f us/block\n",
                     kNames[c], r.rms, r.hash, worst, w.density, w.size, w.pitch, w.time, w.time_amount,
                     1e6 * r.seconds[worst] / point_blocks);
        if(cs == spike::kGranular) { // fresh start, same seeds: same output
            const Run again = RunCase(engine, cs);
            CHECK(again.hash == r.hash);
            static const float kZero[spike::kNumTargets] = {};
            CHECK(RenderCase(engine, cs, 12, nullptr, kZero).hash == r.hash); // zero offsets change nothing
        }
        if(cs == spike::kGranular || cs == spike::kDirtyTape) { // 48-frame callbacks: same output
            CHECK(RenderCase(engine, cs, 48).hash == r.hash);
        }
        if(cs == spike::kGranular || cs == spike::kDelay) { // with the product patch modulating
            static mod::Module modulation;
            modulation.Init(kModInfo, spike::kSampleRate, spike::kTickFrames);
            CHECK(modulation.controller.LoadPatch(spike::MakeProductPatch()) == mod::Status::Ok);
            const Rendered m = RenderCase(engine, cs, 48, &modulation);
            CHECK(m.finite && m.hash != r.hash && m.always_delay);
        }
    }
    if(failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::fprintf(stderr, "PASS: PLANCK load spike host test (%u blocks per point)\n", unsigned(point_blocks));
    return 0;
}
