// SPDX-License-Identifier: MIT
// The looper's Mod on Daisy column table and its combination rules (modulation.h):
// base value from the pot or CC, plus the matrix offset, clamped to the column range.
#include "modulation.h"
#include "mod_on_daisy/module.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace looper::columns;

static int failures = 0;
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static bool Same(float a, float b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
static bool Near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

static mod::Module module;
static const mod::InstrumentInfo kInfo = MakeModInfo();

// Loads one connection (or none) and lets the mod wheel settle at `wheel`.
static void Connect(mod::Source source, Column column, int depth, uint8_t wheel = 127) {
    mod::Patch patch;
    patch.format = 2;  // LFO cross modulation is enabled, as in the firmware
    if (depth) {
        patch.depth[source][column] = int8_t(depth);
        patch.SetEnabled(source, column, true);
    }
    CHECK(module.controller.LoadPatch(patch) == mod::Status::Ok);
    module.live.ModWheel(wheel);
    for (int i = 0; i < 2000; ++i)  // 2 s: the MIDI source smoothing has settled
        module.engine.Tick(48);
}

static void TestTable() {
    CHECK(std::strcmp(kInfo.key, "looper") == 0 && std::strcmp(kInfo.version, LOOPER_VERSION) == 0);
    CHECK(std::strlen(kInfo.version) <= 12);
    const char* names[] = {"Speed", "Start", "Size", "Slice", "Skip", "Inertia", "Fripp Decay", "Mix"};
    for (unsigned t = 0; t < kColumns; ++t) {
        const mod::TargetInfo& target = kInfo.targets[t];
        CHECK(target.name && std::strcmp(target.name, names[t]) == 0 && std::strlen(target.name) <= 12);
        CHECK(target.span_text && std::strlen(target.span_text) <= 8);
        CHECK(target.span > 0.f && target.span <= 1.f);
        CHECK(!target.per_voice && !target.latched);  // one loop, no voices
    }
    CHECK(Near(kInfo.targets[Speed].span * kSpeedRange, 12.f));  // "+-12 st"
    CHECK(kInfo.targets[Mix].span == .5f);                       // base 0.5: full dry .. full loop
    CHECK(mod::AllowedCells(kInfo) == ~uint64_t{0});             // every source on every column
    CHECK(kInfo.mode_count == 4 && std::strcmp(kInfo.modes[2].name, "Frippertronics") == 0);
    for (unsigned m = 0; m < kInfo.mode_count; ++m)
        CHECK(std::strlen(kInfo.modes[m].name) <= mod::kMaxModeText &&
              std::strlen(kInfo.modes[m].hint) <= mod::kMaxModeText);
    std::printf("PASS table: 8 global columns, all 64 cells allowed, four loop modes\n");
}

static void TestMapping() {
    CHECK(SpeedToUnit(looper::kMinSemitones) == 0.f && SpeedToUnit(looper::kMaxSemitones) == 1.f);
    CHECK(Near(SpeedToUnit(0.f), .4f, 1e-6f));  // unity, like the pot at 40 %
    const MixGains unity = MixFromUnit(kMixBase);
    CHECK(unity.dry == 1.f && unity.loop == 1.f);
    CHECK(MixFromUnit(0.f).dry == 1.f && MixFromUnit(0.f).loop == 0.f);
    CHECK(MixFromUnit(1.f).dry == 0.f && MixFromUnit(1.f).loop == 1.f);
    CHECK(Near(MixFromUnit(.75f).dry, .5f) && MixFromUnit(.75f).loop == 1.f);
    std::printf("PASS mapping: speed -24..+36 st over 0..1, mix unity at 0.5\n");
}

static void TestEmptyMatrixIsTransparent() {
    Connect(mod::ModWheel, Speed, 0);
    for (int s = -24; s <= 36; ++s) {
        for (float k : {0.f, .002f, .25f, .5f, .999f, 1.f}) {
            Bases b;
            b.semitones = float(s);
            b.start = k;
            b.size = 1.f - k;
            b.slice = k;
            b.skip = k * k;
            b.inertia = k;
            b.decay = 1.f - k;
            const Values v = Modulate(module.engine, b);
            CHECK(Same(v.semitones, b.semitones) && Same(v.start, b.start) && Same(v.size, b.size));
            CHECK(Same(v.slice, b.slice) && Same(v.skip, b.skip) && Same(v.inertia, b.inertia));
            CHECK(Same(v.decay, b.decay) && v.mix.dry == 1.f && v.mix.loop == 1.f);
        }
    }
    std::printf("PASS empty matrix: every value is its base bit for bit\n");
}

static void TestCombination() {
    Bases b;
    // Speed: depth 100 = +12 semitones on top of the whole-semitone base, clamped at +36.
    Connect(mod::ModWheel, Speed, 100);
    b.semitones = 0.f;
    CHECK(Near(Modulate(module.engine, b).semitones, 12.f, 1e-3f));
    b.semitones = 30.f;
    CHECK(Near(Modulate(module.engine, b).semitones, 36.f, 1e-3f));
    Connect(mod::ModWheel, Speed, -50);
    b.semitones = 7.f;
    CHECK(Near(Modulate(module.engine, b).semitones, 1.f, 1e-3f));
    b.semitones = -20.f;
    CHECK(Near(Modulate(module.engine, b).semitones, -24.f, 1e-3f));
    // Half a wheel: unrounded, the matrix bends between semitones.
    Connect(mod::ModWheel, Speed, 25, 64);
    b.semitones = 0.f;
    const float bent = Modulate(module.engine, b).semitones;
    CHECK(Near(bent, 3.f * 64.f / 127.f, 1e-3f) && bent != std::round(bent));
    // Normalized columns: base + depth/100 * span, clamped to 0..1.
    Connect(mod::ModWheel, Start, 50);
    b.start = .2f;
    CHECK(Near(Modulate(module.engine, b).start, .7f));
    b.start = .8f;
    CHECK(Modulate(module.engine, b).start == 1.f);
    Connect(mod::ModWheel, Size, -100);
    b.size = .6f;
    CHECK(Modulate(module.engine, b).size == 0.f);
    Connect(mod::ModWheel, Decay, 30);
    b.decay = .5f;
    const Values decayed = Modulate(module.engine, b);
    CHECK(Near(decayed.decay, .8f) && decayed.start == b.start && decayed.size == b.size);
    // Mix: depth -100 = dry only, +100 = loop only, +50 = dry at half.
    Connect(mod::ModWheel, Mix, -100);
    CHECK(Modulate(module.engine, b).mix.dry == 1.f && Modulate(module.engine, b).mix.loop == 0.f);
    Connect(mod::ModWheel, Mix, 100);
    CHECK(Modulate(module.engine, b).mix.dry == 0.f && Modulate(module.engine, b).mix.loop == 1.f);
    Connect(mod::ModWheel, Mix, 50);
    CHECK(Near(Modulate(module.engine, b).mix.dry, .5f) && Modulate(module.engine, b).mix.loop == 1.f);
    // A connection elsewhere leaves Speed bit-exact.
    b.semitones = 5.f;
    CHECK(Same(Modulate(module.engine, b).semitones, 5.f));
    std::printf("PASS combination: base + depth x span, clamped; speed bends unrounded\n");
}

static void TestReportedBases() {
    Connect(mod::ModWheel, Speed, 0);
    Bases b;
    b.semitones = 12.f;
    b.start = .25f;
    ReportBases(module.engine, b);
    module.engine.EnableMonitor(true);
    module.engine.Tick(48);  // the monitor copies them on the next tick
    module.engine.Monitor().Fetch();
    const mod::MonitorSnapshot& m = module.engine.Monitor().Front();
    CHECK(Near(m.base[Speed], .6f, 1e-6f) && m.base[Start] == .25f && m.base[Mix] == kMixBase);
    std::printf("PASS monitor: base values as column positions\n");
}

int main() {
    module.Init(kInfo, 48000.f, 48);
    module.EnableExtensions({true, true, true});
    TestTable();
    TestMapping();
    TestEmptyMatrixIsTransparent();
    TestCombination();
    TestReportedBases();
    if (failures) {
        std::printf("FAILED: %d column check(s)\n", failures);
        return 1;
    }
    std::printf("All column tests passed\n");
    return 0;
}
