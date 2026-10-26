// MIT. Host tests of the Tape FX DSP against the verbatim drumlogue TapeVibe/MixForge.
// Every number printed here is a host measurement of this code, not a hardware result.
#include "../src/tape_fx.h"
#include "../src/tape_mixer.h"
#include "../src/tape_panel.h"
#include "../src/version.h"
#define private public // reach MixForge's private stages for stage-by-stage comparison
#include "reference/mixforge.h"
#undef private
#include "reference/tapevibe.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

static bool in_audio = false;
static unsigned allocations = 0;
[[gnu::noinline]] void* operator new(std::size_t n) {
    if(in_audio) ++allocations;
    if(void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using seed::fx::TapeFx;
using seed::fx::TapeMixer;
namespace tape = seed::fx::tape;
constexpr unsigned kBlock = 48;
constexpr double kRate = 48000.0;
constexpr double kTwoPi = 6.283185307179586;

// Deterministic stereo program material: two sines, a chirp and quiet noise.
struct Source {
    uint32_t seed = 1;
    unsigned n = 0;
    float level = .5f;
    float Noise() {
        seed = seed * 1103515245u + 12345u;
        return float((seed >> 9) & 0x7fff) / 16384.f - 1.f;
    }
    void Next(float& l, float& r) {
        const double t = n++ / kRate;
        l = level * float(.6 * std::sin(kTwoPi * 220.0 * t) + .25 * std::sin(kTwoPi * (300.0 + 200.0 * t) * t)) + .05f * Noise();
        r = level * float(.5 * std::sin(kTwoPi * 331.0 * t + 1.0) + .3 * std::sin(kTwoPi * 1250.0 * t)) + .05f * Noise();
    }
};

struct Fx {
    TapeFx fx;
    std::unique_ptr<TapeFx::Storage> storage = std::make_unique<TapeFx::Storage>();
    explicit Fx(const tape::Parameters& p = {}, bool reference = false, std::size_t max_block = kBlock) {
        fx.SetParameters(p);
        fx.SetReferenceMode(reference);
        const bool ok = fx.Prepare({48000, max_block}, *storage);
        assert(ok);
        (void)ok;
    }
    void Run(const float* l, const float* r, float* ol, float* orr, std::size_t n) {
        in_audio = true;
        const bool ok = fx.Process({l, r, ol, orr, n});
        in_audio = false;
        assert(ok);
        (void)ok;
    }
};

// drumlogue percent parameters, in TapeVibe's parameter order.
struct Percent {
    int drive, tone, age, mix, wow, flutter, hiss, hiss_type, dropout, drop_type;
    tape::Parameters Native() const {
        tape::Parameters p;
        p.drive = tape::Parameters::FromPercent(drive);
        p.tone = tape::Parameters::FromPercent(tone);
        p.age = tape::Parameters::FromPercent(age);
        p.wow = tape::Parameters::FromPercent(wow);
        p.flutter = tape::Parameters::FromPercent(flutter);
        p.hiss = tape::Parameters::FromPercent(hiss);
        p.dropout = tape::Parameters::FromPercent(dropout);
        p.hiss_type = tape::Character(hiss_type);
        p.drop_type = tape::Character(drop_type);
        return p;
    }
    void Apply(TapeVibe& t) const {
        const int v[10] = {drive, tone, age, mix, wow, flutter, hiss, hiss_type, dropout, drop_type};
        for(uint8_t i = 0; i < 10; ++i) t.setParameter(i, v[i]);
    }
};

const Percent kCases[] = {
    {35, 50, 40, 40, 20, 15, 20, 1, 10, 1},   // TapeVibe init
    {100, 0, 100, 100, 100, 100, 100, 2, 100, 2},
    {0, 100, 0, 100, 0, 0, 0, 0, 0, 0},
    {70, 25, 80, 65, 60, 40, 50, 0, 70, 0},
    {50, 75, 30, 100, 100, 0, 10, 2, 100, 1},
    {0, 49, 100, 0, 30, 30, 0, 1, 0, 2},
    {100, 51, 0, 100, 0, 100, 100, 1, 100, 2},
};

// Original drumlogue TapeVibe, 2-in/2-out, against TapeFx(reference mode) + TapeMixer::Mix.
// Returns the largest absolute difference between the optimized path and the original.
double CompareWithTapeVibe(const Percent& c, unsigned seconds, double* rms_out = nullptr) {
    auto original = std::make_unique<TapeVibe>();
    unit_runtime_desc_t desc;
    assert(original->Init(&desc) == k_unit_err_none);
    c.Apply(*original);
    Fx exact(c.Native(), true), fast(c.Native(), false);
    const float mix = tape::Parameters::FromPercent(c.mix);
    Source src;
    double max_fast = 0, energy = 0;
    const unsigned blocks = seconds * 1000;
    for(unsigned b = 0; b < blocks; ++b) {
        float in[2 * kBlock], out[2 * kBlock], l[kBlock], r[kBlock], el[kBlock], er[kBlock], fl[kBlock], fr[kBlock];
        for(unsigned i = 0; i < kBlock; ++i) {
            src.Next(l[i], r[i]);
            in[2 * i] = l[i];
            in[2 * i + 1] = r[i];
        }
        original->Process(in, out, kBlock);
        exact.Run(l, r, el, er, kBlock);
        fast.Run(l, r, fl, fr, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) {
            const float a = TapeMixer::Mix(l[i], el[i], mix), b2 = TapeMixer::Mix(r[i], er[i], mix);
            if(a != out[2 * i] || b2 != out[2 * i + 1]) {
                std::printf("FAIL: TapeVibe mismatch at block %u sample %u: %.9g/%.9g vs %.9g/%.9g\n",
                            b, i, a, b2, out[2 * i], out[2 * i + 1]);
                std::abort();
            }
            max_fast = std::max(max_fast, std::fabs(double(TapeMixer::Mix(l[i], fl[i], mix)) - a));
            max_fast = std::max(max_fast, std::fabs(double(TapeMixer::Mix(r[i], fr[i], mix)) - b2));
            energy += double(a) * a;
        }
    }
    if(rms_out) *rms_out = std::sqrt(energy / (blocks * kBlock));
    return max_fast;
}

void TestTapeVibeEquivalence() {
    double worst = 0;
    for(const auto& c : kCases) {
        double rms = 0;
        const double d = CompareWithTapeVibe(c, 20, &rms);
        worst = std::max(worst, d);
        std::printf("  case drive %3d tone %3d age %3d mix %3d wow %3d flutter %3d hiss %3d/%d dropout %3d/%d: "
                    "optimized max |diff| %.3g (%.1f dB re output RMS %.3f)\n",
                    c.drive, c.tone, c.age, c.mix, c.wow, c.flutter, c.hiss, c.hiss_type, c.dropout, c.drop_type,
                    d, d > 0 ? 20 * std::log10(d / rms) : -999.0, rms);
    }
    // The optimized modulators may differ by float rounding only: far below 16-bit LSB.
    assert(worst < 2e-5);
    std::printf("PASS 9: reference port bit-identical to drumlogue TapeVibe (7 settings x 20 s, 2 channels); "
                "optimized path max |diff| %.3g\n", worst);
}

void TestMixForgeStages() {
    auto mf = std::make_unique<MasterFX>();
    unit_runtime_desc_t desc;
    assert(mf->Init(&desc) == k_unit_err_none);
    // Saturation curves, sample by sample over a dense input grid.
    for(int i = -20000; i <= 20000; ++i) {
        const float x = float(i) * 0.0007f;
        assert(tape::SaturateSoft(x) == MasterFX::saturateSoft(x));
        assert(tape::SaturateHard(x) == MasterFX::saturateHard(x));
        assert(tape::SaturateTape(x) == MasterFX::saturateTape(x));
        assert(tape::SaturateFold(x) == MasterFX::saturateFold(x));
        assert(tape::DbToLin(x) == MasterFX::dbToLin(x));
    }
    // Emphasis, compressor and bitcrusher with their state.
    tape::Emphasis pre, de;
    tape::Compressor comp;
    tape::Bitcrusher crush;
    Source src;
    src.level = .9f;
    for(int threshold : {-180, -400, -60, 0}) {
        for(int ratio : {1, 4, 20}) {
            mf->Reset();
            comp.Reset();
            const auto s = tape::Compressor::Make(threshold * 0.1f, float(ratio), 10.f, 300.f);
            const float threshold_lin = MasterFX::dbToLin(threshold * 0.1f);
            const float attack = MasterFX::timeToCoef(10 * 0.001f), release = MasterFX::timeToCoef(300 * 0.001f);
            unsigned shortcuts = 0;
            for(unsigned n = 0; n < 96000; ++n) {
                float l, r;
                src.Next(l, r);
                if(n > 48000) { l *= .05f; r *= .05f; } // release phase
                float a = l, b = r, c = l, d = r;
                mf->processCompressor(a, b, threshold_lin, float(ratio), attack, release);
                comp.Process(c, d, s);
                assert(a == c && b == d);
                shortcuts += mf->comp_env_ + 1e-12f < s.below_knee;
            }
            assert(threshold == 0 ? shortcuts > 40000 : true);
        }
    }
    for(unsigned n = 0; n < 48000; ++n) {
        float l, r;
        src.Next(l, r);
        assert(pre.Pre(l) == mf->tapePreEmphasis(l, 0));
        assert(de.De(r) == mf->tapeDeEmphasis(r, 1));
    }
    for(int bits : {4, 8, 12, 16}) {
        for(int drive : {0, 13, 50, 100}) {
            mf->Reset();
            crush.Reset();
            for(unsigned n = 0; n < 9600; ++n) {
                float l, r;
                src.Next(l, r);
                float a = l, b = r, c = l, d = r;
                mf->processBitcrush(a, b, bits, drive);
                mf->postCrushLowpass(a, b);
                crush.Process(c, d, bits, 1 + drive / 13);
                assert(a == c && b == d);
            }
        }
    }
    std::puts("PASS 9: MixForge curves, tape emphasis, compressor and bitcrusher bit-identical to the drumlogue code");
}

void TestStereoSeparation() {
    tape::Parameters p;
    p.hiss = 0;
    p.dropout = 0;
    p.drive = .8f; p.age = .9f; p.wow = 1; p.flutter = 1;
    for(bool reference : {true, false}) {
        Fx a(p, reference), b(p, reference);
        Source src;
        double left_energy = 0;
        for(unsigned blk = 0; blk < 2000; ++blk) {
            float l[kBlock], r[kBlock], z[kBlock]{}, ol[kBlock], orr[kBlock], sl[kBlock], sr[kBlock];
            for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], r[i]);
            a.Run(l, z, ol, orr, kBlock); // left only
            for(unsigned i = 0; i < kBlock; ++i) {
                assert(orr[i] == 0.0f); // no crosstalk into the silent channel
                left_energy += double(ol[i]) * ol[i];
            }
            float x[kBlock], y[kBlock];
            for(unsigned i = 0; i < kBlock; ++i) { x[i] = r[i]; y[i] = l[i]; }
            b.Run(x, y, sl, sr, kBlock); // swapped program, same modulation
            for(unsigned i = 0; i < kBlock; ++i) assert(std::isfinite(sl[i]) && std::isfinite(sr[i]));
        }
        assert(left_energy > 100);
    }
    // Swap symmetry: (L, R) and (R, L) give swapped outputs bit for bit; the shared
    // wow/flutter modulation keeps the stereo image coherent.
    Fx a(p), b(p);
    Source src;
    for(unsigned blk = 0; blk < 2000; ++blk) {
        float l[kBlock], r[kBlock], al[kBlock], ar[kBlock], bl[kBlock], br[kBlock];
        for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], r[i]);
        a.Run(l, r, al, ar, kBlock);
        b.Run(r, l, bl, br, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) assert(al[i] == br[i] && ar[i] == bl[i]);
    }
    // Identical L/R input stays identical with hiss and dropouts off (mono compatible).
    Fx m(p);
    for(unsigned blk = 0; blk < 500; ++blk) {
        float l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
        for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], r[i]);
        m.Run(l, l, ol, orr, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) assert(ol[i] == orr[i]);
    }
    std::puts("PASS 1: stereo routing, zero crosstalk, exact L/R swap symmetry and mono coherence");
}

void TestMixerEndpoints() {
    TapeMixer mixer;
    mixer.Reset(0.f, false);
    Source src;
    Fx fx;
    float l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
    for(unsigned blk = 0; blk < 200; ++blk) {
        for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], r[i]);
        fx.Run(l, r, ol, orr, kBlock);
        mixer.Process(l, r, ol, orr, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) assert(ol[i] == l[i] && orr[i] == r[i]); // mix 0 = dry
    }
    mixer.SetMix(1.f);
    unsigned samples = 0;
    float previous = mixer.Current();
    while(mixer.Current() != 1.f) {
        for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], r[i]);
        fx.Run(l, r, ol, orr, kBlock);
        mixer.Process(l, r, ol, orr, kBlock);
        samples += kBlock;
        assert(mixer.Current() >= previous && samples <= 1000);
        previous = mixer.Current();
    }
    for(unsigned blk = 0; blk < 100; ++blk) {
        float wl[kBlock], wr[kBlock];
        for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], r[i]);
        fx.Run(l, r, wl, wr, kBlock);
        std::memcpy(ol, wl, sizeof ol); std::memcpy(orr, wr, sizeof orr);
        mixer.Process(l, r, ol, orr, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) { // mix 1: TapeVibe's dry + (wet - dry) * 1, then protection
            assert(ol[i] == TapeMixer::Output(TapeMixer::Mix(l[i], wl[i], 1.f)));
            assert(std::fabs(ol[i] - wl[i]) <= 1e-6f || std::fabs(wl[i]) > .95f);
        }
    }
    mixer.SetBypass(true);
    samples = 0;
    float max_step = 0, last = ol[kBlock - 1];
    while(mixer.Current() != 0.f) {
        for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], r[i]);
        fx.Run(l, r, ol, orr, kBlock);
        mixer.Process(l, r, ol, orr, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) { max_step = std::max(max_step, std::fabs(ol[i] - last)); last = ol[i]; }
        samples += kBlock;
    }
    assert(samples <= 528); // 10 ms full-scale glide, rounded up to blocks
    for(unsigned blk = 0; blk < 50; ++blk) {
        for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], r[i]);
        fx.Run(l, r, ol, orr, kBlock);
        mixer.Process(l, r, ol, orr, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) assert(ol[i] == l[i] && orr[i] == r[i]); // bypass = exact dry
    }
    assert(TapeMixer::Output(NAN) == 0 && TapeMixer::Output(1e9f) <= 1.f && TapeMixer::Output(-1e9f) >= -1.f && TapeMixer::Output(1.2f) < .995f);
    assert(TapeMixer::Mix(.3f, INFINITY, 0.f) == .3f);
    std::printf("PASS 2: mix 0 / bypass exactly dry, mix 1 = wet (TapeVibe formula), 10 ms monotonic glide "
                "(largest output step during bypass glide %.4f)\n", max_step);
}

void TestSilenceAndConstants() {
    // Silence stays exactly silent without hiss, whatever the other controls do.
    for(const auto& c : kCases) {
        tape::Parameters p = c.Native();
        p.hiss = 0;
        Fx fx(p);
        float z[kBlock]{}, ol[kBlock], orr[kBlock];
        for(unsigned blk = 0; blk < 2000; ++blk) {
            fx.Run(z, z, ol, orr, kBlock);
            for(unsigned i = 0; i < kBlock; ++i) assert(ol[i] == 0 && orr[i] == 0);
        }
    }
    // Hiss alone: level ordered Studio < Tape < Cassette, scaled by HISS and AGE.
    double rms[3];
    for(unsigned t = 0; t < 3; ++t) {
        tape::Parameters p;
        p.hiss = 1; p.age = 1; p.dropout = 0; p.hiss_type = tape::Character(t);
        Fx fx(p);
        float z[kBlock]{}, ol[kBlock], orr[kBlock];
        double e = 0, cross = 0;
        for(unsigned blk = 0; blk < 1000; ++blk) {
            fx.Run(z, z, ol, orr, kBlock);
            for(unsigned i = 0; i < kBlock; ++i) { e += double(ol[i]) * ol[i]; cross += double(ol[i]) * orr[i]; }
        }
        rms[t] = std::sqrt(e / 48000);
        assert(std::fabs(cross / e) < .05); // independent noise per channel
    }
    assert(rms[0] < rms[1] && rms[1] < rms[2] && rms[2] < .01);
    std::printf("  hiss RMS at HISS 100 %%, AGE 100 %%: Studio %.1f dBFS, Tape %.1f dBFS, Cassette %.1f dBFS\n",
                20 * std::log10(rms[0]), 20 * std::log10(rms[1]), 20 * std::log10(rms[2]));
    // DC: the chain settles to TapeVibe's static gain, equal on both channels.
    for(float dc : {.1f, .5f, -.8f}) {
        tape::Parameters p;
        p.hiss = 0; p.dropout = 0; p.wow = 1; p.flutter = 1;
        Fx fx(p);
        float in[kBlock], ol[kBlock], orr[kBlock];
        std::fill_n(in, kBlock, dc);
        for(unsigned blk = 0; blk < 3000; ++blk) fx.Run(in, in, ol, orr, kBlock);
        // Steady state: head bump lowpass = x, envelope = |y|, tone lowpass = signal.
        const double age = p.age, drive = p.drive;
        const double y = dc * (1 + (.04 + .12 * age));
        const double comp = 1 / (1 + std::fabs(y) * (.35 + .9 * age + .75 * drive));
        const double x = y * (1 + 9 * drive) * comp;
        const double sat = x * (27 + x * x) / (27 + 9 * x * x);
        const double expected = sat * (1 + .08 * drive) / (1 + .75 * drive + .2 * age);
        assert(std::fabs(ol[kBlock - 1] - expected) < 1e-4 && ol[kBlock - 1] == orr[kBlock - 1]);
    }
    std::puts("PASS 3: exact silence, hiss levels per character, DC settles to the analytic TapeVibe static gain");
}

// Goertzel amplitude of a component of x at frequency f.
double Amplitude(const std::vector<double>& x, double f) {
    double re = 0, im = 0;
    for(std::size_t n = 0; n < x.size(); ++n) {
        re += x[n] * std::cos(kTwoPi * f * n / kRate);
        im += x[n] * std::sin(kTwoPi * f * n / kRate);
    }
    return 2 * std::sqrt(re * re + im * im) / x.size();
}

void TestWowFlutter() {
    std::puts("  wow/flutter (delay modulation in samples; pitch deviation = depth * 2 pi f / 48 kHz):");
    for(float age : {0.f, .4f, 1.f}) {
        for(bool reference : {true, false}) {
            tape::Parameters p;
            p.hiss = 0; p.dropout = 0; p.age = age; p.wow = 1; p.flutter = 0;
            Fx fx(p, reference);
            const double rate = .25 + .55 * age, depth = .6 + 4.5 * age, base = 10 + 8.0 * age;
            const unsigned seconds = 40;
            std::vector<double> delay;
            float z[kBlock]{}, ol[kBlock], orr[kBlock];
            unsigned crossings = 0;
            double lo = 1e9, hi = -1e9, previous = 0, first = 0, last = 0;
            for(unsigned blk = 0; blk < seconds * 1000; ++blk) {
                for(unsigned i = 0; i < kBlock; ++i) {
                    fx.fx.Process({z, z, ol, orr, 1});
                    const double d = fx.fx.LastDelaySamples() - base;
                    if(delay.size() && previous < 0 && d >= 0) {
                        const double t = (delay.size() - d / (d - previous)) / kRate; // interpolated
                        if(!crossings++) first = t;
                        last = t;
                    }
                    previous = d;
                    lo = std::min(lo, d); hi = std::max(hi, d);
                    delay.push_back(d);
                }
            }
            const double measured_rate = (crossings - 1) / (last - first);
            // TapeVibe's float phase accumulator rounds each increment; the true rate is
            // within a few per mille of the nominal one (identical in both modes).
            assert(std::fabs(measured_rate / rate - 1) < .01);
            assert(std::fabs(hi - depth) < 1e-3 && std::fabs(lo + depth) < 1e-3);
            const double cents = 1200 * std::log2(1 + depth * kTwoPi * rate / kRate);
            if(!reference)
                std::printf("    WOW 100 %% AGE %3.0f %%: %.3f Hz (measured %.4f), depth +-%.3f samples (measured %.3f/%.3f), +-%.2f cent\n",
                            age * 100, rate, measured_rate, depth, lo, hi, cents);
        }
        for(bool reference : {true, false}) {
            tape::Parameters p;
            p.hiss = 0; p.dropout = 0; p.age = age; p.wow = 0; p.flutter = 1;
            Fx fx(p, reference);
            const double fa = 6 + 4.0 * age, fb = 9 + 5.0 * age, depth = .25 + 1.4 * age;
            std::vector<double> delay;
            float z[kBlock]{}, ol[kBlock], orr[kBlock];
            for(unsigned n = 0; n < 48000 * 5; ++n) { // whole periods of both components
                fx.fx.Process({z, z, ol, orr, 1});
                delay.push_back(fx.fx.LastDelaySamples() - (10 + 8.0 * age));
            }
            const double a = Amplitude(delay, fa), b = Amplitude(delay, fb);
            assert(std::fabs(a - .7 * depth) < 1e-2 * depth && std::fabs(b - .3 * depth) < 1e-2 * depth);
            const double cents = 1200 * std::log2(1 + depth * kTwoPi * (.7 * fa + .3 * fb) / kRate);
            if(!reference)
                std::printf("    FLUTTER 100 %% AGE %3.0f %%: %.1f Hz %.3f + %.1f Hz %.3f samples (measured %.3f + %.3f), <= +-%.2f cent\n",
                            age * 100, fa, .7 * depth, fb, .3 * depth, a, b, cents);
        }
    }
    // The optimized modulators against the reference over ten minutes: the phase drift
    // of both is float rounding; their trajectories stay within a few millisamples.
    tape::Parameters p;
    p.age = 1; p.wow = 1; p.flutter = 1; p.hiss = 0; p.dropout = 0;
    Fx exact(p, true), fast(p, false);
    float z[kBlock]{}, a[kBlock], b[kBlock];
    double worst = 0;
    for(unsigned blk = 0; blk < 600 * 1000; ++blk) {
        for(unsigned i = 0; i < kBlock; i += 8) {
            exact.fx.Process({z, z, a, b, 8});
            fast.fx.Process({z, z, a, b, 8});
            worst = std::max(worst, double(std::fabs(exact.fx.LastDelaySamples() - fast.fx.LastDelaySamples())));
        }
    }
    assert(worst < .01);
    std::printf("PASS 4: wow/flutter rates and depths as TapeVibe's formulas; optimized vs reference delay after 10 min max %.2e samples\n", worst);
}

double Thd(float drive, tape::DriveCurve curve, double* gain_db) {
    tape::Parameters p;
    p.drive = drive; p.hiss = 0; p.dropout = 0; p.wow = 0; p.flutter = 0; p.tone = .5f; p.curve = curve;
    Fx fx(p);
    std::vector<double> y, x;
    float in[kBlock], ol[kBlock], orr[kBlock];
    unsigned n = 0;
    for(unsigned blk = 0; blk < 2000; ++blk) {
        for(unsigned i = 0; i < kBlock; ++i, ++n) in[i] = float(.5 * std::sin(kTwoPi * 250.0 * n / kRate));
        fx.Run(in, in, ol, orr, kBlock);
        if(blk >= 1000) for(unsigned i = 0; i < kBlock; ++i) { y.push_back(ol[i]); x.push_back(in[i]); }
    }
    const double h1 = Amplitude(y, 250);
    double harmonics = 0;
    for(int h = 2; h <= 9; ++h) harmonics += std::pow(Amplitude(y, 250.0 * h), 2);
    *gain_db = 20 * std::log10(h1 / .5);
    return std::sqrt(harmonics) / h1;
}

void TestDrive() {
    // Static curve: odd, monotonic, unity at 3 (TapeVibe/MixForge rational tanh).
    for(int i = -1000; i < 1000; ++i) {
        const float x = i * .01f, y = tape::SaturateSoft(x), y2 = tape::SaturateSoft(x + .01f);
        assert(tape::SaturateSoft(-x) == -y && y2 >= y - 1e-6f); // f' = 9 (x^2 - 9)^2 / (27 + 9 x^2)^2 >= 0
    }
    assert(tape::SaturateSoft(3.f) == 1.f);
    std::puts("  drive curve, 250 Hz sine at -6 dBFS through the full chain (wow/flutter/hiss/dropout off):");
    const char* names[4] = {"TapeVibe", "MixForge TAPE", "MixForge HARD", "MixForge FOLD"};
    for(unsigned c = 0; c < 4; ++c) {
        double previous = -1;
        for(float drive : {0.f, .25f, .5f, .75f, 1.f}) {
            double gain = 0;
            const double thd = Thd(drive, tape::DriveCurve(c), &gain);
            std::printf("    %-14s DRIVE %3.0f %%: THD %6.2f %%, fundamental %+5.1f dB\n", names[c], drive * 100, thd * 100, gain);
            if(c == 0) {
                assert(thd > previous); // more drive, more distortion
                assert(gain > -8 && gain < 8); // TapeVibe's post gain keeps the level within a few dB
            }
            previous = thd;
        }
    }
    std::puts("PASS 5: drive/saturation characteristic monotonic, THD rises with DRIVE, level compensated");
}

void TestDropoutsDeterministic() {
    // Same parameters and input: identical output; Reset() repeats the sequence.
    tape::Parameters p;
    p.hiss = .7f; p.dropout = 1; p.drop_type = tape::Character::Cassette;
    Fx a(p), b(p);
    Source src;
    std::vector<float> first;
    for(unsigned blk = 0; blk < 1000; ++blk) {
        float l[kBlock], r[kBlock], al[kBlock], ar[kBlock], bl[kBlock], br[kBlock];
        for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], r[i]);
        a.Run(l, r, al, ar, kBlock);
        b.Run(l, r, bl, br, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) { assert(al[i] == bl[i] && ar[i] == br[i]); first.push_back(al[i]); }
    }
    a.fx.Reset();
    Source again;
    for(unsigned blk = 0; blk < 1000; ++blk) {
        float l[kBlock], r[kBlock], al[kBlock], ar[kBlock];
        for(unsigned i = 0; i < kBlock; ++i) again.Next(l[i], r[i]);
        a.Run(l, r, al, ar, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) assert(al[i] == first[blk * kBlock + i]);
    }
    // Event rate and length per character against TapeVibe's probability and lengths.
    std::puts("  dropouts at DROPOUT 100 %, AGE 40 % (60 s each):");
    const int min_len[3] = {24, 40, 64}, max_len[3] = {80, 220, 420};
    const float mult[3] = {1.f, 1.8f, 3.4f};
    for(unsigned t = 0; t < 3; ++t) {
        tape::Parameters q;
        q.hiss = 0; q.dropout = 1; q.drop_type = tape::Character(t);
        Fx fx(q);
        float in[kBlock], ol[kBlock], orr[kBlock];
        std::fill_n(in, kBlock, .25f);
        unsigned events = 0, active = 0;
        bool was = false;
        double min_gain = 1;
        for(unsigned n = 0; n < 48000 * 60; ++n) {
            fx.fx.Process({in, in, ol, orr, 1});
            const bool on = fx.fx.DropoutActive();
            events += on && !was;
            active += on;
            was = on;
            if(n > 48000) min_gain = std::min(min_gain, double(ol[0]));
        }
        const double probability = 1.0 * .00065 * mult[t] * (.6 + .9 * .4);
        const double mean_len = (min_len[t] + max_len[t]) / 2.0;
        const double expected = 60 * 48000 * probability / (1 + probability * mean_len);
        std::printf("    %-8s %4u events (expected %.0f), mean length %.1f samples (expected ~%.0f)\n",
                    t == 0 ? "Studio" : t == 1 ? "Tape" : "Cassette", events, expected, double(active) / events, mean_len);
        assert(std::fabs(events - expected) < 5 * std::sqrt(expected) + 3);
        assert(std::fabs(double(active) / events - mean_len) < .2 * mean_len);
        (void)min_gain;
    }
    std::puts("PASS 6: deterministic hiss/dropout sequences, reset repeatability, dropout rates and lengths per character");
}

void TestParameterChanges() {
    // Random jumps (including non-finite and out-of-range values) every few blocks.
    Fx fx;
    Source src;
    uint32_t r = 7;
    auto rnd = [&]() { r = r * 1664525u + 1013904223u; return float(r >> 8) / 16777216.f; };
    double max_delay_step = 0, max_out = 0;
    float last_delay = fx.fx.LastDelaySamples();
    for(unsigned blk = 0; blk < 60000; ++blk) {
        if(blk % 37 == 0) {
            tape::Parameters p;
            p.drive = rnd(); p.tone = rnd(); p.age = rnd(); p.wow = rnd(); p.flutter = rnd();
            p.hiss = rnd(); p.dropout = rnd();
            p.hiss_type = tape::Character(unsigned(rnd() * 4));
            p.drop_type = tape::Character(unsigned(rnd() * 4));
            p.curve = tape::DriveCurve(unsigned(rnd() * 5));
            p.compressor = rnd() > .5f; p.bitcrush = rnd() > .7f;
            p.threshold_db = -70 + 80 * rnd(); p.ratio = 25 * rnd(); p.bits = int(20 * rnd()); p.crush_stride = int(10 * rnd());
            p.attack_ms = 250 * rnd(); p.release_ms = 2500 * rnd();
            if(blk % 111 == 0) { p.drive = NAN; p.wow = INFINITY; p.age = -3; p.tone = 9; }
            fx.fx.SetParameters(p);
        }
        float l[kBlock], rr[kBlock], ol[kBlock], orr[kBlock];
        for(unsigned i = 0; i < kBlock; ++i) src.Next(l[i], rr[i]);
        for(unsigned i = 0; i < kBlock; i += 8) {
            fx.Run(l + i, rr + i, ol + i, orr + i, 8);
            if(blk || i) // the first block has no previous read position
                max_delay_step = std::max(max_delay_step, double(std::fabs(fx.fx.LastDelaySamples() - last_delay)));
            last_delay = fx.fx.LastDelaySamples();
        }
        for(unsigned i = 0; i < kBlock; ++i) {
            assert(std::isfinite(ol[i]) && std::isfinite(orr[i]));
            max_out = std::max(max_out, double(std::max(std::fabs(ol[i]), std::fabs(orr[i]))));
        }
    }
    // Glide: the read position moves by far less than a sample per 8 samples (no zipper/clicks).
    std::printf("  random parameter run: read-position step %.4f, peak %.3f\n", max_delay_step, max_out);
    assert(max_delay_step < .25 && max_out < 2.5);
    // A Drive jump 0 -> 100 % on a sine: the output never jumps more than the steady
    // 100 % output does from one sample to the next (plus a small margin).
    tape::Parameters p;
    p.drive = 0; p.hiss = 0; p.dropout = 0; p.wow = 0; p.flutter = 0;
    Fx d(p);
    float in[kBlock], ol[kBlock], orr[kBlock];
    unsigned n = 0;
    double steady = 0, jump = 0;
    float last = 0;
    for(unsigned blk = 0; blk < 600; ++blk) {
        if(blk == 300) { p.drive = 1; d.fx.SetParameters(p); }
        for(unsigned i = 0; i < kBlock; ++i, ++n) in[i] = float(.5 * std::sin(kTwoPi * 100.0 * n / kRate));
        d.Run(in, in, ol, orr, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) {
            const double s = std::fabs(ol[i] - last);
            last = ol[i];
            if(blk >= 300 && blk < 340) jump = std::max(jump, s);
            if(blk >= 500) steady = std::max(steady, s);
        }
    }
    assert(jump < 1.5 * steady + 1e-3);
    std::printf("PASS 7: 1600 random/invalid parameter sets finite (peak %.2f), read-position step <= %.4f samples per 8 samples, "
                "drive jump step %.4f vs steady %.4f\n", max_out, max_delay_step, jump, steady);
}

void TestBuffersResetLongRun() {
    // Contract checks.
    TapeFx fx;
    auto storage = std::make_unique<TapeFx::Storage>();
    float l[kBlock]{}, r[kBlock]{}, ol[kBlock], orr[kBlock];
    ol[0] = .7f;
    assert(!fx.Process({l, r, ol, orr, kBlock}) && ol[0] == .7f); // before Prepare
    assert(!fx.Prepare({44100, kBlock}, *storage) && !fx.Prepare({48000, 0}, *storage));
    assert(fx.Prepare({48000, kBlock}, *storage));
    assert(!fx.Process({l, r, ol, orr, kBlock + 1}) && ol[0] == .7f);
    assert(!fx.Process({nullptr, r, ol, orr, kBlock}) && !fx.Process({l, r, ol, ol, kBlock}));
    assert(fx.Process({nullptr, nullptr, nullptr, nullptr, 0}));
    // In place per channel.
    {
        Fx a, b;
        Source s1, s2;
        for(unsigned blk = 0; blk < 200; ++blk) {
            float x[kBlock], y[kBlock], u[kBlock], v[kBlock], ox[kBlock], oy[kBlock];
            for(unsigned i = 0; i < kBlock; ++i) { s1.Next(x[i], y[i]); s2.Next(u[i], v[i]); }
            a.Run(x, y, ox, oy, kBlock);
            b.Run(u, v, u, v, kBlock);
            for(unsigned i = 0; i < kBlock; ++i) assert(ox[i] == u[i] && oy[i] == v[i]);
        }
    }
    // Block-size independence of the reference path; the optimized path stays within rounding.
    for(bool reference : {true, false}) {
        Fx a({}, reference), b({}, reference);
        Source s1, s2;
        uint32_t seed = 3;
        double worst = 0;
        std::vector<float> ya, yb;
        for(unsigned blk = 0; blk < 4000; ++blk) {
            float x[kBlock], y[kBlock], ox[kBlock], oy[kBlock];
            for(unsigned i = 0; i < kBlock; ++i) s1.Next(x[i], y[i]);
            a.Run(x, y, ox, oy, kBlock);
            ya.insert(ya.end(), ox, ox + kBlock);
        }
        std::size_t done = 0;
        while(done < ya.size()) {
            seed = seed * 1664525u + 1013904223u;
            const std::size_t n = std::min<std::size_t>(1 + (seed >> 8) % kBlock, ya.size() - done);
            float x[kBlock], y[kBlock], ox[kBlock], oy[kBlock];
            for(unsigned i = 0; i < n; ++i) s2.Next(x[i], y[i]);
            b.Run(x, y, ox, oy, n);
            yb.insert(yb.end(), ox, ox + n);
            done += n;
        }
        for(std::size_t i = 0; i < ya.size(); ++i) worst = std::max(worst, double(std::fabs(ya[i] - yb[i])));
        assert(reference ? worst == 0 : worst < 1e-5);
    }
    // Long run at extreme settings: ten minutes, the index wraps ~14000 times.
    tape::Parameters p;
    p.drive = 1; p.age = 1; p.wow = 1; p.flutter = 1; p.hiss = 1; p.dropout = 1;
    p.hiss_type = p.drop_type = tape::Character::Cassette;
    p.compressor = true; p.bitcrush = true; p.bits = 6; p.crush_stride = 4; p.curve = tape::DriveCurve::Fold;
    Fx loud(p);
    Source src;
    src.level = 1.5f; // hot input beyond full scale
    double peak = 0;
    for(unsigned blk = 0; blk < 600 * 1000; ++blk) {
        float x[kBlock], y[kBlock], ox[kBlock], oy[kBlock];
        for(unsigned i = 0; i < kBlock; ++i) src.Next(x[i], y[i]);
        loud.Run(x, y, ox, oy, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) {
            assert(std::isfinite(ox[i]) && std::isfinite(oy[i]));
            peak = std::max(peak, double(std::fabs(ox[i])));
        }
    }
    assert(peak < 2.5);
    // A non-finite input never stays in the feedback state.
    float bad[kBlock];
    std::fill_n(bad, kBlock, NAN);
    loud.Run(bad, bad, ol, orr, kBlock);
    float z[kBlock]{};
    loud.Run(z, z, ol, orr, kBlock);
    for(unsigned blk = 0; blk < 10; ++blk) {
        loud.Run(z, z, ol, orr, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) assert(std::isfinite(ol[i]) && std::isfinite(orr[i]));
    }
    // Reset reproduces a fresh instance exactly.
    Fx fresh(p);
    loud.fx.Reset();
    Source s1, s2;
    for(unsigned blk = 0; blk < 500; ++blk) {
        float x[kBlock], y[kBlock], ax[kBlock], ay[kBlock], bx[kBlock], by[kBlock];
        for(unsigned i = 0; i < kBlock; ++i) { s1.Next(x[i], y[i]); }
        loud.Run(x, y, ax, ay, kBlock);
        fresh.Run(x, y, bx, by, kBlock);
        for(unsigned i = 0; i < kBlock; ++i) assert(ax[i] == bx[i] && ay[i] == by[i]);
    }
    (void)s2;
    std::printf("PASS 8: contract bounds, in-place, block-size independence, 10 min extreme run (peak %.2f), NaN recovery, exact reset\n", peak);
}

void TestPanel() {
    seed::fx::TapePanel panel;
    panel.Reset();
    std::array<float, 8> pots{};
    pots.fill(.5f);
    assert(panel.Update(pots, false, seed::fx::TapePanel::kSwitchCenter));
    for(unsigned i = 0; i < 8; ++i) assert(panel.Percent(i) == 50);
    pots[0] = .504f; // ADC jitter within the hysteresis
    assert(!panel.Update(pots, false, 0));
    pots[0] = .508f;
    assert(panel.Update(pots, false, 0) && panel.Percent(0) == 51);
    pots[0] = .002f; pots[4] = .998f;
    panel.Update(pots, false, 0);
    assert(panel.Percent(0) == 0 && panel.Percent(4) == 100);
    const auto p = panel.Apply({});
    assert(p.drive == 0.f && p.wow == 1.f && p.tone == .5f && panel.Mix() == .5f);
    assert(panel.Update(pots, true, 0) && panel.Bypass());
    for(unsigned t = 0; t < 14; ++t) assert(!panel.Update(pots, true, seed::fx::TapePanel::kSwitchUp));
    assert(panel.Character() == tape::Character::Tape);
    assert(panel.Update(pots, true, seed::fx::TapePanel::kSwitchUp));
    assert(panel.Character() == tape::Character::Studio && panel.Apply({}).hiss_type == tape::Character::Studio);
    for(unsigned t = 0; t < 15; ++t) panel.Update(pots, true, seed::fx::TapePanel::kSwitchDown);
    assert(panel.Character() == tape::Character::Cassette && panel.Apply({}).drop_type == tape::Character::Cassette);
    std::puts("PASS: test panel (integer percent with hysteresis, endpoints, bypass switch, 15 ms character switch)");
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Tape FX %s host tests\n", TAPE_FX_VERSION);
    TestTapeVibeEquivalence();
    TestMixForgeStages();
    TestStereoSeparation();
    TestMixerEndpoints();
    TestSilenceAndConstants();
    TestWowFlutter();
    TestDrive();
    TestDropoutsDeterministic();
    TestParameterChanges();
    TestBuffersResetLongRun();
    TestPanel();
    assert(allocations == 0);
    std::puts("PASS Tape FX: no allocation in Process()");
}
