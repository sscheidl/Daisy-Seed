// Host tests of the Acid Seq product chain (ARBEITSAUFTRAG Phase 6, 4.10): the end-to-end
// golden (pattern -> sequencer -> voice -> Drive -> Delay -> limiter), voice ownership
// between sequencer and MIDI (R16), the tempo hand-over to the delay, the soft limiter, and
// switching both effects off down to the exact dry voice. The component goldens are pinned
// by the components' own tests and stay unchanged.
#include "acid_seq/chain.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

// Golden hash of Acid Seq 0.1.0 (4 s, both channels). It changes only on purpose, with the
// reason in CHANGELOG.md; a component change that moves it also moves that component's golden.
constexpr uint32_t kGolden = 0xBC3D9C4Eu;

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static uint32_t Fnv(const std::vector<float>& x) {
    uint32_t h = 2166136261u;
    for(float f : x) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        for(int i = 0; i < 4; ++i) {
            h ^= (bits >> (8 * i)) & 0xFF;
            h *= 16777619u;
        }
    }
    return h;
}

// Two delay memories (4.6 MB each) for chains that run side by side.
static ::seed::fx::StereoDelay::Storage memory[2];

static step_seq::Pattern AcidPattern() {
    static const uint8_t notes[16] = {24, 24, 36, 24, 27, 24, 31, 34, 24, 36, 24, 22, 27, 29, 31, 24};
    step_seq::Pattern p;
    for(int i = 0; i < 16; ++i) {
        step_seq::Step& s = p[std::size_t(i)];
        s.note = notes[i];
        s.gate = i != 5 && i != 11;
        s.accent = i % 4 == 2;
        s.slide = i == 3 || i == 7 || i == 12;
        s.gate_percent = uint8_t(i % 3 == 0 ? 80 : 50);
    }
    return p;
}

struct Rig {
    step_seq::Engine engine;
    acid_seq::Chain chain;
    std::vector<float> out; // interleaved L/R

    explicit Rig(int slot, bool drive = true, bool delay = true) {
        engine.Init();
        engine.pattern = AcidPattern();
        chain.voice_params.cutoff = 600.f;
        chain.voice_params.resonance = 70.f;
        chain.voice_params.env_mod = 60.f;
        chain.voice_params.decay = 250.f;
        chain.voice_params.accent = 60.f;
        chain.drive_params.enabled = drive;
        chain.drive_params.drive = 40.f;
        chain.delay_params.enabled = delay;
        CHECK(chain.Prepare(memory[slot]));
    }
    // One 1 ms block exactly as the firmware callback runs it.
    void Block(bool run = true) {
        step_seq::TickInput in;
        in.run_switch = run;
        static step_seq::TickOutput result;
        engine.Tick(in, result);
        chain.Events(result);
        chain.SetTempo(engine.CurrentBpm());
        float l[48], r[48];
        chain.Process(l, r, 48);
        for(int i = 0; i < 48; ++i) {
            out.push_back(l[i]);
            out.push_back(r[i]);
        }
    }
};

static float Peak(const std::vector<float>& x, std::size_t from = 0) {
    float peak = 0.f;
    for(std::size_t i = from; i < x.size(); ++i)
        peak = std::fabs(x[i]) > peak ? std::fabs(x[i]) : peak;
    return peak;
}

static void TestGolden() {
    uint32_t hashes[2] = {};
    for(int run = 0; run < 2; ++run) {
        auto rig = std::make_unique<Rig>(0);
        for(int b = 0; b < 4000; ++b)
            rig->Block();
        bool finite = true;
        for(float f : rig->out)
            finite &= std::isfinite(f);
        CHECK(finite);
        CHECK(Peak(rig->out) > 0.05f && Peak(rig->out) <= 1.f);
        // The delay is audible: right channel carries the ping-pong echoes.
        float right = 0.f;
        for(std::size_t i = 1; i < rig->out.size(); i += 2)
            right += std::fabs(rig->out[i] - rig->out[i - 1]);
        CHECK(right > 1.f);
        hashes[run] = Fnv(rig->out);
    }
    std::printf("Acid Seq golden: 0x%08X\n", unsigned(hashes[0]));
    CHECK(hashes[0] == hashes[1]);
    CHECK(hashes[0] == kGolden);
}

static step_seq::TickOutput Event(step_seq::OutEvent::Kind kind, uint8_t note, uint32_t tag, uint32_t prev = 0) {
    step_seq::TickOutput out;
    step_seq::OutEvent e;
    e.kind = kind;
    e.note = note;
    e.velocity = 100;
    e.tag = tag;
    e.prev_tag = prev;
    out.Push(e);
    return out;
}

// R16: last note wins; each source ends only its own notes; the held note of either source
// takes over; a sequencer panic removes only sequencer notes.
static void TestVoiceOwnership() {
    using K = step_seq::OutEvent::Kind;
    auto rig = std::make_unique<Rig>(0);
    acid_seq::Chain& c = rig->chain;
    float l[48], r[48];
    c.Events(Event(K::NoteOn, 48, 1));
    c.Process(l, r, 48);
    CHECK(c.GetVoice().Gate() && c.GetVoice().SoundingNote() == 48);
    c.MidiNoteOn(60, 100, 1u << 24);
    c.Process(l, r, 48);
    CHECK(c.GetVoice().SoundingNote() == 60);
    c.Events(Event(K::NoteOff, 48, 1)); // the sequencer ends its own note: MIDI keeps sounding
    c.Process(l, r, 48);
    CHECK(c.GetVoice().Gate() && c.GetVoice().SoundingNote() == 60);
    c.Events(Event(K::NoteOn, 50, 2));
    c.Process(l, r, 48);
    CHECK(c.GetVoice().SoundingNote() == 50);
    c.Events(Event(K::Panic, 0, 0)); // sequencer panic: the held MIDI note takes over
    c.Process(l, r, 48);
    CHECK(c.GetVoice().Gate() && c.GetVoice().SoundingNote() == 60);
    c.Events(Event(K::NoteOn, 52, 3));
    c.MidiNoteOff(60); // MIDI ends its note while the sequencer's sounds
    c.Process(l, r, 48);
    CHECK(c.GetVoice().Gate() && c.GetVoice().SoundingNote() == 52);
    c.Events(Event(K::Legato, 55, 4, 3)); // slide: the new note, the old tag ends
    c.Process(l, r, 48);
    CHECK(c.GetVoice().Gate() && c.GetVoice().SoundingNote() == 55);
    c.MidiNoteOn(62, 100, (1u << 24) + 1);
    c.MidiAllOff(); // MIDI All Notes Off: the sequencer's note takes over again
    c.Process(l, r, 48);
    CHECK(c.GetVoice().Gate() && c.GetVoice().SoundingNote() == 55);
    c.Mute(); // bootloader: everything
    c.Process(l, r, 48);
    CHECK(!c.GetVoice().Gate());
}

static void TestTempoHandOver() {
    auto rig = std::make_unique<Rig>(0);
    for(float bpm : {30.f, 120.f, 140.f, 300.f}) {
        rig->engine.settings.tempo = bpm;
        rig->Block();
        rig->Block();
        CHECK(std::fabs(rig->engine.CurrentBpm() - bpm) < 0.01f);
        CHECK(std::fabs(rig->chain.GetDelay().Tempo() - 48000.f * 60.f / bpm) < 0.5f);
    }
}

static void TestLimiter() {
    CHECK(acid_seq::SoftLimit(0.5f) == 0.5f && acid_seq::SoftLimit(-0.8f) == -0.8f);
    float last = 0.f;
    bool monotonic = true;
    for(float x = 0.f; x < 50.f; x += 0.01f) {
        const float y = acid_seq::SoftLimit(x);
        monotonic &= y >= last && y < 1.f && acid_seq::SoftLimit(-x) == -y;
        last = y;
    }
    CHECK(monotonic);
    CHECK(std::fabs(acid_seq::SoftLimit(1.f) - 0.9f) < 1e-6f);
}

// Both effects off: after the drive fade and the delay tail the output is exactly the
// limited voice of a chain that never had effects.
static void TestEffectsOff() {
    auto with = std::make_unique<Rig>(0, true, true);
    auto dry = std::make_unique<Rig>(1, false, false);
    for(int b = 0; b < 2000; ++b) {
        with->Block();
        dry->Block();
    }
    CHECK(Peak(with->out) > 0.05f);
    with->chain.drive_params.enabled = false;
    with->chain.delay_params.enabled = false;
    int blocks = 0;
    while(with->chain.GetDelayHost().GetState() != ::seed::fx::DelayHost::State::Off && blocks < 25000) {
        with->Block();
        dry->Block();
        ++blocks;
    }
    std::printf("Delay tail after switching off: %d ms\n", blocks);
    CHECK(blocks > 500 && blocks < 21000);
    const std::size_t from = with->out.size();
    for(int b = 0; b < 1000; ++b) {
        with->Block();
        dry->Block();
    }
    bool exact = true;
    for(std::size_t i = from + 2 * 48 * 20; i < with->out.size(); ++i) // after the 10 ms mix fade
        exact &= with->out[i] == dry->out[i];
    CHECK(exact);
    CHECK(Peak(dry->out, from) > 0.05f);
}

int main() {
    TestLimiter();
    TestVoiceOwnership();
    TestTempoHandOver();
    TestEffectsOff();
    TestGolden();
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: end-to-end golden (pattern -> voice -> Drive -> Delay -> limiter), voice ownership "
                "sequencer/MIDI, tempo hand-over to the delay, soft limiter, effects off = exact dry voice\n");
    return 0;
}
