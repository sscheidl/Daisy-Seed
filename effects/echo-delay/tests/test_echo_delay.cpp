// SPDX-License-Identifier: MIT
// Host tests of Echo Delay: FX contract, parameters, the sound of the upstream echo bit for
// bit, and the shared wet host.
#include "../../../common/audio/wet_host.h"
#include "echo_delay/echo.h"
#include <cmath>
#include <cstdio>
#include <memory>

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

using seed::audio::StereoBlock;
using seed::fx::StereoEcho;
constexpr std::size_t kBlock = 48;

struct Rig {
    std::unique_ptr<StereoEcho::Storage> memory = std::make_unique<StereoEcho::Storage>();
    StereoEcho echo;
    Rig() { CHECK(echo.Prepare({48000, kBlock}, *memory)); }
};

static void TestContract() {
    auto memory = std::make_unique<StereoEcho::Storage>();
    StereoEcho echo;
    float in[kBlock] = {1.f}, left[kBlock], right[kBlock];
    left[0] = right[0] = 7.f;
    CHECK(!echo.Process(StereoBlock{in, in, left, right, 1})); // not prepared: rejected, nothing happens
    CHECK(left[0] == 7.f);
    CHECK(!echo.Prepare({44100, kBlock}, *memory));
    CHECK(!echo.Prepare({48000, 0}, *memory));
    CHECK(echo.Prepare({48000, kBlock}, *memory));
    CHECK(!echo.Process(StereoBlock{in, in, left, right, kBlock + 1})); // too long: rejected
    CHECK(left[0] == 7.f);
    CHECK(!echo.Process(StereoBlock{in, in, left, left, 1})); // shared output: rejected
    CHECK(left[0] == 7.f);
    CHECK(echo.Process(StereoBlock{in, in, left, right, 0})); // no-op
    CHECK(left[0] == 7.f);
    CHECK(echo.Process(StereoBlock{in, in, left, right, kBlock})); // wet-only: no dry signal
    CHECK(left[0] == 0.f && right[0] == 0.f);
}

static void TestParameters() {
    Rig rig;
    StereoEcho::Parameters p;
    p.time_s = 100.f;
    p.feedback = 9.f;
    p.lag_s = -1.f;
    rig.echo.SetParameters(p);
    CHECK(rig.echo.Requested().time_s == 5.f);
    CHECK(rig.echo.Requested().feedback == 1.5f);
    CHECK(rig.echo.Requested().lag_s == 0.f);
    p.time_s = 0.f;
    rig.echo.SetParameters(p);
    CHECK(rig.echo.Requested().time_s == 0.01f);
    p.time_s = p.feedback = p.lag_s = std::nanf("");
    rig.echo.SetParameters(p);
    CHECK(rig.echo.Requested().time_s == 0.01f && rig.echo.Requested().feedback == 1.5f);
}

// Left and right are separate lines; the repeat arrives after the set time, through the band-pass.
static void TestRepeat() {
    Rig rig;
    StereoEcho::Parameters p;
    p.lag_s = 0.f;
    p.time_s = 0.1f;
    p.feedback = 0.f;
    rig.echo.SetParameters(p);
    float peak = 0.f, other = 0.f;
    int at = -1;
    for(int i = 0; i < 12000; ++i) {
        float left = i == 0 ? 1.f : 0.f, right = 0.f, out_left, out_right;
        rig.echo.Process(StereoBlock{&left, &right, &out_left, &out_right, 1});
        other = std::fmax(other, std::fabs(out_right));
        if(std::fabs(out_left) > peak) {
            peak = std::fabs(out_left);
            at = i;
        }
    }
    CHECK(other == 0.f);
    CHECK(peak > 0.01f);
    CHECK(at >= 4800 && at < 4800 + 200);
}

// Feedback above 1 saturates: the soft clip keeps the loop bounded.
static void TestBounded() {
    Rig rig;
    StereoEcho::Parameters p;
    p.lag_s = 0.f;
    p.time_s = 0.05f;
    p.feedback = 1.5f;
    rig.echo.SetParameters(p);
    float peak = 0.f;
    for(int i = 0; i < 48000 * 10; ++i) {
        const float x = 0.5f * std::sin(0.105f * float(i)); // 800 Hz, in the band-pass
        float left = x, right = x, out_left, out_right;
        rig.echo.Process(StereoBlock{&left, &right, &out_left, &out_right, 1});
        CHECK(std::isfinite(out_left));
        peak = std::fmax(peak, std::fabs(out_left));
    }
    CHECK(peak <= 1.0001f && peak > 0.1f);
}

// The component adds nothing to the upstream echo, at any block size and in place.
static void TestUpstreamSound() {
    auto lines = std::make_unique<StereoEcho::Line[]>(2);
    Rig rig;
    for(int i = 0; i < 2; ++i) {
        lines[std::size_t(i)].Init(48000.f);
        lines[std::size_t(i)].SetDelayTime(5.f, true);
        lines[std::size_t(i)].SetFeedback(0.5f);
        lines[std::size_t(i)].SetLagTime(0.5f);
    }
    StereoEcho::Parameters p = rig.echo.Requested();
    p.time_s = 0.2f;
    rig.echo.SetParameters(p);
    lines[0].SetDelayTime(0.2f);
    lines[1].SetDelayTime(0.2f);
    bool same = true;
    for(int b = 0; b < 48000 * 3 / int(kBlock); ++b) {
        float left[kBlock], right[kBlock], expected_left[kBlock], expected_right[kBlock];
        for(std::size_t i = 0; i < kBlock; ++i) {
            const float t = float(b * int(kBlock) + int(i));
            left[i] = 0.4f * std::sin(0.03f * t);
            right[i] = 0.3f * std::sin(0.047f * t);
            expected_left[i] = lines[0].Process(left[i]);
            expected_right[i] = lines[1].Process(right[i]);
        }
        rig.echo.Process(StereoBlock{left, right, left, right, kBlock});
        for(std::size_t i = 0; i < kBlock; ++i)
            same = same && left[i] == expected_left[i] && right[i] == expected_right[i];
    }
    CHECK(same);
}

// Shared wet host: mix 0 is exactly dry, the feed fades, the tail rings on.
static void TestWetHost() {
    Rig rig;
    seed::audio::WetHost<StereoEcho, kBlock> host(rig.echo);
    StereoEcho::Parameters p;
    p.lag_s = 0.f;
    p.time_s = 0.01f;
    p.feedback = 0.9f;
    rig.echo.SetParameters(p);
    host.Reset(true, 0.f);
    float left[kBlock], right[kBlock], out_left[kBlock], out_right[kBlock];
    const auto run = [&](float level) {
        for(std::size_t i = 0; i < kBlock; ++i) {
            left[i] = level * std::sin(0.105f * float(i));
            right[i] = left[i] * 0.5f;
        }
        host.Process(left, right, out_left, out_right, kBlock);
    };
    bool dry = true;
    for(int b = 0; b < 100; ++b) {
        run(0.5f);
        for(std::size_t i = 0; i < kBlock; ++i)
            dry = dry && out_left[i] == left[i] && out_right[i] == right[i];
    }
    CHECK(dry);
    host.SetMix(1.f);
    for(int b = 0; b < 100; ++b)
        run(0.5f);
    float wet = 0.f;
    for(float x : out_left)
        wet = std::fmax(wet, std::fabs(x));
    CHECK(wet > 0.001f);
    host.SetEnabled(false);
    CHECK(!host.Enabled());
    for(int b = 0; b < 20; ++b) // 20 ms: the feed is shut
        run(0.5f);
    run(0.f); // the tail keeps ringing without input
    wet = 0.f;
    for(float x : out_left)
        wet = std::fmax(wet, std::fabs(x));
    CHECK(wet > 0.0001f);
    float probe[kBlock + 1] = {};
    out_left[0] = 3.f;
    host.Process(probe, probe, out_left, out_right, kBlock + 1); // too long: nothing
    CHECK(out_left[0] == 3.f);
    host.SetMix(std::nanf(""));
    run(0.f);
    CHECK(std::isfinite(out_left[0]));
}

// R13: a block the echo rejects (unprepared, or longer than its prepared max_block) leaves the
// host's wet buffers unwritten; the host must then return the exact dry input.
static void TestWetHostRejection() {
    float in_l[kBlock], in_r[kBlock];
    for(std::size_t i = 0; i < kBlock; ++i) {
        in_l[i] = .25f + float(i) * .001f;
        in_r[i] = -.125f - float(i) * .001f;
    }
    auto memory = std::make_unique<StereoEcho::Storage>();
    for(int prepared = 0; prepared < 2; ++prepared) {
        StereoEcho echo;
        if(prepared) CHECK(echo.Prepare({48000, 32}, *memory)); // the host accepts 48
        seed::audio::WetHost<StereoEcho, kBlock> host(echo);
        for(float mix : {0.f, .5f, 1.f})
            for(int inplace = 0; inplace < 2; ++inplace) {
                host.Reset(true, mix);
                float ol[kBlock], orr[kBlock];
                for(std::size_t i = 0; i < kBlock; ++i) {
                    ol[i] = in_l[i];
                    orr[i] = in_r[i];
                }
                host.Process(inplace ? ol : in_l, inplace ? orr : in_r, ol, orr, kBlock);
                for(std::size_t i = 0; i < kBlock; ++i)
                    CHECK(ol[i] == in_l[i] && orr[i] == in_r[i]);
            }
    }
}

int main() {
    TestContract();
    TestParameters();
    TestRepeat();
    TestBounded();
    TestUpstreamSound();
    TestWetHost();
    TestWetHostRejection();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("echo-delay host tests passed\n");
    return 0;
}
