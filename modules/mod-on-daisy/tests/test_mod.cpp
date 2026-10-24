// Host tests of TEST_PLAN_PHASE2.md § 2.1-2.4, 2.6 and 2.7.
#include "common.h"
#include <atomic>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <string>
#include <utility>

// ---- allocation counter for the real-time paths (§ 2.3) ----
static std::atomic<long> g_allocations{0};
void* operator new(std::size_t size) {
    ++g_allocations;
    if(void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace test;

namespace {
bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

uint32_t BitsOf(float x) {
    uint32_t u;
    std::memcpy(&u, &x, sizeof(u));
    return u;
}

// ---------------------------------------------------------------- patch / CRC
void TestCrcAndSerialization() {
    const char* check = "123456789";
    assert(Crc32(reinterpret_cast<const uint8_t*>(check), 9) == 0xCBF43926u);
    assert(kPatchBytes == 109);
    std::mt19937 rng(7);
    for(int round = 0; round < 1000; ++round) {
        Patch p;
        for(auto& row : p.depth)
            for(int8_t& d : row)
                d = int8_t(int(rng() % 201) - 100);
        for(uint8_t& e : p.enabled)
            e = uint8_t(rng());
        for(LfoParams& l : p.lfo) {
            l.rate = uint16_t(rng() % (kRateMax + 1));
            l.shape = uint8_t(rng() % kShapeCount);
            l.sync = uint8_t(rng() % kSyncCount);
            l.gate = uint8_t(rng() % 2);
            l.division = uint8_t(rng() % kGateDivisionCount);
            l.retrigger = uint8_t(rng() % 2);
        }
        p.random.rate = uint16_t(rng() % (kRateMax + 1));
        p.random.mode = uint8_t(rng() % 2);
        for(char& c : p.name)
            c = char(rng() % 128);
        const PatchBytes bytes = Serialize(p);
        const Patch q = Deserialize(bytes);
        assert(Serialize(q) == bytes);
        assert(PatchCrc(q) == PatchCrc(p));
    }
    // Fixed test vector: the editor must compute the same CRC (TEST_PLAN § 2.2).
    Patch v;
    v = WithCell(v, Lfo1, 0, 35);
    v = WithCell(v, ChannelAt, 4, -60);
    std::strcpy(v.name, "Vector");
    std::printf("patch CRC test vector: %08X\n", unsigned(PatchCrc(v)));
}

void TestValidation() {
    const InstrumentInfo info = MakeInfo();
    const uint64_t allowed = AllowedCells(info);
    // Columns 6/7 missing, Poly AT on the latched column 3 excluded.
    for(std::size_t s = 0; s < kSources; ++s) {
        assert(!(allowed & CellBit(s, 6)) && !(allowed & CellBit(s, 7)));
        for(std::size_t t = 0; t < 6; ++t)
            assert(bool(allowed & CellBit(s, t)) == !(s == PolyAt && t == 3));
    }
    InstrumentInfo cv_owned = info;
    cv_owned.unavailable_sources = 1u << Cv1;
    for(std::size_t t = 0; t < kTargets; ++t)
        assert(!(AllowedCells(cv_owned) & CellBit(Cv1, t)));

    Patch p;
    assert(Validate(p, allowed) == Status::Ok);
    Patch range = p;
    range.depth[0][0] = 101;
    assert(Validate(range, allowed) == Status::Range);
    Patch off = p; // a switched-off cell may keep a depth, even in a disallowed cell
    off.depth[Lfo1][7] = 50;
    assert(Validate(off, allowed) == Status::Ok);
    assert(Validate(WithCell(p, Lfo1, 7, 50), allowed) == Status::NotAllowed);
    assert(Validate(WithCell(p, PolyAt, 3, 50), allowed) == Status::NotAllowed);
    if(kMaxActive != 8)
        return; // the stress build (64) has no reachable limit with these cells
    Patch full = p;
    for(std::size_t i = 0; i < kMaxActive; ++i)
        full = WithCell(full, Source(i % 7), i % 6, 10);
    assert(PopCount(full.ActiveMask()) == kMaxActive);
    assert(Validate(full, allowed) == Status::Ok);
    Patch over = WithCell(full, PolyAt, 5, 10);
    assert(Validate(over, allowed) == Status::Limit);
    Patch zero = WithCell(full, PolyAt, 5, 0); // enabled with depth 0 is not active
    assert(Validate(zero, allowed) == Status::Ok);
    Patch name = p;
    name.name[0] = char(0xC4);
    assert(Validate(name, allowed) == Status::Range);
    Patch division = p;
    division.lfo[0].sync = GateClock;
    division.lfo[0].division = kGateDivisionCount;
    assert(Validate(division, allowed) == Status::Range);
}

// ---------------------------------------------------------------- sources
void TestShapesAndRates() {
    float worst = 0;
    for(int i = 0; i < 100000; ++i) {
        const float p = float(i) / 100000.0f;
        worst = std::fmax(worst, std::fabs(SinePhase(p) - float(std::sin(2.0 * M_PI * p))));
    }
    std::printf("sine approximation max error: %.5f\n", double(worst));
    assert(worst < 0.0015f);
    assert(ShapeValue(Triangle, 0.0f) == 0.0f && ShapeValue(Triangle, 0.25f) == 1.0f);
    assert(ShapeValue(Triangle, 0.75f) == -1.0f && Near(ShapeValue(Triangle, 0.5f), 0.0f, 1e-6f));
    assert(ShapeValue(SawUp, 0.0f) == -1.0f && ShapeValue(SawDown, 0.0f) == 1.0f);
    assert(ShapeValue(Square, 0.25f) == 1.0f && ShapeValue(Square, 0.75f) == -1.0f);
    assert(Near(RateToHz(0), 0.02f, 1e-6f) && Near(RateToHz(kRateMax), 20.0f, 1e-3f));
    for(uint16_t r = 1; r <= kRateMax; ++r)
        assert(RateToHz(r) > RateToHz(uint16_t(r - 1)));
    for(float hz : {0.02f, 1.0f, 20.0f}) {
        const double actual = double(PhaseIncrement(hz, 48000.0f)) * 48000.0 / 4294967296.0;
        assert(std::fabs(actual - hz) / hz < 5e-4);
    }
}

uint16_t RateFor(float hz) { // inverse of RateToHz, for readable tests
    return uint16_t(std::lround(std::log(hz / 0.02) / std::log(1000.0) * kRateMax));
}

void TestLfoTickIndependence() {
    // The phase after the same number of samples is bit-identical for every tick length.
    const std::size_t ticks[] = {12, 32, 48, 64, 256};
    std::vector<float> values;
    for(std::size_t n : ticks) {
        Device d(n);
        Patch p = WithCell(Patch{}, Lfo1, 4, 100);
        p.lfo[0].shape = SawUp;
        p.lfo[0].rate = RateFor(7.3f);
        assert(d.module.controller.LoadPatch(p) == Status::Ok);
        d.Ticks(288000 / n, n);
        values.push_back(d.module.engine.Source(Lfo1));
    }
    for(float v : values)
        assert(BitsOf(v) == BitsOf(values[0]));
    // Frequency: count cycles over 600 s at 20 Hz.
    Device d;
    Patch p = WithCell(Patch{}, Lfo1, 4, 100);
    p.lfo[0].shape = SawUp;
    p.lfo[0].rate = kRateMax;
    d.module.controller.LoadPatch(p);
    float previous = -2.0f;
    int wraps = 0;
    for(int i = 0; i < 600 * 1000; ++i) {
        d.module.engine.Tick(48);
        const float v = d.module.engine.Source(Lfo1);
        if(v < previous)
            ++wraps;
        previous = v;
    }
    std::printf("LFO 20 Hz: %d cycles in 600 s\n", wraps);
    assert(std::abs(wraps - 12000) <= 6);
}

void TestRetriggerAndSampleHold() {
    Device d;
    Patch p = WithCell(Patch{}, Lfo1, 4, 100);
    p.lfo[0].shape = SawUp;
    p.lfo[0].rate = RateFor(1.0f);
    p.lfo[0].retrigger = 1;
    d.module.controller.LoadPatch(p);
    d.Ticks(300);
    assert(d.module.engine.Source(Lfo1) > -0.5f);
    d.Midi(0x90, 60, 100);
    d.Ticks(1);
    assert(d.module.engine.Source(Lfo1) == -1.0f); // phase restarted at 0

    // S&H shape: constant inside a cycle, new value after a wrap.
    p.lfo[0].shape = SampleHold;
    p.lfo[0].retrigger = 0;
    p.lfo[0].rate = RateFor(10.0f); // 100 ms
    d.module.controller.LoadPatch(p);
    d.Ticks(1);
    int changes = 0;
    float last = d.module.engine.Source(Lfo1);
    for(int i = 0; i < 1000; ++i) { // 1 s
        d.Ticks(1);
        const float v = d.module.engine.Source(Lfo1);
        assert(v >= -1.0f && v < 1.0f);
        if(v != last)
            ++changes;
        last = v;
    }
    assert(changes >= 9 && changes <= 11);
}

void TestRandom() {
    auto run = [](uint8_t mode, std::vector<float>& out) {
        Device d;
        Patch p = WithCell(Patch{}, Random, 4, 100);
        p.random.rate = RateFor(5.0f);
        p.random.mode = mode;
        d.module.controller.LoadPatch(p);
        for(int i = 0; i < 4000; ++i) {
            d.Ticks(1);
            out.push_back(d.module.engine.Source(Random));
        }
    };
    std::vector<float> a, b, smooth;
    run(Stepped, a);
    run(Stepped, b);
    assert(a == b); // fixed seed: reproducible
    int changes = 0;
    for(std::size_t i = 1; i < a.size(); ++i) {
        assert(a[i] >= -1.0f && a[i] < 1.0f);
        changes += a[i] != a[i - 1];
    }
    assert(changes >= 19 && changes <= 21); // 5 Hz for 4 s
    run(Smooth, smooth);
    const float max_step = 2.0f * 48.0f * 5.0f / 48000.0f * 1.01f;
    for(std::size_t i = 2; i < smooth.size(); ++i)
        assert(std::fabs(smooth[i] - smooth[i - 1]) <= max_step);
}

void TestSmoothingAndCv() {
    for(std::size_t n : {48u, 256u}) {
        Device d(n);
        d.module.controller.LoadPatch(WithCell(Patch{}, ModWheel, 4, 100));
        d.Midi(0xB0, 1, 127);
        const std::size_t ticks = std::size_t(std::lround(480.0 / double(n))); // ~10 ms
        float t = 0;
        std::size_t done = 0;
        while(done * n < 480 && done < ticks) {
            d.Ticks(1, n);
            ++done;
        }
        t = float(done * n) / 480.0f;
        const float expected = 1.0f - std::exp(-t);
        assert(Near(d.module.engine.Source(ModWheel), expected, 0.05f));
        d.Ticks(5000 / n + 400, n);
        assert(d.module.engine.Source(ModWheel) == 1.0f); // exact convergence (snap)
    }
    assert(CvDeadBand(0.019f) == 0.0f && CvDeadBand(-0.019f) == 0.0f);
    assert(Near(CvDeadBand(0.021f), 0.001f / 0.98f, 1e-6f));
    assert(CvDeadBand(1.0f) == 1.0f && CvDeadBand(-1.0f) == -1.0f);
    Device d;
    d.module.controller.LoadPatch(WithCell(Patch{}, Cv1, 4, 100));
    d.module.engine.SetHardware(0.5f, 0.0f, false, false);
    d.Ticks(100);
    assert(Near(d.module.engine.Source(Cv1), (0.5f - kCvDeadBand) / (1 - kCvDeadBand), 1e-5f));
    d.module.controller.SetCalibration(0.5f, 0.0f);
    d.Ticks(100);
    assert(d.module.engine.Source(Cv1) == 0.0f);
}

// ---------------------------------------------------------------- engine
void TestNeutral() {
    Device d;
    auto check = [&]() {
        for(int i = 0; i <= 100000; ++i) {
            const float base = float(i) / 100000.0f;
            for(std::size_t t = 0; t < kTargets; ++t) {
                assert(BitsOf(d.module.engine.Apply(t, base)) == BitsOf(base));
                assert(BitsOf(d.module.engine.ApplyVoice(t, base, 0.7f)) == BitsOf(base));
            }
        }
    };
    d.Midi(0xB0, 1, 127);
    d.Ticks(10);
    check();
    d.module.controller.LoadPatch(WithCell(Patch{}, ModWheel, 4, 100));
    d.Ticks(10);
    assert(d.module.engine.Apply(4, 0.2f) > 0.2f);
    d.module.controller.LoadPatch(Patch{});
    d.Ticks(1);
    check(); // back to exactly neutral after a connection was removed
}

void TestSumClampAndSpan() {
    Device d;
    Patch p = WithCell(Patch{}, ModWheel, 4, 50);
    p = WithCell(p, ChannelAt, 4, -50);
    p = WithCell(p, ModWheel, 5, 100);
    p = WithCell(p, ModWheel, 0, 100); // Pitch, span 0.25
    p = WithCell(p, ModWheel, 2, 1);
    d.module.controller.LoadPatch(p);
    d.Midi(0xB0, 1, 127);
    d.Midi(0xD0, 127);
    d.Ticks(1000);
    assert(d.module.engine.Offset(4) == 0.0f);         // +50 % and -50 % cancel
    assert(d.module.engine.Apply(5, 0.8f) == 1.0f);    // clamped after the sum
    assert(Near(d.module.engine.Offset(0), 0.25f, 1e-6f));
    assert(Near(d.module.engine.Offset(2), 0.01f, 1e-6f));
}

void TestProgramSwapKeepsPhase() {
    Device a, b;
    Patch p = WithCell(Patch{}, Lfo1, 4, 100);
    p.lfo[0].shape = SawUp;
    p.lfo[0].rate = RateFor(0.5f);
    a.module.controller.LoadPatch(p);
    b.module.controller.LoadPatch(p);
    a.Ticks(777);
    b.Ticks(777);
    b.module.controller.LoadPatch(WithCell(p, ModWheel, 5, 40)); // swap while running
    a.Ticks(500);
    b.Ticks(500);
    assert(BitsOf(a.module.engine.Source(Lfo1)) == BitsOf(b.module.engine.Source(Lfo1)));
}

// ---------------------------------------------------------------- notes / voices
void TestNotesAndVoices() {
    LiveState live;
    live.NoteOn(60);
    live.PolyPressure(60, 100);
    assert(live.PressureRaw(60) == 100 && live.LastPressureRaw() == 100);
    live.PolyPressure(61, 50); // not held
    assert(live.PressureRaw(61) == 0);
    live.NoteOn(64);
    assert(live.LastNote() == 64 && live.LastPressureRaw() == 0);
    live.PolyPressure(64, 30);
    assert(live.LastPressureRaw() == 30);
    live.NoteOff(64);
    assert(live.LastNote() == 60 && live.LastPressureRaw() == 100);
    live.NoteOn(60); // retrigger resets the pressure of that note
    assert(live.PressureRaw(60) == 0);
    for(uint8_t n = 0; n < 17; ++n)
        live.NoteOn(uint8_t(70 + n));
    assert(live.LastNote() == 86);
    for(uint8_t n = 0; n < 17; ++n)
        live.NoteOff(uint8_t(86 - n));
    assert(live.LastNote() == -1 || live.Held(uint8_t(live.LastNote())));
    live.AllNotesOff();
    assert(live.LastNote() == -1 && !live.Held(60));

    // Voice stealing and shared notes through the engine.
    Device d;
    Patch p = WithCell(Patch{}, PolyAt, 1, 100); // Level, per voice
    p = WithCell(p, PolyAt, 4, 100);             // Reverb, global -> last note
    d.module.controller.LoadPatch(p);
    d.Ticks(1); // the audio side takes the new program at the next tick
    assert(d.module.engine.VoiceGain(1) == 1.0f && d.module.engine.VoiceGain(4) == 0.0f);
    d.Midi(0x90, 60, 100);
    d.Midi(0xA0, 60, 127);
    VoicePressure voice_a, voice_b;
    for(int i = 0; i < 200; ++i) {
        d.Ticks(1);
        voice_a.Process(d.module.engine, 60);
        voice_b.Process(d.module.engine, 60); // same note in two voices: same pressure
    }
    assert(voice_a.Value() == 1.0f && voice_b.Value() == 1.0f);
    assert(d.module.engine.Offset(1) == 0.0f); // per-voice target: not in the global offset
    assert(Near(d.module.engine.Offset(4), 1.0f, 1e-6f));
    assert(d.module.engine.ApplyVoice(1, 0.0f, voice_a.Value()) == 1.0f);
    // The voice is stolen for note 64 (pressure 0): it glides down in ~10 ms.
    d.Midi(0x90, 64, 100);
    d.Ticks(10);
    voice_a.Process(d.module.engine, 64);
    for(int i = 0; i < 9; ++i)
        voice_a.Process(d.module.engine, 64);
    assert(voice_a.Value() < 0.45f && voice_a.Value() > 0.25f);
    voice_a.Reset();
    assert(voice_a.Value() == 0.0f);
    assert(d.module.engine.Apply(1, 0.3f) == 0.3f); // Apply ignores the voice path
}

// ---------------------------------------------------------------- clock sync
void TestMidiClockSync() {
    Device d;
    Patch p = WithCell(Patch{}, Lfo1, 4, 100);
    p.lfo[0].shape = SawUp;
    p.lfo[0].sync = MidiClock;
    p.lfo[0].division = 4; // 1/4 note = 24 pulses
    d.module.controller.LoadPatch(p);
    // 120 BPM: one pulse every 1000 samples; ticks of 48 samples.
    std::size_t sample = 0, next_pulse = 0;
    auto run = [&](std::size_t samples, bool clock) {
        for(std::size_t end = sample + samples; sample < end; sample += 48) {
            while(clock && next_pulse < sample + 48) {
                d.Midi(0xF8, 0);
                next_pulse += 1000;
            }
            d.Ticks(1);
        }
    };
    run(48000, true); // tempo locks
    const float period = d.module.engine.MidiClockFollower().PeriodSamples();
    assert(Near(period, 1000.0f, 30.0f));
    d.Midi(0xFA, 0); // Start: the next pulse is the beat
    next_pulse = sample + 500;
    run(48 * 20, true);
    // After the first pulse the LFO starts the cycle near -1 (phase ~ 0).
    run(48 * 4, true);
    const float early = d.module.engine.Source(Lfo1);
    assert(early < -0.8f);
    // Half a quarter note later (12 pulses) the saw is in the middle.
    run(12000, true);
    assert(Near(d.module.engine.Source(Lfo1), early + 1.0f, 0.15f));
    // Clock stops: the LFO holds instead of running ahead.
    run(4800, false);
    const float held = d.module.engine.Source(Lfo1);
    run(48000, false);
    assert(d.module.engine.Source(Lfo1) == held);
}

void TestGateSync() {
    Device d;
    Patch p = WithCell(Patch{}, Lfo2, 4, 100);
    p.lfo[1].shape = SawUp;
    p.lfo[1].sync = GateClock;
    p.lfo[1].gate = 1;          // Gate 2
    p.lfo[1].division = 3;      // two cycles per pulse
    p = WithCell(p, Random, 5, 100);
    p.random.sync = GateClock;  // Gate 1
    p.random.division = kDefaultGateDivision;
    d.module.controller.LoadPatch(p);
    int lfo_wraps = 0, random_changes = 0;
    float last_lfo = 0, last_random = 0;
    for(int tick = 0; tick < 10000; ++tick) { // 10 s, one pulse every 200 ms (5 Hz)
        const bool high = (tick % 200) < 20;
        d.module.engine.SetHardware(0, 0, high, high);
        d.Ticks(1);
        const float lfo = d.module.engine.Source(Lfo2), rnd = d.module.engine.Source(Random);
        if(tick > 1000) {
            lfo_wraps += lfo < last_lfo;
            random_changes += rnd != last_random;
        }
        last_lfo = lfo;
        last_random = rnd;
    }
    assert(Near(d.module.engine.GateFollower(1).PeriodSamples(), 9600.0f, 1.0f));
    std::printf("gate sync: %d LFO cycles, %d random steps in 9 s\n", lfo_wraps, random_changes);
    assert(lfo_wraps >= 88 && lfo_wraps <= 92);         // 2 per pulse
    assert(random_changes >= 44 && random_changes <= 46); // 1 per pulse
}

// ---------------------------------------------------------------- protocol / controller
void TestProtocolCommands() {
    Device d;
    Reply r = d.Request(proto::Identify, {}, proto::kBroadcast);
    assert(r.present && r.frame.command == (proto::Identify | 0x40));
    proto::Reader id(r.payload.data(), r.payload.size());
    assert(id.U7() == 1 && id.U7() == 0);
    id.U7(); id.U7(); id.U7();
    char key[kMaxKeyLength + 1]{};
    id.Str(key, kMaxKeyLength);
    assert(std::strcmp(key, "test") == 0);
    char version[13];
    id.Str(version, 12);
    assert(id.U7() == kMaxActive && id.U14() == 1000 && id.U7() == 0);
    assert(id.U32() == SchemaHash(d.info));
    assert(id.U7() == 0x0A && id.AtEnd()); // extensions are off unless the instrument enables them

    // The longest planned key is sent in full (review finding: 17 > 16 characters).
    {
        Device pm;
        pm.info.key = "physical-modeling";
        pm.module.Init(pm.info, 48000.0f, 48);
        const Reply ident = pm.Request(proto::Identify, {}, proto::kBroadcast);
        proto::Reader rr(ident.payload.data(), ident.payload.size());
        for(int i = 0; i < 5; ++i)
            rr.U7();
        char long_key[kMaxKeyLength + 1]{};
        assert(rr.Str(long_key, kMaxKeyLength) == 17 && rr.Ok());
        assert(std::strcmp(long_key, "physical-modeling") == 0);
    }

    r = d.Request(proto::GetTarget, Bytes({0}));
    proto::Reader target(r.payload.data(), r.payload.size());
    assert(target.U7() == 0 && target.U7() == 3 && target.U14() == 4096);
    char name[13];
    target.Str(name, 12);
    assert(std::strcmp(name, "Pitch") == 0);
    r = d.Request(proto::GetTarget, Bytes({7}));
    assert(r.payload[1] == 0); // column 7 not present
    r = d.Request(proto::GetTarget, Bytes({8}));
    assert(r.frame.command == proto::kNak && r.payload[1] == proto::ErrRange);

    r = d.Request(proto::GetAllowed, {});
    proto::Reader allowed(r.payload.data(), r.payload.size());
    assert(allowed.Mask64() == AllowedCells(d.info));

    // SET_CELL -64 on LFO 2 -> Bright, switched on.
    r = d.Request(proto::SetCell, Bytes({Lfo2, 2, 64, 3}));
    assert(r.frame.command == (proto::SetCell | 0x40) && r.payload[0] == 0 && r.payload[1] == 1);
    proto::Reader change(r.payload.data() + 2, 5);
    assert(change.U32() == PatchCrc(d.module.controller.CurrentPatch()));
    assert(d.module.controller.CurrentPatch().depth[Lfo2][2] == -64);

    r = d.Request(proto::GetMatrix, {});
    assert(r.size == 100);
    proto::Reader matrix(r.payload.data(), r.payload.size());
    uint8_t depth[kCells];
    matrix.Packed(depth, kCells);
    assert(int8_t(depth[CellIndex(Lfo2, 2)]) == -64);
    assert(matrix.Mask64() == CellBit(Lfo2, 2));
    assert(matrix.U32() == d.module.controller.CurrentCrc() && matrix.AtEnd());

    // SET_MATRIX with the extremes of the depth range.
    uint8_t payload[proto::kMaxPayload];
    proto::Writer w(payload, sizeof(payload));
    uint8_t grid[kCells]{};
    grid[CellIndex(Lfo1, 0)] = uint8_t(int8_t(-100));
    grid[CellIndex(ModWheel, 5)] = 100;
    grid[CellIndex(Cv2, 7)] = uint8_t(int8_t(-1)); // disallowed cell, but switched off
    w.Packed(grid, kCells);
    w.Mask64(CellBit(Lfo1, 0) | CellBit(ModWheel, 5));
    assert(w.Size() == 84);
    uint8_t frame[proto::kMaxFrame];
    const std::size_t size = proto::BuildFrame(frame, 0, proto::SetMatrix, 99, payload, w.Size());
    assert(size == 95);
    d.SendRaw(frame, size);
    r = d.Collect();
    assert(r.frame.command == (proto::SetMatrix | 0x40) && r.payload[1] == 2);
    const Patch& p = d.module.controller.CurrentPatch();
    assert(p.depth[Lfo1][0] == -100 && p.depth[ModWheel][5] == 100 && p.depth[Cv2][7] == -1);
    assert(p.depth[Lfo2][2] == 0);

    // Not allowed, out of range, limit.
    r = d.Request(proto::SetCell, Bytes({PolyAt, 3, 10, 2}));
    assert(r.frame.command == proto::kNak && r.payload[1] == proto::ErrNotAllowed);
    r = d.Request(proto::SetCell, Bytes({Lfo1, 0, 101, 2}));
    assert(r.payload[1] == proto::ErrRange);
    d.Request(proto::ClearMatrix, {});
    for(unsigned i = 0; i < kMaxActive && kMaxActive == 8; ++i) {
        r = d.Request(proto::SetCell, Bytes({i % 7, i % 6, 10, 2}));
        assert(r.frame.command == (proto::SetCell | 0x40));
    }
    const uint32_t crc = d.module.controller.CurrentCrc();
    if(kMaxActive == 8) {
        r = d.Request(proto::SetCell, Bytes({PolyAt, 5, 10, 2}));
        assert(r.frame.command == proto::kNak && r.payload[1] == proto::ErrLimit);
        assert(d.module.controller.CurrentCrc() == crc);
    }
    r = d.Request(proto::SetCell, Bytes({PolyAt, 5, 10, 0})); // depth on a switched-off cell is fine
    assert(r.frame.command == (proto::SetCell | 0x40));

    // Sources: switching to gate sync keeps the division valid.
    w = proto::Writer(payload, sizeof(payload));
    r = d.Request(proto::SetSource, Bytes({0, ParamDivision, 10, 0}));
    assert(r.frame.command == (proto::SetSource | 0x40));
    r = d.Request(proto::SetSource, Bytes({0, ParamSync, GateClock, 0}));
    assert(r.frame.command == (proto::SetSource | 0x40));
    assert(d.module.controller.CurrentPatch().lfo[0].division == kDefaultGateDivision);
    r = d.Request(proto::SetSource, Bytes({0, ParamSync, MidiClock, 0}));
    assert(d.module.controller.CurrentPatch().lfo[0].division == kDefaultMidiDivision); // back to 1/4
    r = d.Request(proto::SetSource, Bytes({0, ParamSync, MidiClock, 0}));             // same source: kept
    assert(d.module.controller.CurrentPatch().lfo[0].division == kDefaultMidiDivision);
    r = d.Request(proto::SetSource, Bytes({0, ParamSync, GateClock, 0}));
    r = d.Request(proto::SetSource, Bytes({0, ParamShape, SampleHold, 0}));
    assert(r.frame.command == (proto::SetSource | 0x40));
    r = d.Request(proto::SetSource, Bytes({0, ParamShape, kShapeCount, 0}));
    assert(r.payload[1] == proto::ErrRange);
    r = d.Request(proto::SetSource, Bytes({Random, ParamRetrigger, 1, 0}));
    assert(r.payload[1] == proto::ErrRange);
    r = d.Request(proto::SetSource, Bytes({0, ParamRate, kRateMax & 127, kRateMax >> 7}));
    r = d.Request(proto::GetSource, Bytes({0}));
    proto::Reader source(r.payload.data(), r.payload.size());
    assert(source.U7() == 0 && source.U14() == kRateMax && source.U7() == SampleHold && source.U7() == GateClock);

    // Name.
    r = d.Request(proto::SetName, Bytes({4, 'P', 'a', 'd', '!'}));
    assert(r.frame.command == (proto::SetName | 0x40));
    r = d.Request(proto::GetName, {});
    assert(r.payload.size() == 5 && r.payload[1] == 'P');

    // Storage is specified only (D8); unknown commands.
    for(uint8_t cmd : {proto::Store, proto::Recall, proto::GetSlotInfo, proto::SetPowerOn}) {
        r = d.Request(cmd, Bytes({0}));
        assert(r.frame.command == proto::kNak && r.payload[1] == proto::ErrNotImplemented);
    }
    r = d.Request(0x3F, {});
    assert(r.payload[1] == proto::ErrUnknown);
    r = d.Request(proto::GetStats, {});
    assert(r.payload[1] == proto::ErrNotImplemented); // no diagnostics in an instrument

    // Calibration (RAM only).
    r = d.Request(proto::SetCalibration, Bytes({0, 66, 0, 64, 0}));
    assert(r.frame.command == (proto::SetCalibration | 0x40));
    r = d.Request(proto::GetCalibration, {});
    proto::Reader cal(r.payload.data(), r.payload.size());
    assert(cal.U14() == 66u * 128 && cal.U14() == 8192 && cal.U7() == 0);
}

void TestFramingErrors() {
    Device d;
    uint8_t payload[1] = {0};
    uint8_t frame[proto::kMaxFrame];
    const std::size_t size = proto::BuildFrame(frame, 0, proto::GetTarget, 5, payload, 1);
    // Every single flipped bit in the body is detected (or makes it not ours / another device).
    for(std::size_t byte = 1; byte < size - 1; ++byte)
        for(int bit = 0; bit < 7; ++bit) {
            uint8_t bad[proto::kMaxFrame];
            std::memcpy(bad, frame, size);
            bad[byte] ^= uint8_t(1 << bit);
            MidiMessage m;
            m.status = 0xF0;
            m.sysex = bad + 1;
            m.sysex_size = size - 2;
            const bool ours = d.module.controller.HandleMidi(m);
            const Reply r = d.Collect();
            if(byte <= 3) { // manufacturer / project changed: passed on to the instrument
                assert(!ours && !r.present);
                continue;
            }
            assert(ours);
            if(byte == 5) { // device byte: another device, consumed silently
                assert(!r.present);
                continue;
            }
            if(r.present && r.frame.command != proto::kNak) {
                assert(false && "corrupted frame accepted");
            }
        }
    // A 130-byte frame that libDaisy truncates to 128 body bytes is rejected.
    uint8_t body[128];
    body[0] = 0x7D; body[1] = 0x4D; body[2] = 0x44; body[3] = 1; body[4] = 0;
    body[5] = proto::GetMatrix; body[6] = 9; body[7] = 120;
    for(std::size_t i = 8; i < sizeof(body); ++i)
        body[i] = 0;
    MidiMessage m;
    m.status = 0xF0;
    m.sysex = body;
    m.sysex_size = sizeof(body);
    assert(d.module.controller.HandleMidi(m));
    Reply r = d.Collect();
    assert(r.frame.command == proto::kNak && r.payload[1] == proto::ErrLength);
    // Wrong protocol version.
    uint8_t v2[proto::kMaxFrame];
    const std::size_t n = proto::BuildFrame(v2, 0, proto::Identify, 3, nullptr, 0);
    v2[4] = 2;
    v2[n - 2] = proto::Checksum(v2 + 4, n - 6);
    d.SendRaw(v2, n);
    r = d.Collect();
    assert(r.frame.command == proto::kNak && r.payload[1] == proto::ErrVersion);
    // Another manufacturer's SysEx is not ours.
    const uint8_t foreign[] = {0x43, 0x10, 0x4C};
    MidiMessage f;
    f.status = 0xF0;
    f.sysex = foreign;
    f.sysex_size = sizeof(foreign);
    assert(!d.module.controller.HandleMidi(f));
}

void TestFuzz() {
    Device d;
    std::mt19937 rng(1234);
    const long rounds = std::getenv("MOD_FUZZ_ROUNDS") ? std::atol(std::getenv("MOD_FUZZ_ROUNDS")) : 1000000;
    uint8_t body[140];
    for(long i = 0; i < rounds; ++i) {
        const std::size_t size = rng() % sizeof(body);
        for(std::size_t j = 0; j < size; ++j)
            body[j] = uint8_t(rng() & 127);
        if(size >= 3 && (rng() & 1)) { // half of the inputs carry our prefix
            body[0] = 0x7D; body[1] = 0x4D; body[2] = 0x44;
            if(size > 4) { body[3] = 1; body[4] = 0; }
        }
        MidiMessage m;
        m.status = 0xF0;
        m.sysex = body;
        m.sysex_size = size;
        d.module.controller.HandleMidi(m);
        d.module.controller.Service(uint32_t(i), d.out);
        d.out.count = 0;
        if((i & 1023) == 0)
            d.Ticks(1);
        assert(Validate(d.module.controller.CurrentPatch(), d.module.controller.Allowed()) == Status::Ok);
    }
    std::printf("fuzz: %ld frames, %u rejected\n", rounds, unsigned(d.module.controller.RejectedFrames()));
}

struct CountingDiagnostics : Diagnostics {
    int scenarios = 0;
    Stats GetStats() override { Stats s; s.ticks = 42; return s; }
    void ResetStats() override {}
    bool SetScenario(uint8_t) override { ++scenarios; return true; }
};

void TestControllerBehavior() {
    CountingDiagnostics diag;
    Device d(48, &diag);
    // A retry with the same seq gets the same answer and is executed once.
    uint8_t frame[proto::kMaxFrame];
    const uint8_t scenario[1] = {4};
    const std::size_t size = proto::BuildFrame(frame, 0, proto::SetScenario, 17, scenario, 1);
    d.SendRaw(frame, size);
    const Reply first = d.Collect();
    d.SendRaw(frame, size);
    const Reply second = d.Collect();
    assert(diag.scenarios == 1 && first.payload == second.payload && second.frame.seq == 17);
    // Reads are never served from the cache: a restarted client reusing a seq gets fresh data.
    uint8_t name_frame[proto::kMaxFrame];
    const std::size_t name_size = proto::BuildFrame(name_frame, 0, proto::GetName, 1, nullptr, 0);
    d.SendRaw(name_frame, name_size);
    assert(d.Collect().payload == Bytes({0}));
    Patch named = d.module.controller.CurrentPatch();
    std::strcpy(named.name, "New");
    d.module.controller.LoadPatch(named);
    d.SendRaw(name_frame, name_size);
    assert(d.Collect().payload.size() == 4);
    // A local change (e.g. a key) invalidates the cached reply of a change request.
    d.module.controller.LoadPatch(Patch{});
    d.SendRaw(frame, size);
    d.Collect();
    assert(diag.scenarios == 2);
    Reply r = d.Request(proto::GetStats, {});
    assert(r.payload.size() == 50);
    proto::Reader stats(r.payload.data(), r.payload.size());
    assert(stats.U32() == 42);

    // At most one reply per Service() call and millisecond; queue of 2.
    for(int i = 0; i < 4; ++i) {
        uint8_t f[proto::kMaxFrame];
        const std::size_t n = proto::BuildFrame(f, 0, proto::GetName, uint8_t(30 + i), nullptr, 0);
        d.SendRaw(f, n);
    }
    assert(d.module.controller.PendingReplies() == 2 && d.module.controller.DroppedReplies() == 2);
    d.out.count = 0;
    d.module.controller.Service(5000, d.out);
    d.module.controller.Service(5000, d.out);
    assert(d.out.count == 1);
    d.module.controller.Service(5001, d.out);
    assert(d.out.count == 2);

    // Monitor: ~20 Hz frames, ends 3 s after the last request.
    Device m;
    m.Request(proto::Monitor, Bytes({20}));
    m.out.count = 0;
    for(uint32_t t = 0; t < 2000; ++t)
        m.module.controller.Service(m.now + t, m.out);
    assert(m.out.count >= 38 && m.out.count <= 41);
    const Reply frame0 = Decode(m.out.frames[0], m.out.sizes[0]);
    assert(frame0.frame.command == proto::MonitorFrame);
    assert(frame0.payload.size() == 36); // plain frames unless the instrument enables extensions
    // No reply code may equal the monitor frame code (found by the editor test).
    for(unsigned cmd = 0; cmd < 0x40; ++cmd)
        assert((cmd | proto::kReplyBit) != proto::MonitorFrame || cmd == 0x3E);
    static_assert(proto::MonitorFrame != (proto::Monitor | proto::kReplyBit), "monitor reply collides");
    m.out.count = 0;
    for(uint32_t t = 2000; t < 6000; ++t)
        m.module.controller.Service(m.now + t, m.out);
    assert(m.out.count < 25); // stopped after 3 s
    const std::size_t stopped = m.out.count;
    for(uint32_t t = 6000; t < 8000; ++t)
        m.module.controller.Service(m.now + t, m.out);
    assert(m.out.count == stopped);

    // Live MIDI routing: CC1/AT consumed, notes and other CCs passed on.
    Device l;
    MidiMessage cc;
    cc.status = 0xB3; cc.data0 = 1; cc.data1 = 64;
    assert(l.module.controller.HandleMidi(cc) && l.module.live.ModWheelRaw() == 64);
    cc.data0 = 20;
    assert(!l.module.controller.HandleMidi(cc));
    MidiMessage note;
    note.status = 0x95; note.data0 = 60; note.data1 = 90;
    assert(!l.module.controller.HandleMidi(note) && l.module.live.Held(60));
    MidiMessage at;
    at.status = 0xD0; at.data0 = 77;
    assert(l.module.controller.HandleMidi(at) && l.module.live.ChannelPressureRaw() == 77);
    MidiMessage off;
    off.status = 0xB0; off.data0 = 123;
    assert(!l.module.controller.HandleMidi(off) && !l.module.live.Held(60));

    // Disconnected host: replies are dropped, not queued forever.
    Device x;
    x.out.connected = false;
    x.Request(proto::GetName, {});
    assert(x.module.controller.PendingReplies() == 0 && x.module.controller.DroppedReplies() == 1);
}

void TestNoAllocation() {
    Device d;
    Patch p = WithCell(Patch{}, Lfo1, 0, 40);
    p = WithCell(p, PolyAt, 1, 100);
    d.module.controller.LoadPatch(p);
    uint8_t frame[proto::kMaxFrame];
    const uint8_t cell[4] = {Random, 4, 30, 2};
    const std::size_t size = proto::BuildFrame(frame, 0, proto::SetCell, 1, cell, 4);
    VoicePressure voice;
    const long before = g_allocations.load();
    float sink = 0;
    for(int i = 0; i < 1000; ++i) {
        d.SendRaw(frame, size);
        frame[7] = uint8_t((frame[7] + 1) & 127);                 // new seq
        frame[size - 2] = proto::Checksum(frame + 4, size - 6);
        d.Midi(0x90, uint8_t(i & 127), 100);
        d.Midi(0xA0, uint8_t(i & 127), 90);
        d.module.controller.Service(uint32_t(i), d.out);
        d.out.count = 0;
        d.module.engine.Tick(48);
        for(std::size_t t = 0; t < kTargets; ++t)
            sink += d.module.engine.Apply(t, 0.5f) + d.module.engine.ApplyVoice(t, 0.5f, voice.Process(d.module.engine, 60));
    }
    assert(g_allocations.load() == before);
    assert(sink > 0);
}
}

#if MOD_ON_DAISY_EXTENSIONS
// Extensions (types.h): extended monitor and LFO cross modulation, switched on at run time.
struct ExtFrame {
    float source[8], offset[8], cv[2], base[8], min[8], max[8];
    unsigned clocks, at_kind, at_note, at_value, at_age;
};
ExtFrame ParseExt(const Reply& f) {
    assert(f.frame.command == proto::MonitorFrame && f.payload.size() == 89);
    proto::Reader r(f.payload.data(), f.payload.size());
    ExtFrame e;
    for(float* group : {e.source, e.offset})
        for(int i = 0; i < 8; ++i)
            group[i] = proto::DecodeSigned(r.U14());
    for(float& v : e.cv)
        v = proto::DecodeSigned(r.U14());
    for(float* group : {e.base, e.min, e.max})
        for(int i = 0; i < 8; ++i)
            group[i] = proto::DecodeSigned(r.U14());
    e.clocks = r.U7();
    e.at_kind = r.U7();
    e.at_note = r.U7();
    e.at_value = r.U7();
    e.at_age = r.U7();
    assert(r.Ok() && r.AtEnd());
    return e;
}
// Runs `ms` milliseconds of engine ticks and Service(); returns the last monitor frame.
ExtFrame RunMonitored(Device& d, unsigned ms, const std::function<void(unsigned)>& each = nullptr) {
    Reply last;
    for(unsigned i = 0; i < ms; ++i) {
        if(each)
            each(i);
        d.Ticks(1);
        d.out.count = 0;
        d.module.controller.Service(++d.now, d.out);
        if(d.out.count && Decode(d.out.frames[0], d.out.sizes[0]).frame.command == proto::MonitorFrame)
            last = Decode(d.out.frames[0], d.out.sizes[0]);
    }
    assert(last.present);
    return ParseExt(last);
}

void TestExtensions() {
    // Off by default even when compiled in: plain caps and 36-byte frames (checked above).
    Device plain;
    assert(plain.Request(proto::SetSource, Bytes({0, ParamFm, 100, 0})).frame.command == proto::kNak);

    Device d;
    d.module.EnableExtensions({true, true});
    const Reply id = d.Request(proto::Identify, {});
    assert(id.payload.back() == (0x0A | 0x30 | 0x40));
    assert(d.module.controller.CurrentPatch().format == 2);
    assert(d.module.controller.CurrentCrc() != 0x61C264C4u); // format 2 carries the fm bytes

    // Patch format 2: round trip, CRC over 111 bytes, format 1 must not carry fm.
    Patch p{};
    p.format = 2;
    p.lfo[0].fm = -37;
    p.lfo[1].fm = 100;
    assert(PatchSize(p) == kPatchBytesV2 && Deserialize(Serialize(p)).lfo[0].fm == -37);
    assert(PatchCrc(Deserialize(Serialize(p))) == PatchCrc(p) && Validate(p, ~0ull) == Status::Ok);
    p.format = 1;
    assert(Validate(p, ~0ull) == Status::Range);
    p.lfo[0].fm = p.lfo[1].fm = 0;
    assert(PatchCrc(p) == PatchCrc(Patch{}) || p.lfo[0].rate != Patch{}.lfo[0].rate);
    assert(PatchCrc(Patch{}) == 0x61C264C4u); // format 1 unchanged by the extension

    // FM parameter: value = fm + 100; Random has none; GET_SOURCE appends it.
    assert(d.Request(proto::SetSource, Bytes({0, ParamFm, 140 & 127, 140 >> 7})).frame.command == (proto::SetSource | 0x40));
    assert(d.module.controller.CurrentPatch().lfo[0].fm == 40);
    assert(d.Request(proto::SetSource, Bytes({2, ParamFm, 140 & 127, 140 >> 7})).frame.command == proto::kNak);
    assert(d.Request(proto::SetSource, Bytes({1, ParamFm, 0x49, 0x01})).frame.command == proto::kNak); // 201
    Reply src = d.Request(proto::GetSource, Bytes({0}));
    assert(src.payload.size() == 10 && (src.payload[8] | (src.payload[9] << 7)) == 140);

    // Extended monitor: base values, min/max of the window, clocks, aftertouch.
    d.Request(proto::SetCell, Bytes({Lfo1, 0, 50, 2}));          // makes the program active
    d.Request(proto::SetSource, Bytes({0, ParamFm, 100, 0}));   // fm 0 for the range check
    d.Request(proto::SetSource, Bytes({0, ParamRate, 0x7F, 0x7F})); // 20 Hz
    d.Request(proto::Monitor, Bytes({20}));
    d.module.engine.ReportBase(3, 0.75f);
    ExtFrame e = RunMonitored(d, 400);
    assert(std::fabs(e.base[3] - 0.75f) < 1e-3f && e.base[0] == -1.0f);
    // 20 Hz sampled every 50 ms always hits the same phase; the window still shows the sweep.
    assert(e.min[Lfo1] < -0.95f && e.max[Lfo1] > 0.95f);
    assert(e.min[ModWheel] == 0.0f && e.max[ModWheel] == 0.0f && e.clocks == 0 && e.at_kind == 0 && e.at_age == 127);
    // A new window starts with every frame: a slow LFO shows a narrow range again.
    d.Request(proto::SetSource, Bytes({0, ParamRate, 0, 0})); // 0.02 Hz
    e = RunMonitored(d, 300);
    assert(e.max[Lfo1] - e.min[Lfo1] < 0.05f);
    // MIDI clock running (24 ppqn at 120 BPM = one pulse per ~21 ms), then stopped.
    e = RunMonitored(d, 300, [&](unsigned i) { if(i % 21 == 0) d.Midi(0xF8, 0); });
    assert(e.clocks & 1);
    e = RunMonitored(d, 600);
    assert(!(e.clocks & 1));
    // Aftertouch: the kind follows the last message, value 0 included; Note Off clears Poly.
    d.Midi(0x90, 60, 100);
    d.Midi(0x90, 64, 100);
    d.Midi(0xA0, 60, 120); // pressure on the first of two held notes
    e = RunMonitored(d, 60);
    assert(e.at_kind == 2 && e.at_note == 60 && e.at_value == 120 && e.at_age < 5);
    assert(e.max[PolyAt] == 0.0f); // the Poly AT source follows the last note (N1): 64 has no pressure
    d.Midi(0x80, 60, 0);
    e = RunMonitored(d, 60);
    assert(e.at_kind == 2 && e.at_value == 0);
    d.Midi(0xD0, 0);
    e = RunMonitored(d, 60);
    assert(e.at_kind == 1 && e.at_value == 0);
    d.Midi(0xD0, 90);
    e = RunMonitored(d, 60);
    assert(e.at_kind == 1 && e.at_value == 90);
    d.Request(proto::Monitor, Bytes({20})); // monitoring ends 3 s after the last request
    e = RunMonitored(d, 2800);
    assert(e.at_kind == 1 && e.at_age == 127); // stays, but marked old

    // FM: LFO 2 a slow square at +1 doubles LFO 1's rate twice (+2 octaves at fm +100).
    Device f;
    f.module.EnableExtensions({false, true});
    f.Request(proto::SetCell, Bytes({Lfo1, 0, 50, 2}));
    f.Request(proto::SetSource, Bytes({0, ParamShape, SawUp, 0}));
    f.Request(proto::SetSource, Bytes({1, ParamShape, Square, 0}));
    f.Request(proto::SetSource, Bytes({1, ParamRate, 0, 0})); // 0.02 Hz: +1 for 25 s
    auto wraps = [&](unsigned ticks) {
        unsigned n = 0;
        float last = f.module.engine.Source(Lfo1);
        for(unsigned i = 0; i < ticks; ++i) {
            f.Ticks(1);
            const float now = f.module.engine.Source(Lfo1);
            n += now < last - 1.0f;
            last = now;
        }
        return n;
    };
    f.Request(proto::SetSource, Bytes({0, ParamRate, 0x00, 0x40})); // 8192: ~0.63 Hz
    f.Ticks(10);
    const unsigned without = wraps(10000);
    f.Request(proto::SetSource, Bytes({0, ParamFm, 200 & 127, 200 >> 7})); // fm +100
    assert(f.module.controller.CurrentPatch().lfo[0].fm == 100);
    f.Ticks(10);
    const unsigned with = wraps(10000);
    assert(without >= 5 && without <= 7 && with >= 4 * without - 2 && with <= 4 * without + 2);
    // Synced LFOs ignore fm (the clock sets the rate).
    f.Request(proto::SetSource, Bytes({0, ParamSync, MidiClock, 0}));
    assert(Compile(f.module.controller.CurrentPatch(), f.info, 48000.0f, {0.0f, 0.0f}).lfo[0].fm_octaves == 0.0f);
    std::printf("extensions: format 2 + FM (%u -> %u cycles in 10 s), monitor ranges, clocks, aftertouch\n",
                without, with);
}

// Instrument modes (PROTOCOL.md § 3.6): capabilities 2, GET_MODE and two monitor bytes.
void TestModes() {
    static const ModeInfo kTwo[] = {{"Alpha", "Bank A, SW18 oben"}, {"Beta", "Bank B, SW18 unten"}};
    // Without the switch, or without the extended monitor: no capabilities 2, GET_MODE unknown.
    Device off;
    off.info.modes = kTwo;
    off.info.mode_count = 2;
    off.module.EnableExtensions({true, true, false});
    assert(off.Request(proto::Identify, {}).payload.back() == (0x0A | 0x30 | 0x40));
    Reply nak = off.Request(proto::GetMode, Bytes({0}));
    assert(nak.frame.command == proto::kNak && nak.payload[1] == proto::ErrUnknown);
    Device no_monitor;
    no_monitor.info.modes = kTwo;
    no_monitor.info.mode_count = 2;
    no_monitor.module.EnableExtensions({false, false, true});
    assert(no_monitor.Request(proto::GetMode, Bytes({0})).frame.command == proto::kNak);

    Device d;
    d.info.modes = kTwo;
    d.info.mode_count = 2;
    d.module.EnableExtensions({true, true, true});
    const Reply id = d.Request(proto::Identify, {});
    assert(id.payload[id.payload.size() - 2] == (0x0A | 0x30 | 0x40) && id.payload.back() == 0x01);
    Reply m = d.Request(proto::GetMode, Bytes({1}));
    assert(m.frame.command == (proto::GetMode | 0x40));
    proto::Reader r(m.payload.data(), m.payload.size());
    char name[kMaxModeText + 1]{}, hint[kMaxModeText + 1]{};
    assert(r.U7() == 1 && r.U7() == 2);
    r.Str(name, kMaxModeText);
    r.Str(hint, kMaxModeText);
    assert(r.Ok() && r.AtEnd() && std::string(name) == "Beta" && std::string(hint) == "Bank B, SW18 unten");
    assert(d.Request(proto::GetMode, Bytes({2})).frame.command == proto::kNak);
    assert(d.Request(proto::GetMode, {}).frame.command == proto::kNak);

    // Monitor: 91 bytes, unknown until reported, then mode and origin as the audio side set them.
    d.Request(proto::Monitor, Bytes({20}));
    Reply last;
    auto run = [&](unsigned ms) {
        for(unsigned i = 0; i < ms; ++i) {
            d.Ticks(1);
            d.out.count = 0;
            d.module.controller.Service(++d.now, d.out);
            if(d.out.count && Decode(d.out.frames[0], d.out.sizes[0]).frame.command == proto::MonitorFrame)
                last = Decode(d.out.frames[0], d.out.sizes[0]);
        }
        assert(last.present && last.payload.size() == 91);
        return std::make_pair(unsigned(last.payload[89]), unsigned(last.payload[90]));
    };
    assert(run(100) == std::make_pair(unsigned(kModeUnknown), unsigned(kModeUnknown)));
    d.module.engine.ReportMode(1, kModeFromMidi);
    assert(run(100) == std::make_pair(1u, unsigned(kModeFromMidi)));
    d.module.engine.ReportMode(0, kModeFromDevice);
    assert(run(100) == std::make_pair(0u, unsigned(kModeFromDevice)));
    std::printf("modes: capabilities 2, GET_MODE, monitor bytes 89/90 (91-byte frames)\n");
}
#endif

int main() {
    TestCrcAndSerialization();
    TestValidation();
    TestShapesAndRates();
    TestLfoTickIndependence();
    TestRetriggerAndSampleHold();
    TestRandom();
    TestSmoothingAndCv();
    TestNeutral();
    TestSumClampAndSpan();
    TestProgramSwapKeepsPhase();
    TestNotesAndVoices();
    TestMidiClockSync();
    TestGateSync();
    TestProtocolCommands();
    TestFramingErrors();
    TestControllerBehavior();
    TestNoAllocation();
    TestFuzz();
#if MOD_ON_DAISY_EXTENSIONS
    TestExtensions();
    TestModes();
#endif
    std::printf("All Mod on Daisy host tests passed.\n");
}
