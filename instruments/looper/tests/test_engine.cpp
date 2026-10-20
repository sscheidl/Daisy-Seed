// SPDX-License-Identifier: MIT
// Host tests for the looper engine core: generated signals, block-boundary
// cases, buffer guards, and exact undo/redo snapshots.
#include "looper_engine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

using namespace looper;

static int failures = 0;
#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
            ++failures;                                                               \
        }                                                                             \
    } while (0)
#define CHECK_NEAR(value, expected, tolerance)                                        \
    do {                                                                              \
        const double v_ = (value), e_ = (expected);                                   \
        if (!(std::fabs(v_ - e_) <= (tolerance))) {                                   \
            std::printf("  FAIL %s:%d: %s = %.9g, expected %.9g +- %g\n", __FILE__,     \
                        __LINE__, #value, v_, e_, double(tolerance));                 \
            ++failures;                                                               \
        }                                                                             \
    } while (0)
static int reported = 0;
#define PASS(...)                                                                     \
    do {                                                                              \
        std::printf(failures == reported ? "PASS " : "FAIL ");                         \
        std::printf(__VA_ARGS__);                                                     \
        reported = failures;                                                          \
    } while (0)

constexpr float kRate = 48000.f;
constexpr uint32_t kSecond = 48000;
constexpr uint32_t kGuard = 4096;
constexpr float kCanary = 1234.5f;
constexpr double kPi = 3.14159265358979323846;

using Signal = std::function<Frame(uint64_t)>;

static Frame Tone(uint64_t t, double hz, float amplitude) {
    const double p = 2.0 * kPi * hz * double(t) / kRate;
    return {amplitude * float(std::sin(p)), amplitude * float(std::sin(1.37 * p + 1.0))};
}
static Signal ToneSignal(double hz, float amplitude) {
    return [=](uint64_t t) { return Tone(t, hz, amplitude); };
}
static Signal Constant(float value) {
    return [=](uint64_t) { return Frame{value, -value}; };
}
static const Signal kSilence = [](uint64_t) { return Frame{}; };

// Two buffers with canary guards on both sides; the engine sees only the middle.
struct Rig {
    uint32_t capacity;
    std::vector<Frame> a, b;
    Engine engine;
    uint64_t t = 0;
    std::vector<float> left, right;
    bool capture = false;
    size_t block = 48;

    explicit Rig(uint32_t frames) : capacity(frames), a(frames + 2 * kGuard), b(frames + 2 * kGuard) {
        for (auto* v : {&a, &b})
            for (uint32_t i = 0; i < kGuard; ++i)
                (*v)[i] = (*v)[kGuard + frames + i] = Frame{kCanary, -kCanary};
        CHECK(engine.Init(kRate, a.data() + kGuard, b.data() + kGuard, frames));
    }
    bool GuardsIntact() const {
        for (const auto* v : {&a, &b})
            for (uint32_t i = 0; i < kGuard; ++i)
                for (const Frame& f : {(*v)[i], (*v)[kGuard + capacity + i]})
                    if (f.left != kCanary || f.right != -kCanary)
                        return false;
        return true;
    }
    void Run(uint64_t samples, const Signal& input = kSilence) {
        std::vector<float> il(block), ir(block), ol(block), orr(block);
        while (samples > 0) {
            const size_t n = size_t(std::min<uint64_t>(samples, block));
            for (size_t i = 0; i < n; ++i) {
                const Frame f = input(t + i);
                il[i] = f.left;
                ir[i] = f.right;
            }
            engine.Process(il.data(), ir.data(), ol.data(), orr.data(), n);
            if (capture) {
                left.insert(left.end(), ol.begin(), ol.begin() + long(n));
                right.insert(right.end(), orr.begin(), orr.begin() + long(n));
            }
            t += n;
            samples -= n;
        }
    }
    // Record a first take of exactly `frames` samples (block-aligned stop not required).
    void RecordTake(uint32_t frames, const Signal& input) {
        engine.PressRecord();
        Run(frames, input);
        if (engine.GetState() == State::Recording)
            engine.PressRecord();
    }
    void WaitIdle(const Signal& input = kSilence) {
        for (int i = 0; i < 100000 && engine.Busy(); ++i)
            Run(block, input);
        Run(kOpRampFrames + 16, input);  // finish any fade back in
    }
    void StartCapture() {
        left.clear();
        right.clear();
        capture = true;
    }
};

static std::vector<Frame> Snapshot(const Engine& e) {
    std::vector<Frame> s(e.Length() + e.TailLength());
    for (uint32_t i = 0; i < s.size(); ++i)
        s[i] = e.StoredFrame(i);
    return s;
}
static bool SameState(const Engine& e, const std::vector<Frame>& s) {
    if (e.Length() + e.TailLength() != s.size())
        return false;
    for (uint32_t i = 0; i < s.size(); ++i) {
        const Frame f = e.StoredFrame(i);
        if (f.left != s[i].left || f.right != s[i].right)
            return false;
    }
    return true;
}
static float MaxStep(const std::vector<float>& x, size_t from = 1) {
    float m = 0.f;
    for (size_t i = std::max<size_t>(from, 1); i < x.size(); ++i)
        m = std::max(m, std::fabs(x[i] - x[i - 1]));
    return m;
}
static float MinAbs(const std::vector<float>& x, size_t from, size_t to) {
    float m = 1e9f;
    for (size_t i = from; i < std::min(to, x.size()); ++i)
        m = std::min(m, std::fabs(x[i]));
    return m;
}

// ------------------------------------------------------------------ tests

static void TestInit() {
    std::vector<Frame> a(2000), b(2000);
    Engine e;
    CHECK(!e.Init(kRate, nullptr, b.data(), 2000));
    CHECK(!e.Init(kRate, a.data(), a.data() + 1000, 2000));  // overlapping
    CHECK(!e.Init(kRate, a.data(), b.data(), 0));
    CHECK(!e.Init(0.f, a.data(), b.data(), 2000));
    CHECK(!e.Init(kRate, a.data(), b.data(), kMaxFrames + 1));
    CHECK(e.Init(kRate, a.data(), b.data(), 2000));
    // An engine that failed Init stays silent and ignores controls.
    Engine dead;
    CHECK(!dead.Init(kRate, nullptr, nullptr, 10));
    dead.PressRecord();
    float in[4] = {1, 1, 1, 1}, out[4] = {9, 9, 9, 9};
    dead.Process(in, in, out, out, 4);
    CHECK(out[0] == 0.f && dead.GetState() == State::Empty);
}

static void TestFirstTake() {
    Rig rig(3 * kSecond);
    const Signal in = Constant(.5f);
    rig.RecordTake(kSecond, in);
    Engine& e = rig.engine;
    CHECK(e.GetState() == State::Looping);
    CHECK(e.Length() == kSecond);
    CHECK(e.CanUndo() && !e.CanRedo());
    for (uint32_t k = 0; k < kRecordFadeFrames; k += 37)
        CHECK_NEAR(e.StoredFrame(k).left, .5 * k / kRecordFadeFrames, 1e-6);
    CHECK(e.StoredFrame(kRecordFadeFrames).left == .5f);
    // Both buffers hold the take.
    CHECK(std::memcmp(rig.a.data() + kGuard, rig.b.data() + kGuard, kSecond * sizeof(Frame)) == 0);
    // Post-roll: the input keeps running past the loop end, faded out.
    CHECK(e.TailLength() == 0 && e.Busy());
    rig.Run(kTailFrames, in);
    CHECK(!e.Busy());
    CHECK(e.TailLength() == kTailFrames);
    for (uint32_t k = 0; k < kTailFrames; k += 41)
        CHECK_NEAR(e.StoredFrame(kSecond + k).left, .5 * (1.0 - double(k) / kTailFrames), 1e-6);
    // Automatic stop at the buffer limit, no post-roll without room.
    Rig full(kSecond);
    full.engine.PressRecord();
    full.Run(kSecond + 5000, in);
    CHECK(full.engine.GetState() == State::Looping);
    CHECK(full.engine.Length() == kSecond);
    full.Run(2000);
    CHECK(full.engine.TailLength() == 0);
    CHECK(full.GuardsIntact() && rig.GuardsIntact());
    // Undo of the first take returns to empty; redo brings it back.
    e.Undo();
    rig.WaitIdle();
    CHECK(e.GetState() == State::Empty && e.CanRedo());
    e.Redo();
    rig.WaitIdle();
    CHECK(e.GetState() == State::Looping && e.Length() == kSecond);
    // Erase during a first take abandons it.
    Rig abandon(kSecond);
    abandon.engine.PressRecord();
    abandon.Run(1000, in);
    abandon.engine.Erase();
    abandon.Run(100, in);
    CHECK(abandon.engine.GetState() == State::Empty && !abandon.engine.CanUndo());
    PASS("first take: fade-in, both buffers, post-roll, auto-stop\n");
}

// Natural slope of the test tone plus the slope a 384-frame fade can add.
static float SeamBound(double hz, float amplitude) {
    return float(2 * kPi * hz / kRate * amplitude * 1.37 + 2.0 * amplitude / kSeamFrames) * 1.05f;
}

static void TestSeams() {
    const double hz = 220.0;
    const float amp = .5f;
    const uint32_t length = kSecond + 777;  // not a whole number of periods
    for (SeamMode mode : {SeamMode::Window, SeamMode::Crossfade}) {
        Rig rig(3 * kSecond);
        rig.engine.SetSeamMode(mode);
        rig.RecordTake(length, ToneSignal(hz, amp));
        rig.StartCapture();
        rig.Run(4 * length, ToneSignal(hz, amp));
        const float bound = SeamBound(hz, amp);
        CHECK(MaxStep(rig.left) <= bound && MaxStep(rig.right) <= bound);
        // Second wrap at sample 2 * length (one frame of latency).
        const size_t wrap = 2 * size_t(length);
        if (mode == SeamMode::Window)
            CHECK(MinAbs(rig.left, wrap - 4, wrap + 4) < 1e-3f);  // dip to zero
        CHECK(rig.GuardsIntact());
    }
    // Sustained material: the window dips, the crossfade keeps the level.
    for (SeamMode mode : {SeamMode::Window, SeamMode::Crossfade}) {
        Rig rig(3 * kSecond);
        rig.engine.SetSeamMode(mode);
        rig.RecordTake(length, Constant(.5f));
        rig.Run(length + 2000, Constant(.5f));
        rig.StartCapture();
        rig.Run(2 * length);
        const float low = MinAbs(rig.left, 0, rig.left.size());
        if (mode == SeamMode::Window)
            CHECK(low < 1e-3f);
        else
            CHECK_NEAR(low, .5, 1e-5);
        CHECK(MaxStep(rig.left) <= .5f / kSeamFrames * 1.01f);
    }
    // At the exact capacity there is no post-roll: the crossfade mode gates the seam.
    {
        Rig rig(length);
        rig.engine.SetSeamMode(SeamMode::Crossfade);
        rig.engine.PressRecord();
        rig.Run(length + 10, ToneSignal(hz, amp));
        CHECK(rig.engine.Length() == length && rig.engine.TailLength() == 0);
        rig.StartCapture();
        rig.Run(3 * length);
        CHECK(MaxStep(rig.left) <= SeamBound(hz, amp));
        CHECK(rig.GuardsIntact());
    }
    // Start/Size window seams inside the loop, both modes, forward and reverse.
    for (SeamMode mode : {SeamMode::Window, SeamMode::Crossfade})
        for (bool reverse : {false, true}) {
            Rig rig(3 * kSecond);
            rig.engine.SetSeamMode(mode);
            rig.RecordTake(2 * kSecond, ToneSignal(hz, amp));
            rig.engine.SetStart(.3f);
            rig.engine.SetSize(.4f);
            rig.engine.SetReverse(reverse);
            rig.Run(kSecond);
            const uint32_t size = rig.engine.WindowSize();
            CHECK(size == kMinWindowFrames + uint32_t(.16 * double(2 * kSecond - kMinWindowFrames)));
            CHECK(rig.engine.WindowStart() == uint32_t(.3 * 2 * kSecond));
            rig.StartCapture();
            rig.Run(4 * size);
            CHECK(MaxStep(rig.left) <= SeamBound(hz, amp));
        }
    // Switching the seam mode while playing never steps the output.
    {
        Rig rig(3 * kSecond);
        rig.RecordTake(kMinWindowFrames + 500, ToneSignal(hz, amp));
        rig.StartCapture();
        for (int i = 0; i < 200; ++i) {
            rig.engine.SetSeamMode(i & 1 ? SeamMode::Crossfade : SeamMode::Window);
            rig.Run(97 + (i * 31) % 300, ToneSignal(hz, amp));  // the post-roll hears the input
        }
        CHECK(MaxStep(rig.left) <= SeamBound(hz, amp));
    }
    PASS("seams: window dip and crossfade are click-free, also at capacity and in windows\n");
}

static void TestWindowAndJump() {
    Rig rig(5 * kSecond);
    const uint32_t length = 4 * kSecond;
    rig.RecordTake(length, ToneSignal(330.0, .5f));
    Engine& e = rig.engine;
    CHECK(e.WindowStart() == 0 && e.WindowSize() == length);
    e.SetSize(0.f);
    rig.Run(kSeamFrames + 10);  // a running jump delays the next change
    CHECK(e.WindowSize() == kMinWindowFrames);  // 24 ms minimum window
    e.SetSize(.002f);                           // inside the dead band
    rig.Run(kSeamFrames + 10);  // a running jump delays the next change
    CHECK(e.WindowSize() == kMinWindowFrames);
    e.SetSize(.998f);
    rig.Run(kSeamFrames + 10);  // a running jump delays the next change
    CHECK(e.WindowSize() == length);
    e.SetSize(.5f);
    e.SetStart(.5f);
    rig.Run(kSeamFrames + 10);  // a running jump delays the next change
    CHECK(e.WindowStart() == length / 2);
    CHECK(e.WindowSize() == kMinWindowFrames + uint32_t(.25 * double(length - kMinWindowFrames)));
    CHECK(e.Position() >= e.WindowStart() && e.Position() < e.WindowStart() + e.WindowSize());
    // Moving Start repeatedly forces head jumps; each is a 384-frame crossfade.
    rig.StartCapture();
    for (int i = 0; i < 40; ++i) {
        e.SetStart(float(i % 7) / 7.f);
        e.SetSize(float(i % 5) / 5.f);
        rig.Run(1500);
    }
    CHECK(MaxStep(rig.left) <= SeamBound(330.0, .5f));
    CHECK(rig.GuardsIntact());
    PASS("start/size window: 1152 + size^2 (len - 1152), dead bands, jump crossfade\n");
}

static void TestOverdubModes() {
    Rig rig(3 * kSecond);
    Engine& e = rig.engine;
    rig.RecordTake(kSecond, Constant(.25f));
    rig.WaitIdle();
    // Sound on Sound clamps the sum to +-1.
    e.PressRecord();
    rig.Run(2 * kSecond, Constant(.9f));
    e.PressRecord();
    rig.WaitIdle();
    for (uint32_t i = kRecordFadeFrames; i < kSecond; i += 997)
        CHECK(e.StoredFrame(i).left == 1.f && e.StoredFrame(i).right == -1.f);
    // Replace: old material crossfades to the new input over 960 frames.
    e.Undo();
    rig.WaitIdle();
    const uint32_t from = e.Position();
    e.SetLoopMode(LoopMode::Replace);
    e.PressRecord();
    rig.Run(kSecond / 4, Constant(-.5f));
    e.PressRecord();
    rig.Run(2 * kPunchFadeFrames);
    rig.WaitIdle();
    // Fully replaced in the middle of the punch, untouched far before it.
    const uint32_t middle = (from + kSecond / 8) % kSecond;
    CHECK(e.StoredFrame(middle).left == -.5f);
    const uint32_t before = (from + kSecond - 3 * kPunchFadeFrames) % kSecond;
    CHECK(e.StoredFrame(before).left == (before < kRecordFadeFrames ? .25f * before / kRecordFadeFrames : .25f));
    // Monotonic punch-in ramp from .25 to -.5.
    float previous = 1.f;
    bool monotonic = true;
    for (uint32_t k = 2; k < kPunchFadeFrames; ++k) {
        const float v = e.StoredFrame((from + k) % kSecond).left;
        monotonic &= v <= previous + 1e-6f;
        previous = v;
    }
    CHECK(monotonic);
    CHECK_NEAR(e.StoredFrame((from + kPunchFadeFrames / 2) % kSecond).left, .25 - .75 * .5, .005);
    CHECK(rig.GuardsIntact());
    PASS("sound on sound clamps to +-1, replace punches with 960-frame fades\n");
}

// Undo after the 1st, 2nd and 3rd overdub returns exactly the previous layer.
static void TestUndoLayers() {
    for (bool waitForCopy : {true, false}) {
        Rig rig(6 * kSecond);
        Engine& e = rig.engine;
        rig.RecordTake(5 * kSecond, ToneSignal(110.0, .2f));
        rig.WaitIdle();
        std::vector<std::vector<Frame>> states{Snapshot(e)};
        // Layers far from frame 0, where the background copy starts, so that
        // undo reads uncopied blocks through the other buffer.
        while (!waitForCopy && e.Position() < 4 * kSecond)
            rig.Run(rig.block);
        for (int layer = 1; layer <= 3; ++layer) {
            e.PressRecord();
            // Short layers leave most of the copy to the background job.
            rig.Run(waitForCopy ? 5 * kSecond + 1000 : kSecond / 20,
                    ToneSignal(150.0 + 70.0 * layer, .2f));
            e.PressRecord();
            rig.Run(kPunchFadeFrames + 10);
            states.push_back(Snapshot(e));
            if (waitForCopy)
                rig.WaitIdle();
            CHECK(e.CanUndo() && !e.CanRedo());
            e.Undo();
            rig.Run(kOpRampFrames + 10);
            CHECK(!e.OperationPending());
            CHECK(SameState(e, states[size_t(layer - 1)]));
            e.Redo();
            rig.Run(kOpRampFrames + 10);
            CHECK(SameState(e, states[size_t(layer)]));
            if (!waitForCopy) {
                e.Undo();
                rig.Run(kOpRampFrames + 10);
                // A new layer waits while the undone state still borrows
                // uncopied blocks from the other buffer.
                const bool copying = e.UndoCopyPending();
                if (layer > 1)
                    CHECK(copying);
                if (copying) {
                    e.PressRecord();
                    rig.Run(10);
                    CHECK(e.RecordPending() && !e.Overdubbing());
                    CHECK(SameState(e, states[size_t(layer - 1)]));
                    e.PressRecord();  // withdraw the request
                    CHECK(!e.RecordPending());
                }
                rig.WaitIdle();
                CHECK(SameState(e, states[size_t(layer - 1)]));
                e.Redo();
                rig.WaitIdle();
                CHECK(SameState(e, states[size_t(layer)]));
            }
        }
        // Undo once more after the third layer, then erase and undo the erase.
        e.Undo();
        rig.WaitIdle();
        CHECK(SameState(e, states[2]));
        e.Erase();
        rig.WaitIdle();
        CHECK(e.GetState() == State::Empty && e.Length() == 0 && e.CanUndo());
        e.Undo();
        rig.WaitIdle();
        CHECK(e.GetState() == State::Looping && SameState(e, states[2]));
        e.Redo();
        rig.WaitIdle();
        CHECK(e.GetState() == State::Empty);
        CHECK(rig.GuardsIntact());
    }
    PASS("undo/redo exact after 1st, 2nd, 3rd overdub, with and without finished copy; erase undoable\n");
}

// Record, overdub, undo and redo at long lengths; the buffer is the full 87 s.
static void TestLongLoops(Rig& rig, double seconds) {
    Engine& e = rig.engine;
    CHECK(e.Init(kRate, rig.a.data() + kGuard, rig.b.data() + kGuard, rig.capacity));
    rig.t = 0;
    const uint32_t requested = uint32_t(seconds * kSecond);
    const uint32_t expected = std::min(requested, rig.capacity);
    e.PressRecord();
    rig.Run(requested, ToneSignal(97.0, .3f));
    if (e.GetState() == State::Recording)
        e.PressRecord();
    CHECK(e.Length() == expected);
    rig.WaitIdle();
    const auto take = Snapshot(e);
    // Overdub a full pass across the loop seam, including the 43.5 s point.
    e.PressRecord();
    rig.Run(expected + kSecond, ToneSignal(131.0, .3f));
    e.PressRecord();
    rig.Run(kPunchFadeFrames + 10);
    const auto layer = Snapshot(e);
    CHECK(!SameState(e, take));
    e.Undo();
    rig.Run(kOpRampFrames + 10);
    CHECK(SameState(e, take));
    e.Redo();
    rig.Run(kOpRampFrames + 10);
    CHECK(SameState(e, layer));
    rig.WaitIdle();
    // Rejected loads leave data and state intact.
    const bool canUndo = e.CanUndo(), canRedo = e.CanRedo();
    const uint32_t position = e.Position();
    std::vector<Frame> source(rig.capacity + 1, Frame{.1f, -.1f});
    CHECK(!e.LoadLoop(source.data(), rig.capacity + 1));
    CHECK(!e.LoadLoop(source.data(), 0));
    CHECK(!e.LoadLoop(nullptr, 100));
    CHECK(SameState(e, layer) && e.CanUndo() == canUndo && e.CanRedo() == canRedo &&
          e.Position() == position && e.GetState() == State::Looping);
    CHECK(rig.GuardsIntact());
    PASS("%.0f s: length %u, overdub, undo, redo exact, oversize load rejected\n", seconds,
                unsigned(e.Length()));
}

static void TestCapacityLoad(Rig& rig) {
    Engine& e = rig.engine;
    CHECK(e.Init(kRate, rig.a.data() + kGuard, rig.b.data() + kGuard, rig.capacity));
    rig.RecordTake(kSecond, ToneSignal(200.0, .3f));
    rig.WaitIdle();
    const auto before = Snapshot(e);
    std::vector<Frame> source(rig.capacity);
    for (uint32_t i = 0; i < rig.capacity; ++i)
        source[i] = Tone(i, 61.0, .4f);
    // Busy engine: rejected without change.
    e.PressRecord();
    rig.Run(100);
    CHECK(!e.LoadLoop(source.data(), rig.capacity));
    e.PressRecord();
    rig.WaitIdle();
    e.Undo();
    rig.WaitIdle();
    CHECK(SameState(e, before));
    // Exactly the capacity is accepted and undoable.
    CHECK(e.LoadLoop(source.data(), rig.capacity));
    CHECK(e.Length() == rig.capacity && e.CanUndo());
    CHECK(e.StoredFrame(rig.capacity - 1).left == source.back().left);
    rig.Run(2 * kSecond);
    e.PressRecord();  // overdub over the end of a full buffer
    rig.Run(3 * kSecond, ToneSignal(400.0, .3f));
    e.PressRecord();
    rig.WaitIdle();
    e.Undo();
    rig.WaitIdle();
    bool same = e.Length() == rig.capacity;
    for (uint32_t i = 0; same && i < rig.capacity; ++i)
        same = e.StoredFrame(i).left == source[i].left && e.StoredFrame(i).right == source[i].right;
    CHECK(same);
    CHECK(rig.GuardsIntact());
    PASS("exact capacity: load of %u frames accepted, overdub and undo exact\n",
                unsigned(rig.capacity));
}

static void TestReverse() {
    Rig rig(3 * kSecond);
    Engine& e = rig.engine;
    rig.RecordTake(2 * kSecond, Constant(.5f));
    rig.Run(kSecond);
    const uint32_t p0 = e.Position();
    rig.StartCapture();
    e.ToggleReverse();
    rig.Run(2 * kReverseRampFrames + 100);
    // Linear fade to silence over 480 frames, direction flip, fade back in.
    CHECK_NEAR(rig.left[kReverseRampFrames / 2 - 1], .25, .002);
    CHECK(MinAbs(rig.left, kReverseRampFrames - 3, kReverseRampFrames + 3) < 1e-3f);
    CHECK_NEAR(rig.left.back(), .5, 1e-6);
    CHECK(MaxStep(rig.left) <= .5f / kReverseRampFrames * 1.01f);
    CHECK(e.Reversed());
    const uint32_t p1 = e.Position();
    rig.Run(1000);
    const uint32_t p2 = e.Position();
    // Forward during the 480-frame fade-out, backwards from the flip on.
    CHECK_NEAR(double(p1), double(p0) + kReverseRampFrames - (kReverseRampFrames + 100), 2.0);
    CHECK(p2 == p1 - 1000);
    // Reverse through the loop start wraps to the end, still click-free on a tone.
    Rig tone(3 * kSecond);
    tone.RecordTake(kSecond, ToneSignal(220.0, .5f));
    tone.engine.SetReverse(true);
    tone.StartCapture();
    tone.Run(3 * kSecond);
    CHECK(MaxStep(tone.left, kReverseRampFrames * 3) <= SeamBound(220.0, .5f));
    PASS("reverse: 480-frame fade out, flip at zero, 480-frame fade in\n");
}

static void TestInertia() {
    struct Case {
        float inertia;
        double tau;
    };
    for (const Case c : {Case{1.f, 2.0}, Case{.5f, 1.998 * .25 + .002}}) {
        Rig rig(3 * kSecond);
        Engine& e = rig.engine;
        rig.RecordTake(kSecond, ToneSignal(100.0, .3f));
        e.SetPlaying(false);
        rig.Run(kSecond);
        CHECK(e.Speed() == 0.f && e.Increment() == 0.f);
        e.SetInertia(c.inertia);
        e.SetPlaying(true);
        const uint32_t samples = uint32_t(c.tau * kRate);
        rig.Run(samples);
        CHECK_NEAR(e.Speed(), 1.0 - std::exp(-1.0), .002);  // one time constant
        rig.Run(uint64_t(4 * samples));
        CHECK_NEAR(e.Speed(), 1.0 - std::exp(-5.0), .002);
        // Tape stop: exponential in linear speed.
        rig.Run(uint64_t(8 * samples));
        e.SetPlaying(false);
        rig.Run(samples);
        CHECK_NEAR(e.Speed(), std::exp(-1.0), .002);
    }
    // Inertial slope: fully right lags only increases, fully left only decreases.
    Rig rig(3 * kSecond);
    Engine& e = rig.engine;
    rig.RecordTake(kSecond, ToneSignal(100.0, .3f));
    e.SetInertia(1.f);
    e.SetInertialSlope(1.f);
    e.SetPlaying(false);
    rig.Run(1);
    CHECK(e.Speed() == 0.f);  // decrease immediate
    e.SetPlaying(true);
    rig.Run(kSecond);
    CHECK(e.Speed() > .3f && e.Speed() < .5f);  // increase lags (tau 2 s)
    e.SetInertialSlope(0.f);
    rig.Run(1);
    CHECK(e.Speed() == 1.f);  // increase immediate
    e.SetPlaying(false);
    rig.Run(kSecond);
    CHECK(e.Speed() > .5f && e.Speed() < .7f);  // decrease lags
    e.SetInertialSlope(.52f);                     // dead band: both weights 1
    e.SetInertia(.02f);                           // below 0.03: off
    rig.Run(1);
    CHECK(e.Speed() == 0.f);
    PASS("inertia: tau = (1.998 a^2 + 0.002) s on linear speed, slope weights\n");
}

static void TestVarispeed() {
    struct Case {
        float semitones;
        double ratio;
    };
    for (const Case c : {Case{12.f, 2.0}, Case{7.f, std::pow(2.0, 7.0 / 12.0)}, Case{-24.f, .25},
                         Case{36.f, 8.0}, Case{50.f, 8.0}, Case{-40.f, .25}}) {
        Rig rig(9 * kSecond);
        Engine& e = rig.engine;
        rig.RecordTake(8 * kSecond, ToneSignal(441.0, .4f));
        rig.WaitIdle();
        e.SetVarispeed(c.semitones);
        CHECK(e.VarispeedTarget() >= kMinSemitones && e.VarispeedTarget() <= kMaxSemitones);
        rig.Run(2000);  // >= 4 semitones jumps; the increment settles in ~200 samples
        CHECK_NEAR(e.Increment(), c.ratio, c.ratio * 1e-5);
        const uint32_t p0 = e.Position();
        const uint32_t n = kSecond / 8;
        rig.StartCapture();
        rig.Run(n);
        const uint32_t p1 = e.Position();
        const uint32_t moved = (p1 + 8 * kSecond - p0) % (8 * kSecond);
        CHECK_NEAR(double(moved), c.ratio * n, 1.0);
        // Measured pitch of the loop: zero crossings of the left channel.
        int crossings = 0;
        for (size_t i = 1; i < rig.left.size(); ++i)
            crossings += (rig.left[i - 1] < 0.f) != (rig.left[i] < 0.f);
        const double hz = crossings / 2.0 / (double(n) / kRate);
        CHECK_NEAR(hz, 441.0 * c.ratio, 441.0 * c.ratio * .01 + 8.0);
        CHECK(rig.GuardsIntact());
    }
    // Glide: below 4 semitones a one-pole with coefficient 0.001 per sample.
    Rig rig(3 * kSecond);
    Engine& e = rig.engine;
    rig.RecordTake(kSecond, ToneSignal(441.0, .4f));
    e.SetVarispeed(3.f);
    rig.Run(1000);
    const double glide = 12.0 * std::log2(double(e.Speed()));
    CHECK_NEAR(glide, 3.0 * (1.0 - std::pow(.999, 1000.0)), .01);
    PASS("varispeed: semitone ratio, x8 limit, -24..+36 clamp, glide\n");
}

// Overdubbing at high speed, in reverse and with varispeed never writes outside.
static void TestBounds() {
    Rig rig(kSecond + 300);
    Engine& e = rig.engine;
    e.PressRecord();
    rig.Run(2 * kSecond, ToneSignal(300.0, .5f));
    CHECK(e.Length() == rig.capacity);
    for (int i = 0; i < 12; ++i) {
        e.SetVarispeed(i % 3 == 0 ? 36.f : (i % 3 == 1 ? -24.f : 5.f));
        e.SetReverse(i & 1);
        e.SetStart(float(i % 4) / 4.f);
        e.SetSize(float(i % 3) / 2.f);
        e.SetLoopMode(i & 2 ? LoopMode::Replace : LoopMode::SoundOnSound);
        e.SetSeamMode(i & 4 ? SeamMode::Crossfade : SeamMode::Window);
        e.PressRecord();
        rig.Run(kSecond / 3, ToneSignal(500.0 + i, .7f));
        e.PressRecord();
        rig.Run(3000);
        if (i % 4 == 3) {
            e.Undo();
            rig.Run(1000);
        }
    }
    rig.WaitIdle();
    bool finite = true;
    for (uint32_t i = 0; i < e.Length(); ++i)
        finite &= std::isfinite(e.StoredFrame(i).left) && std::fabs(e.StoredFrame(i).left) <= 1.f;
    CHECK(finite);
    CHECK(e.StoredFrame(e.Length()).left == 0.f);  // outside the state
    // Odd block sizes and single-sample calls.
    for (size_t block : {size_t(1), size_t(7), size_t(129)}) {
        rig.block = block;
        e.PressRecord();
        rig.Run(5000, ToneSignal(250.0, .3f));
        e.PressRecord();
        rig.WaitIdle();
    }
    CHECK(rig.GuardsIntact());
    PASS("bounds: x8, reverse, windows, odd blocks; guards intact\n");
}

int main() {
    const auto begin = std::chrono::steady_clock::now();
    TestInit();
    TestFirstTake();
    TestSeams();
    TestWindowAndJump();
    TestOverdubModes();
    TestUndoLayers();
    TestReverse();
    TestInertia();
    TestVarispeed();
    TestBounds();
    {
        auto rig = std::make_unique<Rig>(kMaxFrames);
        for (double seconds : {42.0, 44.0, 86.0, 87.0, 88.0})
            TestLongLoops(*rig, seconds);
        TestCapacityLoad(*rig);
    }
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    if (failures) {
        std::printf("FAILED: %d check(s) in %.1f s\n", failures, elapsed);
        return 1;
    }
    std::printf("All looper engine tests passed in %.1f s\n", elapsed);
    return 0;
}
