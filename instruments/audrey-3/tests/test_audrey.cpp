// SPDX-License-Identifier: MIT
// Host tests of Audrey 3: the parameter table, pots with pick-up, MIDI notes and CCs, and
// the engine: stable, replaceable stages, and the note gate leaving the Audrey II path alone.
#include "audrey/composition.h"
#include "audrey/control.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

using namespace audrey;
constexpr float kSampleRate = 48000.f;
constexpr float kControlRate = kSampleRate / 4.f;

static bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

static void TestParameterTable() {
    ParamBank bank;
    bank.Init(kControlRate);
    for(uint8_t i = 0; i < kParamCount; ++i) {
        const ParamSpec& s = Spec(i);
        // The stored control position of each default must lead back to that default.
        CHECK(Near(bank.Target(i), s.initial, std::fabs(s.initial) * 0.002f + 0.0001f));
        bank.SetNorm(i, 0.f);
        CHECK(Near(bank.TargetValue(i), s.min, std::fabs(s.min) * 0.001f + 0.0001f));
        bank.SetNorm(i, 1.f);
        CHECK(Near(bank.TargetValue(i), s.max, std::fabs(s.max) * 0.001f + 0.0001f));
    }
    // Decay follows the tension curve: halfway is far below the linear value.
    bank.SetNorm(kReverbDecay, 0.5f);
    const float expected = 0.2f + 0.8f * (std::expm1(-1.5f) / std::expm1(-3.f));
    CHECK(Near(bank.TargetValue(kReverbDecay), expected, 0.0001f));
    // The warp halves the control value, so the time falls to about a quarter.
    bank.SetNorm(kEchoTime, 0.8f);
    const float plain = bank.TargetValue(kEchoTime);
    bank.SetWarp(true);
    CHECK(Near(bank.TargetValue(kEchoTime), 0.05f + 0.16f * 4.95f, 0.001f));
    CHECK(bank.TargetValue(kEchoTime) < plain * 0.3f);
    bank.SetWarp(false);
    CHECK(Near(bank.TargetValue(kEchoTime), plain, 0.0001f));
}

struct CountingSink {
    int calls[kParamCount] = {};
    float last[kParamCount] = {};
    void Apply(uint8_t id, float value) {
        ++calls[id];
        last[id] = value;
    }
};

static void TestSmoothing() {
    ParamBank bank;
    bank.Init(kControlRate);
    CountingSink sink;
    bank.Process(sink);
    for(uint8_t i = 0; i < kParamCount; ++i)
        CHECK(sink.calls[i] == 1); // the first block sets everything once
    bank.Process(sink);
    for(uint8_t i = 0; i < kParamCount; ++i)
        CHECK(sink.calls[i] == 1); // nothing moved: no filter recalculation
    bank.SetNorm(kOutputVolume, 1.f);
    bank.Process(sink);
    CHECK(sink.calls[kOutputVolume] == 2);
    CHECK(sink.last[kOutputVolume] < 0.6f); // smoothed, no jump
    for(int i = 0; i < int(kControlRate); ++i)
        bank.Process(sink);
    CHECK(Near(sink.last[kOutputVolume], 1.f, 0.001f));
}

static PanelFrame Pots(float value) {
    PanelFrame f;
    for(float& p : f.pots)
        p = value;
    return f;
}

static void TestPotsAndPages() {
    Control c;
    c.Init(kControlRate);
    PanelFrame f = Pots(0.5f);
    c.Panel(f);
    CHECK(Near(c.Bank().Norm(kFrequency), 0.5f, 1e-6f));
    CHECK(Near(c.Bank().Norm(kReverbMix), 0.5f, 1e-6f));
    CHECK(Near(c.Bank().Norm(kOutputVolume), 0.5f, 1e-6f));
    CHECK(Near(c.Bank().Norm(kEchoTime), Spec(kEchoTime).initial_norm, 1e-6f)); // page B untouched

    // A CC stays until the pot moves (last change wins).
    c.ControlChange(kFirstParamCc + kFrequency, 127);
    c.Panel(f);
    CHECK(Near(c.Bank().Norm(kFrequency), 1.f, 1e-6f));
    f.pots[0] = 0.6f;
    c.Panel(f);
    CHECK(Near(c.Bank().Norm(kFrequency), 0.6f, 1e-6f));

    // SW17: the paged pots wait; the first five stay live.
    f.page_b = true;
    c.Panel(f);
    CHECK(c.Waiting(0) && c.Waiting(1) && c.Waiting(2));
    CHECK(Near(c.Bank().Norm(kReverbDecay), Spec(kReverbDecay).initial_norm, 1e-6f));
    f.pots[0] = 0.7f;
    f.pots[6] = 0.45f; // VR7 moves, still above Echo Time's stored 0.30: nothing happens
    c.Panel(f);
    CHECK(Near(c.Bank().Norm(kFrequency), 0.7f, 1e-6f));
    CHECK(Near(c.Bank().Norm(kEchoTime), Spec(kEchoTime).initial_norm, 1e-6f));
    CHECK(Near(c.Bank().Norm(kEchoSend), 0.5f, 1e-6f)); // and page A keeps its value
    f.pots[6] = 0.29f; // crosses the stored value: picked up
    c.Panel(f);
    CHECK(!c.Waiting(1));
    CHECK(Near(c.Bank().Norm(kEchoTime), 0.29f, 1e-6f));
    f.pots[6] = 0.2f;
    c.Panel(f);
    CHECK(Near(c.Bank().Norm(kEchoTime), 0.2f, 1e-6f));

    // Back to page A: Echo Send is at 0.5, the pot at 0.2. It waits, then follows.
    f.page_b = false;
    c.Panel(f);
    CHECK(c.Waiting(1));
    CHECK(Near(c.Bank().Norm(kEchoSend), 0.5f, 1e-6f));
    f.pots[6] = 0.49f;
    c.Panel(f);
    CHECK(!c.Waiting(1));
    CHECK(Near(c.Bank().Norm(kEchoSend), 0.49f, 1e-6f));
}

static void TestMidi() {
    Control c;
    c.Init(kControlRate);
    PanelFrame f = Pots(0.5f);
    f.sw18 = 1; // Play
    c.Panel(f);
    CHECK(c.Mode() == PlayMode::Play);
    CHECK(c.GateTarget() == 0.f);
    c.NoteOn(60);
    CHECK(c.GateTarget() == 1.f);
    CHECK(c.Bank().TargetValue(kFrequency) == 60.f);
    c.NoteOn(100); // beyond the pot range
    CHECK(c.Bank().TargetValue(kFrequency) == 100.f);
    c.NoteOff(100); // back to the held note
    CHECK(c.Bank().TargetValue(kFrequency) == 60.f);
    c.NoteOn(64);
    c.NoteOff(60); // not the top note: the pitch stays
    CHECK(c.Bank().TargetValue(kFrequency) == 64.f);
    c.NoteOff(64);
    CHECK(c.GateTarget() == 0.f);
    CHECK(c.Bank().TargetValue(kFrequency) == 64.f);
    c.NoteOn(50);
    c.AllNotesOff();
    CHECK(c.GateTarget() == 0.f);

    // Glide: the value follows slowly; SW18 down jumps.
    CountingSink sink;
    c.Bank().Process(sink);
    c.NoteOn(70);
    c.Bank().Process(sink);
    CHECK(sink.last[kFrequency] < 70.f);
    c.AllNotesOff();
    f.sw18 = 2;
    c.Panel(f);
    CHECK(c.Mode() == PlayMode::PlayNoGlide);
    c.NoteOn(40);
    c.Bank().Process(sink);
    CHECK(sink.last[kFrequency] == 40.f);
    c.AllNotesOff();

    // Drone: always open, notes only set the pitch.
    f.sw18 = 0;
    c.Panel(f);
    CHECK(c.GateTarget() == 1.f);
    c.NoteOn(45);
    c.NoteOff(45);
    CHECK(c.GateTarget() == 1.f);
    CHECK(c.Bank().TargetValue(kFrequency) == 45.f);

    // All Notes Off leaves the drone; All Sound Off closes it until a note or SW18 reopens it.
    c.AllNotesOff();
    CHECK(c.GateTarget() == 1.f);
    c.AllSoundOff();
    CHECK(c.GateTarget() == 0.f);
    c.Panel(f);
    CHECK(c.GateTarget() == 0.f); // the unchanged panel does not reopen it
    c.NoteOn(45);
    CHECK(c.GateTarget() == 1.f);
    c.NoteOff(45);
    c.AllSoundOff();
    f.sw18 = 1;
    c.Panel(f);
    f.sw18 = 0;
    c.Panel(f);
    CHECK(c.GateTarget() == 1.f);

    // More notes than the stack holds: the oldest goes, nothing breaks.
    NoteStack stack;
    for(uint8_t i = 0; i < 20; ++i)
        stack.Push(i);
    CHECK(stack.Size() == 16);
    CHECK(stack.Top() == 19);
    stack.Push(19);
    CHECK(stack.Size() == 16);

    // All eleven parameters and the warp answer to their CC.
    for(uint8_t i = 0; i < kParamCount; ++i) {
        c.ControlChange(uint8_t(kFirstParamCc + i), 0);
        CHECK(c.Bank().Norm(i) == 0.f);
        c.ControlChange(uint8_t(kFirstParamCc + i), 127);
        CHECK(c.Bank().Norm(i) == 1.f);
    }
    c.ControlChange(kWarpCc, 127);
    CHECK(c.Bank().Warp());
    c.ControlChange(kWarpCc, 0);
    CHECK(!c.Bank().Warp());

    // S1 toggles the warp on the press only.
    PanelFrame k = Pots(0.5f);
    k.keys = 1;
    c.Panel(k);
    CHECK(c.Bank().Warp());
    c.Panel(k);
    CHECK(c.Bank().Warp());
    k.keys = 0;
    c.Panel(k);
    k.keys = 1;
    c.Panel(k);
    CHECK(!c.Bank().Warp());
}

// The engine with its real stages, on the heap as on the kit's memory.
template <class StagesType> struct Rig {
    using E = infrasonic::FeedbackSynth::Engine<StagesType>;
    std::unique_ptr<E> engine = std::make_unique<E>();
    using Reverb = typename StagesType::Reverb;
    using Echo = typename StagesType::Echo;
    std::unique_ptr<typename Reverb::Storage> reverb_memory = std::make_unique<typename Reverb::Storage>();
    std::unique_ptr<typename Echo::Storage> echo_memory = std::make_unique<typename Echo::Storage>();
    Reverb reverb;
    Echo echo;
    Control control;
    Rig() {
        CHECK(reverb.Prepare({48000, 4}, *reverb_memory));
        CHECK(echo.Prepare({48000, 4}, *echo_memory));
        engine->Init(kSampleRate, &reverb, &echo);
        control.Init(kControlRate);
    }
    EngineSink<StagesType> Sink() { return EngineSink<StagesType>(*engine, reverb, echo); }
    void Render(std::vector<float>& left, std::vector<float>& right, std::size_t blocks) {
        auto sink = Sink();
        for(std::size_t b = 0; b < blocks; ++b) {
            control.Bank().Process(sink);
            for(int i = 0; i < 4; ++i) {
                float l = 0.f, r = 0.f;
                engine->Process(0.f, l, r);
                left.push_back(l);
                right.push_back(r);
            }
        }
    }
};

template <class R> static void SetSinging(R& rig) {
    ParamBank& p = rig.control.Bank();
    p.SetNorm(kFeedbackGain, 0.95f);
    p.SetNorm(kFeedbackBody, 0.3f);
    p.SetNorm(kReverbMix, 0.4f);
    p.SetNorm(kReverbDecay, 0.7f);
    p.SetNorm(kEchoSend, 0.6f);
    p.SetNorm(kEchoFeedback, 0.5f);
    p.SetNorm(kOutputVolume, 1.f);
}

static float Rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double sum = 0.0;
    for(std::size_t i = from; i < to; ++i)
        sum += double(x[i]) * double(x[i]);
    return float(std::sqrt(sum / double(to - from)));
}

static void TestEngine() {
    Rig<Stages> rig;
    SetSinging(rig);
    std::vector<float> l, r;
    rig.Render(l, r, 12000 * 3); // 3 s
    bool finite = true;
    float peak = 0.f;
    for(std::size_t i = 0; i < l.size(); ++i) {
        finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
        peak = std::fmax(peak, std::fabs(l[i]));
    }
    CHECK(finite);
    CHECK(peak < 8.f);
    CHECK(Rms(l, l.size() / 2, l.size()) > 0.01f); // the loop sings from its noise seed

    // Default settings: feedback at -60 dB, no sound beyond the noise seed.
    Rig<Stages> quiet;
    std::vector<float> ql, qr;
    quiet.Render(ql, qr, 12000);
    CHECK(Rms(ql, 0, ql.size()) < 0.001f);
}

// The gate must not touch the Audrey II path while it is open.
static void TestGate() {
    Rig<Stages> open, untouched;
    SetSinging(open);
    SetSinging(untouched);
    open.engine->SetGate(1.f);
    std::vector<float> a, ar, b, br;
    open.Render(a, ar, 12000);
    untouched.Render(b, br, 12000);
    bool same = a.size() == b.size();
    for(std::size_t i = 0; same && i < a.size(); ++i)
        same = a[i] == b[i] && ar[i] == br[i];
    CHECK(same);

    // Closed, the output falls while the loop keeps running; reopened, it is back at once.
    Rig<Stages> gated;
    SetSinging(gated);
    gated.control.Bank().SetNorm(kEchoSend, 0.f);
    std::vector<float> g, gr;
    gated.Render(g, gr, 12000 * 2);
    const float before = Rms(g, g.size() - 4800, g.size());
    CHECK(before > 0.01f);
    gated.engine->SetGate(0.f);
    gated.Render(g, gr, 12000);
    const float closed = Rms(g, g.size() - 4800, g.size());
    CHECK(closed < before * 0.001f);
    gated.engine->SetGate(1.f);
    gated.Render(g, gr, 12000 / 20); // 50 ms
    CHECK(Rms(g, g.size() - 480, g.size()) > before * 0.3f);
}

// The string keeps the DC blocker of the DaisySP version Audrey II was built with.
static void TestDcBlock() {
    infrasonic::StringDcBlock block;
    block.Init();
    float y = 0.f;
    for(int i = 0; i < 2000; ++i)
        y = block.Process(1.f);
    CHECK(std::fabs(y) < 1e-6f);
    block.Init();
    CHECK(block.Process(1.f) == 1.f);
    CHECK(Near(block.Process(1.f), 0.99f, 1e-6f));
}

// Regression pin. Below self-oscillation the loop is stable, so these values do not depend on
// tiny differences between compilers; a real change of the sound moves them.
static void TestGolden() {
    Rig<Stages> rig;
    ParamBank& p = rig.control.Bank();
    p.SetNorm(kFeedbackGain, 0.5f);
    p.SetNorm(kFeedbackBody, 0.3f);
    p.SetNorm(kReverbMix, 0.4f);
    p.SetNorm(kReverbDecay, 0.7f);
    p.SetNorm(kEchoSend, 0.6f);
    p.SetNorm(kEchoTime, 0.1f);
    p.SetNorm(kEchoFeedback, 0.5f);
    p.SetNorm(kOutputVolume, 1.f);
    auto sink = rig.Sink();
    p.Process(sink);
    static const float expected[8] = {3.6482916e-03f, 1.9441841e-02f, -8.2759367e-04f, 9.3128551e-03f,
                                      1.2815709e-03f, -7.2225723e-03f, 1.8671565e-03f, 1.6843195e-03f};
    int checkpoint = 0;
    for(int i = 1; i <= 8 * 2400; ++i) {
        float l = 0.f, r = 0.f;
        rig.engine->Process(i == 1 ? 1.f : 0.f, l, r);
        if(i % 2400 == 0) {
            if(std::getenv("AUDREY_PRINT_GOLDEN"))
                std::printf("%.7ef,\n", double(l));
            CHECK(Near(l, expected[checkpoint], 1e-4f));
            ++checkpoint;
        }
    }
}

// The adapter hands one frame to the FX contract: same result as a block.
static void TestFrameStage() {
    auto memory = std::make_unique<seed::fx::StereoEcho::Storage>();
    auto block_memory = std::make_unique<seed::fx::StereoEcho::Storage>();
    FrameStage<seed::fx::StereoEcho> frames;
    seed::fx::StereoEcho blocks;
    CHECK(frames.Prepare({48000, 4}, *memory));
    CHECK(blocks.Prepare({48000, 4}, *block_memory));
    seed::fx::echo::Parameters p;
    p.time_s = 0.02f;
    p.feedback = 0.7f;
    frames.SetParameters(p);
    blocks.SetParameters(p);
    bool same = true;
    for(int b = 0; b < 2000; ++b) {
        float l[4], r[4], block_l[4], block_r[4], frame_l[4], frame_r[4];
        for(int i = 0; i < 4; ++i) {
            l[i] = b == 0 && i == 0 ? 1.f : 0.f;
            r[i] = b == 1 && i == 0 ? -1.f : 0.f;
            frames.Process(l[i], r[i], frame_l[i], frame_r[i]);
        }
        blocks.Process(seed::audio::StereoBlock{l, r, block_l, block_r, 4});
        for(int i = 0; i < 4; ++i)
            same = same && frame_l[i] == block_l[i] && frame_r[i] == block_r[i];
    }
    CHECK(same);
}

// Replacing a stage changes the Stages type only (modular policy). The sink needs a
// `feedback` parameter from the reverb slot; everything else stays as it is.
struct PassThrough {
    struct Storage {};
    struct Parameters {
        float feedback = 0.f;
    };
    bool Prepare(seed::audio::Config, Storage&) { return true; }
    const Parameters& Requested() const { return parameters; }
    void SetParameters(const Parameters& p) { parameters = p; }
    void Process(const seed::audio::StereoBlock& block) {
        for(std::size_t i = 0; i < block.frames; ++i) {
            block.out_left[i] = block.left[i];
            block.out_right[i] = block.right[i];
        }
    }
    Parameters parameters;
};
struct DryStages {
    using Reverb = FrameStage<PassThrough>;
    using Echo = Stages::Echo;
    using Damper = Stages::Damper;
};
struct EchoInReverbSlot { // a real, different effect in the loop
    using Reverb = FrameStage<seed::fx::StereoEcho>;
    using Echo = Stages::Echo;
    using Damper = Stages::Damper;
};

template <class StagesType> static void CheckSings() {
    Rig<StagesType> rig;
    SetSinging(rig);
    std::vector<float> l, r;
    rig.Render(l, r, 12000);
    bool finite = true;
    for(float x : l)
        finite = finite && std::isfinite(x);
    CHECK(finite);
    CHECK(Rms(l, l.size() / 2, l.size()) > 0.001f);
}

static void TestReplaceableStages() {
    CheckSings<DryStages>();
    CheckSings<EchoInReverbSlot>();
}

int main() {
    TestParameterTable();
    TestSmoothing();
    TestPotsAndPages();
    TestMidi();
    TestDcBlock();
    TestEngine();
    TestGate();
    TestGolden();
    TestFrameStage();
    TestReplaceableStages();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("audrey-3 host tests passed\n");
    return 0;
}
