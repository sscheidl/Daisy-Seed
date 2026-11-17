// SPDX-License-Identifier: MIT
// Host tests for the Cumulonimbus component, its rate converter, the host chain and the meter.
// Deterministic: fixed seeds, internally generated impulses, tone bursts and noise.
#include "../src/chain.h"
#include "../src/cumulonimbus.h"
#include "../src/stage_meter.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace seed;
using namespace seed::fx;
namespace cc = cumulonimbus::clouds;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if(!(cond)) {                                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            ++failures;                                                                   \
        }                                                                                 \
    } while(0)

constexpr std::size_t kBlock = 48;
constexpr double kPi = 3.14159265358979323846;

struct Noise {
    uint32_t s = 12345;
    float Next() {
        s = s * 1664525u + 1013904223u;
        return float(int32_t(s)) / 2147483648.f;
    }
};

template <unsigned Rate> struct Rig {
    using Fx = CumulonimbusT<Rate>;
    std::unique_ptr<typename Fx::Storage> storage = std::make_unique<typename Fx::Storage>();
    std::unique_ptr<Fx> fx = std::make_unique<Fx>();
    bool Prepare(typename Fx::Options o = {}) { return fx->Prepare({48000, kBlock}, *storage, o); }
    // Runs `blocks` blocks of input from `gen(frame, channel)`, returns the wet energy per channel.
    template <class Gen> std::pair<double, double> Run(unsigned blocks, Gen gen, std::vector<float>* out_l = nullptr,
                                                       std::vector<float>* out_r = nullptr) {
        double el = 0, er = 0;
        float l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
        for(unsigned b = 0; b < blocks; ++b) {
            for(std::size_t i = 0; i < kBlock; ++i) {
                l[i] = gen(frame + i, 0);
                r[i] = gen(frame + i, 1);
            }
            CHECK(fx->Process({l, r, ol, orr, kBlock}));
            for(std::size_t i = 0; i < kBlock; ++i) {
                CHECK(std::isfinite(ol[i]) && std::isfinite(orr[i]));
                el += double(ol[i]) * ol[i];
                er += double(orr[i]) * orr[i];
                if(out_l) out_l->push_back(ol[i]);
                if(out_r) out_r->push_back(orr[i]);
            }
            frame += kBlock;
        }
        return {el, er};
    }
    std::size_t frame = 0;
};

static CumulonimbusParameters Busy() {
    CumulonimbusParameters p;
    p.position = .02f; // newest audio: a 2 s ring needs no long warm-up
    p.size = .3f;
    p.density = .9f; // random, dense
    p.texture = .5f;
    p.stereo_spread = 0.f;
    return p;
}

template <unsigned Rate> static void TestContract() {
    Rig<Rate> rig;
    using Fx = CumulonimbusT<Rate>;
    typename Fx::Storage* s = rig.storage.get();
    CHECK(!rig.fx->Prepare({44100, kBlock}, *s));
    CHECK(!rig.fx->Prepare({48000, 0}, *s));
    CHECK(!rig.fx->Prepare({48000, 64}, *s));
    typename Fx::Options bad;
    bad.channels = 3;
    CHECK(!rig.fx->Prepare({48000, kBlock}, *s, bad));
    float l[kBlock] = {}, r[kBlock] = {}, ol[kBlock], orr[kBlock];
    CHECK(!rig.fx->Process({l, r, ol, orr, kBlock})); // not prepared
    CHECK(rig.Prepare());
    CHECK(!rig.fx->Process({l, r, ol, ol, kBlock}));        // aliased outputs
    CHECK(!rig.fx->Process({nullptr, r, ol, orr, kBlock})); // null input
    CHECK(rig.fx->Process({l, r, ol, orr, 0}));             // zero frames: no-op
    if(Rate == 32000) {
        for(auto& x : ol) x = 7.f;
        CHECK(!rig.fx->Process({l, r, ol, orr, 47})); // 32 kHz path needs multiples of 3 ...
        CHECK(ol[0] == 7.f && ol[46] == 7.f);           // ... and rejects without touching buffers (R8)
        float a[kBlock], b[kBlock];
        for(std::size_t i = 0; i < kBlock; ++i) a[i] = b[i] = .25f;
        CHECK(!rig.fx->Process({a, b, a, b, 47})); // exact in-place: the dry input survives
        bool untouched = true;
        for(std::size_t i = 0; i < kBlock; ++i) untouched = untouched && a[i] == .25f && b[i] == .25f;
        CHECK(untouched);
        CHECK(rig.fx->Process({l, r, ol, orr, 24}));
    }
    // Unprepared effects reject without touching buffers too.
    {
        Rig<Rate> idle;
        float a[kBlock], o[kBlock];
        for(std::size_t i = 0; i < kBlock; ++i) a[i] = o[i] = .5f;
        CHECK(!idle.fx->Process({a, a, o, a, kBlock}));
        CHECK(o[0] == .5f && a[0] == .5f);
    }
    // NaN parameters fall back to defaults.
    CumulonimbusParameters p;
    p.position = NAN;
    p.pitch = INFINITY;
    rig.fx->SetParameters(p);
    CHECK(rig.fx->GetParameters().position == .25f);
    CHECK(rig.fx->GetParameters().pitch == 0.f);
    p = {};
    p.pitch = 99.f;
    rig.fx->SetParameters(p);
    CHECK(rig.fx->GetParameters().pitch == 24.f);
    p.pitch = -99.f;
    rig.fx->SetParameters(p);
    CHECK(rig.fx->GetParameters().pitch == -24.f);
}

// Genuine stereo: a signal only in L must come out of L only (zero spread, stereo ring).
template <unsigned Rate> static void TestStereoRouting() {
    for(int side = 0; side < 2; ++side) {
        Rig<Rate> rig;
        CHECK(rig.Prepare());
        rig.fx->SetParameters(Busy());
        const auto e = rig.Run(600, [&](std::size_t n, int ch) {
            return ch == side ? float(.5 * std::sin(2 * kPi * 440.0 * double(n) / 48000.0)) : 0.f;
        });
        const double wanted = side == 0 ? e.first : e.second, other = side == 0 ? e.second : e.first;
        CHECK(wanted > 1.0);
        CHECK(other < wanted * 1e-6);
    }
}

// Mono ring with spread: the pan law spreads the mono input to both outputs; spread 0 gives
// Clouds' centre gains sin(pi/4) = cos(pi/4) on both sides.
template <unsigned Rate> static void TestMonoPan() {
    Rig<Rate> rig;
    typename CumulonimbusT<Rate>::Options o;
    o.channels = 1;
    CHECK(rig.Prepare(o));
    CumulonimbusParameters p = Busy();
    rig.fx->SetParameters(p);
    auto tone = [](std::size_t n, int) { return float(.5 * std::sin(2 * kPi * 300.0 * double(n) / 48000.0)); };
    auto e = rig.Run(600, tone);
    CHECK(e.first > 1.0 && std::fabs(e.first / e.second - 1.0) < 1e-3);
    p.stereo_spread = 1.f;
    rig.fx->SetParameters(p);
    rig.Run(200, tone);
    std::vector<float> l, r;
    rig.Run(1500, tone, &l, &r);
    double diff = 0, sum = 0;
    for(std::size_t i = 0; i < l.size(); ++i) {
        diff += std::fabs(l[i] - r[i]);
        sum += std::fabs(l[i]) + std::fabs(r[i]);
    }
    CHECK(sum > 1.0 && diff / sum > .05); // the two sides differ: grains are panned apart
    for(int32_t i = 0; i < cc::kMaxNumGrains; ++i) {
        const auto& g = rig.fx->Scheduler().grain(i);
        if(!g.active()) continue;
        CHECK(std::fabs(g.gain_l() * g.gain_l() + g.gain_r() * g.gain_r() - 1.f) < 2e-3f); // constant power
    }
}

// Freeze holds the ring (head and contents) while grains keep playing; unfreeze resumes.
template <unsigned Rate> static void TestFreeze() {
    Rig<Rate> rig;
    CHECK(rig.Prepare());
    CumulonimbusParameters p = Busy();
    rig.fx->SetParameters(p);
    Noise noise;
    rig.Run(300, [&](std::size_t, int) { return .4f * noise.Next(); });
    p.freeze = true;
    rig.fx->SetParameters(p);
    const int32_t head = rig.fx->Ring(0).head();
    std::vector<int16_t> snapshot(rig.storage->ring[0], rig.storage->ring[0] + 4096);
    const auto e = rig.Run(500, [&](std::size_t, int) { return .4f * noise.Next(); });
    CHECK(rig.fx->Ring(0).head() == head);
    CHECK(std::equal(snapshot.begin(), snapshot.end(), rig.storage->ring[0]));
    CHECK(e.first > 1.0 && e.second > 1.0); // grains still sound from the held ring
    // Silence in, freeze on: output continues (it is the held texture, not the input).
    const auto quiet = rig.Run(200, [](std::size_t, int) { return 0.f; });
    CHECK(quiet.first > .1);
    p.freeze = false;
    rig.fx->SetParameters(p);
    rig.Run(5, [](std::size_t, int) { return 0.f; });
    CHECK(rig.fx->Ring(0).head() != head);
}

// The ring wraps, grains read across the seam without overruns (ASan) and stay finite.
static void TestWrapAndTinyRing() {
    cc::Tables tables;
    tables.Init(48000.f);
    for(int32_t frames : {64 + cc::kInterpolationTail, 1000 + cc::kInterpolationTail, 96000 + cc::kInterpolationTail}) {
        std::vector<int16_t> buffer(static_cast<std::size_t>(frames)), tail(cc::kCrossFadeSize);
        cc::PcmRing ring;
        ring.Init(buffer.data(), frames, tail.data());
        auto scheduler = std::make_unique<cc::LiveScheduler>();
        scheduler->Init(&tables, 1, 40, 48000.f, 24, 7);
        cc::GrainParameters gp;
        gp.size = 1.f;
        gp.overlap = 1.f;
        gp.pitch = 24.f;
        gp.position = 1.f;
        float in[24], out[48];
        Noise noise;
        int32_t last_head = 0;
        bool wrapped = false;
        for(unsigned call = 0; call < 9000; ++call) {
            for(auto& x : in) x = .8f * noise.Next();
            ring.WriteFade(in, 24, 1, true);
            if(ring.head() < last_head) wrapped = true;
            last_head = ring.head();
            gp.pitch = (call / 500) % 2 ? -24.f : 24.f;
            gp.position = float(call % 97) / 96.f;
            scheduler->Play(&ring, gp, call % 50 == 0, out, 24);
            for(float x : out) CHECK(std::isfinite(x) && std::fabs(x) < 8.f);
        }
        CHECK(wrapped);
        // Interpolation tail: the guard samples mirror the ring start.
        for(int32_t i = 0; i < cc::kInterpolationTail; ++i)
            CHECK(buffer[std::size_t(ring.size() + i)] == buffer[std::size_t(i)]);
    }
}

// Pool exhaustion and the cap: never more than `cap` grains, rejected starts are counted, and
// quality drops to medium with occupancy above a quarter of the Clouds reference pool.
template <unsigned Rate> static void TestPoolAndQuality() {
    for(int32_t cap : {0, 8, 16, 32, 64}) {
        Rig<Rate> rig;
        typename CumulonimbusT<Rate>::Options o;
        o.cap = cap;
        CHECK(rig.Prepare(o));
        CumulonimbusParameters p = Busy();
        p.size = 1.f;      // long grains, maximal overlap
        p.density = 0.f;   // deterministic, overlap 1
        rig.fx->SetParameters(p);
        Noise noise;
        rig.Run(1500, [&](std::size_t, int) { return .3f * noise.Next(); });
        const auto t = rig.fx->Telemetry();
        CHECK(t.peak_active <= cap);
        CHECK(t.cap == cap);
        if(cap == 0) {
            CHECK(t.started_high + t.started_medium == 0);
        } else {
            CHECK(t.peak_active == cap || cap > 32); // reachable caps fill up
            if(cap < 32) CHECK(t.rejected > 0);       // over-cap starts are refused and counted
        }
        if(cap >= 16) CHECK(t.started_medium > 0);
        if(cap > 0 && cap <= 8) CHECK(t.started_medium == 0);
    }
    // Lowering the cap lets running grains finish and allows no new ones above it.
    Rig<Rate> rig;
    CHECK(rig.Prepare());
    CumulonimbusParameters p = Busy();
    p.size = 1.f;
    p.density = 0.f;
    rig.fx->SetParameters(p);
    rig.Run(1500, [](std::size_t n, int) { return float(n % 100) / 200.f - .25f; });
    CHECK(rig.fx->ActiveGrains() > 4);
    rig.fx->SetCap(4);
    rig.Run(2000, [](std::size_t n, int) { return float(n % 100) / 200.f - .25f; });
    CHECK(rig.fx->ActiveGrains() <= 4);
}

// R3 (PR #108 review): after lowering the cap below the active count, no grain starts (not even
// a trigger into a free low slot) until the running grains have finished; quality and
// normalization count every audible grain.
static void TestLoweredCap() {
    cc::Tables tables;
    tables.Init(48000.f);
    std::vector<int16_t> pcm(96000 + cc::kInterpolationTail), tail(cc::kCrossFadeSize);
    cc::PcmRing ring;
    ring.Init(pcm.data(), int32_t(pcm.size()), tail.data());
    auto scheduler = std::make_unique<cc::LiveScheduler>();
    scheduler->Init(&tables, 1, 40, 48000.f, 24, 1);
    cc::GrainParameters params;
    params.size = 1.f; // 512 ms grains
    float wet[48];
    scheduler->SetCap(16);
    for(int i = 0; i < 12; ++i) scheduler->Play(&ring, params, true, wet, 24);
    CHECK(scheduler->ActiveGrains() == 12);
    scheduler->SetCap(8);
    const uint32_t refused = scheduler->Counters().rejected;
    scheduler->Play(&ring, params, true, wet, 24);
    CHECK(scheduler->ActiveGrains() == 12);               // was 13 before the fix
    CHECK(scheduler->Counters().rejected == refused + 1); // the refused trigger is counted once
    CHECK(scheduler->Counters().peak_active == 12);
    // Once enough grains have ended (512 ms), starts resume up to the new cap only.
    for(int i = 0; i < 1100; ++i) scheduler->Play(&ring, params, true, wet, 24);
    CHECK(scheduler->ActiveGrains() <= 8 && scheduler->ActiveGrains() > 0);
    // An overdue regular grain counts once per overdue period, not once per waiting sample.
    auto regular = std::make_unique<cc::LiveScheduler>();
    regular->Init(&tables, 1, 40, 48000.f, 24, 1);
    regular->SetCap(1);
    cc::GrainParameters dense = params;
    dense.overlap = 1.f;
    dense.use_deterministic_seed = true;
    for(int i = 0; i < 200; ++i) regular->Play(&ring, dense, false, wet, 24); // 100 ms of one 512 ms grain
    CHECK(regular->ActiveGrains() == 1);
    CHECK(regular->Counters().rejected >= 1 && regular->Counters().rejected <= 2);
}

// R4 (PR #108 review): a Freeze shorter than the 256-sample tail crossfades over what it captured;
// a new Freeze during a fade captures afresh.
static void TestShortFreeze() {
    std::vector<int16_t> pcm(4096 + cc::kInterpolationTail), tail(cc::kCrossFadeSize, int16_t(-1234));
    cc::PcmRing ring;
    ring.Init(pcm.data(), int32_t(pcm.size()), tail.data());
    float ones[64], zeros[64] = {};
    for(auto& x : ones) x = 1.f;
    ring.WriteFade(zeros, 16, 1, true);          // head 16
    ring.WriteFade(ones, 24, 1, false);          // a 24-sample Freeze
    CHECK(ring.Capturing() && ring.head() == 16);
    ring.WriteFade(zeros, 24, 1, true);          // resume with silence
    CHECK(!ring.Capturing() && ring.FadeRemaining() == 0);
    // First resumed sample: tail[0] (1.0) at gain 23/24, the last one fades to 0.
    CHECK(std::abs(int(pcm[16]) - int(32767.f * 23.f / 24.f)) <= 2);
    CHECK(pcm[16 + 23] == 0);
    for(int i = 1; i < 24; ++i) CHECK(pcm[std::size_t(16 + i)] <= pcm[std::size_t(15 + i)]); // monotonic fade
    // Refreeze during a fade: the old fade is abandoned, the new capture starts at tail[0].
    float halves[8];
    for(auto& x : halves) x = .5f;
    ring.WriteFade(ones, 40, 1, false);          // 40 samples captured
    ring.WriteFade(zeros, 10, 1, true);          // fade started (30 left)
    CHECK(ring.FadeRemaining() == 30);
    ring.WriteFade(halves, 8, 1, false);         // refreeze: 8 new samples
    const int32_t head = ring.head();
    ring.WriteFade(zeros, 8, 1, true);
    CHECK(std::abs(int(pcm[std::size_t(head)]) - int(16383.f * 7.f / 8.f)) <= 2); // tail[0] = 0.5 at gain 7/8
    CHECK(ring.FadeRemaining() == 0);
    // A full 256-sample Freeze keeps Clouds' gains counter / 256.
    std::vector<float> long_in(300, 1.f);
    ring.WriteFade(long_in.data(), 300, 1, false);
    const int32_t h2 = ring.head();
    ring.WriteFade(zeros, 1, 1, true);
    CHECK(std::abs(int(pcm[std::size_t(h2)]) - int(32767.f * 255.f / 256.f)) <= 2);
}

// One-shot triggers start exactly one grain per host block, in its first call.
template <unsigned Rate> static void TestTrigger() {
    Rig<Rate> rig;
    CHECK(rig.Prepare());
    CumulonimbusParameters p = Busy();
    p.density = .5f; // dead zone: no scheduled grains
    p.size = 0.f;
    rig.fx->SetParameters(p);
    rig.Run(10, [](std::size_t, int) { return .2f; });
    CHECK(rig.fx->Telemetry().started_high == 0);
    rig.fx->Trigger();
    rig.Run(1, [](std::size_t, int) { return .2f; });
    CHECK(rig.fx->Telemetry().started_high == 1);
    rig.Run(5, [](std::size_t, int) { return .2f; });
    CHECK(rig.fx->Telemetry().started_high == 1); // consumed once, not per sub-call
    const auto& g = rig.fx->Scheduler();
    int starts_at_zero = 0;
    for(int32_t i = 0; i < cc::kMaxNumGrains; ++i)
        starts_at_zero += g.grain(i).active() ? 1 : 0;
    CHECK(starts_at_zero <= 1);
}

// Grain durations are in seconds at both rates: SIZE 0 = 32 ms, SIZE 1 = 512 ms.
template <unsigned Rate> static void TestDurations() {
    cc::Tables t;
    t.Init(float(Rate));
    CHECK(std::fabs(t.grain_size[0] / float(Rate) - .032f) < 1e-4f);
    CHECK(std::fabs(t.grain_size[256] / float(Rate) - .512f) < 1e-4f);
    if(Rate == 32000) {
        CHECK(t.grain_size[0] == 1024.f && t.grain_size[256] == 16384.f); // the Clouds table
        CHECK(t.grain_size[128] == std::floor(1024.f * 4.f));
    }
    // Pitch: a +12 semitone grain reads at 2x (16.16 increment).
    Rig<Rate> rig;
    CHECK(rig.Prepare());
    CumulonimbusParameters p = Busy();
    p.pitch = 12.f;
    rig.fx->SetParameters(p);
    rig.Run(200, [](std::size_t, int) { return .1f; });
    bool found = false;
    for(int32_t i = 0; i < cc::kMaxNumGrains; ++i) {
        const auto& g = rig.fx->Scheduler().grain(i);
        if(g.active()) {
            found = true;
            CHECK(std::abs(g.phase_increment() - 131072) <= 1);
        }
    }
    CHECK(found);
}

// Same seed, same input -> identical output; a different seed differs.
template <unsigned Rate> static void TestDeterminism() {
    auto render = [](uint32_t seed) {
        Rig<Rate> rig;
        typename CumulonimbusT<Rate>::Options o;
        o.seed = seed;
        rig.Prepare(o);
        CumulonimbusParameters p = Busy();
        p.stereo_spread = 1.f;
        rig.fx->SetParameters(p);
        Noise noise;
        std::vector<float> l, r;
        rig.Run(400, [&](std::size_t, int) { return .3f * noise.Next(); }, &l, &r);
        return l;
    };
    const auto a = render(1), b = render(1), c = render(2);
    CHECK(a == b);
    CHECK(a != c);
}

// 48 -> 32 -> 48 kHz: unity in the pass band, alias/image rejection, constant delay.
static void TestRateConversion() {
    cumulonimbus::RatePrototype proto;
    proto.Init();
    auto roundtrip = [&](double hz, double& gain, std::size_t& delay) {
        cumulonimbus::Down48To32 down;
        cumulonimbus::Up32To48 up;
        down.Init(proto);
        up.Init(proto);
        std::vector<float> in(48000), out;
        for(std::size_t n = 0; n < in.size(); ++n)
            in[n] = float(std::sin(2 * kPi * hz * double(n) / 48000.0));
        float mid[32], back[48];
        for(std::size_t b = 0; b < in.size() / 48; ++b) {
            CHECK(down.Process(&in[b * 48], 48, mid) == 32);
            CHECK(up.Process(mid, 32, back) == 48);
            out.insert(out.end(), back, back + 48);
        }
        double e_in = 0, e_out = 0;
        for(std::size_t n = 4800; n < out.size(); ++n) {
            e_in += double(in[n]) * in[n];
            e_out += double(out[n]) * out[n];
        }
        gain = std::sqrt(e_out / e_in);
        // Delay: best integer alignment.
        double best = -1e30;
        delay = 0;
        for(std::size_t d = 0; d < 200; ++d) {
            double c = 0;
            for(std::size_t n = 4800; n < 9600; ++n) c += double(in[n]) * out[n + d];
            if(c > best) { best = c; delay = d; }
        }
    };
    double g;
    std::size_t d;
    roundtrip(1000.0, g, d);
    CHECK(std::fabs(g - 1.0) < .01);
    CHECK(d >= 30 && d <= 40); // 0.74 ms at 48 kHz = 35.5 samples
    roundtrip(10000.0, g, d);
    CHECK(std::fabs(g - 1.0) < .03);
    // 20 kHz is above the 32 kHz Nyquist: removed by the down-sampler.
    roundtrip(20000.0, g, d);
    CHECK(g < 3.2e-3); // better than -50 dB
    // A 15 kHz alias region check on the down path alone: 20 kHz input would alias to 12 kHz.
    cumulonimbus::Down48To32 down;
    down.Init(proto);
    double e = 0;
    float mid[32];
    std::vector<float> in(48);
    std::size_t frame = 0;
    for(unsigned b = 0; b < 1000; ++b) {
        for(auto& x : in) x = float(std::sin(2 * kPi * 20000.0 * double(frame++) / 48000.0));
        down.Process(in.data(), 48, mid);
        if(b > 100)
            for(float x : mid) e += double(x) * x;
    }
    CHECK(std::sqrt(e / (900.0 * 32.0)) < 2.3e-3); // relative to an RMS of 0.707: < -50 dB
}

// Host chain: exact dry at mix 0 and in the suspended bypass, the tail policy, freeze, isolation.
static void TestChain() {
    auto grains = std::make_unique<Cumulonimbus::Storage>();
    auto reverb = std::make_unique<TorusReverb::Storage>();
    auto chain = std::make_unique<GranularReverbChain>();
    CHECK(chain->Prepare(*grains, *reverb));
    chain->SetGrainParameters(Busy());
    float l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
    std::size_t frame = 0;
    auto block = [&](float amp) {
        for(std::size_t i = 0; i < kBlock; ++i, ++frame)
            l[i] = r[i] = amp * float(std::sin(2 * kPi * 220.0 * double(frame) / 48000.0));
        chain->Process(l, r, ol, orr, kBlock);
    };
    auto exact = [&]() {
        bool same = true;
        for(std::size_t i = 0; i < kBlock; ++i) same = same && ol[i] == l[i] && orr[i] == r[i];
        return same;
    };
    // Mix 0 on both stages: exact dry while everything runs.
    chain->SetSettings({0.f, 0.f, .5f});
    for(int i = 0; i < 30; ++i) block(.7f);
    CHECK(exact());
    // Wet: the output differs and has a tail after the input stops.
    chain->SetSettings({.5f, .5f, .7f});
    for(int i = 0; i < 300; ++i) block(.7f);
    CHECK(!exact());
    double tail = 0;
    for(int i = 0; i < 200; ++i) {
        block(0.f);
        for(std::size_t k = 0; k < kBlock; ++k) tail += double(ol[k]) * ol[k];
    }
    CHECK(tail > 1e-3);
    // Bypass: tail rings on (wet still non-zero), then fades and suspends to exact dry.
    for(int i = 0; i < 100; ++i) block(.7f);
    chain->SetMode(ChainMode::Bypass);
    block(.7f);
    CHECK(chain->GetPhase() == GranularReverbChain::Phase::Tail);
    for(unsigned i = 0; i < 500; ++i) block(.7f);
    CHECK(!exact()); // still in the tail
    for(unsigned i = 0; i < GranularReverbChain::kTailBlocks + GranularReverbChain::kFadeBlocks; ++i) block(.7f);
    CHECK(chain->Suspended());
    block(.7f);
    CHECK(exact());
    // Leaving bypass: no jump in the first block (feed and mix glide from 0).
    chain->SetMode(ChainMode::Run);
    block(.7f);
    double jump = 0;
    for(std::size_t i = 0; i < kBlock; ++i) jump = std::max(jump, double(std::fabs(ol[i] - l[i])));
    CHECK(jump < .1);
    CHECK(!chain->Suspended());
    // Re-entering bypass during the tail and leaving again does not suspend.
    chain->SetMode(ChainMode::Bypass);
    for(int i = 0; i < 100; ++i) block(.7f);
    chain->SetMode(ChainMode::Run);
    block(.7f);
    CHECK(chain->GetPhase() == GranularReverbChain::Phase::Active);
    // Freeze keeps the grains running from a held ring.
    chain->SetMode(ChainMode::Freeze);
    const int32_t head = chain->GrainEffect().Ring(0).head();
    for(int i = 0; i < 100; ++i) block(.7f);
    CHECK(chain->GrainEffect().Ring(0).head() == head);
    chain->SetMode(ChainMode::Run);
    // Stage isolation: both stages off -> exact dry, no grain work.
    ChainRoute route;
    route.grains = route.reverb = false;
    chain->SetRoute(route);
    const uint32_t started = chain->GrainEffect().Telemetry().started_high + chain->GrainEffect().Telemetry().started_medium;
    for(int i = 0; i < 50; ++i) block(.7f);
    CHECK(exact());
    CHECK(chain->GrainEffect().Telemetry().started_high + chain->GrainEffect().Telemetry().started_medium == started);
    // mute_wet: work continues, output exact dry after the 10 ms glide.
    route = {};
    route.mute_wet = true;
    chain->SetRoute(route);
    for(int i = 0; i < 300; ++i) block(.7f);
    CHECK(exact());
    CHECK(chain->GrainEffect().Telemetry().started_high + chain->GrainEffect().Telemetry().started_medium > started);
    // Output guard: identity below 0.8, bounded above, NaN -> 0.
    CHECK(OutputGuard(.79f) == .79f && OutputGuard(-.8f) == -.8f);
    CHECK(OutputGuard(5.f) <= 1.f && OutputGuard(5.f) > .95f && OutputGuard(-5.f) >= -1.f);
    CHECK(OutputGuard(NAN) == 0.f);
}

static void TestMeter() {
    using namespace cumulonimbus::diag;
    StageMeter meter;
    meter.Init(10000);
    uint32_t now = 0, ms = 1000;
    for(unsigned b = 0; b < 100; ++b, ++ms) {
        meter.SetApp(kScenario, 3);
        meter.Begin(now, ms);
        meter.Add(kPiano, 1000);
        meter.Add(kGrainRender, 2000 + b);
        meter.End(b < 99 ? 5000 : 12000); // one overrun
        now += 10000;
    }
    // Two periods lost: the next start comes 3 budgets later.
    now += 20000;
    ms += 2;
    meter.Begin(now, ms++);
    meter.End(4000);
    meter.AddAccounting(321);
    meter.RequestSnapshot();
    now += 10000;
    meter.Begin(now, ms);
    meter.SetApp(kGrainsPeak, 17);
    meter.End(4000);
    CHECK(meter.SnapshotReady());
    MeterSnapshot s = meter.Snapshot();
    meter.ReleaseSnapshot();
    CHECK(s.blocks == 102);
    CHECK(s.overruns == 1);
    CHECK(s.missed == 2);
    CHECK(s.window_ms == 103); // 102 blocks spanning 103 ms (two lost periods)
    CHECK(s.accounting_max == 321);
    CHECK(s.app[kScenario] == 3 && s.app[kGrainsPeak] == 17); // metadata taken with the statistics
    CHECK(s.max[kTotal] == 12000 && s.max[kGrainRender] == 2099 && s.max[kPiano] == 1000);
    CHECK(Percentile(s, 500) == 5100);  // 50 % bin -> upper edge 51 %
    CHECK(Percentile(s, 1000) == 12100); // the overrun: 120 % bin, upper edge 121 %
    Metrics m{};
    FillMeterMetrics(s, m);
    CHECK(m[kTotalMax] == 12000 && m[kMissedPeriods] == 2 && m[kP50] == 5100);
    CHECK(m[kWindowMs] == 103 && m[kScenario] == 3 && m[kGrainsPeak] == 17 && m[kAccountingMax] == 321);
    CHECK(m[kMetricSet] == kMetricSetVersion && kMetricCount == 39 && kRecords == 13);
    m[kScenario] = 4;
    m[kFlags] = kFlagProfiling | kFlagRate32;
    m[kWriteMean] = 0x7FFFFFFFu;
    for(unsigned rec = 0; rec < kRecords; ++rec) {
        const auto p = EncodeRecord(m, rec);
        CHECK(p[0] == 0xBF && p[1] == 100 && p[2] == kProtocolId);
        CHECK(p[3 * 19 + 1] == 119 && p[3 * 19 + 2] == 64);
        unsigned sum = 0;
        for(unsigned cc = 101; cc <= 117; ++cc) sum += p[3 * (cc - 100) + 2];
        CHECK(p[3 * 18 + 2] == (sum & 127u));
        for(unsigned v = 0; v < 3; ++v) {
            uint64_t value = 0;
            for(unsigned d = 0; d < 5; ++d) value |= uint64_t(p[3 * (2 + v * 5 + d) + 2]) << (7 * d);
            CHECK(value == m[rec * 3 + v]);
        }
        for(unsigned i = 0; i < 20; ++i) CHECK(p[3 * i + 2] < 128);
    }
    // A main-loop reset request clears at the next Begin.
    meter.RequestReset();
    meter.Begin(now + 10000, 5000);
    meter.End(1);
    meter.RequestSnapshot();
    meter.Begin(now + 20000, 5001);
    meter.End(1);
    CHECK(meter.SnapshotReady());
    CHECK(meter.Snapshot().blocks == 2 && meter.Snapshot().missed == 0 && meter.Snapshot().window_ms == 1);
    meter.ReleaseSnapshot();
    // An audio-thread reset (ResetNow) inside a block: that block is the first of the new window,
    // so a snapshot requested together with the reset never shows the old statistics (R2).
    meter.Begin(now + 30000, 6000);
    meter.End(9000);
    meter.RequestSnapshot();
    meter.Begin(now + 40000, 6001);
    meter.ResetNow();
    meter.End(2000);
    CHECK(meter.SnapshotReady());
    CHECK(meter.Snapshot().blocks == 1 && meter.Snapshot().max[kTotal] == 2000 && meter.Snapshot().window_ms == 0);
    meter.ReleaseSnapshot();
    meter.Begin(now + 50000, 6002); // missed-period detection continues across the reset
    meter.End(1);
    meter.RequestSnapshot();
    meter.Begin(now + 60000, 6003);
    meter.End(1);
    CHECK(meter.Snapshot().missed == 0 && meter.Snapshot().blocks == 3);
    // Stack paint helpers.
    uint32_t stack[16];
    PaintStack(stack, stack + 16);
    stack[10] = 0;
    CHECK(UntouchedStack(stack, stack + 16) == 40);
}

// R12: rejected wet blocks return exact dry, including in-place and zero mix.
static void TestWetHostRejection() {
    Rig<32000> rig;
    audio::WetHost<CumulonimbusT<32000>, kBlock> host(*rig.fx);
    float l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
    for(std::size_t i = 0; i < kBlock; ++i) {
        l[i] = .25f + float(i) * .001f;
        r[i] = -.125f - float(i) * .001f;
    }
    for(bool prepared : {false, true}) {
        if(prepared) CHECK(rig.Prepare());
        for(float mix : {0.f, .5f, 1.f}) {
            host.Reset(true, mix);
            for(bool inplace : {false, true}) {
                for(std::size_t i = 0; i < kBlock; ++i) { ol[i] = l[i]; orr[i] = r[i]; }
                // 47 is rejected by F32; 48 is also rejected before Prepare.
                const std::size_t n = prepared ? 47 : 48;
                host.Process(inplace ? ol : l, inplace ? orr : r, ol, orr, n);
                for(std::size_t i = 0; i < kBlock; ++i) CHECK(ol[i] == l[i] && orr[i] == r[i]);
            }
        }
    }
    host.Reset(true, .5f);
    host.Process(l, r, ol, orr, 48); // valid wet processing still runs
    for(std::size_t i = 0; i < kBlock; ++i) CHECK(std::isfinite(ol[i]) && std::isfinite(orr[i]));
}

int main() {
    TestContract<48000>();
    TestContract<32000>();
    TestStereoRouting<48000>();
    TestStereoRouting<32000>();
    TestMonoPan<48000>();
    TestMonoPan<32000>();
    TestFreeze<48000>();
    TestFreeze<32000>();
    TestWrapAndTinyRing();
    TestPoolAndQuality<48000>();
    TestPoolAndQuality<32000>();
    TestLoweredCap();
    TestShortFreeze();
    TestTrigger<48000>();
    TestTrigger<32000>();
    TestDurations<48000>();
    TestDurations<32000>();
    TestDeterminism<48000>();
    TestDeterminism<32000>();
    TestRateConversion();
    TestChain();
    TestMeter();
    TestWetHostRejection();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("cumulonimbus (%u Hz chain): all tests passed\n", unsigned(CUMULONIMBUS_INTERNAL_RATE));
    return 0;
}
