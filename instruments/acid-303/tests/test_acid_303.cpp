// Host tests of the Acid 303 voice (ARBEITSAUFTRAG Phase 2): golden output for saw and
// square, finite self-oscillation, peak <= 0 dBFS, DC, click criterion on parameter
// jumps, slide time, legato without a new envelope, accent, two-source note stack,
// parameter contract and CC plan, and the versioned 303 fixture for Phase 3.
#include "acid303/params.h"
#include "acid303/voice.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace acid303;

// Golden hashes of Acid 303 0.1.0. They change only on purpose, with the reason in CHANGELOG.md.
#ifndef ACID303_GOLDEN_SAW
#define ACID303_GOLDEN_SAW 0xB05F5A1Fu
#endif
#ifndef ACID303_GOLDEN_SQUARE
#define ACID303_GOLDEN_SQUARE 0x355159C8u
#endif

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static constexpr int kRate = 48000;

// The fixed phrase: 16 steps at 125 BPM (sixteenths of 6000 samples), accents and slides.
struct Note {
    uint8_t note;
    bool accent, slide, gate;
};
static const Note kPhrase[16] = {
    {36, true, false, true},  {36, false, false, true}, {48, false, true, true},  {39, false, false, true},
    {36, true, false, true},  {43, false, true, true},  {46, false, false, true}, {36, false, false, false},
    {41, true, false, true},  {41, false, true, true},  {53, true, false, true},  {36, false, false, true},
    {38, false, true, true},  {39, true, true, true},   {41, false, false, true}, {31, true, false, true},
};

// Renders `seconds` of the phrase like the sequencer drives it: 50 % gates, slides legato.
static std::vector<float> RenderPhrase(Voice& v, double seconds) {
    std::vector<float> out;
    const int step = 6000, total = int(seconds * kRate);
    uint32_t tag = 0;
    bool held = false, slide = false;
    for(int n = 0; n < total; ++n) {
        const int i = (n / step) % 16, pos = n % step;
        if(pos == 0) {
            const Note& s = kPhrase[i];
            if(held && (!slide || !s.gate)) {
                v.NoteOff(tag, Source::Sequencer);
                held = false;
            }
            if(s.gate) {
                const bool legato = held && slide;
                v.NoteOn(s.note, s.accent ? 127 : 100, s.accent, legato, Source::Sequencer, ++tag);
                held = true;
            }
            slide = s.slide && s.gate;
        }
        if(pos == step / 2 && held && !slide) {
            v.NoteOff(tag, Source::Sequencer);
            held = false;
        }
        out.push_back(v.Process());
    }
    return out;
}

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

static Voice Make(Params p = Params{}) {
    Voice v;
    v.SetParams(p);
    v.Prepare();
    return v;
}

static float Peak(const std::vector<float>& x) {
    float m = 0;
    for(float f : x)
        m = std::fabs(f) > m ? std::fabs(f) : m;
    return m;
}

static void TestExp2() {
    for(float x = -20.f; x <= 20.f; x += 0.137f) {
        const double ref = std::pow(2.0, double(x));
        CHECK(std::fabs(Exp2(x) / ref - 1.0) < 1e-6);
    }
    CHECK(std::fabs(NoteFrequency(69) - 440.f) < 1e-3f && std::fabs(NoteFrequency(81) - 880.f) < 2e-3f);
}

static void TestGolden() {
    for(int wave = 0; wave < 2; ++wave) {
        Params p;
        p.waveform = wave;
        p.resonance = 70.f;
        p.env_mod = 60.f;
        Voice v = Make(p);
        const std::vector<float> out = RenderPhrase(v, 2.0);
        const uint32_t hash = Fnv(out);
        std::printf("Acid 303 golden %s: 0x%08X\n", wave ? "square" : "saw", unsigned(hash));
        CHECK(hash == (wave ? ACID303_GOLDEN_SQUARE : ACID303_GOLDEN_SAW));
    }
}

static void TestLimits() {
    // Self-oscillation at maximum resonance stays finite and below 0 dBFS; extreme settings too.
    Params p;
    p.resonance = 100.f;
    p.accent = 100.f;
    p.volume = 100.f;
    p.env_mod = 100.f;
    for(float cutoff : {40.f, 800.f, 12000.f}) {
        for(int wave = 0; wave < 2; ++wave) {
            p.cutoff = cutoff;
            p.waveform = wave;
            Voice v = Make(p);
            const std::vector<float> out = RenderPhrase(v, 4.0);
            bool finite = true;
            for(float f : out)
                finite &= std::isfinite(f);
            CHECK(finite);
            const float peak = Peak(out);
            if(peak >= 1.f)
                std::printf("  peak %.4f at cutoff %.0f wave %d\n", peak, cutoff, wave);
            CHECK(peak < 1.f); // <= 0 dBFS before the host limiter, without the guard
        }
    }
    // The filter really self-oscillates: a held note with the oscillator silent keeps ringing.
    p = Params{};
    p.resonance = 100.f;
    p.env_mod = 0.f;
    p.cutoff = 1000.f;
    Voice v = Make(p);
    v.NoteOn(48, 100, false, false, Source::Sequencer, 1);
    for(int i = 0; i < kRate / 10; ++i)
        v.Render(i < 48 ? 0.5f : 0.f); // a short impulse, then silence
    std::vector<float> ring;
    for(int i = 0; i < kRate / 2; ++i)
        ring.push_back(v.Render(0.f));
    CHECK(Peak(std::vector<float>(ring.end() - 4800, ring.end())) > 0.01f);
    // DC: |mean| < 0.001 over 1 s of the phrase.
    for(int wave = 0; wave < 2; ++wave) {
        Params q;
        q.waveform = wave;
        Voice d = Make(q);
        const std::vector<float> out = RenderPhrase(d, 1.0);
        double sum = 0;
        for(float f : out)
            sum += f;
        CHECK(std::fabs(sum / out.size()) < 0.001);
    }
}

// Click criterion (Phase 2): 100 Hz sine at -12 dBFS through the voice path; after a jump
// the largest sample-to-sample step is at most 1.5 x the settled value.
static void TestClicks() {
    struct Jump {
        int id;
        float from, to;
    } jumps[] = {{4, 200.f, 6000.f}, {4, 6000.f, 200.f}, {5, 0.f, 90.f}, {9, 10.f, 100.f}, {9, 100.f, 0.f}, {3, -100.f, 100.f}};
    for(const Jump& j : jumps) {
        Params p;
        p.env_mod = 0.f;
        p.resonance = 20.f;
        WriteParam(p, uint16_t(j.id), int32_t(j.from * 1000));
        Voice v = Make(p);
        v.Snap();
        v.NoteOn(48, 100, false, false, Source::Sequencer, 1);
        int n = 0;
        auto sine = [&]() { return 0.25f * float(std::sin(2.0 * 3.141592653589793 * 100.0 * n++ / kRate)); };
        float before = 0.f, last = 0.f;
        for(int i = 0; i < kRate / 2; ++i) {
            const float y = v.Render(sine());
            if(i > kRate / 4)
                before = std::fabs(y - last) > before ? std::fabs(y - last) : before;
            last = y;
        }
        WriteParam(p, uint16_t(j.id), int32_t(j.to * 1000));
        v.SetParams(p);
        float prev = v.Render(sine()), jump_max = 0.f;
        for(int i = 0; i < kRate / 5; ++i) {
            const float y = v.Render(sine());
            jump_max = std::fabs(y - prev) > jump_max ? std::fabs(y - prev) : jump_max;
            prev = y;
        }
        float settled = 0.f;
        for(int i = 0; i < kRate / 5; ++i) {
            const float y = v.Render(sine());
            settled = std::fabs(y - prev) > settled ? std::fabs(y - prev) : settled;
            prev = y;
        }
        // The settled step size of the louder of the two settings is the reference.
        const float reference = settled > before ? settled : before;
        const float limit = 1.5f * (reference > 1e-4f ? reference : 1e-4f);
        if(jump_max > limit)
            std::printf("  click: param %d %.0f -> %.0f: %.5f > %.5f\n", j.id, j.from, j.to, jump_max, limit);
        CHECK(jump_max <= limit);
    }
}

static void TestSlideAccentLegato() {
    Params p;
    p.slide_ms = 60.f;
    Voice v = Make(p);
    v.NoteOn(36, 100, false, false, Source::Sequencer, 1);
    for(int i = 0; i < 1000; ++i)
        v.Process();
    const float env_before = v.FilterEnvelope();
    v.NoteOn(48, 100, false, true, Source::Sequencer, 2); // legato
    v.NoteOff(1, Source::Sequencer);
    for(int i = 0; i < kRate * 30 / 1000; ++i)
        v.Process();
    CHECK(v.Pitch() > 40.f && v.Pitch() < 44.f);              // about half way after 30 ms
    CHECK(v.FilterEnvelope() < env_before);                   // no new filter envelope
    for(int i = 0; i < kRate * 32 / 1000; ++i)
        v.Process();
    CHECK(v.Pitch() == 48.f);                                 // there after 60 ms
    // Not legato: new envelope and an immediate pitch.
    v.NoteOn(60, 100, false, false, Source::Sequencer, 3);
    CHECK(v.Pitch() == 60.f && v.FilterEnvelope() == 1.f);
    // Accent: higher peak cutoff and level, shorter decay.
    Params a;
    a.accent = 100.f;
    a.decay = 1000.f;
    Voice plain = Make(a), accented = Make(a);
    plain.NoteOn(36, 100, false, false, Source::Sequencer, 1);
    accented.NoteOn(36, 127, true, false, Source::Sequencer, 1);
    float plain_peak = 0, accent_peak = 0, plain_cut = 0, accent_cut = 0;
    for(int i = 0; i < kRate / 10; ++i) {
        plain_peak = std::fmax(plain_peak, std::fabs(plain.Process()));
        accent_peak = std::fmax(accent_peak, std::fabs(accented.Process()));
        plain_cut = std::fmax(plain_cut, plain.CutoffHz());
        accent_cut = std::fmax(accent_cut, accented.CutoffHz());
    }
    CHECK(accent_peak > plain_peak * 1.3f && accent_cut > plain_cut * 2.f);
    for(int i = 0; i < kRate / 2; ++i) {
        plain.Process();
        accented.Process();
    }
    CHECK(accented.FilterEnvelope() < plain.FilterEnvelope() * 0.1f);
}

static void TestNoteStack() {
    Params p;
    p.slide_mode = SlideMode::Off;
    Voice v = Make(p);
    // Last note wins, whatever its source.
    v.NoteOn(36, 100, false, false, Source::Sequencer, 1);
    v.NoteOn(60, 100, false, false, Source::MidiIn, 50);
    CHECK(v.SoundingNote() == 60 && v.SoundingSource() == Source::MidiIn);
    // A source's Note-Off ends only its own note; the held one of the other source returns.
    v.NoteOff(1, Source::Sequencer);
    CHECK(v.Gate() && v.SoundingNote() == 60);
    v.NoteOn(38, 100, false, false, Source::Sequencer, 2);
    v.NoteOffNote(60, Source::MidiIn);
    CHECK(v.Gate() && v.SoundingNote() == 38);
    // Panic of the sequencer removes only sequencer notes.
    v.NoteOn(62, 100, false, false, Source::MidiIn, 51);
    v.AllNotesOff(Source::Sequencer);
    CHECK(v.Gate() && v.SoundingNote() == 62 && v.Held(Source::Sequencer) == 0);
    v.AllNotesOff(Source::MidiIn);
    CHECK(!v.Gate());
    // Stale Note-Off (old tag) never ends a newer note.
    v.NoteOn(40, 100, false, false, Source::Sequencer, 10);
    v.NoteOff(9, Source::Sequencer);
    CHECK(v.Gate() && v.SoundingNote() == 40);
    // Overflow: the oldest held note of a source falls out, nothing breaks.
    for(uint32_t t = 0; t < 20; ++t)
        v.NoteOn(uint8_t(40 + t), 100, false, false, Source::MidiIn, 100 + t);
    CHECK(v.Held(Source::MidiIn) == Voice::kStack);
    // MIDI slide modes: Legato glides only when overlapping, Always always.
    Params l;
    l.slide_mode = SlideMode::Legato;
    Voice g = Make(l);
    g.NoteOn(36, 100, false, false, Source::MidiIn, 1);
    g.NoteOn(48, 100, false, false, Source::MidiIn, 2);
    CHECK(g.Pitch() > 36.f && g.Pitch() < 37.f); // glides from 36
    g.Mute();
    CHECK(!g.Gate());
}

static void TestParams() {
    Params p;
    for(uint8_t i = 0; i < Contract().count; ++i) {
        const sq::ParamDesc& d = Contract().params[i];
        int32_t v = 0;
        CHECK(ReadParam(p, d.id, v) && v == d.def);
        CHECK(WriteParam(p, d.id, d.max + 1000) && ReadParam(p, d.id, v) && v == d.max);
        CHECK(WriteParam(p, d.id, d.def));
    }
    // CC plan: cutoff CC 74 log, waveform CC 70 enum, volume CC 7 linear.
    const sq::ParamDesc* cutoff = ParamOfCc(Contract(), 74);
    CHECK(cutoff && cutoff->id == 4);
    CHECK(FromCc(*cutoff, 0) == 40000 && FromCc(*cutoff, 127) == 12000000);
    CHECK(std::abs(FromCc(*cutoff, 64) - 40000.0 * std::pow(300.0, 64.0 / 127.0)) < 2000);
    const sq::ParamDesc* wave = ParamOfCc(Contract(), 70);
    CHECK(FromCc(*wave, 63) == 0 && FromCc(*wave, 64) == 1000);
    const sq::ParamDesc* volume = ParamOfCc(Contract(), 7);
    CHECK(FromCc(*volume, 127) == 100000 && FromCc(*volume, 0) == 0);
    CHECK(ParamOfCc(Contract(), 1) == nullptr);
}

// The versioned 303 fixture (Phase 3 auto-level acceptance): 1 s of the phrase, default
// parameters, saw. It must equal the stored file bit for bit.
static std::vector<float> Fixture() {
    Voice v = Make();
    return RenderPhrase(v, 1.0);
}

static void TestFixture(const char* path, bool write) {
    const std::vector<float> f = Fixture();
    if(write) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(f.data()), std::streamsize(f.size() * sizeof(float)));
        std::printf("wrote %s\n", path);
        return;
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<float> stored(f.size());
    in.read(reinterpret_cast<char*>(stored.data()), std::streamsize(stored.size() * sizeof(float)));
    CHECK(in.gcount() == std::streamsize(f.size() * sizeof(float)));
    CHECK(std::memcmp(stored.data(), f.data(), f.size() * sizeof(float)) == 0);
}

int main(int argc, char** argv) {
    const char* fixture = "../../tests/fixtures/acid-seq/acid303_phrase.f32";
    if(argc > 1 && std::string(argv[1]) == "--write-fixture") {
        TestFixture(fixture, true);
        return 0;
    }
    TestExp2();
    TestGolden();
    TestLimits();
    TestClicks();
    TestSlideAccentLegato();
    TestNoteStack();
    TestParams();
    TestFixture(fixture, false);
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: exp2, golden saw/square, self-oscillation, peak, DC, click criterion, slide, legato, "
                "accent, two-source note stack, parameters and CC plan, 303 fixture\n");
    return 0;
}
