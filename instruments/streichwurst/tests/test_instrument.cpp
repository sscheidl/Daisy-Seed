// SPDX-License-Identifier: MIT
// Host tests of the Streichwurst instrument component alone (no FX, board or MIDI):
// clocks and pitch, band limit, envelope time constants, registration/octave transform,
// event order, sustain, retrigger, solo stealing, pending-queue limits, variable and zero
// blocks, invalid input and long renders. Signals are own test signals, not original renders.
#include "streichwurst/instrument.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace streichwurst;
static InstrumentStorage storage_a, storage_b, storage_c; // host-owned key tables

static int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if(!(cond)) {                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++failures;                                                                            \
        }                                                                                          \
    } while(0)

constexpr double kPi = 3.14159265358979323846;
constexpr float kFs = 48000.f;
constexpr std::size_t kBlock = 48;

struct Rendered {
    std::vector<float> strings, solo;
};

// Renders `blocks` blocks; events are given as (absolute sample, event).
static Rendered Render(Instrument& inst, std::size_t samples, const std::vector<std::pair<std::size_t, Event>>& events,
                       std::size_t block = kBlock) {
    Rendered r;
    r.strings.resize(samples);
    r.solo.resize(samples);
    std::size_t next = 0;
    for(std::size_t start = 0; start < samples; start += block) {
        const std::size_t n = std::min(block, samples - start);
        std::vector<Event> block_events;
        while(next < events.size() && events[next].first < start + n) {
            Event e = events[next].second;
            e.frame = uint16_t(events[next].first - start);
            block_events.push_back(e);
            ++next;
        }
        SectionBuses b{r.strings.data() + start, r.solo.data() + start, n};
        inst.RenderSections(block_events.data(), block_events.size(), b);
    }
    return r;
}

static double Goertzel(const std::vector<float>& x, std::size_t from, std::size_t n, double hz) {
    const double w = 2.0 * kPi * hz / kFs, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for(std::size_t i = 0; i < n; ++i) {
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n - 1));
        const double s = x[from + i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / double(n);
}

static double Rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double s = 0;
    for(std::size_t i = from; i < to; ++i)
        s += double(x[i]) * x[i];
    return std::sqrt(s / double(to - from));
}

static bool Finite(const std::vector<float>& x) {
    for(float v : x)
        if(!std::isfinite(v))
            return false;
    return true;
}

static Instrument& Fresh(InstrumentParams p = {}) {
    static Instrument inst;
    CHECK(inst.Prepare(kFs, kBlock, storage_a));
    inst.SetParameters(p);
    return inst;
}

static void TestPrepareAndBuses() {
    Instrument inst;
    CHECK(!inst.Prepare(0.f, kBlock, storage_c));
    CHECK(!inst.Prepare(NAN, kBlock, storage_c));
    CHECK(!inst.Prepare(kFs, 0, storage_c));
    CHECK(inst.Prepare(kFs, kBlock, storage_c));
    float a[kBlock] = {}, b[kBlock] = {};
    a[0] = 7.f;
    CHECK(!inst.RenderSections(nullptr, 0, SectionBuses{a, a, kBlock})); // aliasing rejected
    CHECK(a[0] == 7.f);                                                  // untouched
    CHECK(!inst.RenderSections(nullptr, 0, SectionBuses{a, b, kBlock + 1}));
    CHECK(!inst.RenderSections(nullptr, 0, SectionBuses{nullptr, b, kBlock}));
    CHECK(inst.Stats().rejected_blocks == 3);
    CHECK(inst.RenderSections(nullptr, 0, SectionBuses{a, b, kBlock}));
    for(std::size_t i = 0; i < kBlock; ++i)
        CHECK(a[i] == 0.f && b[i] == 0.f); // silence before any note
}

static void TestClocks() {
    Instrument& inst = Fresh();
    const StringSection& s = inst.Strings();
    for(int c = 0; c < 12; ++c) {
        const float top = float(s.TopNote(c));
        CHECK(int(top) % 12 == c && top <= 127 && top + 12 > 127);
        const float expected = 2.f * 440.f * std::exp2((top - 69.f) / 12.f);
        CHECK(std::fabs(s.ClassTickRate(c) - expected) / expected < 1e-5f);
    }
    // SF-C01 cross-check: the top clock period corresponds to 40000/f(127) at 40 kHz.
    StringSection s40;
    CHECK(s40.Prepare(40000.f));
    const float f127 = 440.f * std::exp2((127.f - 69.f) / 12.f);
    CHECK(std::fabs(s40.ClassTickRate(7) / 2.f - f127) / f127 < 1e-5f);
}

static void TestPitchAndSpectrum() {
    InstrumentParams p;
    p.string_registration = 0.5f;
    p.string_crescendo = 0.f;
    Instrument& inst = Fresh(p);
    const Rendered r = Render(inst, 24000, {{0, NoteOn(0, 69, 100)}});
    const std::size_t from = 9600, n = 8192;
    const double f0 = Goertzel(r.strings, from, n, 440.0);
    const double off = Goertzel(r.strings, from, n, 466.16);
    const double h2 = Goertzel(r.strings, from, n, 880.0);
    std::printf("  A4: 440 Hz %.5f, 466 Hz %.6f, 880 Hz %.5f\n", f0, off, h2);
    CHECK(f0 > 0.005);
    CHECK(off < f0 * 0.01);
    CHECK(h2 > f0 * 0.05); // the 4' footage adds the octave (sawtooth-like registration)
    // Octave "upper" moves every footage one octave up: the 220 Hz (16') part vanishes.
    InstrumentParams up = p;
    up.string_octave = OctaveUpper;
    Instrument& inst_up = Fresh(up);
    const Rendered u = Render(inst_up, 24000, {{0, NoteOn(0, 69, 100)}});
    const double sub_base = Goertzel(r.strings, from, n, 220.0), sub_up = Goertzel(u.strings, from, n, 220.0);
    std::printf("  220 Hz base %.5f upper %.6f\n", sub_base, sub_up);
    CHECK(sub_up < sub_base * 0.05);
    // Pitch bend +2 st (default range): 440 Hz moves to 493.9 Hz after the 5 ms glide.
    Instrument& bent = Fresh(p);
    const Rendered b = Render(bent, 24000, {{0, NoteOn(0, 69, 100)}, {0, PitchBend(0, 8191)}});
    CHECK(Goertzel(b.strings, from, n, 493.88) > 0.8 * f0);
    CHECK(Goertzel(b.strings, from, n, 440.0) < 0.05 * f0);
}

static void TestBandLimit() {
    // Highest note alone (registration with the 8' footage dominant): energy at the alias
    // of its third harmonic stays far below the fundamental.
    InstrumentParams p;
    p.string_registration = 0.f;
    p.string_crescendo = 0.f;
    Instrument& inst = Fresh(p);
    const Rendered r = Render(inst, 24000, {{0, NoteOn(0, 115, 100)}}); // 6271.9 Hz
    const std::size_t from = 9600, n = 8192;
    const double f = 6271.93, f0 = Goertzel(r.strings, from, n, f);
    const double h3 = Goertzel(r.strings, from, n, 3.0 * f);         // 18.8 kHz, in band
    const double alias5 = Goertzel(r.strings, from, n, 48000.0 - 5.0 * f); // 31.4 kHz -> 16.6 kHz
    const double alias7 = Goertzel(r.strings, from, n, 7.0 * f - 48000.0); // 43.9 kHz -> 4.1 kHz
    std::printf("  note 115: f0 %.5f, 3rd %.5f, alias of 5th %.6f, of 7th %.6f\n", f0, h3, alias5, alias7);
    CHECK(f0 > 0.0);
    CHECK(alias5 < f0 * 0.05); // ideal square: 5th = f0/5; band limiting removes most of it
    CHECK(alias7 < f0 * 0.02);
    // Clock bits above 0.45 fs are silent (bend +12 makes the top notes too high).
    InstrumentParams wide = p;
    wide.bend_range = 12;
    Instrument& w = Fresh(wide);
    Render(w, 4800, {{0, PitchBend(0, 8191)}});
    CHECK(w.Strings().FirstAudibleBit(7) >= 1); // note 127 + 12 st would be 25 kHz
    CHECK(w.Strings().FirstAudibleBit(0) == 0); // note 120 + 12 st = 16.7 kHz < 21.6 kHz
}

static void TestEnvelopeTimes() {
    // Time constants of the own curve through the SF-F02 calibration points.
    CHECK(std::fabs(EnvelopeTimeConstant(0.f) - 0.000577103f) < 1e-7f);
    CHECK(std::fabs(EnvelopeTimeConstant(64.f / 127.f) - 0.151328236f) < 1e-5f);
    CHECK(std::fabs(EnvelopeTimeConstant(1.f) - 4.368866664f) < 1e-3f);
    // Measured on the level of note 60 (8' contribution): 63.2 % of the target after tau.
    InstrumentParams p;
    p.string_registration = 0.f;
    p.string_crescendo = 0.5f;
    p.string_release = 0.5f;
    const float tau = EnvelopeTimeConstant(0.5f);
    Instrument& inst = Fresh(p);
    float buf_s[kBlock], buf_o[kBlock];
    Event on = NoteOn(0, 60, 100);
    inst.RenderSections(&on, 1, SectionBuses{buf_s, buf_o, kBlock});
    // targets update at the next control step; render until tau
    const std::size_t blocks = std::size_t(tau * kFs / kBlock);
    for(std::size_t i = 1; i < blocks; ++i)
        inst.RenderSections(nullptr, 0, SectionBuses{buf_s, buf_o, kBlock});
    const float target = inst.Strings().Target(60);
    const float ratio = inst.Strings().Level(60) / target;
    std::printf("  crescendo tau %.4f s: level/target %.3f\n", tau, ratio);
    CHECK(target > 0.f);
    CHECK(std::fabs(ratio - 0.632f) < 0.02f);
    Event off = NoteOff(0, 60);
    const float start = inst.Strings().Level(60);
    inst.RenderSections(&off, 1, SectionBuses{buf_s, buf_o, kBlock});
    for(std::size_t i = 1; i < blocks; ++i)
        inst.RenderSections(nullptr, 0, SectionBuses{buf_s, buf_o, kBlock});
    const float fall = inst.Strings().Level(60) / start;
    std::printf("  release: level after tau %.3f of start\n", fall);
    CHECK(std::fabs(fall - 0.368f) < 0.02f);
    // Release to the cleanup: the section becomes idle and the output exactly zero.
    p.string_release = 0.f;
    Instrument& quick = Fresh(p);
    Rendered r = Render(quick, 4800, {{0, NoteOn(0, 60, 100)}, {2400, NoteOff(0, 60)}});
    r = Render(quick, 48000, {});
    CHECK(quick.Strings().Idle());
    CHECK(r.strings.back() == 0.f);
}

static void TestRegistrationAndOctaves() {
    float frames[kRegistrationFrames][kFootages];
    OwnRegistrationFrames(frames);
    for(auto& f : frames) {
        float sum = 0;
        for(float w : f) {
            CHECK(w >= 0.f && w <= 1.f);
            sum += w;
        }
        CHECK(std::fabs(sum - 1.f) < 1e-5f);
    }
    // SF-F04 in real form: both octaves = 0.75 x neighbour sum, upper = shift by one.
    const float w[kFootages] = {0.1f, 0.4f, 0.2f, 0.1f, 0.1f, 0.05f, 0.03f, 0.02f};
    float o[kFootages];
    OctaveTransform(w, OctaveBase, o);
    for(int i = 0; i < kFootages; ++i)
        CHECK(o[i] == w[i]);
    OctaveTransform(w, OctaveBoth, o);
    CHECK(std::fabs(o[0] - 0.075f) < 1e-7f && std::fabs(o[1] - 0.375f) < 1e-7f);
    OctaveTransform(w, OctaveUpper, o);
    CHECK(o[0] == 0.f && o[1] == w[0] && o[7] == w[6]);
    const float big[kFootages] = {2.f, 2.f, 0, 0, 0, 0, 0, 0};
    OctaveTransform(big, OctaveBoth, o);
    CHECK(o[1] == kMaxFootageWeight); // 0.75 * 4 = 3 is bounded
    // A held note contributes at -12/0/+12... (footage offsets) with the weights.
    InstrumentParams p;
    p.string_registration = 0.3f;
    Instrument& inst = Fresh(p);
    Render(inst, 480, {{0, NoteOn(0, 60, 100)}});
    for(int i = 0; i < kFootages; ++i) {
        const int m = 60 + kFootageOffset[i];
        if(m < kNotes)
            CHECK(std::fabs(inst.Strings().Target(m) - inst.Strings().Weight(i)) < 1e-6f);
    }
    CHECK(inst.Strings().Target(61) == 0.f);
    // Animate offset moves the registration (native units): weights change.
    const float before = inst.Strings().Weight(1);
    inst.SetAnimateOffset(8000.f);
    Render(inst, 48, {});
    CHECK(inst.Strings().Weight(1) != before);
}

static void TestEventOrderAndBlocks() {
    // The same absolute events rendered with different host block sizes give identical
    // output: events split blocks sample-accurately, the control grid runs on.
    const std::vector<std::pair<std::size_t, Event>> ev = {
        {5, NoteOn(0, 60, 100)}, {5, NoteOn(0, 64, 90)}, {130, NoteOn(1, 67, 80)}, {700, NoteOff(0, 60)},
        {701, PitchBend(0, 2000)}, {1500, NoteOff(0, 64)}, {1500, NoteOn(0, 64, 70)}, {2300, NoteOff(1, 67)}};
    InstrumentParams p;
    p.split_mode = SplitSplit;
    p.split_key = 66;
    Instrument& a = Fresh(p);
    const Rendered ra = Render(a, 4096, ev, 48);
    static Instrument b;
    b.Prepare(kFs, 48, storage_b);
    b.SetParameters(p);
    const Rendered rb = Render(b, 4096, ev, 7);
    static Instrument c;
    c.Prepare(kFs, 48, storage_c);
    c.SetParameters(p);
    const Rendered rc = Render(c, 4096, ev, 1);
    bool same = true;
    for(std::size_t i = 0; i < 4096; ++i)
        same &= ra.strings[i] == rb.strings[i] && ra.strings[i] == rc.strings[i] && ra.solo[i] == rb.solo[i] &&
                ra.solo[i] == rc.solo[i];
    CHECK(same);
    CHECK(Rms(ra.solo, 200, 2000) > 0.0); // note 67 >= split key reaches the soli
    // Sample-accurate onset: nothing before the first event's frame on the solo bus.
    InstrumentParams q;
    q.split_mode = SplitSplit;
    q.split_key = 0; // everything to the soli
    Instrument& s = Fresh(q);
    const Rendered rs = Render(s, 96, {{30, NoteOn(0, 72, 127)}});
    for(int i = 0; i < 30; ++i)
        CHECK(rs.solo[std::size_t(i)] == 0.f);
    CHECK(std::fabs(rs.solo[40]) > 0.f);
    CHECK(Rms(rs.strings, 0, 96) == 0.0); // and nothing to the strings
    // Out-of-order and out-of-block frames are clamped (counted), order preserved.
    Instrument& o = Fresh();
    Event bad[3] = {NoteOn(0, 60, 100, 20), NoteOff(0, 60, 10), NoteOn(0, 62, 100, 500)};
    float bs[kBlock], bo[kBlock];
    CHECK(o.RenderSections(bad, 3, SectionBuses{bs, bo, kBlock}));
    CHECK(o.Stats().reordered_events == 2);
    CHECK(!o.StringNoteActive(60)); // the Off came after the On
    CHECK(o.StringNoteActive(62));
}

static void TestNotesSustainRetrigger() {
    Instrument& inst = Fresh();
    float s[kBlock], o[kBlock];
    auto run = [&](std::vector<Event> ev) { inst.RenderSections(ev.data(), ev.size(), SectionBuses{s, o, kBlock}); };
    run({NoteOn(0, 60, 100), NoteOn(0, 60, 100)}); // repeated key
    run({NoteOff(0, 60)});
    CHECK(inst.StringNoteActive(60)); // still held once
    run({NoteOff(0, 60)});
    CHECK(!inst.StringNoteActive(60));
    run({NoteOn(0, 61, 0)}); // velocity 0 is Note-Off (no effect when not held)
    CHECK(!inst.StringNoteActive(61));
    // Channels are tracked separately.
    run({NoteOn(0, 62, 100), NoteOn(3, 62, 100), NoteOff(0, 62)});
    CHECK(inst.StringNoteActive(62));
    run({NoteOff(3, 62)});
    CHECK(!inst.StringNoteActive(62));
    // Sustain holds after the last Note-Off and releases on pedal up (own channel only).
    run({Sustain(0, true), NoteOn(0, 64, 100), NoteOff(0, 64), NoteOn(1, 65, 100), NoteOff(1, 65)});
    CHECK(inst.StringNoteActive(64));
    CHECK(!inst.StringNoteActive(65));
    run({NoteOn(0, 64, 100), NoteOff(0, 64)}); // replayed under the pedal
    CHECK(inst.StringNoteActive(64));
    run({Sustain(0, false)});
    CHECK(!inst.StringNoteActive(64));
    // AllNotesOff releases everything; AllSoundOff silences at once.
    run({NoteOn(0, 60, 100), NoteOn(0, 67, 100), Sustain(0, true)});
    run({AllNotesOff()});
    CHECK(!inst.StringNoteActive(60) && !inst.StringNoteActive(67));
    run({Sustain(0, false)}); // All Notes Off leaves the pedal state alone
    run({NoteOn(0, 60, 100)});
    for(int i = 0; i < 100; ++i)
        run({});
    run({AllSoundOff()});
    run({});
    CHECK(inst.Strings().Idle());
    CHECK(inst.Solo().ActiveVoices() == 0);
    for(float v : s)
        CHECK(v == 0.f);
    // Transposition and split are fixed per key while it sounds.
    InstrumentParams p;
    p.transpose = 12;
    inst.SetParameters(p);
    run({NoteOn(0, 50, 100)});
    CHECK(inst.StringNoteActive(62));
    p.transpose = 0;
    inst.SetParameters(p);
    run({NoteOff(0, 50)});
    CHECK(!inst.StringNoteActive(62)); // released at the transposed note, nothing hangs
    CHECK(!inst.StringNoteActive(50));
}

static void TestSoloAllocation() {
    InstrumentParams p;
    p.split_mode = SplitSplit;
    p.split_key = 0;
    p.solo_attack = 0.f;
    p.solo_release = 0.6f;
    Instrument& inst = Fresh(p);
    float s[kBlock], o[kBlock];
    auto run = [&](std::vector<Event> ev) { inst.RenderSections(ev.data(), ev.size(), SectionBuses{s, o, kBlock}); };
    for(int i = 0; i < 8; ++i) {
        run({NoteOn(0, uint8_t(60 + i), 100)});
        CHECK(inst.Solo().Voice(i).note == 60 + i); // lowest free index
    }
    run({NoteOn(0, 63, 100)}); // retrigger: same voice
    CHECK(inst.Solo().ActiveVoices() == 8);
    CHECK(inst.Solo().Voice(3).note == 63);
    run({NoteOff(0, 65)});
    for(int i = 0; i < 20; ++i)
        run({});
    run({NoteOn(0, 80, 100)}); // steals the releasing voice first
    CHECK(inst.Solo().Voice(5).note == 80);
    int lowest = 0; // all gated: the lowest level (ties: oldest start) is stolen
    for(int i = 1; i < 8; ++i) {
        const SoloVoice &v = inst.Solo().Voice(i), &b = inst.Solo().Voice(lowest);
        if(v.env < b.env || (v.env == b.env && v.start < b.start))
            lowest = i;
    }
    const float level = inst.Solo().Voice(lowest).env;
    run({NoteOn(0, 81, 100)});
    CHECK(inst.Solo().Voice(lowest).note == 81);
    // Steal keeps the envelope level continuous (no reset to zero).
    CHECK(inst.Solo().Voice(lowest).env >= level * 0.99f);
}

static void TestPendingQueue() {
    Instrument& inst = Fresh();
    float s[kBlock], o[kBlock];
    // Zero-frame block: events wait for the next block; no phase advances.
    Event on = NoteOn(0, 60, 100);
    CHECK(inst.RenderSections(&on, 1, SectionBuses{s, o, 0}));
    CHECK(inst.Pending() == 1);
    CHECK(!inst.StringNoteActive(60));
    CHECK(inst.RenderSections(nullptr, 0, SectionBuses{s, o, kBlock}));
    CHECK(inst.StringNoteActive(60));
    CHECK(inst.Pending() == 0);
    // Overflow: Note-Ons beyond the queue are dropped, a lost release forces All Off.
    std::vector<Event> many;
    for(int i = 0; i < 70; ++i)
        many.push_back(NoteOn(0, uint8_t(i), 100));
    inst.RenderSections(many.data(), many.size(), SectionBuses{s, o, 0});
    CHECK(inst.Stats().dropped_note_ons == 6);
    Event off = NoteOff(0, 3);
    inst.RenderSections(&off, 1, SectionBuses{s, o, 0});
    CHECK(inst.Stats().forced_all_off == 1);
    inst.RenderSections(nullptr, 0, SectionBuses{s, o, kBlock});
    // All Off ran before the 64 queued Note-Ons, which sound; note 3 included.
    CHECK(inst.StringNoteActive(3) && inst.StringNoteActive(63) && !inst.StringNoteActive(64));
    Event all = AllNotesOff();
    inst.RenderSections(&all, 1, SectionBuses{s, o, kBlock});
    for(int m = 0; m < kNotes; ++m)
        CHECK(!inst.StringNoteActive(m));
    // Invalid events are counted and ignored.
    Event bad = NoteOn(16, 60, 100);
    Event bad2 = NoteOn(0, 128, 100);
    inst.RenderSections(&bad, 1, SectionBuses{s, o, kBlock});
    inst.RenderSections(&bad2, 1, SectionBuses{s, o, kBlock});
    CHECK(inst.Stats().invalid_events == 2);
}

static void TestInvalidParamsAndLongRender() {
    InstrumentParams p;
    p.string_registration = NAN;
    p.solo_tone = INFINITY;
    p.string_crescendo = -3.f;
    p.split_key = 400;
    Instrument& inst = Fresh(p);
    CHECK(inst.Parameters().string_registration == 0.5f); // previous valid value kept
    CHECK(inst.Parameters().solo_tone == 0.5f);
    CHECK(inst.Parameters().string_crescendo == 0.f);
    CHECK(inst.Parameters().split_key == 127);
    inst.SetAnimateOffset(NAN);
    inst.SetPitchModulation(INFINITY);
    // Long dense render with parameter sweeps: finite and bounded.
    InstrumentParams q;
    std::vector<std::pair<std::size_t, Event>> ev;
    for(int i = 0; i < 24; ++i)
        ev.push_back({std::size_t(i * 37), NoteOn(uint8_t(i % 3), uint8_t(36 + i * 2), uint8_t(40 + i * 3))});
    Instrument& d = Fresh(q);
    float peak = 0;
    bool finite = true;
    for(int seg = 0; seg < 40; ++seg) {
        q.string_registration = float(seg % 10) / 9.f;
        q.string_octave = seg % 3;
        q.solo_tone = float(seg % 7) / 6.f;
        q.solo_tremolo_depth = 1.f;
        d.SetParameters(q);
        const Rendered r = Render(d, 4800, seg == 0 ? ev : std::vector<std::pair<std::size_t, Event>>{});
        finite &= Finite(r.strings) && Finite(r.solo);
        for(std::size_t i = 0; i < r.strings.size(); ++i)
            peak = std::max(peak, std::max(std::fabs(r.strings[i]), std::fabs(r.solo[i])));
    }
    std::printf("  dense 24-key chord, octave both, 40 x 0.1 s: peak %.3f\n", peak);
    CHECK(finite);
    CHECK(peak < 8.f); // extreme case; the product's gain plan and limiter follow
}

int main() {
    TestPrepareAndBuses();
    TestClocks();
    TestPitchAndSpectrum();
    TestBandLimit();
    TestEnvelopeTimes();
    TestRegistrationAndOctaves();
    TestEventOrderAndBlocks();
    TestNotesSustainRetrigger();
    TestSoloAllocation();
    TestPendingQueue();
    TestInvalidParamsAndLongRender();
    if(failures) {
        std::printf("streichwurst instrument: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("streichwurst instrument: all tests passed\n");
    return 0;
}
