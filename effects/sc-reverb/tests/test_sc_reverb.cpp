// SPDX-License-Identifier: MIT
// Host tests of SC Reverb: FX contract, parameters, and the sound of ReverbSc bit for bit.
#include "sc_reverb/reverb.h"
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
using seed::fx::ScReverb;
constexpr std::size_t kBlock = 48;

struct Rig {
    std::unique_ptr<ScReverb::Storage> memory = std::make_unique<ScReverb::Storage>();
    ScReverb reverb;
    Rig() { CHECK(reverb.Prepare({48000, kBlock}, *memory)); }
};

static void TestContract() {
    auto memory = std::make_unique<ScReverb::Storage>();
    ScReverb reverb;
    float in[kBlock] = {1.f}, left[kBlock], right[kBlock];
    left[0] = right[0] = 7.f;
    reverb.Process(StereoBlock{in, in, left, right, 1}); // not prepared: nothing happens
    CHECK(left[0] == 7.f);
    CHECK(!reverb.Prepare({44100, kBlock}, *memory));
    CHECK(!reverb.Prepare({48000, 0}, *memory));
    CHECK(reverb.Prepare({48000, kBlock}, *memory));
    reverb.Process(StereoBlock{in, in, left, right, kBlock + 1}); // too long: rejected
    CHECK(left[0] == 7.f);
    reverb.Process(StereoBlock{in, in, left, left, 1}); // shared output: rejected
    CHECK(left[0] == 7.f);
    reverb.Process(StereoBlock{in, in, left, right, 0}); // no-op
    CHECK(left[0] == 7.f);
    reverb.Process(StereoBlock{in, in, left, right, kBlock}); // wet-only: no dry signal
    CHECK(left[0] == 0.f && right[0] == 0.f);
}

static void TestParameters() {
    Rig rig;
    CHECK(rig.reverb.Requested().feedback == 0.85f && rig.reverb.Requested().lp_hz == 12000.f);
    ScReverb::Parameters p;
    p.feedback = 3.f;
    p.lp_hz = 1.f;
    rig.reverb.SetParameters(p);
    CHECK(rig.reverb.Requested().feedback == 1.f && rig.reverb.Requested().lp_hz == 100.f);
    p.feedback = p.lp_hz = std::nanf("");
    rig.reverb.SetParameters(p);
    CHECK(rig.reverb.Requested().feedback == 1.f && rig.reverb.Requested().lp_hz == 100.f);
}

static float Energy(ScReverb& reverb, int blocks) {
    double sum = 0.0;
    for(int b = 0; b < blocks; ++b) {
        float in[kBlock] = {}, left[kBlock], right[kBlock];
        if(b == 0)
            in[0] = 1.f;
        reverb.Process(StereoBlock{in, in, left, right, kBlock});
        for(std::size_t i = 0; i < kBlock; ++i) {
            CHECK(std::isfinite(left[i]) && std::isfinite(right[i]));
            if(b >= blocks / 2)
                sum += double(left[i]) * double(left[i]);
        }
    }
    return float(sum);
}

// A click rings on; a longer decay leaves more of it in the second half.
static void TestDecay() {
    Rig short_decay, long_decay;
    ScReverb::Parameters p;
    p.feedback = 0.3f;
    short_decay.reverb.SetParameters(p);
    p.feedback = 0.95f;
    long_decay.reverb.SetParameters(p);
    const float little = Energy(short_decay.reverb, 2000), much = Energy(long_decay.reverb, 2000);
    CHECK(much > 0.f);
    CHECK(much > little * 10.f);
}

// The component adds nothing to ReverbSc, at any block size and in place.
static void TestUpstreamSound() {
    auto direct = std::make_unique<daisysp::ReverbSc>();
    Rig rig;
    CHECK(direct->Init(48000.f) == 0);
    direct->SetFeedback(0.7f);
    direct->SetLpFreq(9000.f);
    ScReverb::Parameters p;
    p.feedback = 0.7f;
    p.lp_hz = 9000.f;
    rig.reverb.SetParameters(p);
    bool same = true;
    for(int b = 0; b < 3000; ++b) {
        float left[kBlock], right[kBlock], expected_left[kBlock], expected_right[kBlock];
        for(std::size_t i = 0; i < kBlock; ++i) {
            const float t = float(b * int(kBlock) + int(i));
            left[i] = 0.4f * std::sin(0.03f * t);
            right[i] = 0.3f * std::sin(0.047f * t);
            direct->Process(left[i], right[i], &expected_left[i], &expected_right[i]);
        }
        rig.reverb.Process(StereoBlock{left, right, left, right, kBlock});
        for(std::size_t i = 0; i < kBlock; ++i)
            same = same && left[i] == expected_left[i] && right[i] == expected_right[i];
    }
    CHECK(same);
}

int main() {
    TestContract();
    TestParameters();
    TestDecay();
    TestUpstreamSound();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("sc-reverb host tests passed\n");
    return 0;
}
