// Host tests of the Stereo Delay (ARBEITSAUFTRAG Phase 4): impulse responses of both modes
// (spacing, alternation, decay), synced note values 30-300 BPM including a dotted whole
// note at 30 BPM (12 s), interpolation and the clamp at the maximum, time changes against
// the click criterion, feedback 95 % with loud input, damping response, the host's
// switch-off (feed first, tail, end detection, 20 s limit, re-enable), parameters, golden.
#include "stereo_delay/host.h"
#include "stereo_delay/params.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using seed::fx::StereoDelay;
using seed::fx::DelayHost;
namespace delay = seed::fx::delay;

// Golden of Stereo Delay 0.1.0: FNV-1a over 3 s of both modes with a fixed input.
#ifndef STEREO_DELAY_GOLDEN
#define STEREO_DELAY_GOLDEN 0xD11BFF18u
#endif

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static auto storage = std::make_unique<StereoDelay::Storage>();
static constexpr double kPi = 3.141592653589793;

struct Rig {
    StereoDelay fx;
    std::vector<float> l, r;
    explicit Rig(delay::Parameters p) {
        fx.SetParameters(p);
        fx.Prepare({48000, 48}, *storage);
    }
    // Feeds `in` (stereo pairs from a function of the sample index) for `frames`.
    template <class F>
    void Run(std::size_t frames, F&& input) {
        float il[48], ir[48], ol[48], orr[48];
        for(std::size_t done = 0; done < frames; done += 48) {
            for(int i = 0; i < 48; ++i)
                input(l.size() + std::size_t(i), il[i], ir[i]);
            CHECK(fx.Process({il, ir, ol, orr, 48}));
            for(int i = 0; i < 48; ++i) {
                l.push_back(ol[i]);
                r.push_back(orr[i]);
            }
        }
    }
};

static double Energy(const std::vector<float>& x, std::size_t centre) {
    double e = 0;
    for(std::size_t i = centre > 40 ? centre - 40 : 0; i < centre + 40 && i < x.size(); ++i)
        e += double(x[i]) * x[i];
    return e;
}

static void TestImpulse() {
    for(int mode = 0; mode < 2; ++mode) {
        delay::Parameters p;
        p.mode = delay::Mode(mode);
        p.sync = false;
        p.time_ms = 100.f;
        p.feedback = 50.f;
        p.damping_hz = 20000.f;
        Rig rig(p);
        rig.Run(48000, [](std::size_t n, float& a, float& b) {
            a = n == 0 ? 1.f : 0.f;
            b = 0.f;
        });
        const std::size_t T = 4800;
        std::vector<double> el, er;
        for(int k = 1; k <= 6; ++k) {
            el.push_back(Energy(rig.l, k * T));
            er.push_back(Energy(rig.r, k * T));
        }
        if(mode == 0) {
            // Ping-Pong: echoes T apart, alternating L, R, L, ... (mono feed (L+R)/2).
            for(int k = 0; k < 6; ++k) {
                const bool left = k % 2 == 0;
                CHECK((left ? el[std::size_t(k)] : er[std::size_t(k)]) > 1e-4);
                CHECK((left ? er[std::size_t(k)] : el[std::size_t(k)]) < 1e-9);
            }
            // Each echo: one pass of feedback 0.5 (energy x 0.25), damping at 20 kHz nearly flat.
            for(int k = 1; k < 5; ++k) {
                const double now = k % 2 ? er[std::size_t(k)] : el[std::size_t(k)];
                const double before = k % 2 ? el[std::size_t(k - 1)] : er[std::size_t(k - 1)];
                CHECK(now / before > 0.18 && now / before < 0.26);
            }
        } else {
            // Stereo L/R: the left impulse echoes only left, every T.
            for(int k = 0; k < 6; ++k)
                CHECK(el[std::size_t(k)] > 1e-4 && er[std::size_t(k)] < 1e-12);
            for(int k = 1; k < 5; ++k)
                CHECK(el[std::size_t(k)] / el[std::size_t(k - 1)] > 0.18 && el[std::size_t(k)] / el[std::size_t(k - 1)] < 0.26);
        }
        // Nothing before the first echo.
        double early = 0;
        for(std::size_t i = 0; i + 50 < T; ++i)
            early += double(rig.l[i]) * rig.l[i] + double(rig.r[i]) * rig.r[i];
        CHECK(early == 0.0);
    }
}

static void TestSync() {
    StereoDelay fx;
    fx.Prepare({48000, 48}, *storage);
    delay::Parameters p;
    for(int bpm : {30, 60, 120, 300}) {
        const float quarter = 48000.f * 60.f / float(bpm);
        fx.SetTempo(quarter);
        for(int v = 0; v < 18; ++v) {
            p.note_value = v;
            CHECK(std::fabs(fx.WantedSamples(p) - delay::kNoteQuarters[v] * quarter) < 1e-2f);
        }
    }
    // A dotted whole note at 30 BPM: exactly 12 s, the full line; not clamped.
    fx.SetTempo(96000.f);
    p.note_value = 17;
    fx.SetParameters(p);
    float l[48] = {}, r[48] = {}, ol[48], orr[48];
    fx.Process({l, r, ol, orr, 48});
    CHECK(fx.DelaySamples() == 576000.f && !fx.Clamped());
    CHECK(std::fabs(delay::kNoteQuarters[17] * 96000.f / 48000.f - 12.f) < 1e-6f);
    // Slower still: clamped at 12 s and reported.
    fx.SetTempo(110000.f);
    fx.Process({l, r, ol, orr, 48});
    CHECK(fx.Clamped() && fx.TargetSamples() == 576000.f);
    // Other note values: 1/4 at 120 BPM = 500 ms, 1/8 triplet = 166.7 ms, 1/16 dotted = 187.5 ms.
    fx.SetTempo(24000.f);
    p.note_value = 9;
    CHECK(fx.WantedSamples(p) == 24000.f);
    p.note_value = 7;
    CHECK(std::fabs(fx.WantedSamples(p) - 8000.f) < 0.01f);
    p.note_value = 5;
    CHECK(fx.WantedSamples(p) == 9000.f);
}

static void TestMaxDelayAndInterpolation() {
    // A 12 s echo really arrives after 12 s (no wrap into the margin).
    delay::Parameters p;
    p.mode = delay::Mode::Stereo;
    p.feedback = 0.f;
    p.note_value = 17;
    Rig rig(p);
    rig.fx.SetTempo(96000.f);
    rig.Run(576000 + 4800, [](std::size_t n, float& a, float& b) {
        a = n == 0 ? 1.f : 0.f;
        b = 0.f;
    });
    std::size_t at = 0;
    float peak = 0.f;
    for(std::size_t i = 0; i < rig.l.size(); ++i)
        if(std::fabs(rig.l[i]) > peak) {
            peak = std::fabs(rig.l[i]);
            at = i;
        }
    CHECK(at == 576000 && peak > 0.99f);
    // Fractional delay: a 1 kHz sine 100.5 samples late, linear interpolation error < 1 %.
    delay::Parameters q;
    q.mode = delay::Mode::Stereo;
    q.sync = false;
    q.feedback = 0.f;
    q.time_ms = 100.5f / 48.f;
    q.damping_hz = 20000.f;
    // 2.09 ms is below the 10 ms minimum: check the clamp, then use a longer fractional time.
    StereoDelay d;
    d.SetParameters(q);
    CHECK(d.Requested().time_ms == delay::kMinDelayMs);
    q.time_ms = 480.5f / 48.f; // 480.5 samples
    Rig s(q);
    s.Run(4800, [](std::size_t n, float& a, float& b) { a = b = float(std::sin(2 * kPi * 1000.0 * double(n) / 48000.0)); });
    double worst = 0;
    for(std::size_t n = 1000; n < 4800; ++n)
        worst = std::fmax(worst, std::fabs(s.r[n] - std::sin(2 * kPi * 1000.0 * (double(n) - 480.5) / 48000.0)));
    CHECK(worst < 0.01);
}

// Click criterion: 100 Hz sine at -12 dBFS; after a time change the largest sample step of
// the wet output is at most 1.5 x the settled value.
static void TestTimeChange() {
    for(float to : {500.f, 310.f, 120.f}) {
        delay::Parameters p;
        p.mode = delay::Mode::Stereo;
        p.sync = false;
        p.time_ms = 300.f;
        p.feedback = 0.f;
        p.damping_hz = 20000.f;
        Rig rig(p);
        auto sine = [](std::size_t n, float& a, float& b) { a = b = 0.25f * float(std::sin(2 * kPi * 100.0 * double(n) / 48000.0)); };
        rig.Run(48000, sine);
        p.time_ms = to;
        rig.fx.SetParameters(p);
        rig.Run(96000, sine);
        float settled = 0.f, jump = 0.f;
        for(std::size_t i = 24001; i < 48000; ++i)
            settled = std::fmax(settled, std::fabs(rig.r[i] - rig.r[i - 1]));
        for(std::size_t i = 48000; i < rig.r.size(); ++i)
            jump = std::fmax(jump, std::fabs(rig.r[i] - rig.r[i - 1]));
        if(jump > 1.5f * settled)
            std::printf("  time change 300 -> %.0f ms: %.5f > 1.5 x %.5f\n", to, jump, settled);
        CHECK(jump <= 1.5f * settled);
    }
}

static void TestFeedbackAndDamping() {
    // Feedback 95 % with loud input stays bounded and finite in both modes.
    for(int mode = 0; mode < 2; ++mode) {
        delay::Parameters p;
        p.mode = delay::Mode(mode);
        p.sync = false;
        p.time_ms = 50.f;
        p.feedback = 95.f;
        p.damping_hz = 20000.f;
        Rig rig(p);
        uint32_t x = 1;
        rig.Run(48000 * 20, [&](std::size_t, float& a, float& b) {
            x = x * 1664525u + 1013904223u;
            a = b = (float(x >> 8) / 8388608.f - 1.f);
        });
        float peak = 0.f;
        bool finite = true;
        for(std::size_t i = 0; i < rig.l.size(); ++i) {
            finite &= std::isfinite(rig.l[i]) && std::isfinite(rig.r[i]);
            peak = std::fmax(peak, std::fmax(std::fabs(rig.l[i]), std::fabs(rig.r[i])));
        }
        CHECK(finite && peak < 4.f);
        std::printf("Stereo Delay feedback 95 %%, mode %d, 20 s full-scale noise: peak %.2f\n", mode, peak);
    }
    // Damping: echo gain of 200 Hz and 8 kHz after one pass, 1 kHz versus 20 kHz.
    auto echo_gain = [](float damping, double hz) {
        delay::Parameters p;
        p.mode = delay::Mode::Stereo;
        p.sync = false;
        p.time_ms = 20.f;
        p.feedback = 90.f;
        p.damping_hz = damping;
        Rig rig(p);
        // Feed one 20 ms burst, then compare the first and the second echo.
        rig.Run(4800, [&](std::size_t n, float& a, float& b) {
            a = b = n < 960 ? 0.1f * float(std::sin(2 * kPi * hz * double(n) / 48000.0)) : 0.f;
        });
        double first = 0, second = 0;
        for(std::size_t i = 960 + 100; i < 1920 - 100; ++i)
            first += double(rig.l[i]) * rig.l[i];
        for(std::size_t i = 1920 + 100; i < 2880 - 100; ++i)
            second += double(rig.l[i]) * rig.l[i];
        return 10.0 * std::log10(second / first);
    };
    const double low_dark = echo_gain(1000.f, 200.0), high_dark = echo_gain(1000.f, 8000.0);
    const double low_open = echo_gain(20000.f, 200.0), high_open = echo_gain(20000.f, 8000.0);
    std::printf("Damping per pass: 1 kHz: 200 Hz %.1f dB, 8 kHz %.1f dB; 20 kHz: 200 Hz %.1f dB, 8 kHz %.1f dB\n",
                low_dark, high_dark, low_open, high_open);
    CHECK(high_dark < low_dark - 12.0);
    CHECK(std::fabs(high_open - low_open) < 2.0);
}

static void TestHost() {
    // Switch-off: the feed fades out within 10 ms, the tail keeps ringing, then ends.
    delay::Parameters p;
    p.sync = false;
    p.time_ms = 100.f;
    p.feedback = 50.f;
    StereoDelay fx;
    fx.SetParameters(p);
    fx.Prepare({48000, 48}, *storage);
    DelayHost host(fx);
    host.Reset(true);
    host.SetMix(100.f);
    float il[48], ir[48], ol[48], orr[48];
    uint32_t x = 7;
    auto block = [&](bool noise) {
        for(int i = 0; i < 48; ++i) {
            x = x * 1664525u + 1013904223u;
            il[i] = ir[i] = noise ? 0.3f * (float(x >> 8) / 8388608.f - 1.f) : 0.f;
        }
        host.Process(il, ir, ol, orr, 48);
        float peak = 0.f;
        for(int i = 0; i < 48; ++i)
            peak = std::fmax(peak, std::fmax(std::fabs(ol[i]), std::fabs(orr[i])));
        return peak;
    };
    for(int b = 0; b < 1000; ++b)
        block(true);
    host.SetEnabled(false);
    for(int b = 0; b < 10; ++b)
        block(true);
    CHECK(host.Feed() == 0.f && host.GetState() == DelayHost::State::Tail);
    float tail = 0.f;
    for(int b = 0; b < 150; ++b)
        tail = std::fmax(tail, block(true)); // the input continues, but no longer enters
    CHECK(tail > 0.01f);
    int ms = 0;
    while(host.GetState() == DelayHost::State::Tail && ms < 30000) {
        block(true);
        ++ms;
    }
    CHECK(host.GetState() == DelayHost::State::Off && ms < 5000);
    CHECK(block(true) == 0.f); // off: only the dry path with mix 100 % -> silence
    // Feedback 95 % with 1 s echoes (-0.45 dB per second): the 20 s limit fades the tail over 1 s.
    p.feedback = 95.f;
    p.time_ms = 1000.f;
    p.damping_hz = 20000.f;
    fx.SetParameters(p);
    host.SetEnabled(true);
    for(int b = 0; b < 1000; ++b)
        block(true);
    host.SetEnabled(false);
    ms = 0;
    while(host.GetState() == DelayHost::State::Tail && ms < 30000) {
        block(false);
        ++ms;
    }
    CHECK(host.GetState() == DelayHost::State::Off && ms >= 20000 && ms <= 21100);
    // Re-enable during the tail: the feed fades in, the lines are kept.
    host.SetEnabled(true);
    for(int b = 0; b < 1000; ++b)
        block(true);
    host.SetEnabled(false);
    for(int b = 0; b < 300; ++b)
        block(false);
    host.SetEnabled(true);
    CHECK(host.GetState() == DelayHost::State::On);
    CHECK(block(false) > 0.001f); // the echoes were not cleared
}

static void TestParamsAndGolden() {
    delay::Parameters p;
    for(uint8_t i = 0; i < sq::kStereoDelayComponent.count; ++i) {
        const sq::ParamDesc& d = sq::kStereoDelayComponent.params[i];
        int32_t v = 0;
        CHECK(delay::ReadParam(p, d.id, v) && v == d.def);
        CHECK(delay::WriteParam(p, d.id, d.max + 1000) && delay::ReadParam(p, d.id, v) && v == d.max);
        CHECK(delay::WriteParam(p, d.id, d.def));
    }
    uint32_t hash = 2166136261u;
    for(int mode = 0; mode < 2; ++mode) {
        delay::Parameters q;
        q.mode = delay::Mode(mode);
        q.feedback = 70.f;
        q.damping_hz = 4000.f;
        Rig rig(q);
        rig.fx.SetTempo(48000.f * 60.f / 132.f);
        rig.Run(48000 * 3, [](std::size_t n, float& a, float& b) {
            const float burst = (n % 24000) < 2400 ? 1.f : 0.f;
            a = burst * 0.3f * float((n * 7) % 97) / 97.f;
            b = burst * 0.2f * float((n * 13) % 89) / 89.f;
        });
        for(std::size_t i = 0; i < rig.l.size(); ++i)
            for(float f : {rig.l[i], rig.r[i]}) {
                uint32_t bits;
                std::memcpy(&bits, &f, 4);
                for(int k = 0; k < 4; ++k) {
                    hash ^= (bits >> (8 * k)) & 0xFF;
                    hash *= 16777619u;
                }
            }
    }
    std::printf("Stereo Delay golden: 0x%08X\n", unsigned(hash));
    CHECK(hash == STEREO_DELAY_GOLDEN);
}

int main() {
    TestImpulse();
    TestSync();
    TestMaxDelayAndInterpolation();
    TestTimeChange();
    TestFeedbackAndDamping();
    TestHost();
    TestParamsAndGolden();
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: impulse responses (ping-pong, stereo), sync 30-300 BPM with 12 s at 30 BPM, max time and "
                "interpolation, time change click criterion, feedback 95 %%, damping, switch-off/tail/end/20 s/re-enable, "
                "parameters, golden\n");
    return 0;
}
