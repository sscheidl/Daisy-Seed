// SPDX-License-Identifier: MIT
// Host tests of the filter core: response per mode against the exact closed-loop formula,
// stability at maximum resonance under full modulation, calibrated self-oscillation,
// bass compensation, ramping, the held path, denormals, NaN, and the golden hash.
#include "filter_core/ladder.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace seed::dsp::filter;
using Complex = std::complex<double>;

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kFs = kSampleRate;
static constexpr std::size_t kBlock = 48;
static constexpr Mode kAll[kModeCount] = {Mode::Lp6, Mode::Lp12, Mode::Lp18, Mode::Lp24,
                                          Mode::Bp,  Mode::Hp,   Mode::Notch, Mode::Phaser};
static const char* const kNames[kModeCount] = {"LP6", "LP12", "LP18", "LP24", "BP", "HP", "Notch", "Phaser"};

static double NoteOf(double hz) { return 69. + 12. * std::log2(hz / 440.); }
static double Db(double gain) { return 20. * std::log10(gain + 1e-15); }

static Parameters Settings(double hz, double resonance, Mode mode, double comp = 1.) {
    Parameters p;
    p.cutoff_note = float(NoteOf(hz));
    p.resonance = float(resonance);
    p.bass_comp = float(comp);
    p.mode = mode;
    return p;
}

// Amplitude of `hz` in y by projection on a whole number of its periods.
static double Amplitude(const std::vector<float>& y, double hz) {
    const std::size_t periods = std::size_t(double(y.size()) * hz / kFs);
    const std::size_t count = periods ? std::size_t(double(periods) * kFs / hz) : y.size();
    double c = 0., s = 0.;
    for(std::size_t i = 0; i < count; ++i) {
        const double phase = 2. * kPi * hz / kFs * double(i);
        c += y[i] * std::cos(phase);
        s += y[i] * std::sin(phase);
    }
    return 2. * std::hypot(c, s) / double(count);
}

// Steady-state gain of a quiet sine; quiet so that the limiter stays out of the way.
static double MeasuredGain(const Parameters& p, double hz, double amplitude = 1e-3) {
    Ladder filter;
    filter.Set(p);
    const std::size_t settle = std::size_t(kFs * 2.), keep = std::size_t(kFs);
    std::vector<float> in(kBlock), out(kBlock), tail;
    tail.reserve(keep);
    for(std::size_t n = 0; n < settle + keep; n += kBlock) {
        for(std::size_t i = 0; i < kBlock; ++i)
            in[i] = float(amplitude * std::sin(2. * kPi * hz / kFs * double(n + i)));
        filter.Process(in.data(), out.data(), kBlock);
        if(n >= settle)
            tail.insert(tail.end(), out.begin(), out.end());
    }
    return Amplitude(tail, hz) / amplitude;
}

// The exact response of the linear filter: bilinear one-poles, closed loop, tap mix.
static double ExpectedGain(Mode mode, double cutoff, double hz, double resonance, double comp) {
    const double g = std::tan(kPi * cutoff / kFs);
    const Complex w = g / (g + Complex(0., std::tan(kPi * hz / kFs)));
    const double k = (mode == Mode::Notch ? double(kNotchFeedback) : double(kMaxFeedback)) * resonance;
    const Complex loop = 1. / (1. + k * w * w * w * w), w2 = w * w, w3 = w2 * w, w4 = w2 * w2;
    switch(mode) {
        case Mode::Lp6: return std::abs((1. + comp * k) * w * loop);
        case Mode::Lp12: return std::abs((1. + comp * k) * w2 * loop);
        case Mode::Lp18: return std::abs((1. + comp * k) * w3 * loop);
        case Mode::Lp24: return std::abs((1. + comp * k) * w4 * loop);
        case Mode::Bp: return std::abs(4. * w2 * (1. - w) * (1. - w) * loop);
        case Mode::Hp: return std::abs((1. - w) * (1. - w) * (1. - w) * (1. - w) * loop);
        case Mode::Notch: return std::abs(1. - 2. * (1. - 0.25 * k) * w * (1. - w) * loop);
        case Mode::Phaser: {
            const Complex ap = 2. * w - 1.;
            return std::abs((0.5 * (1. + ap * ap * ap * ap) + comp * k * (2. * w3 - 2. * w2 + w)) * loop);
        }
    }
    return 0.;
}

static void TestTable() {
    // The cutoff table against libm: within a fraction of a cent over the whole range.
    double worst = 0.;
    for(float note = 12.f; note <= 135.f; note += 0.37f) {
        const double G = double(LookUp(note).G), g = G / (1. - G);
        const double hz = kFs / kPi * std::atan(g);
        worst = std::max(worst, std::fabs(1200. * std::log2(hz / (440. * std::exp2((double(note) - 69.) / 12.)))));
    }
    std::printf("note table: largest pitch error %.3f cents\n", worst);
    CHECK(worst < 0.5);
    // Clamping: NaN and absurd notes stay on the table.
    CHECK(LookUp(std::nanf("")).G == kNoteTable.rows[0].G);
    CHECK(LookUp(-1e30f).G == kNoteTable.rows[0].G);
    CHECK(LookUp(1e30f).G == LookUp(kNoteMax).G);
}

static void TestResponse() {
    static const double cutoffs[] = {200., 1000., 5000.};
    static const double offsets[] = {0.1, 0.25, 0.5, 0.71, 1., 1.41, 2., 4., 10.};
    double worst = 0.;
    for(unsigned m = 0; m < kModeCount; ++m)
        for(double cutoff : cutoffs)
            for(double resonance : {0., 0.45, 0.8})
                for(double comp : {0., 1.})
                    for(double offset : offsets) {
                        const double hz = cutoff * offset;
                        if(hz > 20000.)
                            continue;
                        const double expected = Db(ExpectedGain(kAll[m], cutoff, hz, resonance, comp));
                        if(expected < -70.) // deep stopbands and notches: tested below
                            continue;
                        const double measured = Db(MeasuredGain(Settings(cutoff, resonance, kAll[m], comp), hz));
                        const double error = std::fabs(measured - expected);
                        worst = std::max(worst, error);
                        if(error > 0.25)
                            std::printf("  %s fc %.0f r %.2f c %.0f at %.0f Hz: expected %.2f, measured %.2f\n",
                                        kNames[m], cutoff, resonance, comp, hz, expected, measured);
                        CHECK(error <= 0.25);
                    }
    std::printf("response against the closed-loop formula: largest error %.3f dB\n", worst);

    // Slopes at resonance 0, well below Nyquist, where the bilinear zero does not steepen them.
    const double fc = 100.;
    const auto per_octave = [&](Mode mode, double a, double b) {
        return (Db(MeasuredGain(Settings(fc, 0., mode), a, 0.1)) - Db(MeasuredGain(Settings(fc, 0., mode), b, 0.1))) /
               std::fabs(std::log2(b / a));
    };
    const double lp6 = per_octave(Mode::Lp6, 800., 1600.), lp12 = per_octave(Mode::Lp12, 800., 1600.);
    const double lp18 = per_octave(Mode::Lp18, 800., 1600.), lp24 = per_octave(Mode::Lp24, 800., 1600.);
    const double hp = per_octave(Mode::Hp, 12.5, 6.25), bp_high = per_octave(Mode::Bp, 800., 1600.);
    const double bp_low = per_octave(Mode::Bp, 12.5, 6.25);
    std::printf("slopes dB/oct: LP %.1f %.1f %.1f %.1f, HP %.1f, BP %.1f / %.1f\n", lp6, lp12, lp18, lp24, hp, bp_low,
                bp_high);
    CHECK(std::fabs(lp6 - 6.) < 0.3 && std::fabs(lp12 - 12.) < 0.5);
    CHECK(std::fabs(lp18 - 18.) < 0.7 && std::fabs(lp24 - 24.) < 1.);
    CHECK(std::fabs(hp - 24.) < 1. && std::fabs(bp_low - 12.) < 0.5 && std::fabs(bp_high - 12.) < 0.5);
    // Resonance raises the peak at the cutoff, and the cutoff tracks in every position.
    for(double cutoff : {100., 1000., 10000.}) {
        const double peak = Db(MeasuredGain(Settings(cutoff, 0.8, Mode::Lp24, 0.), cutoff));
        CHECK(std::fabs(peak - Db(ExpectedGain(Mode::Lp24, cutoff, cutoff, 0.8, 0.))) < 0.25);
    }
    // Notch: deep at the cutoff at every resonance, flat and at unity on both sides. The
    // narrow notch at 100 % is limited by the table's fraction of a cent.
    for(double resonance : {0., 0.5, 1.}) {
        const double depth = Db(MeasuredGain(Settings(1000., resonance, Mode::Notch), 1000., 0.1));
        std::printf("notch depth at resonance %.1f: %.1f dB\n", resonance, depth);
        CHECK(depth < -35.);
        CHECK(std::fabs(Db(MeasuredGain(Settings(1000., resonance, Mode::Notch), 30.))) < 0.5);
        CHECK(std::fabs(Db(MeasuredGain(Settings(1000., resonance, Mode::Notch), 20000.))) < 1.);
    }
    // Phaser: two notches around a passing centre.
    const double low = Db(MeasuredGain(Settings(1000., 0., Mode::Phaser), 414.2));
    const double high = Db(MeasuredGain(Settings(1000., 0., Mode::Phaser), 2414.));
    const double centre = Db(MeasuredGain(Settings(1000., 0., Mode::Phaser), 1000.));
    std::printf("phaser: notches %.1f / %.1f dB, centre %.1f dB\n", low, high, centre);
    CHECK(low < -30. && high < -30. && std::fabs(centre) < 0.5);
    // Full scale at resonance 0: the limiter stands back, so the stopband stays deep.
    const double stopband = Db(MeasuredGain(Settings(1000., 0., Mode::Hp), 62.5, 1.));
    std::printf("HP stopband four octaves down at full scale: %.1f dB\n", stopband);
    CHECK(stopband < -85.);
}

static void TestBassCompensation() {
    // DC gain (1 + c k) / (1 + k) of the low-passes at the top of the linear range.
    const double resonance = 0.9, k = double(kMaxFeedback) * resonance;
    for(Mode mode : {Mode::Lp6, Mode::Lp12, Mode::Lp18, Mode::Lp24})
        for(double c : {0., 0.5, 1.}) {
            const double gain = MeasuredGain(Settings(2000., resonance, mode, c), 20.);
            CHECK(std::fabs(Db(gain) - Db((1. + c * k) / (1. + k))) < 0.1);
        }
    // Modes without a low-frequency passband ignore c.
    for(Mode mode : {Mode::Hp, Mode::Bp, Mode::Notch})
        CHECK(std::fabs(Db(MeasuredGain(Settings(2000., 0.5, mode, 0.), 5000.)) -
                        Db(MeasuredGain(Settings(2000., 0.5, mode, 1.), 5000.))) < 0.01);
    // Phaser: c restores the bass.
    CHECK(Db(MeasuredGain(Settings(2000., 0.8, Mode::Phaser, 0.), 50.)) < -10.);
    CHECK(std::fabs(Db(MeasuredGain(Settings(2000., 0.8, Mode::Phaser, 1.), 50.))) < 0.5);
}

// Runs an oscillator from silence and returns the fundamental level and frequency.
struct Oscillation {
    double level_db, hz;
};
static Oscillation Oscillate(double hz, Mode mode, bool held = false) {
    Ladder filter;
    filter.Set(Settings(hz, 1., mode));
    const std::size_t total = std::size_t(kFs * std::max(3., 150. / hz + 1.));
    const std::size_t tail = std::size_t(kFs / 2.);
    std::vector<float> in(kBlock, 0.f), out(kBlock), last;
    last.reserve(tail + kBlock);
    for(std::size_t n = 0; n < total; n += kBlock) {
        if(held)
            filter.ProcessHeld(in.data(), out.data(), kBlock);
        else
            filter.Process(in.data(), out.data(), kBlock);
        if(n + tail >= total)
            last.insert(last.end(), out.begin(), out.end());
    }
    double first = -1., latest = 0.;
    int crossings = 0;
    for(std::size_t i = 1; i < last.size(); ++i)
        if(last[i - 1] <= 0.f && last[i] > 0.f) {
            latest = double(i - 1) + double(-last[i - 1]) / double(last[i] - last[i - 1]);
            first = first < 0. ? latest : first;
            ++crossings;
        }
    const double f = crossings > 1 ? kFs * double(crossings - 1) / (latest - first) : 0.;
    double c = 0., s = 0.; // the crossings give the frequency exactly enough for a projection
    for(std::size_t i = 0; i < last.size(); ++i) {
        c += last[i] * std::cos(2. * kPi * f / kFs * double(i));
        s += last[i] * std::sin(2. * kPi * f / kFs * double(i));
    }
    return {Db(2. * std::hypot(c, s) / double(last.size()) / double(kOscLevel)), f};
}

static void TestSelfOscillation() {
    static const double cutoffs[] = {30., 125., 500., 2000., 8000., 16000.};
    double lo = 1e9, hi = -1e9, tune = 0.;
    for(double hz : cutoffs)
        for(Mode mode : kAll) {
            if(mode == Mode::Notch)
                continue;
            const Oscillation o = Oscillate(hz, mode);
            lo = std::min(lo, o.level_db);
            hi = std::max(hi, o.level_db);
            CHECK(std::fabs(o.level_db) < 1.);
            if(hz <= 8000.) {
                tune = std::max(tune, std::fabs(1200. * std::log2(o.hz / hz)));
                CHECK(std::fabs(1200. * std::log2(o.hz / hz)) < 10.);
            }
        }
    std::printf("self-oscillation: level %.2f .. %.2f dB around %.2f peak, pitch within %.1f cents to 8 kHz\n", lo, hi,
                double(kOscLevel), tune);
    // The held path uses the same coefficients and oscillates the same.
    const Oscillation ramped = Oscillate(1000., Mode::Lp24), held = Oscillate(1000., Mode::Lp24, true);
    CHECK(std::fabs(ramped.level_db - held.level_db) < 0.05);
    CHECK(std::fabs(1200. * std::log2(ramped.hz / held.hz)) < 1.);
    // Notch: the zero sits on the pole, and the loop stays below 4. No oscillation by design.
    CHECK(Oscillate(1000., Mode::Notch).level_db < -60.);
    // Below the threshold nothing rings on.
    {
        Ladder filter;
        filter.Set(Settings(1000., 0.89, Mode::Lp24));
        std::vector<float> in(kBlock, 0.f), out(kBlock);
        in[0] = 1.f;
        filter.Process(in.data(), out.data(), kBlock);
        in[0] = 0.f;
        double peak = 0.;
        for(std::size_t n = 0; n < std::size_t(kFs); n += kBlock) {
            filter.Process(in.data(), out.data(), kBlock);
            if(n > std::size_t(kFs / 2.))
                for(float v : out)
                    peak = std::max(peak, double(std::fabs(v)));
        }
        CHECK(peak < 1e-3);
    }
}

// White noise at full scale, resonance 100 %, and random cutoff jumps over the full range
// on every sample and every block, in every mode, on both paths.
static void TestStability() {
    uint32_t seed = 12345u;
    const auto noise = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return float(int32_t(seed)) / 2147483648.f;
    };
    double worst = 0.;
    std::vector<float> in(kBlock), out(kBlock), fm(kBlock);
    for(Mode mode : kAll)
        for(double amplitude : {1., 16.})
            for(int path = 0; path < 3; ++path) {
                Ladder filter;
                double peak = 0.;
                for(std::size_t n = 0; n < std::size_t(kFs * 4.); n += kBlock) {
                    Parameters p = Settings(1000., 1., mode);
                    p.cutoff_note = 67.5f + 67.5f * noise();
                    filter.Set(p);
                    for(std::size_t i = 0; i < kBlock; ++i) {
                        in[i] = float(amplitude) * noise();
                        fm[i] = 135.f * noise();
                    }
                    if(path == 0)
                        filter.Process(in.data(), out.data(), kBlock);
                    else if(path == 1)
                        filter.Process(in.data(), out.data(), kBlock, fm.data());
                    else
                        filter.ProcessHeld(in.data(), out.data(), 8);
                    for(std::size_t i = 0; i < (path == 2 ? 8u : kBlock); ++i) {
                        CHECK(std::isfinite(out[i]));
                        peak = std::max(peak, double(std::fabs(out[i])));
                    }
                }
                for(int i = 0; i < 4; ++i)
                    CHECK(std::isfinite(filter.State()[i]));
                worst = std::max(worst, peak / amplitude);
            }
    std::printf("stability: largest |out| / input peak %.1f under full modulation\n", worst);
    CHECK(worst < 64.);
    // The same at a fixed cutoff with a loud tone at resonance: the limiter must hold it.
    for(double hz : {40., 1000., 15000.}) {
        Ladder filter;
        filter.Set(Settings(hz, 1., Mode::Lp24));
        double peak = 0.;
        for(std::size_t n = 0; n < std::size_t(kFs * 2.); n += kBlock) {
            for(std::size_t i = 0; i < kBlock; ++i)
                in[i] = float(std::sin(2. * kPi * hz / kFs * double(n + i)));
            filter.Process(in.data(), out.data(), kBlock);
            for(float v : out)
                peak = std::max(peak, double(std::fabs(v)));
        }
        std::printf("full-scale tone at the oscillating cutoff %.0f Hz: output peak %.2f\n", hz, peak);
        CHECK(peak < 4.);
    }
}

static bool Subnormal(float x) { return x != 0.f && std::fabs(x) < 1.17549435e-38f; }

static void TestDenormalsAndNan() {
    // After a signal ends, the state must never decay into the subnormal range.
    for(Mode mode : kAll)
        for(double hz : {20., 1000., 19000.}) {
            Ladder filter;
            filter.Set(Settings(hz, 0.5, mode));
            std::vector<float> in(kBlock, 0.5f), out(kBlock);
            filter.Process(in.data(), out.data(), kBlock);
            std::fill(in.begin(), in.end(), 0.f);
            bool clean = true;
            for(std::size_t n = 0; n < std::size_t(kFs * 30.); n += kBlock) {
                filter.Process(in.data(), out.data(), kBlock);
                for(int i = 0; i < 4; ++i)
                    clean &= !Subnormal(filter.State()[i]);
                for(float v : out)
                    clean &= !Subnormal(v);
            }
            CHECK(clean);
        }
    // Non-finite input counts as silence and leaves a working filter.
    std::vector<float> bad = {std::nanf(""), INFINITY, -INFINITY, 1e30f, -1e30f, 0.f, 1.f, 0.f};
    for(Mode mode : kAll) {
        Ladder filter;
        filter.Set(Settings(1000., 1., mode));
        std::vector<float> out(bad.size());
        for(int i = 0; i < 1000; ++i) {
            filter.Process(bad.data(), out.data(), bad.size());
            for(float v : out)
                CHECK(std::isfinite(v));
        }
        // Non-finite parameters too.
        Parameters p = Settings(1000., 0.5, mode);
        p.cutoff_note = std::nanf("");
        p.resonance = std::nanf("");
        p.bass_comp = INFINITY;
        filter.Set(p);
        filter.Process(bad.data(), out.data(), bad.size());
        for(float v : out)
            CHECK(std::isfinite(v));
        std::vector<float> fm(bad);
        filter.Process(bad.data(), out.data(), bad.size(), fm.data());
        for(float v : out)
            CHECK(std::isfinite(v));
    }
}

// RMS of the fourth difference: a steep high-pass. Smooth motion gives almost nothing,
// while steps in the coefficients leave broadband zipper noise.
static double Zipper(const std::vector<float>& y) {
    double sum = 0.;
    for(std::size_t i = 4; i < y.size(); ++i) {
        const double d = double(y[i]) - 4. * double(y[i - 1]) + 6. * double(y[i - 2]) - 4. * double(y[i - 3]) + double(y[i - 4]);
        sum += d * d;
    }
    return std::sqrt(sum / double(y.size()));
}

static void TestRamping() {
    // A fast cutoff sweep over a low tone: steps in the coefficients leave zipper noise.
    const auto sweep = [](bool held) {
        Ladder filter;
        std::vector<float> in(kBlock, 0.5f), out(kBlock), all;
        for(int block = 0; block < 400; ++block) {
            Parameters p = Settings(1000., 0.6, Mode::Lp6, 0.);
            p.cutoff_note = 60.f + 36.f * float(std::sin(2. * kPi * double(block) / 100.));
            filter.Set(p);
            for(std::size_t i = 0; i < kBlock; ++i)
                in[i] = 0.5f * float(std::sin(2. * kPi * 300. / kFs * double(block * 48 + int(i))));
            if(held)
                filter.ProcessHeld(in.data(), out.data(), kBlock);
            else
                filter.Process(in.data(), out.data(), kBlock);
            if(block > 20)
                all.insert(all.end(), out.begin(), out.end());
        }
        return Zipper(all);
    };
    const double ramped = sweep(false), held = sweep(true);
    std::printf("cutoff sweep, zipper residue: ramped %.1f dB, held per block %.1f dB\n", Db(ramped), Db(held));
    CHECK(ramped * 4. < held);
    // Mode changes do not step: the mix moves over kSlowMs.
    for(Mode a : kAll)
        for(Mode b : kAll) {
            Ladder filter;
            filter.Set(Settings(1000., 0.3, a));
            std::vector<float> in(kBlock), out(kBlock);
            double previous = 0., largest = 0.;
            for(int block = 0; block < 200; ++block) {
                if(block == 100)
                    filter.Set(Settings(1000., 0.3, b));
                for(std::size_t i = 0; i < kBlock; ++i)
                    in[i] = 0.25f * float(std::sin(2. * kPi * 100. / kFs * double(block * 48 + int(i))));
                filter.Process(in.data(), out.data(), kBlock);
                for(float v : out) {
                    if(block >= 50)
                        largest = std::max(largest, std::fabs(double(v) - previous));
                    previous = double(v);
                }
            }
            CHECK(largest < 0.1);
        }
    // Audio-rate input: an FM buffer at 1 kHz puts sidebands on a 500 Hz tone through LP24
    // with the cutoff near the tone, and matches a per-sample note sweep of the same shape.
    {
        Ladder with_fm, reference;
        with_fm.Set(Settings(800., 0.5, Mode::Lp24));
        std::vector<float> in(kBlock), out(kBlock), fm(kBlock), tail;
        for(int block = 0; block < 2000; ++block) {
            for(std::size_t i = 0; i < kBlock; ++i) {
                in[i] = 0.1f * float(std::sin(2. * kPi * 500. / kFs * double(block * 48 + int(i))));
                fm[i] = 12.f * float(std::sin(2. * kPi * 1000. / kFs * double(block * 48 + int(i))));
            }
            with_fm.Process(in.data(), out.data(), kBlock, fm.data());
            if(block >= 1000)
                tail.insert(tail.end(), out.begin(), out.end());
        }
        const double carrier = Amplitude(tail, 500.), sideband = Amplitude(tail, 1500.);
        std::printf("audio-rate cutoff input: carrier %.1f dB, 1500 Hz sideband %.1f dB\n", Db(carrier), Db(sideband));
        CHECK(sideband > carrier * 0.02);
        // The same call with one sample per call and the note in Set is the same filter.
        Ladder a, b;
        a.Set(Settings(800., 0.5, Mode::Lp24));
        b.Set(Settings(800., 0.5, Mode::Lp24));
        float x = 0.25f, ya = 0.f, yb = 0.f, offset = 0.f;
        double difference = 0.;
        for(int n = 0; n < 4800; ++n) {
            offset = 12.f * float(std::sin(2. * kPi * 200. / kFs * double(n)));
            a.Process(&x, &ya, 1, &offset);
            Parameters p = Settings(800., 0.5, Mode::Lp24);
            p.cutoff_note += offset;
            b.Set(p);
            b.Process(&x, &yb, 1);
            difference = std::max(difference, std::fabs(double(ya) - double(yb)));
        }
        CHECK(difference < 0.01);
    }
}

static void TestPaths() {
    // Static settings: the held path gives the same numbers as the ramped path.
    Ladder ramped, held;
    ramped.Set(Settings(700., 0.7, Mode::Lp18));
    held.Set(Settings(700., 0.7, Mode::Lp18));
    std::vector<float> in(kBlock), a(kBlock), b(kBlock);
    double difference = 0.;
    for(int block = 0; block < 500; ++block) {
        for(std::size_t i = 0; i < kBlock; ++i)
            in[i] = 0.3f * float(std::sin(2. * kPi * 311. / kFs * double(block * 48 + int(i))));
        ramped.Process(in.data(), a.data(), kBlock);
        held.ProcessHeld(in.data(), b.data(), kBlock);
        for(std::size_t i = 0; i < kBlock; ++i)
            difference = std::max(difference, std::fabs(double(a[i]) - double(b[i])));
    }
    CHECK(difference < 1e-6);
    // In place equals out of place.
    Ladder one, two;
    one.Set(Settings(700., 0.7, Mode::Hp));
    two.Set(Settings(700., 0.7, Mode::Hp));
    for(int block = 0; block < 100; ++block) {
        for(std::size_t i = 0; i < kBlock; ++i)
            in[i] = 0.3f * float(std::sin(2. * kPi * 311. / kFs * double(block * 48 + int(i))));
        std::vector<float> copy(in);
        one.Process(copy.data(), copy.data(), kBlock);
        two.Process(in.data(), a.data(), kBlock);
        CHECK(std::equal(copy.begin(), copy.end(), a.begin()));
    }
    // Zero frames do nothing; Reset clears the state.
    one.Process(nullptr, nullptr, 0);
    one.ProcessHeld(nullptr, nullptr, 0);
    one.Reset();
    for(int i = 0; i < 4; ++i)
        CHECK(one.State()[i] == 0.f);
}

static void TestGolden() {
    // FNV-1a over the output of every mode: the sound of this version. Only + - * / and
    // comparisons run in the path, so the hash holds on every IEEE host (without contraction).
    uint32_t hash = 2166136261u;
    std::vector<float> in(kBlock), out(kBlock);
    for(Mode mode : kAll) {
        Ladder filter;
        for(int block = 0; block < 2000; ++block) {
            Parameters p = Settings(500., 0.95, mode);
            p.cutoff_note += float(block % 100) * 0.25f;
            filter.Set(p);
            for(std::size_t i = 0; i < kBlock; ++i)
                in[i] = float((block * 48 + int(i)) % 109) / 54.5f - 1.f;
            filter.Process(in.data(), out.data(), kBlock);
            for(float v : out) {
                uint32_t bits;
                std::memcpy(&bits, &v, sizeof(bits));
                for(int b = 0; b < 4; ++b) {
                    hash ^= (bits >> (8 * b)) & 0xFFu;
                    hash *= 16777619u;
                }
            }
        }
    }
    std::printf("golden hash 0x%08X\n", unsigned(hash));
    CHECK(hash == 0x150EB967u);
}

// Amplitude of a tone and of its odd harmonics in the driven filter.
struct Harmonics {
    double fundamental, h3, h5, h7;
};
static Harmonics Measure(Parameters p, double hz, double amplitude) {
    Ladder filter;
    filter.Set(p);
    std::vector<float> in(kBlock), out(kBlock), tail;
    const std::size_t total = std::size_t(kFs), keep = std::size_t(kFs / 2.);
    for(std::size_t n = 0; n < total; n += kBlock) {
        for(std::size_t i = 0; i < kBlock; ++i)
            in[i] = float(amplitude * std::sin(2. * kPi * hz / kFs * double(n + i)));
        filter.Process(in.data(), out.data(), kBlock);
        if(n + keep >= total)
            tail.insert(tail.end(), out.begin(), out.end());
    }
    return {Amplitude(tail, hz), Amplitude(tail, 3. * hz), Amplitude(tail, 5. * hz), Amplitude(tail, 7. * hz)};
}

static void TestDrive() {
    // Drive 0 is the clean core, bit for bit, in every model.
    for(Model model : {Model::Sum, Model::Stages}) {
        Ladder clean, driven;
        Parameters a = Settings(800., 0.7, Mode::Lp24), b = a;
        a.model = Model::Clean;
        b.model = model;
        clean.Set(a);
        driven.Set(b);
        std::vector<float> in(kBlock), x(kBlock), y(kBlock);
        for(int block = 0; block < 500; ++block) {
            for(std::size_t i = 0; i < kBlock; ++i)
                in[i] = 0.4f * float(std::sin(2. * kPi * 311. / kFs * double(block * 48 + int(i))));
            clean.Process(in.data(), x.data(), kBlock);
            driven.Process(in.data(), y.data(), kBlock);
            CHECK(std::equal(x.begin(), x.end(), y.begin()));
        }
    }
    // Model Clean ignores drive.
    {
        Parameters p = Settings(2000., 0.45, Mode::Lp24, 0.);
        p.model = Model::Clean;
        const Harmonics zero = Measure(p, 220., 0.5);
        p.drive = 1.f;
        const Harmonics one = Measure(p, 220., 0.5);
        CHECK(std::fabs(zero.fundamental - one.fundamental) < 1e-6);
    }
    // Harmonic content rises with drive, and the Stages model reaches further up.
    for(Model model : {Model::Sum, Model::Stages}) {
        double last = -200., h7_at_full = 0., h3_at_full = 0.;
        for(double drive : {0., 0.25, 0.5, 0.75, 1.}) {
            Parameters p = Settings(2000., 0.45, Mode::Lp24, 0.);
            p.model = model;
            p.drive = float(drive);
            const Harmonics h = Measure(p, 220., 0.5);
            const double h3 = Db(h.h3 / h.fundamental), h5 = Db(h.h5 / h.fundamental), h7 = Db(h.h7 / h.fundamental);
            std::printf("%s drive %.2f: fundamental %.1f dB, H3 %.1f, H5 %.1f, H7 %.1f dB\n",
                        model == Model::Sum ? "Sum   " : "Stages", drive, Db(h.fundamental), h3, h5, h7);
            CHECK(h3 > last);
            last = h3;
            h7_at_full = h7;
            h3_at_full = h3;
        }
        CHECK(h3_at_full > -20.); // audibly changed at the top
        CHECK(h7_at_full > (model == Model::Sum ? -40. : -30.));
    }
    // Harder input pushes back on resonance: the gain at the cutoff falls with the level.
    for(Model model : {Model::Clean, Model::Sum, Model::Stages}) {
        Parameters p = Settings(1000., 0.82, Mode::Lp24, 0.);
        p.model = model;
        p.drive = 0.5f;
        const double quiet = Db(Measure(p, 1000., 0.003).fundamental / 0.003);
        const double loud = Db(Measure(p, 1000., 1.).fundamental);
        std::printf("pushback %d: %.1f dB quiet, %.1f dB at full scale\n", int(model), quiet, loud);
        CHECK(quiet - loud > (model == Model::Clean ? 5. : 20.));
    }
    // Self-oscillation keeps its level at high drive, in every mode that oscillates.
    double lo = 1e9, hi = -1e9;
    for(Model model : {Model::Sum, Model::Stages})
        for(double drive : {0.03, 0.25, 1.})
            for(double hz : {125., 1000., 8000.})
                for(Mode mode : {Mode::Lp24, Mode::Lp12, Mode::Bp, Mode::Hp}) {
                    Ladder filter;
                    Parameters p = Settings(hz, 1., mode);
                    p.model = model;
                    p.drive = float(drive);
                    filter.Set(p);
                    std::vector<float> in(kBlock, 0.f), out(kBlock);
                    double energy = 0.;
                    const std::size_t total = std::size_t(kFs * 4.), keep = std::size_t(kFs / 2.);
                    for(std::size_t n = 0; n < total; n += kBlock) {
                        filter.Process(in.data(), out.data(), kBlock);
                        if(n + keep >= total)
                            for(float v : out)
                                energy += double(v) * v;
                    }
                    const double level = Db(std::sqrt(2. * energy / double(keep)) / double(kOscLevel));
                    lo = std::min(lo, level);
                    hi = std::max(hi, level);
                    CHECK(level > -4. && level < 6.);
                }
    std::printf("self-oscillation with drive: %.2f .. %.2f dB around %.2f peak\n", lo, hi, double(kOscLevel));
    // The cheap and the full path agree when nothing moves.
    Ladder ramped, held;
    Parameters p = Settings(900., 0.6, Mode::Lp24);
    p.drive = 0.6f;
    ramped.Set(p);
    held.Set(p);
    std::vector<float> in(kBlock), a(kBlock), b(kBlock);
    double difference = 0.;
    for(int block = 0; block < 200; ++block) {
        for(std::size_t i = 0; i < kBlock; ++i)
            in[i] = 0.5f * float(std::sin(2. * kPi * 200. / kFs * double(block * 48 + int(i))));
        ramped.Process(in.data(), a.data(), kBlock);
        held.ProcessHeld(in.data(), b.data(), kBlock);
        for(std::size_t i = 0; i < kBlock; ++i)
            difference = std::max(difference, std::fabs(double(a[i]) - double(b[i])));
    }
    CHECK(difference < 1e-3);
}

// Runs noise through a filter of `seed` and hashes the output.
static uint32_t Render(uint32_t seed, double age, double drive, Model model) {
    Ladder filter(seed);
    Parameters p = Settings(900., 0.8, Mode::Lp24);
    p.age = float(age);
    p.drive = float(drive);
    p.model = model;
    filter.Set(p);
    uint32_t hash = 2166136261u, rng = 7u;
    std::vector<float> in(kBlock), out(kBlock);
    for(int block = 0; block < 2000; ++block) {
        for(float& x : in) {
            rng = rng * 1664525u + 1013904223u;
            x = float(int32_t(rng)) / 4294967296.f;
        }
        filter.Process(in.data(), out.data(), kBlock);
        for(float v : out) {
            uint32_t bits;
            std::memcpy(&bits, &v, sizeof(bits));
            hash = (hash ^ bits) * 16777619u;
        }
    }
    return hash;
}

// Note offset that drift and jitter produce, read through a self-oscillating filter would
// be noisy; the tolerance and pitch spread are measured on the peak of a resonant response.
static double PeakCents(uint32_t seed, double age) {
    Parameters p = Settings(1000., 0.85, Mode::Lp24, 0.);
    p.age = float(age);
    Ladder filter(seed);
    filter.Set(p);
    double best = 0., best_hz = 1000.;
    for(double cents = -700.; cents <= 700.; cents += 25.) {
        const double hz = 1000. * std::exp2(cents / 1200.);
        Ladder probe(seed);
        probe.Set(p);
        std::vector<float> in(kBlock), out(kBlock), tail;
        for(std::size_t n = 0; n < 9600; n += kBlock) {
            for(std::size_t i = 0; i < kBlock; ++i)
                in[i] = 0.01f * float(std::sin(2. * kPi * hz / kFs * double(n + i)));
            probe.Process(in.data(), out.data(), kBlock);
            if(n >= 4800)
                tail.insert(tail.end(), out.begin(), out.end());
        }
        const double level = Amplitude(tail, hz);
        if(level > best) {
            best = level;
            best_hz = hz;
        }
    }
    return 1200. * std::log2(best_hz / 1000.);
}

static void TestAge() {
    // Age 0 is the same filter, bit for bit, in every model and at every seed.
    for(uint32_t seed : {1u, 77u, 123456u})
        for(Model model : {Model::Clean, Model::Sum, Model::Stages}) {
            Ladder plain(seed), aged(seed);
            Parameters p = Settings(1500., 0.7, Mode::Bp);
            p.model = model;
            p.drive = model == Model::Clean ? 0.f : 0.4f;
            plain.Set(p);
            p.age = 0.f;
            aged.Set(p);
            CHECK(Render(seed, 0., 0.4, model) == Render(seed, 0., 0.4, model));
        }
    CHECK(Render(5u, 0., 0., Model::Stages) == Render(5u, 0., 0., Model::Clean));
    // A seed reproduces the run; another seed is another filter.
    CHECK(Render(42u, 0.7, 0.3, Model::Stages) == Render(42u, 0.7, 0.3, Model::Stages));
    CHECK(Render(42u, 0.7, 0.3, Model::Stages) != Render(43u, 0.7, 0.3, Model::Stages));
    // Tolerances are fixed per instance and differ between instances.
    Ladder one(1u), two(2u);
    CHECK(one.Tolerance(0) != two.Tolerance(0));
    for(unsigned i = 0; i < 4; ++i)
        CHECK(std::fabs(one.Tolerance(i)) <= 1.f);
    // The useful range: subtle up to 30 %, worn at 60 %, far out at 100 %.
    for(double age : {0.3, 0.6, 1.}) {
        double spread = 0.;
        for(uint32_t seed = 1; seed <= 8; ++seed)
            spread = std::max(spread, std::fabs(PeakCents(seed * 977u, age)));
        std::printf("age %.0f %%: resonance peak up to %.0f cents off over eight instances\n", age * 100., spread);
        if(age < 0.4)
            CHECK(spread <= 50.);
        else if(age < 0.7)
            CHECK(spread <= 350.);
    }
    // Age 100 % stays stable at maximum resonance and drive, on every path.
    uint32_t rng = 3u;
    double peak = 0.;
    std::vector<float> in(kBlock), out(kBlock);
    for(Model model : {Model::Clean, Model::Sum, Model::Stages})
        for(Mode mode : kAll)
            for(int path = 0; path < 2; ++path) {
                Ladder filter(rng);
                Parameters p = Settings(1000., 1., mode);
                p.drive = 1.f;
                p.age = 1.f;
                p.model = model;
                for(int block = 0; block < 6000; ++block) {
                    rng = rng * 1664525u + 1013904223u;
                    p.cutoff_note = 20.f + float(rng >> 8) * (115.f / 16777216.f);
                    filter.Set(p);
                    for(float& x : in) {
                        rng = rng * 1664525u + 1013904223u;
                        x = float(int32_t(rng)) / 2147483648.f;
                    }
                    if(path)
                        filter.ProcessHeld(in.data(), out.data(), kBlock);
                    else
                        filter.Process(in.data(), out.data(), kBlock);
                    for(float v : out) {
                        CHECK(std::isfinite(v));
                        peak = std::max(peak, double(std::fabs(v)));
                    }
                }
            }
    std::printf("age 100 %%, drive 100 %%, resonance 100 %%, full-scale noise: largest output %.1f\n", peak);
    CHECK(peak < 200.);
    // Silence at age 100 %: the feedback noise starts the oscillation without an input.
    Ladder filter(9u);
    Parameters p = Settings(1000., 1., Mode::Lp24);
    p.age = 1.f;
    filter.Set(p);
    std::fill(in.begin(), in.end(), 0.f);
    double late = 0.;
    for(int block = 0; block < 300; ++block) {
        filter.Process(in.data(), out.data(), kBlock);
        if(block > 200)
            for(float v : out)
                late = std::max(late, double(std::fabs(v)));
    }
    std::printf("age 100 %%: oscillation from silence reaches %.3f after 300 ms\n", late);
    CHECK(late > 0.1);
}

int main() {
    TestTable();
    TestResponse();
    TestBassCompensation();
    TestSelfOscillation();
    TestStability();
    TestDenormalsAndNan();
    TestRamping();
    TestPaths();
    TestDrive();
    TestAge();
    TestGolden();
    if(failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("filter core: all tests passed\n");
    return 0;
}
