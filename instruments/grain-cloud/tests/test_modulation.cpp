// SPDX-License-Identifier: MIT
// R8 and B5 with the real Mod on Daisy core: one matrix, one writer, one-sided targets.
#include "test_util.h"
#include <cstring>
#include <memory>

using namespace grain_cloud;
using namespace testing;

struct ModRig {
    mod::LiveState live;
    mod::Engine matrix;
    mod::Patch patch;
    Rig rig;
    mod::InstrumentInfo info = ModInfo("0.1.0");

    void Start(const SampleView& view, Config config = Config{}) {
        matrix.Init(live, 48000.f, 48);
        CHECK(rig.Start(view, config));
        rig.engine.AttachModulation(&matrix);
    }
    // The main-loop path every matrix edit takes: validate, compile, publish.
    void Publish() {
        CHECK(mod::Validate(patch, mod::AllowedCells(info)) == mod::Status::Ok);
        const float offsets[2] = {0.f, 0.f};
        matrix.Programs().Back() = mod::Compile(patch, info, 48000.f, offsets);
        matrix.Programs().Publish();
    }
    void Lfo(float hz, uint8_t shape = mod::Sine) {
        // Rate value from the logarithmic 0.02..20 Hz law of Mod on Daisy.
        const float position = std::log(hz / 0.02f) / std::log(1000.f);
        patch.lfo[0].rate = uint16_t(position * float(mod::kRateMax));
        patch.lfo[0].shape = shape;
    }
};

// An empty matrix leaves every target exactly at its base value.
static void TestEmptyMatrix() {
    const auto data = DcSample(48000);
    auto m = std::make_unique<ModRig>();
    m->Start(ViewOf(data));
    m->rig.Set(Param::Density, 0.37f);
    m->rig.Set(Param::Position, 0.61f);
    m->rig.Blocks(3);
    for(std::size_t t = 0; t < kModTargets; ++t)
        CHECK(m->rig.engine.Live().value[t] == m->rig.engine.Base(Param(t)));
    // Without any matrix attached the engine behaves the same.
    auto alone = std::make_unique<Rig>();
    CHECK(alone->Start(ViewOf(data)));
    alone->Blocks(3);
    for(std::size_t t = 0; t < kModTargets; ++t)
        CHECK(alone->engine.Live().value[t] == kParamInfo[t].initial);
}

// R8: one writer per tick, and the base value stays what the knob set.
static void TestOneWriter() {
    const auto data = DcSample(48000);
    for(uint32_t tick_blocks : {1u, 4u}) {
        auto m = std::make_unique<ModRig>();
        Config config;
        config.tick_blocks = tick_blocks;
        m->Start(ViewOf(data), config);
        m->Lfo(5.f);
        mod::Patch& patch = m->patch;
        CellEdit(patch, mod::Lfo1, std::size_t(Param::Position), 50);
        m->Publish();
        m->rig.Set(Param::Position, 0.5f);
        m->rig.Blocks(4);
        float seen_low = 1.f, seen_high = 0.f;
        for(int b = 1; b <= 800; ++b) {
            const Modulated before = m->rig.engine.Live();
            const uint32_t ticks = m->rig.engine.Diagnostics().ticks;
            m->rig.Block();
            const bool ticked = m->rig.engine.Diagnostics().ticks != ticks;
            const bool moved = std::memcmp(&before, &m->rig.engine.Live(), sizeof(before)) != 0;
            CHECK(ticked || !moved); // modulated values change on ticks only
            seen_low = std::fmin(seen_low, m->rig.engine.Live()[Param::Position]);
            seen_high = std::fmax(seen_high, m->rig.engine.Live()[Param::Position]);
            CHECK(m->rig.engine.Base(Param::Position) == 0.5f);
        }
        CHECK(m->rig.engine.Diagnostics().modulation_writes == m->rig.engine.Diagnostics().ticks);
        CHECK_MSG(seen_low < 0.1f && seen_high > 0.9f, "%f .. %f", double(seen_low), double(seen_high));
    }
}

// R8: the simplified view edits one cell of the data, nothing else.
static void TestCellEdit() {
    const auto data = DcSample(48000);
    auto m = std::make_unique<ModRig>();
    m->Start(ViewOf(data));
    // Free routing already in the patch.
    CellEdit(m->patch, mod::Lfo2, std::size_t(Param::Spray), 40);
    CellEdit(m->patch, mod::Random, std::size_t(Param::Pitch), -25);
    CellEdit(m->patch, mod::Cv1, std::size_t(Param::Level), 10);
    m->Publish();
    m->rig.Set(Param::Size, 0.42f);
    m->rig.Blocks(2);
    const mod::Patch before = m->patch;
    float base[kParamCount];
    for(std::size_t i = 0; i < kParamCount; ++i)
        base[i] = m->rig.engine.Base(Param(i));

    // A CC moves "wheel to density" through the view, repeatedly.
    for(int cc = 0; cc <= 127; cc += 7) {
        CellEdit(m->patch, mod::ModWheel, std::size_t(Param::Density), int8_t(cc * 100 / 127));
        m->Publish();
        m->rig.Block();
        for(std::size_t s = 0; s < mod::kSources; ++s)
            for(std::size_t t = 0; t < mod::kTargets; ++t) {
                if(s == mod::ModWheel && t == std::size_t(Param::Density))
                    continue;
                CHECK(m->patch.depth[s][t] == before.depth[s][t]);
                CHECK(m->patch.Enabled(s, t) == before.Enabled(s, t));
            }
        for(std::size_t i = 0; i < kParamCount; ++i)
            CHECK(m->rig.engine.Base(Param(i)) == base[i]); // never a write to a target
    }
    // Switching the cell off leaves the same data as before the first touch.
    CellEdit(m->patch, mod::ModWheel, std::size_t(Param::Density), 0);
    CHECK(std::memcmp(&m->patch, &before, sizeof(before)) == 0);
}

struct Trace {
    float low = 1.f, high = 0.f;
    int at_base = 0, ticks = 0;
};
static Trace Run(ModRig& m, Param param, float base, int blocks) {
    Trace trace;
    m.rig.Set(param, base);
    m.rig.Blocks(2);
    for(int b = 0; b < blocks; ++b) {
        m.rig.Block();
        const float value = m.rig.engine.Live()[param];
        trace.low = std::fmin(trace.low, value);
        trace.high = std::fmax(trace.high, value);
        trace.at_base += value <= base + 1e-6f;
        ++trace.ticks;
    }
    return trace;
}

// B5: an LFO at base 0 sweeps a whole cycle above the base, for spray, size and density.
static void TestOneSided() {
    const auto data = DcSample(48000);
    for(Param param : {Param::Spray, Param::Size, Param::Density}) {
        for(int depth : {60, -60}) {
            auto m = std::make_unique<ModRig>();
            m->Start(ViewOf(data));
            m->Lfo(2.f);
            CellEdit(m->patch, mod::Lfo1, std::size_t(param), int8_t(depth));
            m->Publish();
            const Trace zero = Run(*m, param, 0.f, 1500);
            CHECK(zero.low <= 0.02f);
            CHECK_MSG(Near(zero.high, 0.6f, 0.02f), "high %f", double(zero.high));
            // A bipolar law would sit at the clamp for half of each cycle.
            CHECK_MSG(float(zero.at_base) / float(zero.ticks) < 0.05f, "%d of %d", zero.at_base, zero.ticks);
            // The knob sets the minimum.
            const Trace raised = Run(*m, param, 0.3f, 1500);
            CHECK(raised.low >= 0.3f - 1e-5f);
            CHECK(Near(raised.high, 0.9f, 0.02f));
            // Clamped only at the top of the range.
            const Trace top = Run(*m, param, 1.f, 500);
            CHECK(top.low == 1.f && top.high == 1.f);
        }
    }
    // Two connections add, and the wheel keeps its sign and its rest position.
    auto m = std::make_unique<ModRig>();
    m->Start(ViewOf(data));
    CellEdit(m->patch, mod::ModWheel, std::size_t(Param::Spray), 50);
    m->Publish();
    m->rig.Set(Param::Spray, 0.2f);
    m->rig.Blocks(4);
    CHECK(Near(m->rig.engine.Live()[Param::Spray], 0.2f, 1e-5f));
    m->live.ModWheel(127);
    m->rig.Blocks(400);
    CHECK(Near(m->rig.engine.Live()[Param::Spray], 0.7f, 0.002f));
}

// Other targets stay bipolar around the base.
static void TestBipolarTargets() {
    const auto data = DcSample(48000);
    auto m = std::make_unique<ModRig>();
    m->Start(ViewOf(data));
    m->Lfo(2.f);
    CellEdit(m->patch, mod::Lfo1, std::size_t(Param::Position), 20);
    m->Publish();
    const Trace trace = Run(*m, Param::Position, 0.5f, 1500);
    CHECK(Near(trace.low, 0.3f, 0.01f));
    CHECK(Near(trace.high, 0.7f, 0.01f));
}

// B4 through the matrix: density modulation is a ratio, the same at every base.
static void TestDensityDepth() {
    const auto data = DcSample(48000);
    float octaves[2];
    int i = 0;
    for(float base : {0.1f, 0.5f}) {
        auto m = std::make_unique<ModRig>();
        m->Start(ViewOf(data));
        m->Lfo(2.f);
        CellEdit(m->patch, mod::Lfo1, std::size_t(Param::Density), 30);
        m->Publish();
        const Trace trace = Run(*m, Param::Density, base, 1500);
        octaves[i++] = std::log2(DensityHz(trace.high) / DensityHz(trace.low));
    }
    CHECK(Near(octaves[0], octaves[1], 0.02f));
    CHECK(Near(octaves[0], 0.3f * kDensityOctaves, 0.05f));
}

// A modulated window reaches the shared table, at the morph's own pace (B6).
static void TestWindowTarget() {
    const auto data = DcSample(48000);
    auto m = std::make_unique<ModRig>();
    m->Start(ViewOf(data));
    m->Lfo(1.f, mod::Square);
    CellEdit(m->patch, mod::Lfo1, std::size_t(Param::Window), 50);
    m->Publish();
    m->rig.Set(Param::Window, 0.5f);
    float previous = -1.f, worst = 0.f;
    for(int b = 0; b < 3000; ++b) {
        m->rig.Block();
        const float morph = m->rig.engine.Window().Morph();
        if(b > 0)
            worst = std::fmax(worst, std::fabs(morph - previous));
        previous = morph;
    }
    CHECK(worst <= 0.0011f); // a square wave target, still one slew step per tick
}

int main() {
    TestEmptyMatrix();
    TestOneWriter();
    TestCellEdit();
    TestOneSided();
    TestBipolarTargets();
    TestDensityDepth();
    TestWindowTarget();
    if(failures) {
        std::printf("test_modulation: %d failures\n", failures);
        return 1;
    }
    std::printf("test_modulation: ok\n");
    return 0;
}
