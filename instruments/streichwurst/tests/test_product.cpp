// SPDX-License-Identifier: MIT
// Host tests of the Streichwurst product layer: the parameter register (wire values,
// defaults, read-only, CC plan), the audio graph (soli bypass the string FX, the reverb
// sees the later mix, the ensemble gain word acts after solo/reverb, balance shape, empty
// matrix, panic ramp, headroom), the exchangeable global FX slot (reverb -> stereo delay
// without touching instrument or string FX), the kit controls (pages, soft takeover,
// S9/S16 and the bootloader chord, keys, presets, LEDs) and an own sound fingerprint.
#include "controls.h"
#include "product.h"
#include "stereo_delay/delay.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace streichwurst;
using namespace streichwurst::product;

static int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if(!(cond)) {                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++failures;                                                                            \
        }                                                                                          \
    } while(0)

constexpr float kFs = 48000.f;
constexpr std::size_t kBlock = 48;
static InstrumentStorage keys;
static streichwurst_fx::FxStorage fx_storage;

struct Out {
    std::vector<float> l, r;
};

template <class P>
static Out Run(P& p, std::size_t blocks, std::vector<std::pair<std::size_t, Event>> events = {}) {
    Out o;
    o.l.resize(blocks * kBlock);
    o.r.resize(blocks * kBlock);
    std::size_t next = 0;
    for(std::size_t b = 0; b < blocks; ++b) {
        std::vector<Event> ev;
        while(next < events.size() && events[next].first == b)
            ev.push_back(events[next++].second);
        CHECK(p.Render(ev.data(), ev.size(), o.l.data() + b * kBlock, o.r.data() + b * kBlock, kBlock));
    }
    return o;
}
static double Rms(const std::vector<float>& x, std::size_t a, std::size_t b) {
    double s = 0;
    for(std::size_t i = a; i < b; ++i)
        s += double(x[i]) * x[i];
    return std::sqrt(s / double(b - a));
}
static double MaxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double m = 0;
    for(std::size_t i = 0; i < a.size(); ++i)
        m = std::max(m, double(std::fabs(a[i] - b[i])));
    return m;
}

static Product<>& Fresh(const State& s) {
    static Product<> p;
    CHECK(p.Prepare(kFs, kBlock, keys, fx_storage));
    p.Update(s, nullptr);
    p.Reset();
    return p;
}
static State SoloOnly(State s = {}) {
    s.inst.split_mode = SplitSplit;
    s.inst.split_key = 0;
    return s;
}

static void TestRegister() {
    // Defaults of the typed parameters equal the contract defaults.
    const State s;
    for(uint8_t c = 0; c < kComponents; ++c)
        for(uint8_t i = 0; i < Table(c).count; ++i) {
            const sq::ParamDesc& d = Table(c).params[i];
            int32_t v = 0;
            CHECK(ReadParam(s, c, d.id, v));
            if(v != d.def)
                std::printf("  default mismatch %s: %d != %d\n", d.key, v, d.def);
            CHECK(v == d.def);
            // Round trip over min, max, middle (rasterized for whole-number types).
            for(int32_t w : {d.min, d.max, d.min + (d.max - d.min) / 2}) {
                State t;
                const bool ro = d.flags & sq::FlagReadonly;
                CHECK(WriteParam(t, c, d.id, w) == !ro);
                CHECK(WriteParam(t, c, d.id, w, true));
                int32_t back = 0;
                ReadParam(t, c, d.id, back);
                CHECK(back == sq::Constrain(d, w));
            }
            // Out of range is clamped.
            State t;
            WriteParam(t, c, d.id, d.max + 50000, true);
            int32_t back = 0;
            ReadParam(t, c, d.id, back);
            CHECK(back == d.max);
        }
    State t;
    CHECK(!WriteParam(t, kInstrument, 99, 0));
    CHECK(!WriteParam(t, 7, 1, 0));
    CHECK(!WriteParam(t, kProduct, id::PotPage, 1000)); // read only for the protocol
    // CC plan: every CC of the tables is found, enums/bools follow the shared rule.
    for(uint8_t c = 0; c < kComponents; ++c)
        for(uint8_t i = 0; i < Table(c).count; ++i)
            if(Table(c).params[i].cc != sq::kNoCc) {
                const CcTarget hit = ParamOfCc(Table(c).params[i].cc);
                CHECK(hit.desc == &Table(c).params[i] && hit.component == c);
            }
    CHECK(!ParamOfCc(1).desc && !ParamOfCc(64).desc && !ParamOfCc(123).desc); // MD, sustain, mode
    const sq::ParamDesc* mode = sq::FindParam(Table(kFx), id::EnsembleMode);
    CHECK(FromCc(*mode, 0) == 0 && FromCc(*mode, 42) == 0 && FromCc(*mode, 43) == 1000 && FromCc(*mode, 127) == 2000);
    const sq::ParamDesc* on = sq::FindParam(Table(kFx), id::EnsembleOn);
    CHECK(FromCc(*on, 63) == 0 && FromCc(*on, 64) == 1000);
    const sq::ParamDesc* rate = sq::FindParam(Table(kFx), id::PhaserRate);
    CHECK(FromCc(*rate, 0) == rate->min && FromCc(*rate, 127) == rate->max);
    CHECK(std::fabs(Normalized(*rate, FromCc(*rate, 64)) - 64.f / 127.f) < 0.01f); // log scale
    // Presets are own defaults; preset 0 equals the contract defaults.
    const State p0 = Preset(0);
    CHECK(p0.inst.string_registration == s.inst.string_registration && p0.preset == 0);
    CHECK(Preset(3).inst.split_mode == SplitSplit && Preset(3).preset == 3);
}

static void TestBalanceCurve() {
    // SF-F07 shape: r = 64 -> both at factor 1; r = 127 -> exactly 2 (strings off).
    CHECK(Product<>::BalanceCurve(0.f) == 0.f);
    CHECK(std::fabs(Product<>::BalanceCurve(64.f / 127.f) - 1.f) < 1e-5f);
    CHECK(std::fabs(Product<>::BalanceCurve(1.f) - 2.f) < 1e-6f);
    CHECK(std::fabs(Product<>::BalanceCurve(32.f / 127.f) - 0.5f) < 1e-5f);
}

static void TestRouting() {
    // Soli bypass the string FX: with ensemble and phaser on, a solo-only patch stays
    // mono (L == R) when the reverb is off; strings alone are stereo.
    State solo = SoloOnly();
    solo.fx.reverb_wet = 0.f;
    solo.fx.phaser_amount = 1.f;
    Product<>& p = Fresh(solo);
    Out o = Run(p, 400, {{0, NoteOn(0, 72, 100)}});
    CHECK(Rms(o.l, 4800, o.l.size()) > 1e-3);
    CHECK(MaxDiff(o.l, o.r) == 0.0);
    State strings;
    strings.fx.reverb_wet = 0.f;
    Product<>& q = Fresh(strings);
    Out so = Run(q, 400, {{0, NoteOn(0, 60, 100)}});
    CHECK(MaxDiff(so.l, so.r) > 1e-3);
    // The ensemble gain word acts on the soli too (after the mix): mode 1 -> 0.625.
    State off = SoloOnly();
    off.fx.reverb_wet = 0.f;
    off.fx.ensemble_on = false;
    Product<>& a = Fresh(off);
    const Out oa = Run(a, 400, {{0, NoteOn(0, 72, 100)}});
    State mode1 = off;
    mode1.fx.ensemble_on = true;
    Product<>& b = Fresh(mode1);
    const Out ob = Run(b, 400, {{0, NoteOn(0, 72, 100)}});
    const double ratio = Rms(ob.l, 9600, ob.l.size()) / Rms(oa.l, 9600, oa.l.size());
    std::printf("  solo with ensemble mode 1 / bypass: %.4f (gain word 0.625)\n", ratio);
    CHECK(std::fabs(ratio - 0.625) < 0.002);
    // The reverb processes the later mix: a solo note leaves a tail after its release.
    State rev = SoloOnly();
    rev.fx.reverb_wet = 1.f;
    rev.fx.reverb_decay = 1.f;
    rev.inst.solo_release = 0.f;
    Product<>& c = Fresh(rev);
    const Out oc = Run(c, 600, {{0, NoteOn(0, 72, 100)}, {200, NoteOff(0, 72)}});
    State dry = rev;
    dry.fx.reverb_wet = 0.f;
    Product<>& d = Fresh(dry);
    const Out od = Run(d, 600, {{0, NoteOn(0, 72, 100)}, {200, NoteOff(0, 72)}});
    const double tail_wet = Rms(oc.l, 300 * kBlock, 400 * kBlock), tail_dry = Rms(od.l, 300 * kBlock, 400 * kBlock);
    std::printf("  solo tail 100-200 ms after release: wet %.5f dry %.7f\n", tail_wet, tail_dry);
    CHECK(tail_wet > 100.0 * tail_dry + 1e-6);
}

static void TestBalanceMix() {
    // Layer: strings and soli of the same key; balance 0 = strings only, 1 = soli only.
    auto level = [](float balance, bool solo_part) {
        State s;
        s.fx.reverb_wet = 0.f;
        s.prod.balance = balance;
        if(solo_part)
            s = SoloOnly(s);
        else {
            s.inst.split_mode = SplitSplit;
            s.inst.split_key = 127; // everything below: strings
        }
        Product<>& p = Fresh(s);
        const Out o = Run(p, 300, {{0, NoteOn(0, 60, 100)}});
        return Rms(o.l, 4800, o.l.size());
    };
    CHECK(level(0.f, true) == 0.0);
    CHECK(level(1.f, false) < 1e-9);
    const double mid_s = level(64.f / 127.f, false), mid_o = level(64.f / 127.f, true);
    const double full_s = level(0.f, false), full_o = level(1.f, true);
    std::printf("  balance mid: strings %.4f (full %.4f), soli %.4f (full %.4f)\n", mid_s, full_s, mid_o, full_o);
    CHECK(std::fabs(mid_s / full_s - 1.0) < 0.01); // both sections at factor 1, no -3 dB
    CHECK(std::fabs(mid_o / full_o - 1.0) < 0.01);
}

static void TestMatrixAndModulation() {
    // An empty matrix (all offsets zero) is bit-identical to no matrix.
    State s;
    s.fx.phaser_amount = 0.3f;
    s.fx.animate_amount = 0.4f;
    const float zero[kMatrixTargets] = {};
    Product<>& a = Fresh(s);
    const Out oa = Run(a, 200, {{0, NoteOn(0, 60, 100)}, {0, NoteOn(0, 67, 90)}});
    Product<>& b = Fresh(s);
    b.Update(s, zero);
    const Out ob = Run(b, 200, {{0, NoteOn(0, 60, 100)}, {0, NoteOn(0, 67, 90)}});
    CHECK(MaxDiff(oa.l, ob.l) == 0.0 && MaxDiff(oa.r, ob.r) == 0.0);
    // A registration offset changes the sound; the register state stays unmodulated.
    float offsets[kMatrixTargets] = {};
    offsets[TargetRegistration] = 0.4f;
    Product<>& c = Fresh(s);
    c.Update(s, offsets);
    const Out oc = Run(c, 200, {{0, NoteOn(0, 60, 100)}, {0, NoteOn(0, 67, 90)}});
    CHECK(MaxDiff(oa.l, oc.l) > 1e-3);
    CHECK(c.GetState().inst.string_registration == s.inst.string_registration);
    CHECK(std::fabs(c.GetInstrument().Parameters().string_registration - 0.9f) < 1e-6f);
    // Pitch target: +0.5 offset = +1 semitone; NaN offsets are ignored.
    offsets[TargetPitch] = NAN;
    c.Update(s, offsets);
    const Out od = Run(c, 10);
    for(float v : od.l)
        CHECK(std::isfinite(v));
}

static void TestPanic() {
    State s;
    Product<>& p = Fresh(s);
    Out o = Run(p, 200, {{0, NoteOn(0, 60, 100)}, {0, NoteOn(0, 72, 100)}});
    CHECK(Rms(o.l, 4800, o.l.size()) > 1e-3);
    p.Panic();
    o = Run(p, 3);
    CHECK(p.Stats().panics == 1);
    // ramp: decreasing over 2 ms (96 samples), then silent after the clear
    CHECK(std::fabs(o.l[95]) < 0.05f);
    o = Run(p, 50);
    CHECK(!p.Muting());
    CHECK(Rms(o.l, 1000, o.l.size()) < 1e-6);
    CHECK(p.GetInstrument().Strings().Idle());
    // and plays again afterwards
    o = Run(p, 200, {{0, NoteOn(0, 64, 100)}});
    CHECK(Rms(o.l, 4800, o.l.size()) > 1e-3);
    // Output protection: |y| < 1, continuous at the knee, NaN -> 0 (counted).
    CHECK(p.Protect(0.5f) == 0.5f && p.Protect(0.8f) == 0.8f);
    CHECK(p.Protect(10.f) < 1.f && p.Protect(-10.f) > -1.f);
    CHECK(std::fabs(p.Protect(0.8001f) - 0.8001f) < 1e-5f);
    const uint32_t before = p.Stats().non_finite;
    CHECK(p.Protect(NAN) == 0.f && p.Protect(INFINITY) == 0.f);
    CHECK(p.Stats().non_finite == before + 2);
}

static void TestHeadroom() {
    // A six-note chord in mode 1 with reverb at default master stays below the limiter knee.
    State s;
    s.fx.reverb_wet = 0.4f;
    Product<>& p = Fresh(s);
    const Out o = Run(p, 1000, {{0, NoteOn(0, 48, 100)}, {0, NoteOn(0, 55, 100)}, {0, NoteOn(0, 60, 100)},
                                {0, NoteOn(0, 64, 100)}, {0, NoteOn(0, 67, 100)}, {0, NoteOn(0, 72, 100)}});
    float peak = 0;
    for(std::size_t i = 0; i < o.l.size(); ++i)
        peak = std::max(peak, std::max(std::fabs(o.l[i]), std::fabs(o.r[i])));
    std::printf("  six-note chord, layer, mode 1, reverb 40 %%: peak %.3f, RMS %.3f\n", peak, Rms(o.l, 24000, o.l.size()));
    CHECK(peak < 0.8f);
    CHECK(Rms(o.l, 24000, o.l.size()) > 0.03);
}

// Global slot exchange: the stereo delay (FX contract 1.0) instead of the reverb. Only the
// product composition changes; instrument and FX string stage are the same code.
static void TestSlotExchange() {
    static seed::fx::StereoDelay::Storage delay_memory;
    static Product<ContractSlot<seed::fx::StereoDelay>> p;
    p.slot.storage = &delay_memory;
    p.slot.wet = 0.5f;
    CHECK(p.Prepare(kFs, kBlock, keys, fx_storage));
    State s = SoloOnly();
    p.Update(s, nullptr);
    p.Reset();
    const Out o = Run(p, 1500, {{0, NoteOn(0, 72, 100)}, {50, NoteOff(0, 72)}});
    // The delay repeats the short note: energy long after the solo release.
    const double late = Rms(o.l, 600 * kBlock, 1500 * kBlock);
    std::printf("  stereo delay in the global slot: echo RMS %.5f\n", late);
    CHECK(late > 1e-4);
    // Without storage the slot refuses to prepare.
    static Product<ContractSlot<seed::fx::StereoDelay>> bad;
    CHECK(!bad.Prepare(kFs, kBlock, keys, fx_storage));
}

static ControlFrame Frame(uint32_t ms, uint16_t keys_held, float pot0 = 0.5f) {
    ControlFrame f;
    for(float& p : f.pots)
        p = 0.5f;
    f.pots[0] = pot0;
    f.keys = keys_held;
    f.ms = ms;
    return f;
}

static void TestControls() {
    Controls c;
    State s;
    uint32_t ms = 0;
    c.Process(Frame(ms++, 0, 0.1f), s); // power-up: all pots latched
    CHECK(c.Latched(0));
    // Soft takeover: VR1 (registration 0.5) moves from 0.1 to 0.3 -> no change.
    c.Process(Frame(ms++, 0, 0.3f), s);
    CHECK(s.inst.string_registration == 0.5f);
    // crosses 0.5 -> picked up, then follows
    c.Process(Frame(ms++, 0, 0.55f), s);
    CHECK(!c.Latched(0));
    c.Process(Frame(ms++, 0, 0.7f), s);
    CHECK(std::fabs(s.inst.string_registration - 0.7f) < 1e-3f);
    // Small noise below the hysteresis does not write.
    c.Process(Frame(ms++, 0, 0.702f), s);
    CHECK(std::fabs(s.inst.string_registration - 0.7f) < 1e-3f);
    // A MIDI/app change latches the pot again.
    WriteParam(s, kInstrument, id::Registration, 20000);
    c.Latch(kInstrument, id::Registration);
    c.Process(Frame(ms++, 0, 0.8f), s);
    CHECK(std::fabs(s.inst.string_registration - 0.2f) < 1e-6f);
    // S9 changes the page on release; nothing jumps on the new page.
    constexpr uint16_t S9 = 1u << 8, S16 = 1u << 15;
    c.Process(Frame(ms++, S9, 0.8f), s);
    CHECK(c.Page() == 0);
    ControlOutput out = c.Process(Frame(ms++, 0, 0.8f), s);
    CHECK(c.Page() == 1 && s.pot_page == 1 && out.state_changed);
    const float tone = s.inst.solo_tone;
    c.Process(Frame(ms++, 0, 0.9f), s); // VR1 = solo tone now, latched
    CHECK(s.inst.solo_tone == tone);
    // LEDs: S10 (page 1) lit, S9 off.
    CHECK(((c.LedFrame() >> (2 * 9)) & 3u) != 0 && ((c.LedFrame() >> (2 * 8)) & 3u) == 0);
    // S16 alone: panic on release.
    c.Process(Frame(ms++, S16), s);
    out = c.Process(Frame(ms++, 0), s);
    CHECK(out.panic);
    // S9+S16 chord released early: neither a page change nor a panic.
    const int page = c.Page();
    c.Process(Frame(ms++, S9), s);
    c.Process(Frame(ms++, S9 | S16), s);
    for(int i = 0; i < 1000; ++i)
        c.Process(Frame(ms++, S9 | S16), s);
    out = c.Process(Frame(ms++, S16), s);
    CHECK(!out.panic && !out.bootloader);
    out = c.Process(Frame(ms++, 0), s);
    CHECK(!out.panic && c.Page() == page);
    // Held for 5 s: the bootloader request, still no page change or panic afterwards.
    bool boot = false;
    c.Process(Frame(ms++, S16), s);
    for(int i = 0; i < 5100; ++i)
        boot |= c.Process(Frame(ms++, S9 | S16), s).bootloader;
    CHECK(boot);
    out = c.Process(Frame(ms++, S9), s);
    out = c.Process(Frame(ms++, 0), s);
    CHECK(!out.panic && c.Page() == page);
    // Another key during the chord aborts the gesture.
    boot = false;
    for(int i = 0; i < 2000; ++i)
        boot |= c.Process(Frame(ms++, S9 | S16), s).bootloader;
    for(int i = 0; i < 4000; ++i)
        boot |= c.Process(Frame(ms++, S9 | S16 | 1u), s).bootloader;
    CHECK(!boot);
    c.Process(Frame(ms++, 0), s);
    // Local keys S1..S8 play C major from C4 on the kit channel.
    s.prod.midi_channel = 3;
    out = c.Process(Frame(ms++, 1u | 4u), s);
    CHECK(out.event_count == 2 && out.events[0].type == EventType::NoteOn && out.events[0].note == 60 &&
          out.events[1].note == 64 && out.events[0].channel == 2);
    out = c.Process(Frame(ms++, 0), s);
    CHECK(out.event_count == 2 && out.events[0].type == EventType::NoteOff);
    // Toggles: S10 octaves, S11 mode, S12 ensemble, S13 sustain mode, S14 split.
    c.Process(Frame(ms++, 1u << 9), s);
    c.Process(Frame(ms++, 0), s);
    CHECK(s.inst.string_octave == 1);
    c.Process(Frame(ms++, 1u << 10), s);
    c.Process(Frame(ms++, 0), s);
    CHECK(s.fx.ensemble_mode == 2);
    c.Process(Frame(ms++, 1u << 11), s);
    c.Process(Frame(ms++, 0), s);
    CHECK(!s.fx.ensemble_on);
    c.Process(Frame(ms++, 1u << 12), s);
    c.Process(Frame(ms++, 0), s);
    CHECK(s.inst.solo_sustain_mode == 1);
    c.Process(Frame(ms++, 1u << 13), s);
    c.Process(Frame(ms++, 0), s);
    CHECK(s.inst.split_mode == 1);
    // S15: next RAM preset; channel/master/balance stay, all pots latched.
    s.prod.master = 0.33f;
    c.Process(Frame(ms++, 1u << 14), s);
    CHECK(s.preset == 1 && s.prod.midi_channel == 3 && s.prod.master == 0.33f);
    CHECK(s.inst.string_registration == Preset(1).inst.string_registration);
    CHECK(c.Latched(0) && c.Latched(7));
}

// Own sound fingerprint (RMS per section of a fixed sequence). Not a statement about the
// original; it pins the own behaviour so that sound changes are deliberate. Tolerances
// cover compiler/libm differences between the Windows and Linux host builds.
static void TestFingerprint() {
    State s;
    s.fx.phaser_amount = 0.25f;
    s.fx.animate_amount = 0.2f;
    s.inst.string_octave = OctaveBoth;
    Product<>& p = Fresh(s);
    std::vector<std::pair<std::size_t, Event>> ev = {{0, NoteOn(0, 48, 100)}, {0, NoteOn(0, 60, 100)}, {100, NoteOn(0, 67, 90)},
                                                     {300, NoteOff(0, 48)}, {500, NoteOff(0, 60)}, {700, NoteOff(0, 67)}};
    const Out o = Run(p, 1200, ev);
    const double r[4] = {Rms(o.l, 0, 14400), Rms(o.r, 14400, 28800), Rms(o.l, 28800, 43200), Rms(o.r, 43200, 57600)};
    std::printf("  fingerprint RMS %.6f %.6f %.6f %.6f\n", r[0], r[1], r[2], r[3]);
    const double expected[4] = {0.116761, 0.079304, 0.027349, 0.002420}; // product 0.1.0, 2026-11-01
    for(int i = 0; i < 4; ++i)
        CHECK(std::fabs(r[i] - expected[i]) <= 2e-3 * expected[i] + 1e-6);
}

int main() {
    TestRegister();
    TestBalanceCurve();
    TestRouting();
    TestBalanceMix();
    TestMatrixAndModulation();
    TestPanic();
    TestHeadroom();
    TestSlotExchange();
    TestControls();
    TestFingerprint();
    if(failures) {
        std::printf("streichwurst product: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("streichwurst product: all tests passed\n");
    return 0;
}
