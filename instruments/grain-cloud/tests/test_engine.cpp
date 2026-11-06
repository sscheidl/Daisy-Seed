// SPDX-License-Identifier: MIT
// Rule and behaviour tests of the Grain Cloud engine: R1-R7, R9, B3, B4, B6, B10, B11, C7,
// sample lifetime, determinism and a long randomized run. Modulation (R8, B5) is in
// test_modulation.cpp, the modules in test_units.cpp. docs/BEHAVIOUR.md maps rules to tests.
#include "fx_contract.h"
#include "test_util.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <thread>

using namespace grain_cloud;
using namespace testing;

// Real-time rule: nothing allocates after Prepare().
static std::atomic<long> allocations{0};
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size ? size : 1);
    if(!p)
        std::abort();
    return p;
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static Config Cfg(std::size_t polyphony = 4, uint32_t tick_blocks = 1, std::size_t pool = kMaxGrains) {
    Config c;
    c.polyphony = polyphony;
    c.tick_blocks = tick_blocks;
    c.pool_size = pool;
    return c;
}
static const Voice* FindNote(const Rig& rig, uint8_t note, bool tail = false) {
    for(std::size_t i = 0; i < kVoiceSlots; ++i) {
        const Voice& v = rig.engine.VoiceAt(i);
        if(v.active && v.tail == tail && v.note == note)
            return &v;
    }
    return nullptr;
}
// The knob value that gives a density in grains per second.
static float DensityFor(float hz) { return std::log2(hz / kDensityMinHz) / kDensityOctaves; }
static float SizeFor(float seconds) { return std::log2(seconds / kSizeMinSeconds) / kSizeOctaves; }

// R1: tick cadence, order against the block, and notes that do not wait.
static void TestControlTick() {
    const auto data = DcSample(48000);
    for(uint32_t tick_blocks : {1u, 4u}) {
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(4, tick_blocks)));
        rig->Blocks(40);
        CHECK(rig->engine.Diagnostics().ticks == 40 / tick_blocks);
        CHECK(rig->engine.TickFrames() == 48 * tick_blocks);
    }
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(4, 4)));
    rig->Blocks(1); // block 1 ticks; the next tick is block 5
    // A parameter waits for the tick and is valid in that same block, before rendering.
    rig->Set(Param::Position, 0.75f);
    rig->Block();
    CHECK(rig->engine.Base(Param::Position) == 0.75f);
    CHECK(rig->engine.Live()[Param::Position] != 0.75f);
    rig->Blocks(2);
    CHECK(rig->engine.Live()[Param::Position] != 0.75f);
    rig->Block();
    CHECK(rig->engine.Live()[Param::Position] == 0.75f);
    // A note in a block without a tick still starts there, with gain moving.
    rig->Block();
    rig->engine.Post(NoteOn(60, 100));
    rig->Block();
    const Voice* v = FindNote(*rig, 60);
    CHECK(v && v->count >= 1);
    CHECK(v && v->gain.Target() > 0.f);
}

// R4: a jump and a sweep of a running value have no step, at either tick rate.
static void TestRamps() {
    const auto data = DcSample(96000);
    for(uint32_t tick_blocks : {1u, 4u}) {
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(1, tick_blocks)));
        rig->Set(Param::Size, 1.f);
        rig->Set(Param::Density, 0.f);
        rig->Set(Param::PanSpread, 0.f);
        rig->Set(Param::Attack, 0.f);
        rig->engine.Post(NoteOn(60, 127));
        rig->Blocks(48000 / 48); // 1 s: the only grain is near its peak
        const std::size_t start = rig->left.size();
        const float before = rig->left.back();
        rig->Set(Param::Level, 0.2f);
        rig->Blocks(8);
        const float after = rig->left.back();
        const float change = before - after;
        CHECK(change > 0.05f);
        float worst = 0.f;
        for(std::size_t i = start; i < rig->left.size(); ++i)
            worst = std::fmax(worst, std::fabs(rig->left[i] - rig->left[i - 1]));
        const float tick_frames = 48.f * float(tick_blocks);
        CHECK_MSG(worst <= change / tick_frames * 1.15f, "tick_blocks %u worst %f", tick_blocks, double(worst));

        // Sweep: the changes at tick boundaries are no larger than the changes between them.
        const std::size_t sweep = rig->left.size();
        for(int n = 0; n < 400; ++n) {
            rig->Set(Param::Level, 0.2f + 0.6f * float(n) / 400.f);
            rig->Block();
        }
        double inside = 0.0, boundary = 0.0;
        std::size_t inside_n = 0, boundary_n = 0;
        const std::size_t period = std::size_t(tick_frames);
        for(std::size_t i = sweep + 96; i < rig->left.size(); ++i) {
            const double d = std::fabs(double(rig->left[i]) - rig->left[i - 1]);
            if(i % period == 0) {
                boundary += d;
                ++boundary_n;
            } else {
                inside += d;
                ++inside_n;
            }
        }
        const double ratio = (boundary / double(boundary_n)) / (inside / double(inside_n));
        CHECK_MSG(ratio < 1.5, "tick_blocks %u boundary/inside %f", tick_blocks, ratio);
    }
}

// R5: no clamp before the end; one clip point, also with a stage in the gap.
static void TestSingleClip() {
    const auto data = DcSample(48000, 30000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(8)));
    rig->Set(Param::Size, SizeFor(0.2f));
    rig->Set(Param::Density, DensityFor(100.f));
    rig->Set(Param::Level, 1.f);
    rig->Set(Param::PanSpread, 0.f);
    rig->Set(Param::Attack, 0.f);
    for(uint8_t n = 0; n < 8; ++n)
        rig->engine.Post(NoteOn(uint8_t(48 + n), 127));
    float peak_sum = 0.f;
    for(int b = 0; b < 2000; ++b) {
        float l[48], r[48];
        rig->engine.RenderSum(l, r, 48);
        for(int i = 0; i < 48; ++i)
            peak_sum = std::fmax(peak_sum, std::fabs(l[i]));
        rig->engine.Output().Process(l, r, l, r, 48);
        for(int i = 0; i < 48; ++i)
            CHECK(std::fabs(l[i]) < 1.f && std::fabs(r[i]) < 1.f);
    }
    CHECK_MSG(peak_sum > 1.f, "peak_sum %f", double(peak_sum)); // the sum itself is not limited
    CHECK(rig->engine.Output().PeakIn() == peak_sum);
    CHECK(rig->engine.Output().Clipped() > 0);

    // Normal levels pass the clip point bit for bit: nothing else shapes the signal.
    auto quiet = std::make_unique<Rig>();
    const auto noise = NoiseSample(48000);
    CHECK(quiet->Start(ViewOf(noise), Cfg(2)));
    quiet->Set(Param::Level, 0.3f);
    quiet->engine.Post(NoteOn(60, 90));
    quiet->Blocks(500);
    float sum_l[48], sum_r[48], out_l[48], out_r[48];
    for(int b = 0; b < 200; ++b) {
        quiet->engine.RenderSum(sum_l, sum_r, 48);
        quiet->engine.Output().Process(sum_l, sum_r, out_l, out_r, 48);
        for(int i = 0; i < 48; ++i)
            CHECK(std::fabs(sum_l[i]) > kClipKnee || out_l[i] == sum_l[i]);
    }
}

// R2: effects are not in the core. One stereo point after the voice sum, on FX contract 1.0.
class HalfGainStage {
  public:
    struct Storage {};
    bool Prepare(const seed::audio::Config& config, Storage&) {
        config_ = config;
        return true;
    }
    void Process(const seed::audio::StereoBlock& block) {
        ++calls;
        if(!seed::audio::Valid(config_, block))
            return;
        for(std::size_t i = 0; i < block.frames; ++i) {
            block.out_left[i] = block.left[i] * 0.5f;
            block.out_right[i] = block.right[i] * 0.5f;
        }
    }
    int calls = 0;

  private:
    seed::audio::Config config_;
};

static void TestEffectPoint() {
    const auto noise = NoiseSample(48000);
    for(uint8_t voices : {uint8_t(2), uint8_t(6)}) {
        auto rig = std::make_unique<Rig>();
        auto twin = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(noise), Cfg(8)));
        CHECK(twin->Start(ViewOf(noise), Cfg(8)));
        for(uint8_t n = 0; n < voices; ++n) {
            rig->engine.Post(NoteOn(uint8_t(50 + n), 100));
            twin->engine.Post(NoteOn(uint8_t(50 + n), 100));
        }
        HalfGainStage stage;
        HalfGainStage::Storage storage;
        CHECK(stage.Prepare(seed::audio::Config{}, storage));
        for(int b = 0; b < 500; ++b) {
            float l[48], r[48], wl[48], wr[48], dl[48], dr[48];
            rig->engine.RenderSum(l, r, 48);
            stage.Process(seed::audio::StereoBlock{l, r, wl, wr, 48});
            rig->engine.Output().Process(wl, wr, wl, wr, 48);
            twin->engine.RenderSum(dl, dr, 48);
            for(int i = 0; i < 48; ++i)
                CHECK(wl[i] == SoftClip(dl[i] * 0.5f)); // the same sum, whatever the voice count
        }
        CHECK(stage.calls == 500); // once per block, not per voice
    }
}

// R6: level stays usable over a density sweep, and the gain only moves in small steps.
static void TestCountGainSweep() {
    const auto noise = NoiseSample(96000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(noise), Cfg(1)));
    rig->Set(Param::Size, SizeFor(0.128f));
    rig->Set(Param::Spray, 1.f);
    rig->Set(Param::Attack, 0.f);
    rig->Set(Param::Density, 0.5f);
    rig->engine.Post(NoteOn(60, 127));
    rig->Blocks(1000);
    const int seconds = 20, blocks = seconds * 1000;
    float worst_gain_step = 0.f, last_gain = 0.f;
    float low = 1e9f, high = 0.f;
    const std::size_t begin = rig->left.size();
    for(int b = 0; b < blocks; ++b) {
        rig->Set(Param::Density, 0.5f + 0.4f * float(b) / float(blocks));
        rig->Block();
        const Voice* v = FindNote(*rig, 60);
        CHECK(v != nullptr);
        if(!v)
            return;
        if(b > 0)
            worst_gain_step = std::fmax(worst_gain_step, std::fabs(v->gain.Value() - last_gain));
        last_gain = v->gain.Value();
        if(b && b % 500 == 0) {
            const float rms = rig->Rms(begin + std::size_t(b - 500) * 48, begin + std::size_t(b) * 48);
            low = std::fmin(low, rms);
            high = std::fmax(high, rms);
        }
    }
    const float spread_db = 20.f * std::log10(high / low);
    // Without the law this sweep (3 to 60 grains) rises by about 13 dB.
    CHECK_MSG(spread_db < 5.f, "spread %f dB (rms %f .. %f)", double(spread_db), double(low), double(high));
    CHECK_MSG(worst_gain_step < 0.01f, "gain step %f", double(worst_gain_step));
    CHECK(rig->engine.Output().PeakIn() < 1.f);
}

static void CheckStartCloud(StartMode mode) {
    const auto data = RampSample(30000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(4)));
    const float size = 0.1f, hz = 200.f; // 20 grains wanted
    rig->Set(Param::Size, SizeFor(size));
    rig->Set(Param::Density, DensityFor(hz));
    rig->Set(Param::Spray, 0.f);
    rig->Set(Param::Position, 0.5f);
    rig->Set(Param::Start, StartModeValue(mode));
    rig->Block();
    rig->engine.Post(NoteOn(60, 100));
    rig->Block();
    const Voice* v = FindNote(*rig, 60);
    CHECK(v != nullptr);
    if(!v)
        return;
    CHECK_MSG(v->count >= 15 && v->count <= kMaxBirthsPerBlock + 1, "count %d", int(v->count));
    CHECK(rig->engine.Diagnostics().peak_births_in_block <= kMaxBirthsPerBlock);
    rig->Blocks(2);
    CHECK_MSG(v->count >= 18 && v->count <= 24, "count %d", int(v->count));
    CHECK(v->cloud_left == 0);
    // Random ages, and the position follows the mode.
    const double center = 15000.0, size_frames = double(size) * 48000.0;
    std::size_t aged = 0, mismatched = 0;
    rig->EachGrain([&](const Voice&, const Grain& g) {
        const double age = double(g.win_phase) / 4294967296.0;
        if(age > 0.05)
            ++aged;
        // Birth position plus what the grain has read since: `age` at note start only in
        // FullCloud, and the few blocks of this test for both.
        const double position = double(g.pos) / 4294967296.0;
        const double start_age = age * size_frames;
        const double expected = mode == StartMode::FullCloud ? center + start_age : center;
        if(std::fabs(position - expected) > 48.0 * 4.0)
            ++mismatched;
    });
    CHECK(aged >= 12);
    CHECK(mismatched == 0);
}

// R7: the four note starts.
static void TestNoteStarts() {
    const auto data = DcSample(48000);
    { // Grow: nothing extra at note-on.
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(4)));
        rig->Set(Param::Size, SizeFor(0.1f));
        rig->Set(Param::Density, DensityFor(200.f));
        rig->Block();
        rig->engine.Post(NoteOn(60, 100));
        rig->Block();
        const Voice* v = FindNote(*rig, 60);
        CHECK(v && v->count <= 2);
        rig->Blocks(300);
        CHECK(v && v->count >= 14 && v->count <= 26);
    }
    CheckStartCloud(StartMode::FullCloud);
    CheckStartCloud(StartMode::FullCloudAtStart);

    // Spray still applies at note start; the position does not advance by age.
    {
        const auto ramp = RampSample(30000);
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(ramp), Cfg(4)));
        rig->Set(Param::Size, SizeFor(0.1f));
        rig->Set(Param::Density, DensityFor(200.f));
        rig->Set(Param::Spray, 0.05f);
        rig->Set(Param::Position, 0.5f);
        rig->Set(Param::Start, StartModeValue(StartMode::FullCloudAtStart));
        rig->Block();
        rig->engine.Post(NoteOn(60, 100));
        rig->Blocks(2);
        float low = 1e9f, high = -1e9f;
        rig->EachGrain([&](const Voice&, const Grain& g) {
            const float position = float(double(g.pos) / 4294967296.0);
            low = std::fmin(low, position);
            high = std::fmax(high, position);
        });
        CHECK(high - low > 200.f);
        CHECK(low >= 15000.f - 1500.f && high <= 15000.f + 1500.f + 96.f);
    }

    // The cloud lapses at note-off and obeys the voice's share and the active limit.
    {
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(4)));
        rig->Set(Param::Size, SizeFor(1.f));
        rig->Set(Param::Density, DensityFor(200.f)); // 200 grains wanted
        rig->Set(Param::ActiveLimit, 0.25f);         // 64
        rig->Set(Param::Start, StartModeValue(StartMode::FullCloud));
        rig->Block();
        rig->engine.Post(NoteOn(60, 100));
        rig->Block();
        const Voice* v = FindNote(*rig, 60);
        CHECK(v && v->cloud_left > 0 && v->cloud_left < 64);
        rig->engine.Post(NoteOff(60));
        rig->Block();
        CHECK(v && v->cloud_left == 0);
        CHECK(v && v->count < 40);
        rig->Blocks(50);
        CHECK(rig->engine.Pool().Active() <= 64);
    }
}

// R7: a voice attack shapes a cloud that starts mid-window. Fixes kMinAttackSeconds.
static void TestAttackShapesCloud() {
    const auto data = DcSample(48000);
    for(StartMode mode : {StartMode::FullCloud, StartMode::FullCloudAtStart}) {
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(4)));
        rig->Set(Param::Size, SizeFor(0.05f));
        rig->Set(Param::Density, DensityFor(400.f));
        rig->Set(Param::PanSpread, 0.f);
        rig->Set(Param::Attack, 0.f); // the minimum
        rig->Set(Param::Start, StartModeValue(mode));
        rig->Block();
        rig->engine.Post(NoteOn(60, 127));
        rig->Blocks(100);
        float steady = 0.f;
        for(std::size_t i = 48 * 40; i < rig->left.size(); ++i)
            steady = std::fmax(steady, std::fabs(rig->left[i]));
        const float onset = rig->MaxStep(49);
        // The rise takes at least 1/4 of the minimum attack in frames; never one big jump.
        const float limit = steady / (kMinAttackSeconds * 48000.f) * 4.f;
        CHECK_MSG(onset <= limit, "onset %f limit %f steady %f", double(onset), double(limit), double(steady));
        CHECK(std::fabs(rig->left[48]) < steady * 0.05f);
    }
}

// R7 Keep: survival, generations, and the new note's envelope.
static void TestKeep() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(1)));
    rig->Set(Param::Size, SizeFor(0.5f));
    rig->Set(Param::Density, DensityFor(40.f));
    rig->Set(Param::Start, StartModeValue(StartMode::Keep));
    rig->Set(Param::Release, 1.f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 100));
    rig->Blocks(200);
    const Voice* v = FindNote(*rig, 60);
    CHECK(v != nullptr);
    if(!v)
        return;
    const uint16_t generation = v->generation, count = v->count;
    CHECK(count >= 6);
    rig->engine.Post(NoteOff(60));
    rig->Blocks(20);
    const float level = v->env.Level();
    rig->engine.Post(NoteOn(60, 100)); // retrigger of the same voice
    rig->Block();
    CHECK(FindNote(*rig, 60) == v);
    CHECK(v->generation == generation);
    CHECK(v->count >= count);
    CHECK(v->env.Level() >= level); // from where it was, under the new envelope
    CHECK(rig->Consistent());

    // Stolen for another note: the old generation runs out on its own, at its own level.
    rig->Blocks(200);
    const float gain = v->gain.Value();
    uint32_t step_sum = 0;
    rig->EachGrain([&](const Voice&, const Grain& g) { step_sum += g.win_step; });
    rig->engine.Post(NoteOn(72, 100));
    rig->Block();
    const Voice* old = FindNote(*rig, 60, true);
    const Voice* fresh = FindNote(*rig, 72);
    CHECK(old && fresh && old != fresh);
    if(!old || !fresh)
        return;
    CHECK(old->env.CurrentStage() == Envelope::Stage::Hold);
    CHECK(fresh->generation != 0);
    uint32_t old_steps = 0;
    rig->EachGrain([&](const Voice& owner, const Grain& g) {
        if(&owner == old)
            old_steps += g.win_step;
    });
    CHECK(old_steps == step_sum); // not shortened
    rig->Blocks(10);
    CHECK(Near(old->gain.Value(), gain, gain * 0.1f));
    CHECK(rig->Consistent());
    rig->Blocks(700); // longer than the grain length
    CHECK(!FindNote(*rig, 60, true));
    CHECK(rig->engine.Diagnostics().hard_kills == 0);
}

// Other modes: a retrigger is a new generation; the old one is never under the new envelope.
static void TestRetrigger() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(4)));
    rig->Set(Param::Size, SizeFor(0.2f));
    rig->Set(Param::Density, DensityFor(100.f));
    rig->Set(Param::PanSpread, 0.f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 127));
    rig->Blocks(400);
    const Voice* first = FindNote(*rig, 60);
    CHECK(first != nullptr);
    if(!first)
        return;
    rig->engine.Post(NoteOn(60, 127));
    rig->Block();
    const Voice* second = FindNote(*rig, 60);
    CHECK(second && second != first);
    CHECK(first->tail);
    CHECK(first->env.CurrentStage() == Envelope::Stage::FastRelease);
    CHECK(rig->Consistent());
    const std::size_t at = rig->left.size();
    rig->Blocks(30);
    CHECK(rig->Consistent());
    CHECK(!FindNote(*rig, 60, true)); // gone after the fast release
    CHECK(rig->engine.Diagnostics().hard_kills == 0);
    // The crossover has no click: a fade over kFastReleaseSeconds, not a jump.
    float worst = 0.f;
    for(std::size_t i = at; i < rig->left.size(); ++i)
        worst = std::fmax(worst, std::fabs(rig->left[i] - rig->left[i - 1]));
    CHECK_MSG(worst < 0.02f, "worst %f", double(worst));
}

// C7: a stolen note and panic use the fast release, whatever the release setting is.
static void TestStealAndPanic() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(2)));
    rig->Set(Param::Size, SizeFor(0.2f));
    rig->Set(Param::Density, DensityFor(100.f));
    rig->Set(Param::Release, 1.f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 127));
    rig->engine.Post(NoteOn(64, 127));
    rig->Blocks(300);
    rig->engine.Post(NoteOn(67, 127));
    rig->Block();
    CHECK(rig->engine.Diagnostics().voices_stolen == 1);
    CHECK(FindNote(*rig, 60, true) != nullptr); // the oldest
    CHECK(FindNote(*rig, 64) != nullptr);
    const int ticks = int(kFastReleaseSeconds * 1000.f) + 3;
    rig->Blocks(ticks);
    CHECK(FindNote(*rig, 60, true) == nullptr);

    // Released notes are stolen before held ones.
    rig->engine.Post(NoteOff(67));
    rig->Blocks(5);
    rig->engine.Post(NoteOn(69, 127));
    rig->Block();
    CHECK(FindNote(*rig, 67, true) != nullptr);
    CHECK(FindNote(*rig, 64) != nullptr);

    rig->Blocks(50);
    const std::size_t at = rig->left.size();
    rig->engine.Post(AllSoundOff());
    rig->Blocks(ticks + 2);
    CHECK(rig->ActiveVoices() == 0);
    CHECK(rig->engine.Pool().Active() == 0);
    CHECK(rig->engine.Diagnostics().hard_kills == 0);
    CHECK(rig->engine.Diagnostics().silent_frees > 0);
    CHECK(rig->left.back() == 0.f);
    float worst = 0.f;
    for(std::size_t i = at; i < rig->left.size(); ++i)
        worst = std::fmax(worst, std::fabs(rig->left[i] - rig->left[i - 1]));
    CHECK_MSG(worst < 0.02f, "worst %f", double(worst));
}

// A note outlives the pedal rules, and the sustain pedal holds it.
static void TestSustain() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(4)));
    rig->Block();
    rig->engine.Post(Sustain(true));
    rig->engine.Post(NoteOn(60, 100));
    rig->Blocks(50);
    rig->engine.Post(NoteOff(60));
    rig->Blocks(50);
    const Voice* v = FindNote(*rig, 60);
    CHECK(v && v->sustained && !v->held);
    CHECK(v && v->env.CurrentStage() != Envelope::Stage::Release);
    rig->engine.Post(Sustain(false));
    rig->Blocks(2);
    CHECK(v && v->env.CurrentStage() == Envelope::Stage::Release);
}

// R9: one test per row of the table. Running grains keep what they were born with.
struct GrainFacts {
    int64_t inc;
    uint32_t win_step;
    float gain_l, gain_r;
    int64_t pos;
};
static std::map<uint16_t, GrainFacts> Snapshot(const Rig& rig) {
    std::map<uint16_t, GrainFacts> facts;
    for(std::size_t i = 0; i < kVoiceSlots; ++i) {
        const Voice& v = rig.engine.VoiceAt(i);
        if(!v.active)
            continue;
        for(uint16_t g = v.head; g != kNoGrain; g = rig.engine.Pool().At(g).next) {
            const Grain& grain = rig.engine.Pool().At(g);
            facts[g] = GrainFacts{grain.inc, grain.win_step, grain.gain_l, grain.gain_r, grain.pos};
        }
    }
    return facts;
}

template <class NewGrainCheck>
static void CheckBirthRow(const char* name, Param param, float value, NewGrainCheck born_right) {
    const auto data = NoiseSample(1 << 20);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(1)));
    rig->Set(Param::Size, SizeFor(0.5f));
    rig->Set(Param::Density, DensityFor(60.f));
    rig->Set(Param::Position, 0.2f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 100));
    rig->Blocks(200);
    const auto before = Snapshot(*rig);
    CHECK(before.size() >= 8);
    rig->Set(param, value);
    rig->Blocks(100);
    const auto after = Snapshot(*rig);
    std::size_t kept = 0, fresh = 0, right = 0;
    const int64_t frames = 100 * 48;
    for(const auto& [index, facts] : after) {
        const auto old = before.find(index);
        if(old != before.end() && old->second.win_step == facts.win_step &&
           old->second.pos + frames * old->second.inc == facts.pos) {
            ++kept; // same read path and same fixed values as at its birth
            CHECK_MSG(old->second.inc == facts.inc && old->second.gain_l == facts.gain_l &&
                      old->second.gain_r == facts.gain_r, "%s", name);
        } else {
            ++fresh;
            right += born_right(rig->engine.Pool().At(index));
        }
    }
    CHECK_MSG(kept >= 3, "%s kept %zu", name, kept);
    CHECK_MSG(fresh >= 3 && right == fresh, "%s fresh %zu right %zu", name, fresh, right);
}

static void TestBirthTable() {
    CheckBirthRow("position", Param::Position, 0.8f, [](const Grain& g) {
        const double at = double(g.pos) / 4294967296.0 / double(1 << 20);
        return at > 0.7 && at < 0.9;
    });
    CheckBirthRow("spray", Param::Spray, 0.f, [](const Grain& g) {
        const double at = double(g.pos) / 4294967296.0 / double(1 << 20);
        return at > 0.19 && at < 0.22;
    });
    CheckBirthRow("pitch", Param::Pitch, 0.75f, [](const Grain& g) { return g.inc > (int64_t(3) << 31); }); // +12 semitones
    CheckBirthRow("size", Param::Size, 0.9f, [](const Grain& g) {
        return g.win_step < uint32_t(4294967296.0 / (0.5 * 48000.0) / 1.2);
    });
    CheckBirthRow("direction", Param::Direction, 1.f, [](const Grain& g) { return g.inc < 0; });
    CheckBirthRow("pan", Param::PanSpread, 0.f, [](const Grain& g) { return g.gain_l == g.gain_r; });
}

// R9 Scan: moves the birth point of new grains only (three separate time values in the spec).
static void TestScanRow() {
    const auto data = NoiseSample(480000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(1)));
    rig->Set(Param::Size, SizeFor(0.02f));
    rig->Set(Param::Density, DensityFor(200.f));
    rig->Set(Param::Spray, 0.f);
    rig->Set(Param::Position, 0.f);
    rig->Set(Param::Scan, 1.f); // a quarter span per second
    rig->Block();
    rig->engine.Post(NoteOn(60, 100));
    rig->Blocks(1000);
    double newest = 0.0;
    rig->EachGrain([&](const Voice&, const Grain& g) { newest = std::fmax(newest, double(g.pos) / 4294967296.0); });
    CHECK_MSG(Near(float(newest / 480000.0), 0.25f, 0.01f), "scan %f", newest);
}

// R9 envelope rows: a changed value reaches a sounding note at the next tick, through its ramp.
static void TestEnvelopeRows() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(1)));
    rig->Set(Param::Attack, 0.f);
    rig->Set(Param::Decay, 0.f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 127));
    rig->Blocks(100);
    const Voice* v = FindNote(*rig, 60);
    CHECK(v && v->env.CurrentStage() == Envelope::Stage::Sustain);
    if(!v)
        return;
    rig->Set(Param::Sustain, 0.25f);
    rig->Blocks(2);
    CHECK(v->env.Level() == 0.25f);
    rig->Set(Param::Release, 0.f); // set while the note is held, used by its release
    rig->Block();
    rig->engine.Post(NoteOff(60));
    rig->Blocks(int(DecaySeconds(0.f) * 1000.f) + 4);
    CHECK(rig->ActiveVoices() == 0);
}

// R9 Active Limit: refuses later births; running grains end normally, even far over a new limit.
static void TestLimitRow() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(1)));
    rig->Set(Param::Size, SizeFor(0.5f));
    rig->Set(Param::Density, DensityFor(100.f));
    rig->Block();
    rig->engine.Post(NoteOn(60, 100));
    rig->Blocks(300);
    const std::size_t active = rig->engine.Pool().Active();
    CHECK(active >= 25);
    rig->Set(Param::ActiveLimit, 0.04f); // about 10
    rig->Blocks(100);
    CHECK(rig->engine.Pool().Active() == active); // none ends yet, none is added
    CHECK(rig->engine.Diagnostics().steals == 0);
    rig->Blocks(600);
    CHECK(rig->engine.Pool().Active() <= 10);
}

// R9 Window (owner decision B6): running grains follow the shared table.
static void TestWindowRow() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(1)));
    rig->Set(Param::Size, SizeFor(1.f));
    rig->Set(Param::Density, DensityFor(10.f));
    rig->Set(Param::PanSpread, 0.f);
    rig->Set(Param::Attack, 0.f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 127));
    rig->Blocks(500);
    const auto before = Snapshot(*rig);
    const float morph = rig->engine.Window().Morph();
    rig->Set(Param::Window, 1.f);
    rig->Blocks(300);
    CHECK(rig->engine.Window().Morph() > morph + 0.25f);
    CHECK(rig->engine.Window().Morph() < 0.35f); // one second for the whole range
    const auto after = Snapshot(*rig);
    for(const auto& [index, facts] : after) {
        const auto old = before.find(index);
        if(old != before.end())
            CHECK(old->second.win_step == facts.win_step); // the grain holds no window state of its own
    }
    CHECK(rig->engine.Diagnostics().window_points_peak == kWindowPoints);
    const uint32_t ticks = rig->engine.Diagnostics().ticks;
    CHECK(ticks > 0);
}

// B6: a fast sweep of the shape does not click.
static void TestWindowSweep() {
    const auto data = DcSample(48000);
    float reference = 0.f;
    for(bool sweep : {false, true}) {
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(1)));
        rig->Set(Param::Size, SizeFor(0.1f));
        rig->Set(Param::Density, DensityFor(100.f));
        rig->Set(Param::PanSpread, 0.f);
        rig->Set(Param::Attack, 0.1f);
        rig->Block();
        rig->engine.Post(NoteOn(60, 127));
        rig->Blocks(600);
        const std::size_t from = rig->left.size();
        for(int b = 0; b < 4000; ++b) {
            if(sweep && b % 100 == 0)
                rig->Set(Param::Window, (b / 100) % 2 ? 0.f : 1.f); // far faster than the morph
            rig->Block();
        }
        if(!sweep) {
            reference = rig->MaxStep(from);
        } else {
            const float worst = rig->MaxStep(from);
            CHECK_MSG(worst < reference * 4.f + 0.002f, "worst %f reference %f", double(worst), double(reference));
        }
    }
}

// B3: the share of reverse grains follows the setting.
static void TestDirection() {
    const auto data = NoiseSample(48000);
    for(float probability : {0.f, 0.25f, 0.5f, 0.9f, 1.f}) {
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(1)));
        rig->Set(Param::Size, SizeFor(0.01f));
        rig->Set(Param::Density, DensityFor(800.f));
        rig->Set(Param::Direction, probability);
        rig->Block();
        rig->engine.Post(NoteOn(60, 100));
        rig->Blocks(20000);
        const Stats& s = rig->engine.Diagnostics();
        const float share = float(s.reverse_births) / float(s.births);
        CHECK(s.births > 10000);
        if(probability == 0.f || probability == 1.f)
            CHECK(share == probability);
        else
            CHECK_MSG(Near(share, probability, 0.02f), "share %f of %f", double(share), double(probability));
    }
}

// Spec scheduling: a fractional rate holds in the long run, and births land on their frame.
static void TestScheduler() {
    const auto data = DcSample(48000);
    for(float hz : {3.3f, 37.f, 160.f}) {
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(1)));
        rig->Set(Param::Size, SizeFor(0.002f));
        rig->Set(Param::Density, DensityFor(hz));
        rig->Block();
        rig->engine.Post(NoteOn(60, 100));
        rig->Blocks(30000);
        const float expected = hz * 30.f;
        CHECK_MSG(std::fabs(float(rig->engine.Diagnostics().births) - expected) <= 2.f + expected * 0.001f,
                  "%f Hz: %u births", double(hz), rig->engine.Diagnostics().births);
    }
    // Period 300 frames, not a multiple of 48: onsets must not snap to block edges.
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(1)));
    rig->Set(Param::Size, SizeFor(0.002f));
    rig->Set(Param::Density, DensityFor(160.f));
    rig->Set(Param::PanSpread, 0.f);
    rig->Set(Param::Attack, 0.f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 127));
    rig->Blocks(1000);
    std::vector<std::size_t> onsets;
    for(std::size_t i = 48 * 200; i < rig->left.size(); ++i)
        if(rig->left[i] != 0.f && rig->left[i - 1] == 0.f)
            onsets.push_back(i);
    CHECK(onsets.size() > 100);
    for(std::size_t i = 1; i < onsets.size(); ++i)
        CHECK_MSG(onsets[i] - onsets[i - 1] >= 298 && onsets[i] - onsets[i - 1] <= 302, "gap %zu",
                  onsets[i] - onsets[i - 1]);
}

// B10 inside the engine: a cloud around the position, from the voice's own generator.
static void TestSprayDistribution() {
    const auto data = NoiseSample(1 << 20);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(1)));
    rig->Set(Param::Size, SizeFor(0.002f));
    rig->Set(Param::Density, DensityFor(1000.f));
    rig->Set(Param::Spray, 0.2f);
    rig->Set(Param::Position, 0.5f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 100));
    int inner = 0, total = 0;
    float low = 1.f, high = 0.f;
    for(int b = 0; b < 8000; ++b) {
        rig->Block();
        rig->EachGrain([&](const Voice&, const Grain& g) {
            const float at = float(double(g.pos) / 4294967296.0 / double(1 << 20));
            low = std::fmin(low, at);
            high = std::fmax(high, at);
            ++total;
            inner += std::fabs(at - 0.5f) < 0.1f;
        });
    }
    CHECK(total > 3000);
    CHECK(low >= 0.299f && high <= 0.701f);
    // Triangular: 75 % inside half the spread; uniform would be 50 %.
    CHECK_MSG(Near(float(inner) / float(total), 0.75f, 0.04f), "inner %f", double(float(inner) / float(total)));
}

// B11: pool pressure ends grains sooner. No hard kill, no click, the limit holds.
static void TestGrainStealing() {
    const auto data = DcSample(96000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(4, 1, 32)));
    rig->Set(Param::Size, SizeFor(0.5f));
    rig->Set(Param::Density, DensityFor(200.f));
    rig->Set(Param::PanSpread, 0.f);
    rig->Set(Param::Attack, 0.3f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 127)); // fills the pool
    rig->Blocks(600);
    CHECK(rig->engine.Pool().Active() >= 30); // a slot freed now is taken by the next birth
    const Stats before = rig->engine.Diagnostics();
    CHECK(before.steals == 0); // alone, a voice is not robbed by itself
    CHECK(before.dropped > 0);
    const std::size_t at = rig->left.size();
    rig->engine.Post(NoteOn(67, 127));
    for(int b = 0; b < 1000; ++b) {
        rig->Block();
        CHECK(rig->engine.Pool().Active() <= 32);
        CHECK(rig->Consistent());
    }
    const Stats after = rig->engine.Diagnostics();
    CHECK(after.steals > 0);
    CHECK(after.hard_kills == 0);
    const Voice* a = FindNote(*rig, 60);
    const Voice* b = FindNote(*rig, 67);
    CHECK(a && b);
    if(a && b) // fair: the second note reached its share
        CHECK_MSG(b->count >= 12 && a->count >= 12, "shares %d %d", int(a->count), int(b->count));
    const float step_while_stealing = rig->MaxStep(at);
    CHECK_MSG(step_while_stealing < 0.01f, "step %f", double(step_while_stealing));

    // A note in release gives way first.
    rig->engine.Post(NoteOff(60));
    rig->engine.Post(NoteOn(72, 127));
    rig->Blocks(400);
    CHECK(rig->engine.Diagnostics().hard_kills == 0);
    const Voice* third = FindNote(*rig, 72);
    CHECK(third && third->count >= 8);
}

// Ending sooner changes only the window step, and the slot stays taken until the end.
static void TestStolenGrainEndsInTime() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(2, 1, 8)));
    rig->Set(Param::Size, 1.f); // about two seconds: nothing ends by itself in this test
    rig->Set(Param::Density, DensityFor(100.f));
    rig->Block();
    rig->engine.Post(NoteOn(60, 127));
    rig->Blocks(200);
    CHECK(rig->engine.Pool().Active() == 8);
    const auto before = Snapshot(*rig);
    rig->engine.Post(NoteOn(67, 127));
    rig->Block();
    CHECK(rig->engine.Diagnostics().steals == 1);
    CHECK(rig->engine.Pool().Active() == 8); // the hard limit holds during the fade
    const auto after = Snapshot(*rig);
    std::size_t shortened = 0;
    for(uint16_t index = 0; index < 8; ++index) {
        const auto old = before.find(index), now = after.find(index);
        CHECK(old != before.end() && now != after.end());
        if(old == before.end() || now == after.end())
            continue;
        CHECK(now->second.inc == old->second.inc && now->second.gain_l == old->second.gain_l);
        if(now->second.win_step == old->second.win_step)
            continue;
        ++shortened;
        const Grain& g = rig->engine.Pool().At(index);
        const uint64_t left = (uint64_t{1} << 32) - g.win_phase;
        CHECK(left / g.win_step <= kStealFrames);
    }
    CHECK(shortened == 1);
    rig->Blocks(int(kStealFrames / 48)); // the fade is over: the slot is free again
    CHECK(rig->engine.Pool().Active() == 7);
    rig->Blocks(30);
    const Voice* second = FindNote(*rig, 67);
    CHECK(second && second->count >= 2); // freed slots went to the new note
    CHECK(rig->engine.Pool().Active() <= 8);
    CHECK(rig->engine.Diagnostics().hard_kills == 0);
}

// Density sweep at the limit and queue overflow: nothing hangs.
static void TestNoHangingNotes() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(8)));
    rig->Set(Param::Release, 0.f);
    rig->Block();
    for(uint8_t n = 0; n < 40; ++n)
        rig->engine.Post(NoteOn(uint8_t(30 + n), 100));
    rig->Blocks(30);
    CHECK(rig->ActiveVoices() <= kVoiceSlots);
    CHECK(rig->Consistent());
    std::size_t notes = 0;
    for(std::size_t i = 0; i < kVoiceSlots; ++i)
        notes += rig->engine.VoiceAt(i).active && !rig->engine.VoiceAt(i).tail;
    CHECK(notes == 8);
    // All releases lost: the sticky request still ends every note.
    for(std::size_t i = 0; i < ControlBridge::kReleases + 4; ++i)
        rig->engine.Post(NoteOff(100));
    CHECK(rig->engine.QueueOverflows() >= 4);
    rig->Blocks(2000);
    CHECK(rig->ActiveVoices() == 0);
}

static void FillReleases(Rig& rig) {
    for(std::size_t i = 0; i < ControlBridge::kReleases; ++i)
        CHECK(rig.engine.Post(NoteOff(100)));
}

// A lost release acts after the events before it and before the events after it.
static void TestLostReleaseOrder() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(4)));
    rig->Set(Param::Release, 0.f);
    rig->Block();
    FillReleases(*rig);
    CHECK(rig->engine.Post(NoteOn(60, 100))); // still queued when its release is lost
    CHECK(!rig->engine.Post(NoteOff(60)));
    CHECK(rig->engine.Post(NoteOn(62, 100))); // after the loss: this note really is held
    rig->Blocks(8);                           // 16 events per block: all are through
    const Voice* lost = FindNote(*rig, 60);
    const Voice* kept = FindNote(*rig, 62);
    CHECK(!lost || !lost->held);
    CHECK(kept && kept->held);
    rig->Blocks(100);
    CHECK(!FindNote(*rig, 60)); // the note ended
    CHECK(FindNote(*rig, 62) != nullptr);

    // A lost pedal-up must not leave the pedal down for later notes.
    auto pedal = std::make_unique<Rig>();
    CHECK(pedal->Start(ViewOf(data), Cfg(4)));
    pedal->Set(Param::Release, 0.f);
    pedal->Block();
    pedal->engine.Post(Sustain(true));
    pedal->engine.Post(NoteOn(60, 100));
    pedal->Block();
    FillReleases(*pedal);
    CHECK(!pedal->engine.Post(Sustain(false)));
    pedal->Blocks(8);
    pedal->engine.Post(NoteOn(64, 100));
    pedal->Blocks(20);
    pedal->engine.Post(NoteOff(64));
    pedal->Blocks(100);
    CHECK(pedal->ActiveVoices() == 0);
}

// A note-on with velocity 0 is a release, also when the ordinary queue is full.
static void TestZeroVelocityRelease() {
    const auto data = DcSample(48000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(4)));
    rig->Set(Param::Release, 0.f);
    rig->Block();
    rig->engine.Post(NoteOn(60, 100));
    rig->Blocks(5);
    CHECK(FindNote(*rig, 60) != nullptr);
    for(std::size_t i = 0; i < ControlBridge::kEvents; ++i)
        CHECK(rig->engine.Post(PitchBend(0.f)));
    CHECK(!rig->engine.Post(PitchBend(0.f)));
    CHECK(rig->engine.Post(NoteOn(60, 0)));
    rig->Blocks(100);
    CHECK(!FindNote(*rig, 60));
}

// R7: the cloud uses the parameters posted with its note, at either tick rate.
static void TestCloudUsesCurrentParams() {
    const auto data = DcSample(48000);
    { // The whole batch straight after Prepare().
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(4)));
        rig->Set(Param::Density, 1.f);
        rig->Set(Param::Size, 1.f);
        rig->Set(Param::Start, StartModeValue(StartMode::FullCloud));
        rig->engine.Post(NoteOn(60, 100));
        rig->Block();
        const Voice* v = FindNote(*rig, 60);
        CHECK(v != nullptr);
        if(!v)
            return;
        CHECK_MSG(v->count_smooth > 250.f, "count law sees %f", double(v->count_smooth));
        rig->Blocks(int(kMaxGrains / kMaxBirthsPerBlock) + 1);
        CHECK_MSG(rig->engine.Pool().Active() == kMaxGrains, "active %zu", rig->engine.Pool().Active());
    }
    { // In a block without a tick at 250 Hz: the tick comes forward, for count and grain size.
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(4, 4)));
        rig->Set(Param::Start, StartModeValue(StartMode::FullCloud));
        rig->Set(Param::Density, 0.f);
        rig->Set(Param::Size, 0.f);
        rig->Blocks(2); // the next tick is two blocks away
        const uint32_t ticks = rig->engine.Diagnostics().ticks;
        rig->Set(Param::Density, DensityFor(200.f));
        rig->Set(Param::Size, SizeFor(0.1f));
        rig->engine.Post(NoteOn(60, 100));
        rig->Block();
        CHECK(rig->engine.Diagnostics().ticks == ticks + 1);
        const Voice* v = FindNote(*rig, 60);
        CHECK(v && v->count + v->cloud_left >= 19);
        rig->EachGrain([&](const Voice&, const Grain& g) {
            CHECK(Near(float(g.win_step), 4294967296.f / 4800.f, 4294967296.f / 4800.f * 0.01f));
        });
    }
    { // Nothing changed: the note does not move the tick.
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(4, 4)));
        rig->Set(Param::Start, StartModeValue(StartMode::FullCloud));
        rig->Blocks(2);
        const uint32_t ticks = rig->engine.Diagnostics().ticks;
        rig->engine.Post(NoteOn(60, 100));
        rig->Block();
        CHECK(rig->engine.Diagnostics().ticks == ticks);
        const Voice* v = FindNote(*rig, 60);
        CHECK(v && v->count >= 1); // and it still starts at once
    }
    { // A chord in one block shares the pool evenly, and a note released at once has no cloud.
        auto rig = std::make_unique<Rig>();
        CHECK(rig->Start(ViewOf(data), Cfg(4)));
        rig->Set(Param::Density, 1.f);
        rig->Set(Param::Size, 1.f);
        rig->Set(Param::Start, StartModeValue(StartMode::FullCloud));
        rig->Block();
        rig->engine.Post(NoteOn(60, 100));
        rig->engine.Post(NoteOn(64, 100));
        rig->engine.Post(NoteOn(67, 100));
        rig->engine.Post(NoteOff(67));
        rig->Block();
        const Voice* a = FindNote(*rig, 60);
        const Voice* b = FindNote(*rig, 64);
        const Voice* c = FindNote(*rig, 67);
        CHECK(a && b && c);
        if(a && b && c) {
            CHECK(a->cloud_left + a->count >= 80 && b->cloud_left + b->count >= 80);
            CHECK(c->cloud_left == 0 && c->count <= 1);
        }
    }
}

// Spec lifetime rule: a sample stays until the last grain has left it.
static void TestSampleHandOver() {
    const auto second = DcSample(48000, 1000);
    auto first = std::make_unique<std::vector<int16_t>>(NoiseSample(48000));
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(*first, 1), Cfg(1)));
    rig->Set(Param::Size, SizeFor(0.1f));
    rig->Set(Param::Density, DensityFor(100.f));
    rig->Block();
    rig->engine.Post(NoteOn(60, 100));
    rig->Blocks(300);
    uint8_t old_slot = 0;
    rig->EachGrain([&](const Voice&, const Grain& g) { old_slot = g.flags & kGrainSampleSlot; });
    CHECK(rig->engine.PublishSample(ViewOf(second, 2)));
    CHECK(!rig->engine.ConsumeSampleRetired(1));
    rig->Block();
    CHECK(!rig->engine.ConsumeSampleRetired(1)); // grains still read it
    int blocks = 1;
    bool retired = false;
    while(!(retired = rig->engine.ConsumeSampleRetired(1)) && blocks < 400) {
        rig->Block();
        ++blocks;
    }
    CHECK(retired);
    CHECK(!rig->engine.ConsumeSampleRetired(1)); // once
    CHECK_MSG(blocks >= 90 && blocks <= 120, "retired after %d blocks", blocks);
    first.reset(); // under ASan any later read of the old sample fails the test
    rig->Blocks(200);
    rig->EachGrain([&](const Voice&, const Grain& g) { CHECK((g.flags & kGrainSampleSlot) != old_slot); });
    SampleView invalid;
    CHECK(!rig->engine.PublishSample(invalid));

    // A different sample rate is part of the read step.
    const auto data = DcSample(48000);
    auto half = std::make_unique<Rig>();
    SampleView slow = ViewOf(data, 3);
    slow.sample_rate = 24000.f;
    CHECK(half->Start(slow, Cfg(1)));
    half->Block();
    half->engine.Post(NoteOn(60, 100));
    half->Blocks(4);
    half->EachGrain([&](const Voice&, const Grain& g) { CHECK(g.inc == (int64_t(1) << 31)); });
}

// An owner that polls late still gets every retirement, in order.
static void TestRetirementsKept() {
    const auto a = DcSample(100), b = DcSample(100), c = DcSample(100), d = DcSample(100);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(a, 1), Cfg(1)));
    rig->Block();                                  // sample 1 sounds
    CHECK(rig->engine.PublishSample(ViewOf(b, 2)));
    rig->Block();                                  // sample 2 adopted, 1 is retiring
    CHECK(rig->engine.PublishSample(ViewOf(c, 3))); // staged before the retirement exists
    rig->Blocks(50);                               // nobody polls for a long time
    CHECK(!rig->engine.PublishSample(ViewOf(d, 4))); // backpressure instead of a lost retirement
    CHECK(!rig->engine.ConsumeSampleRetired(2));   // sample 2 is still the current one
    CHECK(!rig->engine.ConsumeSampleRetired(3));
    CHECK(rig->engine.ConsumeSampleRetired(1));
    CHECK(!rig->engine.ConsumeSampleRetired(1));
    rig->Blocks(3);                                // now 3 is adopted and 2 retires
    CHECK(!rig->engine.PublishSample(ViewOf(d, 4))); // nothing staged, the retirement alone refuses
    CHECK(rig->engine.ConsumeSampleRetired(2));
    CHECK(rig->engine.PublishSample(ViewOf(d, 4)));
    rig->Blocks(3);
    CHECK(rig->engine.ConsumeSampleRetired(3));
    CHECK(!rig->engine.ConsumeSampleRetired(0));   // 0 means none, never a retirement
    // The first sample replaces nothing, so it needs no consumption.
    auto first = std::make_unique<Rig>();
    CHECK(first->Start(ViewOf(a, 7), Cfg(1)));
    first->Blocks(3);
    CHECK(first->engine.PublishSample(ViewOf(b, 8)));
}

// Edge samples in the engine: wraps, clamps and tiny files survive every setting.
static void TestTinySamples() {
    for(uint32_t frames : {1u, 2u, 3u, 7u}) {
        for(bool loop : {false, true}) {
            const auto data = NoiseSample(frames);
            auto rig = std::make_unique<Rig>();
            CHECK(rig->Start(ViewOf(data, 1, loop), Cfg(2)));
            rig->Set(Param::Spray, 1.f);
            rig->Set(Param::Direction, 0.5f);
            rig->Set(Param::Pitch, 1.f);
            rig->Set(Param::Density, 0.8f);
            rig->Block();
            rig->engine.Post(NoteOn(96, 100));
            rig->engine.Post(NoteOn(30, 100));
            rig->Blocks(400);
            for(float x : rig->left)
                CHECK(std::isfinite(x));
            CHECK(rig->engine.Diagnostics().births > 0);
        }
    }
    auto none = std::make_unique<Rig>();
    SampleView empty;
    CHECK(none->engine.Prepare(Cfg()));
    CHECK(!none->engine.PublishSample(empty));
    none->engine.Post(NoteOn(60, 100));
    none->Blocks(10);
    CHECK(none->engine.Pool().Active() == 0);
    CHECK(none->left.back() == 0.f);
}

static uint64_t Hash(const std::vector<float>& data) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for(float x : data) {
        uint32_t bits;
        std::memcpy(&bits, &x, sizeof(bits));
        hash = (hash ^ bits) * 0x100000001B3ull;
    }
    return hash;
}
static uint64_t RenderScript(uint32_t seed, uint32_t tick_blocks, Rig& rig, const std::vector<int16_t>& data) {
    Config config = Cfg(4, tick_blocks);
    config.seed = seed;
    rig.left.clear();
    rig.right.clear();
    CHECK(rig.Start(ViewOf(data), config));
    rig.Set(Param::Spray, 0.3f);
    rig.Set(Param::Direction, 0.3f);
    rig.Set(Param::Start, StartModeValue(StartMode::FullCloud));
    rig.engine.Post(NoteOn(60, 100));
    rig.Blocks(300);
    rig.engine.Post(NoteOn(67, 100));
    rig.Set(Param::Window, 0.7f);
    rig.Blocks(300);
    rig.engine.Post(NoteOff(60));
    rig.Blocks(300);
    return Hash(rig.left) ^ (Hash(rig.right) << 1);
}

// Same input, same output; the seed is the only source of variation.
static void TestDeterminism() {
    const auto data = NoiseSample(48000);
    for(uint32_t tick_blocks : {1u, 4u}) {
        auto a = std::make_unique<Rig>();
        auto b = std::make_unique<Rig>();
        const uint64_t first = RenderScript(1234u, tick_blocks, *a, data);
        CHECK(first == RenderScript(1234u, tick_blocks, *b, data));
        CHECK(first == RenderScript(1234u, tick_blocks, *a, data)); // Prepare() resets everything
        CHECK(first != RenderScript(4321u, tick_blocks, *b, data));
    }
}

// A long random run: invariants, finite output, no allocation.
static void TestStress() {
    const auto data = NoiseSample(200000);
    auto rig = std::make_unique<Rig>();
    CHECK(rig->Start(ViewOf(data), Cfg(8, 1, 64)));
    GrainRng rng(99u);
    rig->left.reserve(1);
    const long before = allocations.load();
    float l[48], r[48];
    for(int b = 0; b < 60000; ++b) {
        const uint32_t pick = rng.Next() >> 24;
        const uint8_t note = uint8_t(36 + (rng.Next() >> 27));
        if(pick < 12)
            rig->engine.Post(NoteOn(note, uint8_t(1 + (rng.Next() >> 25))));
        else if(pick < 24)
            rig->engine.Post(NoteOff(note));
        else if(pick < 80)
            rig->engine.Post(SetParam(Param(uint8_t(rng.Next() >> 24) % kParamCount), rng.Unit()));
        else if(pick == 80)
            rig->engine.Post(AllSoundOff());
        else if(pick == 81)
            rig->engine.Post(Sustain(rng.Next() >> 31));
        else if(pick == 82)
            rig->engine.Post(PitchBend(rng.Bipolar() * 12.f));
        rig->engine.Process(l, r, 48);
        for(int i = 0; i < 48; ++i)
            CHECK(std::isfinite(l[i]) && std::fabs(l[i]) < 1.f && std::fabs(r[i]) < 1.f);
        if(b % 16 == 0)
            CHECK(rig->Consistent());
        CHECK(rig->engine.Pool().Active() <= 64);
    }
    CHECK(allocations.load() == before);
    const Stats& s = rig->engine.Diagnostics();
    CHECK(s.hard_kills == 0);
    CHECK(s.peak_births_in_block <= kMaxBirthsPerBlock);
    CHECK(s.invalid_blocks == 0);
    CHECK(rig->engine.Output().NonFinite() == 0);
    std::printf("stress: births %u dropped %u steals %u voices stolen %u peak active %u\n", s.births, s.dropped,
                s.steals, s.voices_stolen, s.peak_active);
}

// A wrong block or configuration is refused, not rendered.
static void TestInvalidUse() {
    auto engine = std::make_unique<Engine>();
    Config bad;
    bad.block_frames = 0;
    CHECK(!engine->Prepare(bad));
    bad = Config{};
    bad.polyphony = kMaxVoices + 1;
    CHECK(!engine->Prepare(bad));
    bad = Config{};
    bad.pool_size = kMaxGrains + 1;
    CHECK(!engine->Prepare(bad));
    bad = Config{};
    bad.tick_blocks = 0;
    CHECK(!engine->Prepare(bad));
    CHECK(engine->Prepare(Config{}));
    float l[32], r[32];
    engine->RenderSum(l, r, 32);
    CHECK(l[0] == 0.f && engine->Diagnostics().invalid_blocks == 1);
    engine->Post(SetParam(Param::Level, std::nanf("")));
    float a[48], b[48];
    engine->Process(a, b, 48);
    CHECK(engine->Base(Param::Level) == kParamInfo[size_t(Param::Level)].initial);
}

// One main thread against the audio side: run under TSan by make test-thread.
static void TestThreads() {
    const auto first = NoiseSample(48000), second = DcSample(48000);
    auto engine = std::make_unique<Engine>();
    CHECK(engine->Prepare(Cfg()));
    CHECK(engine->PublishSample(ViewOf(first, 1)));
    std::atomic<bool> stop{false};
    std::thread control([&] {
        Stats stats;
        uint32_t next = 2, oldest = 1;
        while(!stop.load(std::memory_order_relaxed)) {
            if(engine->ConsumeSampleRetired(oldest))
                ++oldest;
            engine->Post(NoteOn(60, 100));
            engine->Post(SetParam(Param::Density, 0.7f));
            engine->Post(NoteOff(60));
            engine->ReadStats(stats);
            if(engine->PublishSample(ViewOf(next % 2 ? first : second, next)))
                ++next;
            std::this_thread::yield();
        }
    });
    float l[48], r[48];
    for(int b = 0; b < 3000; ++b)
        engine->Process(l, r, 48);
    stop.store(true);
    control.join();
    Stats stats;
    CHECK(engine->ReadStats(stats) || stats.ticks == 0);
}

int main() {
    TestControlTick();
    TestRamps();
    TestSingleClip();
    TestEffectPoint();
    TestCountGainSweep();
    TestNoteStarts();
    TestAttackShapesCloud();
    TestKeep();
    TestRetrigger();
    TestStealAndPanic();
    TestSustain();
    TestBirthTable();
    TestScanRow();
    TestEnvelopeRows();
    TestLimitRow();
    TestWindowRow();
    TestWindowSweep();
    TestDirection();
    TestScheduler();
    TestSprayDistribution();
    TestGrainStealing();
    TestStolenGrainEndsInTime();
    TestNoHangingNotes();
    TestLostReleaseOrder();
    TestZeroVelocityRelease();
    TestCloudUsesCurrentParams();
    TestSampleHandOver();
    TestRetirementsKept();
    TestTinySamples();
    TestDeterminism();
    TestStress();
    TestInvalidUse();
    TestThreads();
    if(failures) {
        std::printf("test_engine: %d failures\n", failures);
        return 1;
    }
    std::printf("test_engine: ok\n");
    return 0;
}
