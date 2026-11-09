// SPDX-License-Identifier: MIT
// Board-independent firmware logic: the shared control map, scenario routes, the channel-16 CC
// surface, the deterministic test source and the paced telemetry sender.
#include "../src/diagnostics.h"
#include "../src/fx_panel.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace seed::fx;
namespace diag = cumulonimbus::diag;
static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if(!(cond)) {                                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            ++failures;                                                                   \
        }                                                                                 \
    } while(0)

static void TestControlMap() {
    std::array<float, 8> x = {.1f, .2f, .3f, .5f, .6f, .7f, .8f, .9f};
    FxControls c = MapFxPots(x);
    CHECK(c.grains.position == .1f && c.grains.size == .2f && c.grains.density == .3f);
    CHECK(c.grains.pitch == 0.f); // centre detent
    CHECK(c.chain.grain_mix == .6f && c.grains.stereo_spread == .7f);
    CHECK(c.chain.hall_decay == .8f && c.chain.hall_mix == .9f);
    x[3] = 1.f;
    CHECK(MapFxPots(x).grains.pitch == 24.f);
    x[3] = 0.f;
    CHECK(MapFxPots(x).grains.pitch == -24.f);
    x[3] = .52f;
    CHECK(std::fabs(MapFxPots(x).grains.pitch - .96f) < 1e-4f);
    CHECK(ModeFromSwitch(kSwitch3Up) == ChainMode::Freeze);
    CHECK(ModeFromSwitch(0) == ChainMode::Run);
    CHECK(ModeFromSwitch(kSwitch3Down) == ChainMode::Bypass);
    const FxControls d = MapFxPots(FxDefaultPots());
    CHECK(d.chain.grain_mix <= .25f && d.chain.hall_mix <= .25f); // safe low-wet start
}

static void TestRoutes() {
    CHECK(diag::RouteFor(diag::kScenarioFull).grains && diag::RouteFor(diag::kScenarioFull).reverb);
    const auto dry = diag::RouteFor(diag::kScenarioDry);
    CHECK(!dry.grains && !dry.reverb);
    CHECK(!diag::RouteFor(diag::kScenarioGrainsOnly).reverb && diag::RouteFor(diag::kScenarioGrainsOnly).grains);
    CHECK(!diag::RouteFor(diag::kScenarioReverbOnly).grains && diag::RouteFor(diag::kScenarioReverbOnly).reverb);
    CHECK(!diag::RouteFor(diag::kScenarioWriteOnly).grain_render && diag::RouteFor(diag::kScenarioWriteOnly).grain_write);
    CHECK(!diag::RouteFor(diag::kScenarioRenderOnly).grain_write && diag::RouteFor(diag::kScenarioRenderOnly).grain_render);
    CHECK(diag::RouteFor(diag::kScenarioMuted).mute_wet);
    CHECK(diag::RouteFor(99).grains && diag::RouteFor(99).reverb); // unknown -> full
}

static void TestControlChanges() {
    diag::Requests q;
    CHECK(!diag::HandleControlChange(q, 0, 20, 3)); // other channels are not ours
    CHECK(diag::HandleControlChange(q, 15, 20, 3) && q.scenario.load() == 3);
    CHECK(diag::HandleControlChange(q, 15, 20, 99) && q.scenario.load() == 3); // out of range ignored
    CHECK(diag::HandleControlChange(q, 15, 21, 24) && q.cap.load() == 24);
    CHECK(diag::HandleControlChange(q, 15, 21, 65) && q.cap.load() == 24);
    CHECK(diag::HandleControlChange(q, 15, 22, 2) && q.stimulus.load() == 2);
    CHECK(diag::HandleControlChange(q, 15, 119, 1) && q.telemetry.load() == 1);
    CHECK(diag::HandleControlChange(q, 15, 119, 5) && q.telemetry.load() == 1);
    CHECK(!diag::HandleControlChange(q, 15, 1, 64)); // CC1 stays with Mod on Daisy
}

static void TestSource() {
    diag::TestSource s;
    float l[48], r[48];
    s.Select(diag::TestSource::kImpulses);
    std::vector<float> L, R;
    for(int b = 0; b < 500; ++b) {
        s.Process(l, r, 48);
        L.insert(L.end(), l, l + 48);
        R.insert(R.end(), r, r + 48);
    }
    CHECK(L[0] == .9f && R[0] == 0.f && R[12000] == .9f && L[12000] == 0.f && L.size() == 24000);
    unsigned nonzero = 0;
    for(float v : L) nonzero += v != 0.f;
    CHECK(nonzero == 1); // 24000 frames = one period
    s.Select(diag::TestSource::kBursts);
    double el = 0, er = 0, both = 0;
    for(int b = 0; b < 1000; ++b) {
        s.Process(l, r, 48);
        for(int i = 0; i < 48; ++i) {
            el += l[i] * l[i];
            er += r[i] * r[i];
            both += std::fabs(l[i] * r[i]);
        }
    }
    CHECK(el > 10 && er > 10 && both == 0.0); // bursts alternate sides, never overlap
    s.Select(diag::TestSource::kNoise);
    s.Process(l, r, 48);
    std::vector<float> first(l, l + 48);
    s.Select(diag::TestSource::kNoise);
    s.Process(l, r, 48);
    CHECK(std::vector<float>(l, l + 48) == first); // repeatable
    bool independent = false;
    for(int i = 0; i < 48; ++i) independent = independent || l[i] != r[i];
    CHECK(independent);
    s.Select(7);
    CHECK(s.External());
}

static void TestSender() {
    diag::TelemetrySender sender;
    diag::Metrics m{};
    for(unsigned i = 0; i < m.size(); ++i) m[i] = i * 1000u + 7u;
    sender.Start(m);
    unsigned packets = 0;
    std::vector<uint8_t> all;
    while(sender.Busy()) {
        sender.Service([&](const uint8_t* p, std::size_t n) {
            ++packets;
            all.insert(all.end(), p, p + n);
        });
    }
    CHECK(packets == diag::kRecords);
    CHECK(all.size() == 60u * diag::kRecords);
    sender.Service([&](const uint8_t*, std::size_t) { ++packets; });
    CHECK(packets == diag::kRecords); // idle after the last record
}

// The firmware encoder's bytes for tools/test_cumulonimbus_probe.py (metric i = i * 1000 + 7).
static void WriteProbeFixture() {
    diag::Metrics m{};
    for(unsigned i = 0; i < m.size(); ++i) m[i] = i * 1000u + 7u;
    if(std::FILE* f = std::fopen("build/host/telemetry_fixture.bin", "wb")) {
        for(unsigned r = 0; r < diag::kRecords; ++r) {
            const auto p = diag::EncodeRecord(m, r);
            std::fwrite(p.data(), 1, p.size(), f);
        }
        std::fclose(f);
    }
}

int main() {
    WriteProbeFixture();
    TestControlMap();
    TestRoutes();
    TestControlChanges();
    TestSource();
    TestSender();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("cumulonimbus firmware logic: all tests passed\n");
    return 0;
}
