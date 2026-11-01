// SPDX-License-Identifier: MIT
// Host tests of the Streichwurst FX component alone (no instrument, board or MIDI):
// ensemble delay ranges, LFO rates, mode sums and DC/stereo relations (SF-D05/D07/F06),
// impulse response without feedback, bypass/mode transitions, Animate and Phaser
// pass-through at amount 0, reverb decay and tails, the FX-contract reverb adapter,
// invalid blocks and parameters, long renders. Own signals only.
#include "streichwurst_fx/fx.h"
#include "streichwurst_fx/reverb_adapter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace streichwurst_fx;

static int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if(!(cond)) {                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++failures;                                                                            \
        }                                                                                          \
    } while(0)

constexpr float kFs = 48000.f;
constexpr std::size_t kBlock = 48;
static FxStorage storage; // host-owned memory (SDRAM/AXI on the board)

static StreichwurstFx& Fresh(FxParams p) {
    static StreichwurstFx fx;
    CHECK(fx.Prepare(FxConfig{kFs, kBlock}, storage));
    fx.SetParameters(p);
    fx.Reset(); // start with the configured state, no transition
    return fx;
}

static FxParams Dry() {
    FxParams p;
    p.ensemble_on = false;
    p.reverb_wet = 0.f;
    return p;
}

// Processes `in` through the string stage in host blocks.
static void Strings(StreichwurstFx& fx, const std::vector<float>& in, std::vector<float>& l, std::vector<float>& r,
                    std::vector<float>* gain = nullptr, std::size_t block = kBlock) {
    l.assign(in.size(), 0.f);
    r.assign(in.size(), 0.f);
    if(gain)
        gain->assign(in.size(), 0.f);
    for(std::size_t s = 0; s < in.size(); s += block) {
        const std::size_t n = std::min(block, in.size() - s);
        CHECK(fx.ProcessStrings(in.data() + s, l.data() + s, r.data() + s, gain ? gain->data() + s : nullptr, n));
    }
}

static void TestDelayRangesAndRates() {
    Ensemble e;
    CHECK(e.Prepare(kFs));
    float lo[6] = {1, 1, 1, 1, 1, 1}, hi[6] = {};
    // Track the chorus delay to estimate its LFO period from its maxima.
    std::vector<float> chorus;
    const std::size_t total = std::size_t(kFs * 8.f);
    for(std::size_t i = 0; i < total; ++i) {
        e.Process(0.f);
        for(int k = 0; k < 6; ++k) {
            lo[k] = std::min(lo[k], e.DelaySeconds(k));
            hi[k] = std::max(hi[k], e.DelaySeconds(k));
        }
        if(i % 16 == 0)
            chorus.push_back(e.DelaySeconds(3));
    }
    for(int k = 0; k < 3; ++k) {
        std::printf("  combined tap %d: %.3f..%.3f ms\n", k, lo[k] * 1e3, hi[k] * 1e3);
        CHECK(lo[k] > 176.f / 40000.f - 1e-4f && hi[k] < 320.f / 40000.f + 1e-4f); // SF-D05
        CHECK(hi[k] - lo[k] > 0.003f);
    }
    for(int k = 3; k < 6; ++k) {
        std::printf("  chorus tap %d: %.3f..%.3f ms\n", k, lo[k] * 1e3, hi[k] * 1e3);
        CHECK(std::fabs(lo[k] - 120.f / 40000.f) < 1e-4f && std::fabs(hi[k] - 376.f / 40000.f) < 1e-4f); // 3..9.4 ms
    }
    // Period of the chorus LFO: distance of successive maxima ~ 1 / 0.32 Hz = 3.125 s.
    std::vector<std::size_t> peaks;
    for(std::size_t i = 1; i + 1 < chorus.size(); ++i)
        if(chorus[i] >= chorus[i - 1] && chorus[i] > chorus[i + 1] && chorus[i] > 9.3e-3f)
            peaks.push_back(i);
    CHECK(peaks.size() >= 2);
    if(peaks.size() >= 2) {
        const float period = float(peaks[1] - peaks[0]) * 16.f / kFs;
        std::printf("  chorus LFO period %.3f s (0.32 Hz -> 3.125 s)\n", period);
        CHECK(std::fabs(period - 1.f / kLfoChorusHz) < 0.01f);
    }
    // Same time behaviour at 40 kHz (own time scaling of the 40 kHz sample counts).
    Ensemble e40;
    CHECK(e40.Prepare(40000.f));
    float hi40 = 0;
    for(int i = 0; i < 40000 * 4; ++i) {
        e40.Process(0.f);
        hi40 = std::max(hi40, e40.DelaySeconds(3));
    }
    CHECK(std::fabs(hi40 - hi[3]) < 2e-5f);
}

static void TestDcSumsAndStereo() {
    // Coherent DC per mode (SF-F06 conditional calculation): sum minus 21/128 tap, times the
    // gain word: 2.30420, 3.64746, 2.87695 relative to dry.
    const float expected[3] = {2.30420f, 3.64746f, 2.87695f};
    for(int mode = 0; mode < 3; ++mode) {
        FxParams p = Dry();
        p.ensemble_on = true;
        p.ensemble_mode = mode;
        StreichwurstFx& fx = Fresh(p);
        std::vector<float> in(9600, 1.f), l, r, g;
        Strings(fx, in, l, r, &g);
        const float out_l = l.back() * g.back(), out_r = r.back() * g.back();
        std::printf("  mode %d: DC L %.5f R %.5f gain %.4f\n", mode, out_l, out_r, g.back());
        CHECK(std::fabs(out_l - expected[mode]) < 1e-3f);
        CHECK(std::fabs(out_r - expected[mode]) < 1e-3f);
        CHECK(g.back() == Ensemble::GainWord(true, mode));
        // A sine: L and R differ (stereo), the mono sum keeps F - (21/256)(ta + tb).
        std::vector<float> sine(48000), sl, sr;
        for(std::size_t i = 0; i < sine.size(); ++i)
            sine[i] = std::sin(2.f * 3.14159265f * 330.f * float(i) / kFs);
        Strings(fx, sine, sl, sr);
        double diff = 0, sum = 0;
        for(std::size_t i = 24000; i < sine.size(); ++i) {
            diff += std::fabs(sl[i] - sr[i]);
            sum += std::fabs(sl[i] + sr[i]);
        }
        CHECK(diff > 100.0);  // stereo arms differ
        CHECK(sum > diff);    // the mono sum is not cancelled
    }
}

static void TestImpulseNoFeedback() {
    for(int mode = 0; mode < 3; ++mode) {
        FxParams p = Dry();
        p.ensemble_on = true;
        p.ensemble_mode = mode;
        StreichwurstFx& fx = Fresh(p);
        std::vector<float> in(4800, 0.f), l, r;
        in[100] = 1.f;
        Strings(fx, in, l, r);
        // Mode 0 replaces the input: nothing at the impulse itself; modes 1/2 pass it.
        CHECK(mode == 0 ? std::fabs(l[100]) < 1e-6f : std::fabs(l[100] - 1.f) < 1e-6f);
        // All energy lies within the documented delay span (max 9.4 ms + interpolation).
        double late = 0, early = 0;
        for(std::size_t i = 0; i < in.size(); ++i) {
            const double v = std::fabs(l[i]) + std::fabs(r[i]);
            if(i > 100 + std::size_t(0.0096f * kFs))
                late += v;
            else if(i > 101 && i < 100 + std::size_t(0.0029f * kFs))
                early += v;
        }
        CHECK(late == 0.0);  // no feedback tail
        CHECK(early == 0.0); // no tap shorter than 3 ms
    }
}

static void TestBypassAndTransitions() {
    // Bypass with Animate/Phaser at 0: both outputs equal the input exactly.
    StreichwurstFx& fx = Fresh(Dry());
    std::vector<float> in(4800), l, r, g;
    for(std::size_t i = 0; i < in.size(); ++i)
        in[i] = std::sin(0.05f * float(i)) * 0.7f;
    Strings(fx, in, l, r, &g);
    bool exact = true;
    for(std::size_t i = 0; i < in.size(); ++i)
        exact &= l[i] == in[i] && r[i] == in[i] && g[i] == 1.f;
    CHECK(exact);
    // In-place use (in aliases out_l) is allowed.
    std::vector<float> buf = in, rr(in.size());
    for(std::size_t s = 0; s < buf.size(); s += kBlock)
        CHECK(fx.ProcessStrings(buf.data() + s, buf.data() + s, rr.data() + s, nullptr, kBlock));
    CHECK(buf[1000] == in[1000]);
    // Switching on: a 20 ms cross-fade, no sample jump beyond the sine's own slope.
    FxParams on = Dry();
    std::vector<float> sine(9600), sl, sr, sg;
    for(std::size_t i = 0; i < sine.size(); ++i)
        sine[i] = std::sin(2.f * 3.14159265f * 220.f * float(i) / kFs);
    StreichwurstFx& t = Fresh(on);
    std::vector<float> first(sine.begin(), sine.begin() + 4800), second(sine.begin() + 4800, sine.end());
    Strings(t, first, sl, sr);
    on.ensemble_on = true;
    on.ensemble_mode = Mode1;
    t.SetParameters(on);
    CHECK(t.GetEnsemble().Fading());
    Strings(t, second, sl, sr, &sg);
    float jump = 0;
    for(std::size_t i = 1; i < sl.size(); ++i)
        jump = std::max(jump, std::fabs(sl[i] - sl[i - 1]));
    std::printf("  bypass -> mode 1: largest step %.4f (sine slope x6 taps %.4f)\n", jump, 6.f * 2.f * 3.14159265f * 220.f / kFs);
    CHECK(jump < 6.f * 2.f * 3.14159265f * 220.f / kFs * 1.2f);
    CHECK(!t.GetEnsemble().Fading());
    CHECK(sg.front() <= 1.f && sg.front() > 0.98f); // gain word fades from 1 to 0.625
    CHECK(sg.back() == 0.625f);
    // Mode change 1 -> 2 -> 0 during a fade: still bounded and settles.
    on.ensemble_mode = Mode2;
    t.SetParameters(on);
    std::vector<float> part(240, 0.5f);
    Strings(t, part, sl, sr);
    on.ensemble_mode = Mode0;
    t.SetParameters(on);
    Strings(t, second, sl, sr, &sg);
    CHECK(sg.back() == 0.8125f);
}

static void TestAnimateAndPhaser() {
    // Animate at 0: exact pass-through and zero registration offset.
    FxParams p = Dry();
    p.animate_rate_hz = 2.f;
    StreichwurstFx& fx = Fresh(p);
    std::vector<float> in(4800), l, r;
    for(std::size_t i = 0; i < in.size(); ++i)
        in[i] = std::sin(0.01f * float(i * i % 977));
    Strings(fx, in, l, r);
    CHECK(fx.AnimateRegistrationOffset() == 0.f);
    CHECK(l[3000] == in[3000]);
    // Animate at 1: offset swings over +-kSpan with the set rate.
    p.animate_amount = 1.f;
    StreichwurstFx& a = Fresh(p);
    float lo = 0, hi = 0;
    for(int b = 0; b < 1000; ++b) { // 1 s, two cycles at 2 Hz
        std::vector<float> blk(kBlock, 0.f), bl, br;
        Strings(a, blk, bl, br);
        lo = std::min(lo, a.AnimateRegistrationOffset());
        hi = std::max(hi, a.AnimateRegistrationOffset());
    }
    std::printf("  animate offset %.0f..%.0f\n", lo, hi);
    CHECK(hi > 0.98f * Animate::kSpan && lo < -0.98f * Animate::kSpan);
    // Phaser at 1: changes the signal, stays bounded; at 0 exact.
    FxParams ph = Dry();
    ph.phaser_amount = 1.f;
    ph.phaser_rate_hz = 0.3f;
    StreichwurstFx& f = Fresh(ph);
    std::vector<float> noise(48000), nl, nr;
    uint32_t seed = 1;
    for(float& v : noise) {
        seed = seed * 1664525u + 1013904223u;
        v = float(int32_t(seed)) / 2147483648.f * 0.5f;
    }
    Strings(f, noise, nl, nr);
    double diff = 0, peak = 0;
    for(std::size_t i = 0; i < noise.size(); ++i) {
        diff += std::fabs(nl[i] - noise[i]);
        peak = std::max(peak, double(std::fabs(nl[i])));
    }
    CHECK(diff > 1000.0);
    CHECK(peak < 2.0);
}

static void TestReverb() {
    FxParams p = Dry();
    p.reverb_wet = 1.f;
    p.reverb_decay = 1.f;
    StreichwurstFx& fx = Fresh(p);
    CHECK(fx.ReverbWet() == StreichwurstFx::kMaxReverbWet);
    CHECK(fx.GetReverb().Feedback() < 1.f);
    const std::size_t n = std::size_t(kFs * 6.f);
    std::vector<float> il(n, 0.f), ir(n, 0.f), wl(n), wr(n);
    il[0] = ir[0] = 1.f;
    for(std::size_t s = 0; s < n; s += kBlock)
        CHECK(fx.ProcessReverb(il.data() + s, ir.data() + s, wl.data() + s, wr.data() + s, kBlock));
    auto energy = [&](std::size_t a, std::size_t b) {
        double e = 0;
        for(std::size_t i = a; i < b; ++i)
            e += double(wl[i]) * wl[i] + double(wr[i]) * wr[i];
        return e;
    };
    const double e1 = energy(0, std::size_t(kFs * 0.5f)), e2 = energy(std::size_t(kFs * 2.f), std::size_t(kFs * 2.5f)),
                 e3 = energy(std::size_t(kFs * 5.5f), n);
    std::printf("  reverb energy 0-0.5 s %.4g, 2-2.5 s %.4g, 5.5-6 s %.4g\n", e1, e2, e3);
    CHECK(e1 > 0.0 && e2 < e1 * 0.1 && e3 < e2 * 0.1); // decays
    double corr = 0, el = 0, er = 0;
    for(std::size_t i = 0; i < std::size_t(kFs); ++i) {
        corr += double(wl[i]) * wr[i];
        el += double(wl[i]) * wl[i];
        er += double(wr[i]) * wr[i];
    }
    CHECK(std::fabs(corr) / std::sqrt(el * er) < 0.5); // decorrelated arms
    fx.ClearTails();
    std::vector<float> z(kBlock, 0.f), zl(kBlock), zr(kBlock);
    fx.ProcessReverb(z.data(), z.data(), zl.data(), zr.data(), kBlock);
    CHECK(*std::max_element(zl.begin(), zl.end()) == 0.f);
    // The contract adapter gives the same wet output as the stage inside the component.
    static ReverbFx::Storage adapter_storage;
    ReverbFx adapter;
    CHECK(adapter.Prepare(seed::audio::Config{48000, kBlock}, adapter_storage));
    adapter.SetParameters({1.f});
    std::vector<float> al(kBlock), ar(kBlock), bl(kBlock, 0.f), br(kBlock, 0.f);
    bl[0] = br[0] = 1.f;
    CHECK(adapter.Process({bl.data(), br.data(), al.data(), ar.data(), kBlock}));
    CHECK(!adapter.Process({bl.data(), br.data(), al.data(), al.data(), kBlock})); // aliasing outputs
    StreichwurstFx& same = Fresh(p);
    std::vector<float> sl(kBlock), sr(kBlock);
    same.ProcessReverb(bl.data(), br.data(), sl.data(), sr.data(), kBlock);
    CHECK(sl == al && sr == ar);
}

static void TestInvalidAndLong() {
    StreichwurstFx fx;
    float a[kBlock] = {}, b[kBlock] = {};
    CHECK(!fx.ProcessStrings(a, a, b, nullptr, kBlock)); // not prepared
    CHECK(fx.Prepare(FxConfig{kFs, kBlock}, storage));
    CHECK(!fx.ProcessStrings(a, b, b, nullptr, kBlock)); // aliased outputs
    CHECK(!fx.ProcessStrings(a, a, b, nullptr, kBlock + 1));
    CHECK(fx.ProcessStrings(nullptr, nullptr, nullptr, nullptr, 0)); // zero frames: no-op
    StreichwurstFx bad;
    CHECK(!bad.Prepare(FxConfig{96000.f, kBlock}, storage)); // reverb storage sized for <= 48 kHz
    FxParams p;
    p.animate_amount = NAN;
    p.reverb_wet = INFINITY;
    p.ensemble_mode = 9;
    fx.SetParameters(p);
    CHECK(fx.Parameters().animate_amount == 0.f && fx.Parameters().reverb_wet == 0.2f && fx.Parameters().ensemble_mode == 2);
    // Long render with every stage active and parameter sweeps: finite, bounded.
    FxParams q;
    q.animate_amount = 1.f;
    q.phaser_amount = 1.f;
    q.reverb_wet = 1.f;
    q.reverb_decay = 1.f;
    StreichwurstFx& f = Fresh(q);
    bool finite = true;
    float peak = 0;
    std::vector<float> blk(kBlock), l(kBlock), r(kBlock), g(kBlock), wl(kBlock), wr(kBlock);
    uint32_t seed = 7;
    for(int i = 0; i < 20000; ++i) { // 20 s
        if(i % 500 == 0) {
            q.ensemble_mode = (i / 500) % 3;
            q.ensemble_on = (i / 1500) % 2 == 0;
            f.SetParameters(q);
        }
        for(float& v : blk) {
            seed = seed * 1664525u + 1013904223u;
            v = float(int32_t(seed)) / 2147483648.f * 0.3f;
        }
        f.ProcessStrings(blk.data(), l.data(), r.data(), g.data(), kBlock);
        f.ProcessReverb(l.data(), r.data(), wl.data(), wr.data(), kBlock);
        for(std::size_t k = 0; k < kBlock; ++k) {
            finite &= std::isfinite(l[k]) && std::isfinite(r[k]) && std::isfinite(wl[k]) && std::isfinite(wr[k]);
            peak = std::max(peak, std::max(std::fabs(wl[k]), std::fabs(l[k])));
        }
    }
    std::printf("  20 s noise through all stages: peak %.3f\n", peak);
    CHECK(finite);
    CHECK(peak < 8.f);
}

int main() {
    TestDelayRangesAndRates();
    TestDcSumsAndStereo();
    TestImpulseNoFeedback();
    TestBypassAndTransitions();
    TestAnimateAndPhaser();
    TestReverb();
    TestInvalidAndLong();
    if(failures) {
        std::printf("streichwurst-fx: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("streichwurst-fx: all tests passed\n");
    return 0;
}
