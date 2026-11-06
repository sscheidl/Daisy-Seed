// SPDX-License-Identifier: MIT
// Module tests of Grain Cloud: control laws, random generator, window table, envelope,
// output stage, grain renderer and the event bridge. Engine rules are in test_engine.cpp.
#include "test_util.h"
#include <atomic>
#include <memory>
#include <thread>

using namespace grain_cloud;
using namespace testing;

// B4: equal knob steps are equal ratios of rate, across the whole range.
static void TestDensityLaw() {
    CHECK(Near(DensityHz(0.f), kDensityMinHz, 1e-4f));
    CHECK(Near(DensityHz(1.f), kDensityMinHz * 2048.f, 0.01f));
    float last = DensityHz(0.f);
    const float expected = std::exp2(kDensityOctaves * 0.05f);
    for(int i = 1; i <= 20; ++i) {
        const float now = DensityHz(float(i) * 0.05f);
        CHECK_MSG(Near(now / last, expected, expected * 1e-3f), "step %d ratio %f", i, double(now / last));
        last = now;
    }
    // The same modulation depth moves the same number of octaves at every base.
    for(float base : {0.1f, 0.4f, 0.8f})
        CHECK(Near(std::log2(DensityHz(base + 0.1f) / DensityHz(base)), 0.1f * kDensityOctaves, 1e-3f));
}

// R6: one grain at full level; RMS of unrelated grains settles; the law has no step.
static void TestCountLaw() {
    CHECK(Near(CountGain(0.f), 1.f, 1e-5f));
    CHECK(Near(CountGain(1.f), 1.f, 1e-5f));
    float previous = 1.f;
    for(float n = 1.f; n <= 256.f; n += 0.25f) {
        const float gain = CountGain(n);
        CHECK(gain <= previous + 1e-6f);
        CHECK(previous - gain < 0.1f);
        previous = gain;
    }
    const float dense = CountGain(64.f) * std::sqrt(64.f), denser = CountGain(256.f) * std::sqrt(256.f);
    CHECK(Near(dense, denser, 0.03f));
}

// B10: triangular shape from the generator alone.
static void TestTriangularSpray() {
    GrainRng rng(7u);
    constexpr int kDraws = 200000;
    int bins[8] = {};
    double sum = 0.0, square = 0.0;
    for(int i = 0; i < kDraws; ++i) {
        const float x = rng.Triangular();
        CHECK(x >= -1.f && x < 1.f);
        sum += x;
        square += double(x) * x;
        ++bins[int((x + 1.f) * 4.f)];
    }
    CHECK(Near(float(sum / kDraws), 0.f, 0.01f));
    CHECK(Near(float(square / kDraws), 1.f / 6.f, 0.005f));
    // Exact bin weights of the triangle: 1, 3, 5, 7 of 32 on each side.
    for(int b = 0; b < 8; ++b) {
        const int side = b < 4 ? b : 7 - b;
        const float expected = float(2 * side + 1) / 32.f;
        CHECK_MSG(Near(float(bins[b]) / float(kDraws), expected, 0.005f), "bin %d", b);
    }
    // Two generators with different seeds do not walk together.
    GrainRng a(1u), b(2u);
    int equal = 0;
    for(int i = 0; i < 1000; ++i)
        equal += a.Next() == b.Next();
    CHECK(equal == 0);
}

// B6: shapes, ends, slew, and bounded work.
static void TestWindowTable() {
    WindowTable window;
    const float tick = 0.001f;
    window.Prepare(tick);
    for(std::size_t s = 0; s < kWindowShapes; ++s) {
        CHECK(window.Shape(s)[0] == 0.f);
        CHECK(window.Shape(s)[kWindowSegments] == 0.f);
        float peak = 0.f;
        for(std::size_t i = 0; i < kWindowPoints; ++i) {
            CHECK(window.Shape(s)[i] >= 0.f);
            peak = std::fmax(peak, window.Shape(s)[i]);
        }
        CHECK(peak > 0.7f && peak <= 1.0001f);
    }
    CHECK(window.Tick() == 0); // resting: no work at all
    // Hann through the interpolated read.
    for(uint32_t k = 1; k < 64; ++k) {
        const float u = float(k) / 64.f + 0.0013f;
        const float expected = 0.5f - 0.5f * std::cos(6.2831853f * u);
        CHECK(Near(WindowAt(window.Data(), uint32_t(double(u) * 4294967296.0)), expected, 1e-4f));
    }
    // A jump of the target from 0 to 1 is a slow morph with a bounded change per tick.
    window.SetTarget(1.f);
    float before[kWindowPoints];
    int ticks = 0;
    float worst = 0.f;
    while(window.Morph() < 1.f && ticks < 5000) {
        for(std::size_t i = 0; i < kWindowPoints; ++i)
            before[i] = window.Data()[i];
        CHECK(window.Tick() == kWindowPoints);
        for(std::size_t i = 0; i < kWindowPoints; ++i)
            worst = std::fmax(worst, std::fabs(window.Data()[i] - before[i]));
        CHECK(window.Data()[0] == 0.f && window.Data()[kWindowSegments] == 0.f);
        ++ticks;
    }
    CHECK_MSG(ticks >= 990 && ticks <= 1010, "ticks %d", ticks);
    CHECK_MSG(worst < 0.004f, "worst change per tick %f", double(worst));
    for(std::size_t i = 0; i < kWindowPoints; ++i)
        CHECK(window.Data()[i] == window.Shape(kWindowShapes - 1)[i]);
    CHECK(window.Tick() == 0);
    // Turning back mid-way needs no rebuild, only a new direction.
    window.SetTarget(0.f);
    for(int i = 0; i < 100; ++i)
        window.Tick();
    window.SetTarget(1.f);
    CHECK(window.Tick() == kWindowPoints);
    static_assert(sizeof(Grain) <= 48, "");
}

// C7: curve shapes.
static void TestEnvelopeCurves() {
    const float tick = 0.001f;
    const EnvelopeRates rates = MakeRates(0.3f, 0.3f, 0.25f, 0.3f, tick);
    Envelope env;
    env.NoteOn();
    // Linear attack: equal steps.
    float last = 0.f, first_step = 0.f;
    int attack = 0;
    while(env.CurrentStage() == Envelope::Stage::Attack) {
        const float level = env.Step(rates);
        if(env.CurrentStage() != Envelope::Stage::Attack)
            break;
        if(attack == 0)
            first_step = level - last;
        CHECK(Near(level - last, first_step, 1e-5f));
        last = level;
        ++attack;
    }
    CHECK(Near(float(attack), AttackSeconds(0.3f) / tick, 2.f));
    // Exponential decay: constant ratio of the distance to sustain.
    float distance = env.Level() - rates.sustain;
    for(int i = 0; i < 50; ++i) {
        const float next = env.Step(rates) - rates.sustain;
        CHECK(Near(next / distance, rates.decay_coeff, 1e-3f));
        distance = next;
    }
    while(env.CurrentStage() == Envelope::Stage::Decay)
        env.Step(rates);
    CHECK(env.CurrentStage() == Envelope::Stage::Sustain && env.Level() == rates.sustain);
    // Exponential release: constant ratio, ends exactly at zero.
    env.NoteOff();
    float level = env.Level();
    int release = 0;
    while(!env.Idle() && release < 100000) {
        const float next = env.Step(rates);
        if(!env.Idle())
            CHECK(Near(next / level, rates.release_coeff, 1e-4f));
        level = next;
        ++release;
    }
    CHECK(env.Idle() && env.Level() == 0.f);

    // Retrigger continues from the current level: no step at any stage.
    Envelope retrigger;
    retrigger.NoteOn();
    for(int i = 0; i < 2000; ++i)
        retrigger.Step(rates);
    retrigger.NoteOff();
    for(int i = 0; i < 10; ++i)
        retrigger.Step(rates);
    const float held = retrigger.Level();
    retrigger.NoteOn();
    CHECK(retrigger.Level() == held);
    const float after = retrigger.Step(rates);
    CHECK(Near(after - held, rates.attack_step, 1e-6f));
}

// C7: the fast release does not depend on the release setting, and hold freezes.
static void TestFastRelease() {
    for(float tick : {0.001f, 0.004f}) {
        const EnvelopeRates rates = MakeRates(0.f, 0.f, 1.f, 1.f, tick); // the longest release
        Envelope env;
        env.NoteOn();
        for(int i = 0; i < 100; ++i)
            env.Step(rates);
        CHECK(Near(env.Level(), 1.f, 1e-4f));
        env.BeginFastRelease(rates);
        int ticks = 0;
        float previous = env.Level(), worst = 0.f;
        while(!env.Idle() && ticks < 100) {
            const float level = env.Step(rates);
            worst = std::fmax(worst, previous - level);
            previous = level;
            ++ticks;
        }
        CHECK(env.Idle());
        CHECK(float(ticks) * tick <= kFastReleaseSeconds + tick);
        CHECK(worst <= 0.51f); // at least two steps, each ramped by the voice gain
    }
    const EnvelopeRates rates = MakeRates(0.f, 0.f, 0.5f, 0.5f, 0.001f);
    Envelope env;
    env.NoteOn();
    for(int i = 0; i < 2000; ++i)
        env.Step(rates);
    const float level = env.Level();
    env.BeginHold();
    for(int i = 0; i < 100; ++i)
        CHECK(env.Step(rates) == level);
    env.NoteOff();
    CHECK(env.CurrentStage() == Envelope::Stage::Hold);
    env.BeginFastRelease(rates);
    CHECK(env.CurrentStage() == Envelope::Stage::FastRelease);
}

// R4: the ramp is exactly linear and lands on its target.
static void TestRamp() {
    Ramp ramp;
    ramp.Reset(0.f);
    ramp.Set(1.f, 48);
    float last = 0.f;
    for(int i = 0; i < 48; ++i) {
        const float value = ramp.Next();
        CHECK(Near(value - last, 1.f / 48.f, 1e-5f));
        last = value;
    }
    CHECK(ramp.Value() == 1.f && ramp.Done());
    ramp.Set(0.5f, 96);
    ramp.Next();
    ramp.Set(0.f, 48); // a new target before the old one: continues from where it is
    CHECK(ramp.Value() < 1.f && ramp.Value() > 0.99f);
}

// R5: below the knee nothing changes; above it the curve is smooth and bounded.
static void TestOutputStage() {
    CHECK(SoftClip(0.5f) == 0.5f);
    CHECK(SoftClip(-kClipKnee) == -kClipKnee);
    CHECK(SoftClip(1000.f) < 1.f);
    CHECK(SoftClip(-1000.f) > -1.f);
    float last = 0.f;
    for(float x = 0.f; x < 8.f; x += 0.001f) {
        const float y = SoftClip(x);
        CHECK(y >= last);
        CHECK(y - last <= 0.00101f); // slope at most one: no kink upward at the knee
        last = y;
    }
    OutputStage output;
    output.Prepare(48000.f);
    float l[48], r[48];
    for(int i = 0; i < 48; ++i) {
        l[i] = 4.f;
        r[i] = -0.25f;
    }
    l[3] = std::nanf("");
    r[4] = INFINITY;
    output.Process(l, r, l, r, 48);
    CHECK(output.NonFinite() == 2);
    CHECK(l[3] == 0.f && r[4] == 0.f);
    CHECK(output.PeakIn() >= 4.f);
    CHECK(output.PeakOut() < 1.f);
    CHECK(r[0] == -0.25f);
}

static Grain MakeGrain(double position, double step, uint32_t frames, bool wraps) {
    Grain g;
    g.pos = int64_t(position * 4294967296.0);
    g.inc = int64_t(step * 4294967296.0);
    g.win_step = uint32_t(4294967296.0 / double(frames));
    g.gain_l = g.gain_r = 1.f;
    g.flags = wraps ? kGrainWraps : 0;
    return g;
}

// Fast and slow paths agree, and neighbours are read by rule at every edge.
static void TestRenderer() {
    WindowTable window;
    window.Prepare(0.001f);
    const auto data = RampSample(4096);
    const SampleView view = ViewOf(data);

    // Linear interpolation a + t * (b - a), with the window applied.
    float l[48] = {}, r[48] = {};
    Grain g = MakeGrain(100.25, 1.0, 48000, true);
    g.win_phase = 0x80000000u; // peak of the Hann
    CHECK(RenderGrain(g, view, window.Data(), l, r, 48));
    for(int i = 0; i < 48; ++i)
        CHECK(Near(l[i], 100.25f + float(i), 0.02f));

    // Birth offset: silence, then the grain, inside one block.
    float d[48] = {}, e[48] = {};
    Grain delayed = MakeGrain(100.0, 1.0, 48000, true);
    delayed.win_phase = 0x80000000u;
    delayed.delay = 20;
    RenderGrain(delayed, view, window.Data(), d, e, 48);
    CHECK(d[19] == 0.f && d[20] > 99.f);
    CHECK(delayed.delay == 0);

    // A grain ends exactly when its phase carries, after `frames` samples.
    Grain end = MakeGrain(10.0, 1.0, 64, true);
    float m[48] = {}, n[48] = {};
    CHECK(RenderGrain(end, view, window.Data(), m, n, 48));
    CHECK(!RenderGrain(end, view, window.Data(), m, n, 48));

    // Wrap: the right neighbour of the last loop frame is the loop start.
    std::vector<int16_t> loop = {100, 200, 300, 400};
    SampleView small = ViewOf(loop);
    float a[48] = {}, b[48] = {};
    Grain wrap = MakeGrain(3.5, 1.0, 48000, true);
    wrap.win_phase = 0x80000000u;
    RenderGrain(wrap, small, window.Data(), a, b, 48);
    CHECK(Near(a[0], 250.f, 0.05f)); // halfway 400 -> 100
    CHECK(Near(a[1], 150.f, 0.05f)); // 0.5 after the wrap
    Grain back = MakeGrain(0.5, -1.0, 48000, true);
    back.win_phase = 0x80000000u;
    float c[48] = {}, f[48] = {};
    RenderGrain(back, small, window.Data(), c, f, 48);
    CHECK(Near(c[0], 150.f, 0.05f));
    CHECK(Near(c[1], 250.f, 0.05f)); // crossed the loop start backwards

    // Clamp: holds the end frames, for reads past both ends.
    SampleView one_shot = ViewOf(loop, 1, false);
    float h[48] = {}, k[48] = {};
    Grain past = MakeGrain(2.5, 1.0, 48000, false);
    past.win_phase = 0x80000000u;
    RenderGrain(past, one_shot, window.Data(), h, k, 48);
    CHECK(Near(h[0], 350.f, 0.05f));
    CHECK(Near(h[1], 400.f, 0.05f));
    CHECK(Near(h[40], 400.f, 0.05f));
    Grain before = MakeGrain(1.5, -1.0, 48000, false);
    before.win_phase = 0x80000000u;
    float p[48] = {}, q[48] = {};
    RenderGrain(before, one_shot, window.Data(), p, q, 48);
    CHECK(Near(p[40], 100.f, 0.05f));

    // One- and two-frame samples: defined values, no read outside.
    std::vector<int16_t> single = {500}, pair = {100, 300};
    for(double step : {1.0, -1.0, 7.5}) {
        for(const auto* samples : {&single, &pair}) {
            SampleView view_short = ViewOf(*samples, 1, false);
            float u[48] = {}, w[48] = {};
            Grain tiny = MakeGrain(0.0, step, 96, false);
            tiny.win_phase = 0x80000000u;
            RenderGrain(tiny, view_short, window.Data(), u, w, 48);
            for(float x : u)
                CHECK(x >= 0.f && x <= 501.f);
        }
    }

    // The two loops of RenderGrain give the same numbers.
    const auto noise = NoiseSample(1024);
    const SampleView noisy = ViewOf(noise);
    float fast[48] = {}, fast_r[48] = {}, slow[48] = {}, slow_r[48] = {};
    Grain gf = MakeGrain(200.3, 0.77, 4800, true);
    RenderGrain(gf, noisy, window.Data(), fast, fast_r, 48);
    SampleView tight = noisy; // the same data, but the edge is inside this block
    tight.loop_end = 225;
    Grain gs = MakeGrain(200.3, 0.77, 4800, true);
    RenderGrain(gs, tight, window.Data(), slow, slow_r, 48);
    for(int i = 0; i < 30; ++i) // 200.3 + 30 * 0.77 is still below the edge at 224
        CHECK(fast[i] == slow[i]);
}

// Input order holds across both queues; a lost release becomes all notes off.
static void TestBridge() {
    ControlBridge bridge;
    bridge.Post(NoteOn(60, 100));
    bridge.Post(NoteOff(60));
    bridge.Post(NoteOn(60, 100));
    int order[3] = {};
    for(int& type : order) {
        const Event* e = bridge.Peek();
        CHECK(e != nullptr);
        type = int(e->type);
        bridge.Drop();
    }
    CHECK(order[0] == int(EventType::NoteOn) && order[1] == int(EventType::NoteOff) &&
          order[2] == int(EventType::NoteOn));
    CHECK(bridge.Peek() == nullptr);

    for(std::size_t i = 0; i < ControlBridge::kEvents; ++i)
        CHECK(bridge.Post(NoteOn(60, 100)));
    CHECK(!bridge.Post(NoteOn(61, 100))); // notes can be lost and are counted
    CHECK(bridge.Overflows() == 1);
    CHECK(bridge.Post(NoteOff(60)));      // a flood of notes does not block a release
    CHECK(bridge.Post(NoteOn(60, 0)));    // nor one written as a note-on with velocity 0
    CHECK(bridge.LostRelease() == 0);
    for(std::size_t i = 2; i < ControlBridge::kReleases; ++i)
        CHECK(bridge.Post(NoteOff(60)));
    CHECK(!bridge.Post(NoteOff(62)));
    const uint32_t lost = bridge.LostRelease();
    CHECK(lost != 0);
    // Later than every event that got in, so older events can be told from newer ones.
    CHECK(!bridge.Post(NoteOff(63)));
    CHECK(int32_t(bridge.LostRelease() - lost) > 0);
    bridge.ClearLostRelease(lost); // a newer loss must survive clearing the older one
    CHECK(bridge.LostRelease() != 0);
    bridge.ClearLostRelease(bridge.LostRelease());
    CHECK(bridge.LostRelease() == 0);
}

// A queue that can run the producer between two reads of the consumer.
static void (*peek_hook)() = nullptr;
template <class T, std::size_t N>
class HookedQueue : public step_seq::SpscQueue<T, N> {
  public:
    const T* Peek() const {
        if(peek_hook)
            peek_hook();
        return step_seq::SpscQueue<T, N>::Peek();
    }
};
using HookedBridge = BasicControlBridge<HookedQueue>;

static HookedBridge* hooked = nullptr;
static int peeks_until_post = 0, post_pattern = 0;
static void PostAtPeek() {
    if(peeks_until_post-- != 0)
        return;
    switch(post_pattern) {
        case 0: // the note and its end
            hooked->Post(NoteOn(60, 100));
            hooked->Post(NoteOff(60));
            break;
        case 1: // the end of one note and the next note
            hooked->Post(NoteOff(60));
            hooked->Post(NoteOn(60, 100));
            break;
        default:
            hooked->Post(NoteOn(60, 100));
            hooked->Post(NoteOff(60));
            hooked->Post(NoteOn(62, 100));
            hooked->Post(NoteOff(62));
            break;
    }
}

// Every point at which the producer can post during Peek() and Drop(): nothing is lost,
// nothing comes twice, and the order holds. The queue may hold older items or be empty.
static void TestBridgeInterleavings() {
    for(int pattern = 0; pattern < 3; ++pattern) {
        for(int preload = 0; preload < 3; ++preload) {
            for(int at = 0; at < 8; ++at) {
                auto bridge = std::make_unique<HookedBridge>();
                hooked = bridge.get();
                uint32_t total = 0;
                if(preload == 1)
                    total += bridge->Post(NoteOn(50, 100));
                if(preload == 2)
                    total += bridge->Post(NoteOff(50));
                peeks_until_post = at;
                post_pattern = pattern;
                peek_hook = PostAtPeek;
                total += pattern == 2 ? 4u : 2u;
                uint32_t last = 0, seen = 0;
                for(int round = 0; round < 32; ++round) {
                    const Event* e = bridge->Peek();
                    if(!e)
                        continue;
                    const uint32_t sequence = e->sequence;
                    CHECK_MSG(sequence > last, "pattern %d preload %d at %d: %u after %u", pattern, preload, at,
                              sequence, last);
                    last = sequence;
                    ++seen;
                    bridge->Drop();
                }
                peek_hook = nullptr;
                CHECK_MSG(seen == total, "pattern %d preload %d at %d: %u of %u", pattern, preload, at, seen, total);
            }
        }
    }
    // Drop() without a selected item, or a second time, removes nothing.
    ControlBridge bridge;
    bridge.Post(NoteOn(60, 100));
    bridge.Post(NoteOn(62, 100));
    bridge.Drop();
    const Event* first = bridge.Peek();
    CHECK(first && first->note == 60);
    bridge.Drop();
    bridge.Drop();
    const Event* second = bridge.Peek();
    CHECK(second && second->note == 62);
}

// The same with real threads, for TSan and for what the interleaving test cannot reach.
static void TestBridgeThreads() {
    auto bridge = std::make_unique<ControlBridge>();
    constexpr uint32_t kPairs = 1000000;
    std::atomic<uint32_t> received{0};
    std::atomic<uint32_t> posted{0};
    std::thread producer([&] {
        for(uint32_t i = 0; i < kPairs; ++i) {
            while(posted.load(std::memory_order_relaxed) - received.load(std::memory_order_relaxed) > 32)
                std::this_thread::yield();
            bridge->Post(NoteOn(60, 100));
            bridge->Post(NoteOff(60));
            posted.fetch_add(2, std::memory_order_relaxed);
        }
    });
    uint32_t last = 0, disorder = 0;
    while(received.load(std::memory_order_relaxed) < kPairs * 2) {
        const Event* e = bridge->Peek();
        if(!e) {
            std::this_thread::yield();
            continue;
        }
        disorder += e->sequence != last + 1;
        last = e->sequence;
        bridge->Drop();
        received.fetch_add(1, std::memory_order_relaxed);
    }
    producer.join();
    CHECK(bridge->Overflows() == 0);
    CHECK_MSG(disorder == 0, "%u events out of order", disorder);
    CHECK(last == kPairs * 2);
}

int main() {
    TestDensityLaw();
    TestCountLaw();
    TestTriangularSpray();
    TestWindowTable();
    TestEnvelopeCurves();
    TestFastRelease();
    TestRamp();
    TestOutputStage();
    TestRenderer();
    TestBridge();
    TestBridgeInterleavings();
    TestBridgeThreads();
    if(failures) {
        std::printf("test_units: %d failures\n", failures);
        return 1;
    }
    std::printf("test_units: ok\n");
    return 0;
}
