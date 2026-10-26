// Host tests of the Step Seq core (ARBEITSAUFTRAG Phase 1): playback rules, transport
// table, time base, notes, keys and pots, MIDI tracking, calibration, DAC adapter, SQ
// codec and device, golden event hash. Shared vectors come from ../contracts/.
#include "step_seq/controls.h"
#include "step_seq/cv.h"
#include "step_seq/device.h"
#include "step_seq/engine.h"
#include "step_seq/midi_out.h"
#include "step_seq/params.h"
#include "step_seq/sq_protocol.h"
#include "vectors.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace step_seq;
using Kind = OutEvent::Kind;

// Event golden of Step Seq 0.1.0 (pattern, 64 steps, three modes). Changes only on purpose,
// with the reason in CHANGELOG.md.
#ifndef STEP_SEQ_GOLDEN
#define STEP_SEQ_GOLDEN 0x7528DC6Fu
#endif

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static std::string Contract(const char* name) { return std::string(CONTRACTS_DIR) + "/" + name; }

struct Event {
    uint32_t tick;
    OutEvent e;
};

// Engine with recorded outputs; one Run() tick = one 48-sample block.
struct Rig {
    Engine engine;
    TickInput in;
    TickOutput out;
    std::vector<Event> events;
    std::vector<std::pair<uint32_t, int>> steps; // (tick, 1-based step)
    std::vector<std::pair<uint32_t, bool>> gates;
    bool gate = false;
    Rig() {
        engine.Init();
        for(auto& s : engine.pattern)
            s = Step{};
    }
    void Tick(int n = 1) {
        for(int i = 0; i < n; ++i) {
            engine.Tick(in, out);
            in.gate1_rise = false;
            in.gate2_rise = false;
            for(int k = 0; k < out.count; ++k)
                events.push_back({engine.Now(), out.events[k]});
            if(out.step_changed)
                steps.push_back({engine.Now(), out.step + 1});
            if(out.gate != gate) {
                gates.push_back({engine.Now(), out.gate});
                gate = out.gate;
            }
        }
    }
    void Run() {
        in.run_switch = true;
        Tick();
    }
    void Pulse() {
        in.gate1_rise = true;
        Tick();
    }
    int Count(Kind k) const {
        int n = 0;
        for(auto& e : events)
            n += e.e.kind == k;
        return n;
    }
    std::vector<int> StepList() const {
        std::vector<int> v;
        for(auto& s : steps)
            v.push_back(s.second);
        return v;
    }
    void Clear() {
        events.clear();
        steps.clear();
        gates.clear();
    }
};

// ----- playback vectors (transport_vectors.json) -----
static Mode ModeOf(const std::string& s) {
    return s == "forward" ? Mode::Forward : (s == "reverse" ? Mode::Reverse : Mode::Random);
}

static void TestTransportVectors() {
    const std::string text = vectors::ReadFile(Contract("transport_vectors.json"));
    const std::string sequences = text.substr(text.find("\"sequences\""));
    int cases = 0;
    for(const std::string& block : vectors::Blocks(sequences, "name")) {
        std::string mode, length, steps, changes;
        vectors::Field(block, "mode", mode);
        vectors::Field(block, "length", length);
        vectors::Field(block, "steps", steps);
        const std::vector<long> expected = vectors::Numbers(steps);
        Rig r;
        r.engine.settings.mode = ModeOf(mode);
        r.engine.settings.length = std::atoi(length.c_str());
        r.engine.settings.tempo = 300;
        // changes: [{"after": n, "length": l} or {"after": n, "mode": m}]
        std::vector<std::pair<int, std::string>> change_list;
        std::size_t p = block.find("\"changes\"");
        while(p != std::string::npos && p < block.find("\"steps\"")) {
            const std::size_t a = block.find("\"after\":", p);
            if(a == std::string::npos || a > block.find("\"steps\""))
                break;
            const std::size_t end = block.find('}', a);
            change_list.push_back({std::atoi(block.c_str() + a + 8), block.substr(a, end - a)});
            p = end;
        }
        r.Run();
        while(r.steps.size() < expected.size()) {
            for(auto& c : change_list) {
                if(int(r.steps.size()) == c.first) {
                    std::string v;
                    if(vectors::Field(c.second, "length", v))
                        r.engine.settings.length = std::atoi(v.c_str());
                    if(vectors::Field(c.second, "mode", v))
                        r.engine.settings.mode = ModeOf(v);
                    c.first = -1;
                }
            }
            r.Tick();
        }
        std::vector<int> got = r.StepList();
        bool same = got.size() == expected.size();
        for(std::size_t i = 0; same && i < got.size(); ++i)
            same = got[i] == expected[i];
        if(!same)
            std::printf("  sequence '%s' differs\n", block.substr(8, 40).c_str());
        CHECK(same);
        ++cases;
    }
    CHECK(cases >= 12);
    // Random draws with set seeds, zero seed replaced by the constant.
    const std::string random = text.substr(text.find("\"draws\""), text.find("\"sequences\"") - text.find("\"draws\""));
    for(const std::string& block : vectors::Blocks(random, "seed")) {
        std::string seed, length, steps;
        vectors::Field(block, "seed", seed);
        vectors::Field(block, "length", length);
        vectors::Field(block, "steps", steps);
        Xorshift32 rng(uint32_t(std::strtoul(seed.c_str(), nullptr, 10)));
        for(long s : vectors::Numbers(steps))
            CHECK(rng.Draw(std::atoi(length.c_str())) == s);
    }
    // Random playback with a set seed matches the draws; the generator runs on across stop.
    Rig r;
    r.engine.Init(1);
    r.engine.settings.mode = Mode::Random;
    r.engine.settings.tempo = 300;
    r.Run();
    while(r.steps.size() < 6)
        r.Tick();
    const std::vector<int> first = r.StepList();
    const int expected_first[] = {2, 2, 6, 16, 2, 1};
    for(int i = 0; i < 6; ++i)
        CHECK(first[std::size_t(i)] == expected_first[i]);
    r.in.run_switch = false;
    r.Tick();
    r.Clear();
    r.Run();
    while(r.steps.size() < 1)
        r.Tick();
    CHECK(r.StepList()[0] == 11); // 7th draw of seed 1: no re-seed on start
}

static void TestLengths() {
    for(int length : {1, 7, 8, 9, 16}) {
        for(Mode mode : {Mode::Forward, Mode::Reverse, Mode::Random}) {
            Rig r;
            r.engine.settings.length = length;
            r.engine.settings.mode = mode;
            r.engine.settings.tempo = 300;
            r.Run();
            while(r.steps.size() < 40)
                r.Tick();
            for(int s : r.StepList())
                CHECK(s >= 1 && s <= length);
            if(mode == Mode::Forward)
                CHECK(r.StepList()[std::size_t(length)] == 1);
            if(mode == Mode::Reverse)
                CHECK(r.StepList()[0] == length && r.StepList()[std::size_t(length)] == length);
        }
    }
}

// ----- transport table (4.3) -----
static void TestTransportInternal() {
    Rig r;
    r.Tick(5);
    CHECK(r.engine.GetState() == State::Stop && r.steps.empty());
    // Enable rises (also at power-up with SW17 on Run): the start point plays at once.
    r.Run();
    CHECK(r.engine.GetState() == State::Running);
    CHECK(r.steps.size() == 1 && r.steps[0].second == 1 && r.steps[0].first == r.engine.Now());
    CHECK(r.Count(Kind::Start) == 1 && r.Count(Kind::NoteOn) == 1);
    // Panel stop -> PAUSE with panic, position kept; panel start -> from the start point.
    r.Tick(400);
    const int position = r.engine.Position();
    CHECK(r.engine.PanelStop());
    r.Tick();
    CHECK(r.engine.GetState() == State::Paused && !r.engine.GateHigh());
    CHECK(r.Count(Kind::Panic) == 1 && r.Count(Kind::Stop) == 1);
    CHECK(r.engine.Position() == position);
    r.Clear();
    CHECK(r.engine.PanelStart());
    r.Tick();
    CHECK(r.engine.GetState() == State::Running && r.StepList().size() == 1 && r.StepList()[0] == 1);
    // Enable falls -> STOP and panic; panel commands without enable are refused.
    r.in.run_switch = false;
    r.Clear();
    r.Tick();
    CHECK(r.engine.GetState() == State::Stop && r.Count(Kind::Panic) == 1 && !r.engine.GateHigh());
    CHECK(!r.engine.PanelStart() && !r.engine.PanelStop());
    // Reset in STOP: the next start begins at the start point anyway; in PAUSE too.
    r.Run();
    r.Tick(130);
    CHECK(r.engine.PanelStop());
    r.Tick();
    r.in.gate2_rise = true;
    r.Tick();
    r.Clear();
    CHECK(r.engine.PanelStart());
    r.Tick();
    CHECK(r.StepList()[0] == 1);
}

static void TestTransportGate() {
    Rig r;
    r.engine.settings.clock_source = ClockSource::GateIn1;
    r.Run();
    CHECK(r.engine.GetState() == State::Waiting && r.steps.empty());
    r.Tick(20);
    CHECK(r.steps.empty() && r.Count(Kind::NoteOn) == 0);
    // The first pulse plays the start point.
    r.Pulse();
    CHECK(r.engine.GetState() == State::Running && r.StepList() == std::vector<int>({1}));
    for(int i = 0; i < 3; ++i) {
        r.Tick(99);
        r.Pulse();
    }
    CHECK(r.StepList() == std::vector<int>({1, 2, 3, 4}));
    // The period estimate follows the pulses (100 ms per step = 150 BPM).
    CHECK(std::fabs(r.engine.CurrentBpm() - 150.f) < 1.f);
    // Clock stops: after max(2 s, 4 x period) -> WAITING, gate low, notes off, position kept.
    r.Clear();
    r.Tick(2000);
    CHECK(r.engine.GetState() == State::Running);
    r.Tick(2);
    CHECK(r.engine.GetState() == State::Waiting && !r.engine.GateHigh() && r.Count(Kind::Panic) == 1);
    CHECK(r.engine.Position() == 4);
    // The next pulse plays the next step.
    r.Clear();
    r.Pulse();
    CHECK(r.engine.GetState() == State::Running && r.StepList() == std::vector<int>({5}));
    CHECK(r.Count(Kind::Continue) == 1);
    // Panel stop / start: PAUSE, then WAITING for the source; reset while waiting.
    CHECK(r.engine.PanelStop());
    r.Tick();
    CHECK(r.engine.GetState() == State::Paused);
    CHECK(r.engine.PanelStart());
    r.Tick();
    CHECK(r.engine.GetState() == State::Waiting);
    r.in.gate2_rise = true;
    r.Tick();
    r.Clear();
    r.Pulse();
    CHECK(r.StepList() == std::vector<int>({1}));
}

static void TestTransportMidi() {
    Rig r;
    r.engine.settings.clock_source = ClockSource::Midi;
    r.Run();
    CHECK(r.engine.GetState() == State::Waiting);
    // Plain clocks only arm the port; Start plays the start point with the next clock.
    for(int i = 0; i < 12; ++i) {
        r.engine.MidiClock(Port::Usb);
        r.Tick();
    }
    CHECK(r.steps.empty());
    r.engine.MidiStart(Port::Usb);
    r.Tick();
    CHECK(r.engine.GetState() == State::Running && r.steps.empty());
    for(int i = 0; i < 13; ++i) {
        r.engine.MidiClock(Port::Usb);
        r.engine.MidiClock(Port::Din); // the other port is ignored and counted
        r.Tick(3);
    }
    CHECK(r.StepList() == std::vector<int>({1, 2, 3}));
    CHECK(r.engine.counters.clock_ignored == 13);
    // Received clock is never sent on, nor Start/Stop (echo guard).
    CHECK(r.Count(Kind::Clock) == 0 && r.Count(Kind::Start) == 0);
    // Stop -> PAUSE with panic; Continue -> from the remembered position with the next clock.
    r.engine.MidiStop(Port::Usb);
    r.Tick();
    CHECK(r.engine.GetState() == State::Paused && r.Count(Kind::Panic) == 1);
    r.Clear();
    r.engine.MidiContinue(Port::Usb);
    r.engine.MidiClock(Port::Usb);
    r.Tick();
    CHECK(r.StepList() == std::vector<int>({4}));
    // Timeout -> WAITING; the next clock (either port now) plays the next step.
    r.Tick(2100);
    CHECK(r.engine.GetState() == State::Waiting);
    r.Clear();
    r.engine.MidiClock(Port::Din);
    r.Tick();
    CHECK(r.StepList() == std::vector<int>({5}));
}

static void TestTransportAuto() {
    Rig r;
    r.engine.settings.clock_source = ClockSource::Auto;
    r.engine.settings.tempo = 120; // 125 ticks per step
    r.Run();
    CHECK(r.engine.GetState() == State::Running && r.engine.GetActive() == Active::Internal);
    // A gate source appears: it takes over at the next step boundary.
    r.Tick(50);
    r.Pulse();
    r.Tick(80);
    CHECK(r.engine.GetActive() == Active::Gate);
    for(int i = 0; i < 5; ++i) {
        r.Tick(99);
        r.Pulse();
    }
    // Its clock stops: the internal clock takes over phase-correctly with the measured tempo.
    const uint32_t last_pulse_step = r.steps.back().first;
    r.Tick(2100);
    CHECK(r.engine.GetActive() == Active::Internal && r.engine.GetState() == State::Running);
    r.Tick(500);
    const auto& s = r.steps;
    CHECK(s.size() >= 5 && s[s.size() - 1].first - s[s.size() - 2].first == 100); // measured tempo
    for(std::size_t i = s.size() - 4; i < s.size(); ++i)
        CHECK((s[i].first - last_pulse_step) % 100 == 0); // on the grid of the lost clock
    // MIDI clock on a port that receives our clock is no source (echo guard).
    r.engine.MidiClock(Port::Usb);
    r.Tick(200);
    CHECK(r.engine.GetActive() == Active::Internal);
    r.engine.settings.usb_out = false;
    r.engine.MidiClock(Port::Usb);
    r.Tick(130);
    CHECK(r.engine.GetActive() == Active::Usb);
}

static void TestGate2Run() {
    Rig r;
    r.engine.settings.gate2_mode = Gate2Mode::Run;
    r.Run();
    CHECK(r.engine.GetState() == State::Stop); // SW17 Run and Gate IN 2 low: no enable
    r.in.gate2_high = true;
    r.Tick();
    CHECK(r.engine.GetState() == State::Running);
    r.in.gate2_high = false;
    r.Tick();
    CHECK(r.engine.GetState() == State::Stop);
    r.in.gate2_high = true;
    r.in.run_switch = false; // SW17 on Stop always wins
    r.Tick();
    CHECK(r.engine.GetState() == State::Stop);
}

// ----- time base -----
static void TestNoDrift() {
    Rig r;
    r.engine.settings.tempo = 133.f;
    r.engine.settings.length = 16;
    r.Run();
    const uint32_t start = r.steps[0].first;
    while(r.steps.size() < 10001)
        r.Tick();
    const double samples_per_step = 48000.0 * 60.0 / (133.0 * 4.0);
    for(std::size_t k : {std::size_t(1), std::size_t(5000), std::size_t(10000)}) {
        const double expected = samples_per_step * double(k);
        const double block = double(r.steps[k].first - start) * 48.0;
        CHECK(block >= expected - 48.0 && block <= expected + 48.0);
    }
}

static void TestSwing() {
    Rig r;
    r.engine.settings.tempo = 120.f; // T = 6000 samples = 125 ticks
    r.engine.settings.swing = 75.f;
    r.Run();
    while(r.steps.size() < 9)
        r.Tick();
    for(std::size_t k = 0; k + 2 < 9; k += 2) {
        const uint32_t first = r.steps[k + 1].first - r.steps[k].first;
        const uint32_t second = r.steps[k + 2].first - r.steps[k + 1].first;
        CHECK(first + second == 250);              // a pair takes 2T
        CHECK(first >= 187 && first <= 188);       // 2T * 0.75
    }
    Rig even;
    even.engine.settings.swing = 50.f;
    even.Run();
    while(even.steps.size() < 5)
        even.Tick();
    CHECK(even.steps[2].first - even.steps[1].first == 125 && even.steps[1].first - even.steps[0].first == 125);
}

static void TestGateBounds() {
    // 5 % at 300 BPM: 2.5 ms -> 2 or 3 ticks, never under 2 ms high.
    Rig r;
    r.engine.settings.tempo = 300.f;
    for(auto& s : r.engine.pattern)
        s.gate_percent = 5;
    r.Run();
    r.Tick(300);
    for(std::size_t i = 0; i + 1 < r.gates.size(); i += 2) {
        CHECK(r.gates[i].second && !r.gates[i + 1].second);
        CHECK(r.gates[i + 1].first - r.gates[i].first >= 2);
    }
    // 100 % without slide: the gate falls and rises again 1 ms later (retrigger gap).
    Rig full;
    for(auto& s : full.engine.pattern)
        s.gate_percent = 100;
    full.Run();
    full.Tick(400);
    CHECK(full.gates.size() >= 6);
    for(std::size_t i = 1; i + 1 < full.gates.size(); i += 2) {
        CHECK(!full.gates[i].second && full.gates[i + 1].second);
        CHECK(full.gates[i + 1].first - full.gates[i].first >= 1);
    }
    // Order in a tick with gate-off and step change: Off before On, never both in one tick.
    for(std::size_t i = 1; i < full.events.size(); ++i)
        if(full.events[i].e.kind == Kind::NoteOn && full.events[i - 1].e.kind == Kind::NoteOff)
            CHECK(full.events[i].tick > full.events[i - 1].tick);
}

static void TestExternalTiming() {
    // Start value of the period estimate: VR5 tempo.
    Rig r;
    r.engine.settings.clock_source = ClockSource::GateIn1;
    r.engine.settings.tempo = 90.f;
    r.Run();
    CHECK(std::fabs(r.engine.CurrentBpm() - 90.f) < 0.01f);
    // Early pulse: the open gate closes before the next step's gate (order as in 3.3).
    for(auto& s : r.engine.pattern)
        s.gate_percent = 100;
    r.Pulse();
    r.Tick(150);
    r.Clear();
    r.Pulse(); // 151 ms < 167 ms estimate: the gate is still high
    CHECK(r.events.size() >= 1 && r.events[0].e.kind == Kind::NoteOff);
    r.Tick();
    CHECK(r.engine.GateHigh());
    // Swing with external clock: steps 1 and 2 straight, the 4th step is delayed.
    Rig s;
    s.engine.settings.clock_source = ClockSource::GateIn1;
    s.engine.settings.swing = 75.f;
    s.Run();
    for(int i = 0; i < 6; ++i) {
        s.Pulse();
        s.Tick(99);
    }
    CHECK(s.steps.size() == 6);
    CHECK(s.steps[1].first - s.steps[0].first == 100);
    CHECK(s.steps[3].first - s.steps[2].first > 100);
    // Ppq24: six pulses per step.
    Rig q;
    q.engine.settings.clock_source = ClockSource::GateIn1;
    q.engine.settings.divider = Divider::Ppq24;
    q.Run();
    for(int i = 0; i < 18; ++i) {
        q.Pulse();
        q.Tick(9);
    }
    CHECK(q.StepList() == std::vector<int>({1, 2, 3}));
}

// ----- notes -----
static void TestNotes() {
    Step s;
    s.note = 13; // C#2 relative to C1
    CHECK(StepMidiNote(s, Scale::Chromatic, 0) == 37);
    CHECK(StepMidiNote(s, Scale::Major, 0) == 36);   // C# -> C (next lower degree)
    s.note = 6; // F#
    CHECK(StepMidiNote(s, Scale::Major, 0) == 29);   // F
    CHECK(StepMidiNote(s, Scale::Minor, 0) == 29);
    s.note = 11; // B
    CHECK(StepMidiNote(s, Scale::Minor, 0) == 34);   // Bb
    s.note = 59;
    CHECK(StepMidiNote(s, Scale::Chromatic, 12 + 24) == 119);
    CHECK(StepMidiNote(s, Scale::Chromatic, 12 + 24, 0, 100) == 100); // calibrated CV range
    s.note = 0;
    CHECK(StepMidiNote(s, Scale::Chromatic, -36) == 0);  // clamp at MIDI 0
    // CV IN 1: semitones with hysteresis, +-24.
    CvQuantizer q;
    CHECK(q.Process(1.f / 60.f) == 1);           // 1 semitone
    CHECK(q.Process(1.6f / 60.f) == 1);          // 1.6 stays (hysteresis)
    CHECK(q.Process(1.8f / 60.f) == 2);
    CHECK(q.Process(1.f) == 24 && q.Process(-1.f) == -24);
    // Transpose is taken at the step start, not within the note.
    Rig r;
    r.Run();
    uint8_t n0 = 0;
    for(auto& e : r.events)
        if(e.e.kind == Kind::NoteOn)
            n0 = e.e.note;
    r.in.cv1_semitones = 5;
    r.Tick(30);
    CHECK(r.out.pitch == n0);
    while(r.steps.size() < 2)
        r.Tick();
    CHECK(r.out.pitch == n0 + 5);
}

static void TestSlides() {
    Rig r;
    auto& p = r.engine.pattern;
    p[0].note = 12; p[0].slide = true;
    p[1].note = 17; p[1].slide = true;   // legato into another note
    p[2].note = 17; p[2].slide = false;  // same note with slide before: stays held
    p[3].note = 17;                      // same note without slide: retrigger
    p[4].note = 20; p[4].slide = true;
    p[5].gate = false;                   // slide into a step without gate: gate falls at its end
    r.engine.settings.length = 6;
    r.Run();
    while(r.steps.size() < 6)
        r.Tick();
    r.Tick(50);
    std::vector<Kind> kinds;
    for(auto& e : r.events)
        if(e.e.kind != Kind::Clock && e.e.kind != Kind::Start)
            kinds.push_back(e.e.kind);
    const std::vector<Kind> expected = {Kind::NoteOn, Kind::Legato, Kind::NoteOff, Kind::NoteOn,
                                        Kind::NoteOff, Kind::NoteOn, Kind::NoteOff};
    CHECK(kinds == expected);
    // The gate stayed high from step 1 to the end of step 3 (no new envelope).
    CHECK(r.gates.size() >= 2 && r.gates[1].first >= r.steps[2].first);
    // The slide step 5 holds the gate until step 6 (gate off) begins.
    const uint32_t step6 = r.steps[5].first;
    CHECK(r.gates.back().first == step6 && !r.gates.back().second);
}

static void TestPreview() {
    Rig r;
    r.Tick();
    r.engine.PreviewStep(3);
    r.Tick();
    CHECK(r.engine.GateHigh() && r.Count(Kind::NoteOn) == 1);
    const uint32_t first_tag = r.events.back().e.tag;
    r.Tick(50);
    // A new preview ends the old one first, then sounds after the 1 ms gap.
    r.engine.PreviewStep(4);
    r.Tick();
    CHECK(r.events.back().e.kind == Kind::NoteOff && r.events.back().e.tag == first_tag);
    r.Tick();
    CHECK(r.engine.GateHigh());
    r.Tick(160);
    CHECK(!r.engine.GateHigh());
    // Start aborts a preview; its 150 ms Note-Off never hits the first step's note.
    r.Clear();
    r.engine.PreviewStep(2);
    r.Tick(10);
    r.Run();
    const uint32_t preview_tag = r.events[0].e.tag;
    CHECK(r.events.size() >= 2);
    r.Tick(200);
    int offs_for_preview = 0;
    for(auto& e : r.events)
        if(e.e.kind == Kind::NoteOff && e.e.tag == preview_tag)
            ++offs_for_preview;
    CHECK(offs_for_preview == 1);
    // While running, previews are refused.
    r.engine.PreviewStep(5);
    r.Tick();
    CHECK(r.engine.GetState() == State::Running);
}

// ----- keys and pots (4.1) -----
struct Panel {
    Rig rig;
    Controls controls;
    ControlFrame frame;
    bool boot = false;
    Panel() {
        for(float& p : frame.pots)
            p = 0.5f;
        frame.sw18 = 0;
    }
    void Tick(int n = 1) {
        for(int i = 0; i < n; ++i) {
            ++frame.ms;
            boot |= controls.Process(frame, rig.engine);
            rig.Tick();
        }
    }
    void Press(int key) { frame.keys |= uint16_t(1u << key); }
    void Release(int key) { frame.keys &= uint16_t(~(1u << key)); }
};

static void TestKeys() {
    Panel k;
    k.Tick(5);
    // Short press selects; long press toggles the gate on release.
    k.Press(2);
    k.Tick(100);
    k.Release(2);
    k.Tick();
    CHECK(k.controls.Selected() == 2 && k.rig.engine.pattern[2].gate);
    k.Press(3);
    k.Tick(450);
    k.Release(3);
    k.Tick();
    CHECK(!k.rig.engine.pattern[3].gate);
    // Chords: no long-press action in any order of pressing and releasing, with a third key.
    for(int order = 0; order < 4; ++order) {
        Panel c;
        c.Tick(5);
        c.Press(5);
        c.Tick(order & 1 ? 1 : 200);
        c.Press(6);
        if(order & 2) {
            c.Tick(100);
            c.Press(7);
        }
        c.frame.pots[0] = 0.9f; // a pot moves during the chord
        c.Tick(600);
        if(order & 1) {
            c.Release(5);
            c.Tick(50);
            c.Release(6);
        } else {
            c.Release(6);
            c.Tick(50);
            c.Release(5);
        }
        c.Release(7);
        c.Tick(5);
        CHECK(c.rig.engine.pattern[5].gate && c.rig.engine.pattern[6].gate && c.rig.engine.pattern[7].gate);
        CHECK(c.controls.Selected() == (order & 2 ? 7 : 6)); // the selection on press stays
    }
    // S9 + S16 under 5 s: no bootloader and no step action; from 5 s: the request.
    Panel b;
    b.Tick(5);
    b.Press(8);
    b.Press(15);
    b.Tick(4900);
    b.Release(8);
    b.Release(15);
    b.Tick(5);
    CHECK(!b.boot && b.rig.engine.pattern[8].gate && b.rig.engine.pattern[15].gate);
    b.Press(8);
    b.Tick(10);
    b.Press(15);
    b.Tick(5100);
    CHECK(b.boot);
    b.Release(8);
    b.Release(15);
    b.Tick(5);
    CHECK(b.rig.engine.pattern[8].gate && b.rig.engine.pattern[15].gate);
}

static void TestPots() {
    Panel p;
    p.Tick(200);
    // VR5-VR8 act at power-up and whenever they move.
    CHECK(std::fabs(p.rig.engine.settings.tempo - 165.f) < 0.5f);
    CHECK(p.rig.engine.settings.length == 9 && p.rig.engine.settings.transpose == 0);
    // Stepped pots: 25 % hysteresis of one step width at the level edge.
    Stepper st(4); // levels of 0.25
    CHECK(st.Update(0.3f) == 1);
    CHECK(st.Update(0.2f) == 1);   // inside the hysteresis band (0.1875)
    CHECK(st.Update(0.18f) == 0);
    CHECK(st.Update(0.30f) == 0);  // 0.3125 needed to come back up
    CHECK(st.Update(0.32f) == 1);
    // Pick-up: after selecting another step VR1-VR4 act only after moving > 2 %.
    p.Press(4);
    p.Tick(20);
    p.Release(4);
    p.Tick(5);
    const Step before = p.rig.engine.pattern[4];
    p.frame.pots[2] = 0.51f;
    p.Tick(200);
    CHECK(p.rig.engine.pattern[4].gate_percent == before.gate_percent);
    p.frame.pots[2] = 0.9f;
    p.Tick(200);
    CHECK(p.rig.engine.pattern[4].gate_percent >= 90 && p.rig.engine.pattern[4].gate_percent <= 91);
    // VR1/VR2 while stopped sound the note (preview); VR4 sets accent and slide.
    p.rig.Clear();
    p.frame.pots[1] = 0.95f; // semitone B
    p.Tick(300);
    CHECK(p.rig.engine.pattern[4].note % 12 == 11);
    CHECK(p.rig.Count(Kind::NoteOn) >= 1);
    p.frame.pots[3] = 0.9f;
    p.Tick(300);
    CHECK(p.rig.engine.pattern[4].accent && p.rig.engine.pattern[4].slide);
    // SW18 needs 20 ms stable.
    p.frame.sw18 = 2;
    p.Tick(10);
    CHECK(p.rig.engine.settings.mode == Mode::Forward);
    p.Tick(15);
    CHECK(p.rig.engine.settings.mode == Mode::Random);
    // A panel write to VR5's tempo holds until the pot moves again.
    p.rig.engine.settings.tempo = 200.f;
    p.controls.ReleaseGlobal(4);
    p.Tick(50);
    CHECK(p.rig.engine.settings.tempo == 200.f);
    p.frame.pots[4] = 0.2f;
    p.Tick(300);
    CHECK(std::fabs(p.rig.engine.settings.tempo - 84.f) < 0.5f);
}

// ----- MIDI tracking and panic (4.4) -----
struct Sink {
    std::vector<std::vector<uint8_t>>* log;
    void operator()(uint8_t* b, int n) const { log->emplace_back(b, b + n); }
};

static int Notes(const std::vector<std::vector<uint8_t>>& log, uint8_t status) {
    int n = 0;
    for(auto& m : log)
        n += m.size() == 3 && (m[0] & 0xF0) == status;
    return n;
}

static void TestMidiOut() {
    std::vector<std::vector<uint8_t>> usb_log, din_log;
    MidiPort<Sink> usb(Sink{&usb_log}, 1 << 30), din(Sink{&din_log}, 24);
    NoteFeed feed;
    Rig r;
    r.engine.settings.tempo = 300;
    r.Run();
    feed.Feed(r.out);
    for(int i = 0; i < 1000; ++i) {
        ServiceMidi(feed, 1, true, usb, din);
        r.Tick();
        feed.Feed(r.out);
    }
    ServiceMidi(feed, 1, true, usb, din);
    CHECK(Notes(usb_log, 0x90) >= 20 && Notes(usb_log, 0x90) == Notes(din_log, 0x90));
    int clocks = 0;
    for(auto& m : usb_log)
        clocks += m.size() == 1 && m[0] == 0xF8;
    CHECK(clocks >= 6 * 19); // 24 PPQN: 6 per step
    // Panic on stop: every sent note gets its Off on both ports, nothing hangs.
    r.in.run_switch = false;
    r.Tick();
    feed.Feed(r.out);
    ServiceMidi(feed, 1, true, usb, din);
    CHECK(!usb.AnySent(1) && !din.AnySent(1));
    CHECK(Notes(usb_log, 0x90) == Notes(usb_log, 0x80) || Notes(usb_log, 0xB0) > 0);
    // Queue overflow: Note-Ons are dropped and counted, Offs come from the table.
    NoteFeed full;
    TickOutput burst;
    for(int i = 0; i < 80; ++i) {
        burst.count = 0;
        OutEvent on;
        on.kind = Kind::NoteOn;
        on.note = uint8_t(40 + (i % 20));
        burst.Push(on);
        full.Feed(burst);
    }
    CHECK(full.dropped_on.load() == 80 - 64);
    usb_log.clear();
    for(int pass = 0; pass < 20; ++pass)
        ServiceMidi(full, 1, true, usb, din);
    burst.count = 0;
    OutEvent panic;
    panic.kind = Kind::Panic;
    burst.Push(panic);
    full.Feed(burst);
    for(int pass = 0; pass < 20; ++pass)
        ServiceMidi(full, 1, true, usb, din);
    CHECK(!usb.AnySent(1) && !din.AnySent(1));
    // Send budget: at most 8 note messages per port and pass, DIN at most 24 bytes.
    NoteFeed busy;
    for(int i = 0; i < 20; ++i) {
        TickOutput t;
        OutEvent on;
        on.kind = Kind::NoteOn;
        on.note = uint8_t(60 + i);
        t.Push(on);
        busy.Feed(t);
    }
    usb_log.clear();
    din_log.clear();
    ServiceMidi(busy, 1, true, usb, din);
    int bytes = 0;
    for(auto& m : din_log)
        bytes += int(m.size());
    CHECK(Notes(usb_log, 0x90) <= 8 && bytes <= 24);
    // Output switched off: its notes end first; USB lost: the table is void.
    usb.Panic(1);
    din.Panic(1);
    CHECK(!usb.AnySent(1) && !din.AnySent(1));
    usb.NoteOn(1, 64, 100);
    usb.Forget();
    CHECK(!usb.AnySent(1));
    usb_log.clear();
    usb.enabled = false;
    usb.NoteOn(1, 65, 100);
    CHECK(usb_log.empty() && !usb.AnySent(1));
}

// ----- CV calibration and DAC adapter (4.5, R01) -----
static OutputCal GoodCal() {
    OutputCal c;
    c.offset = 2048000;
    c.scale = -409600;
    c.gate_low = 2048;
    c.gate_high = 410;
    c.clamp_min = 0;
    c.clamp_max = 4095;
    return c;
}

static void TestCv() {
    CvOut cv;
    uint16_t pitch = 1, gate = 1;
    // Locked: no dynamic output at all.
    CHECK(!cv.Tick(true, 60, false, 60, pitch, gate));
    CHECK(!cv.Enable(true)); // without valid calibration of both outputs
    OutputCal bad = GoodCal();
    bad.gate_high = bad.gate_low;
    cv.SetCal(1, bad);
    CHECK(!cv.cal[1].valid);
    bad = GoodCal();
    bad.scale = 50000;
    CHECK(!ValidCal(bad));
    bad = GoodCal();
    bad.clamp_min = 3000;
    bad.clamp_max = 2000;
    CHECK(!ValidCal(bad));
    cv.SetCal(0, GoodCal());
    cv.SetCal(1, GoodCal());
    CHECK(cv.Enable(true));
    CHECK(cv.Tick(true, 24, false, 60, pitch, gate));
    CHECK(pitch == 2048 && gate == 410); // C1 = 0 V; gate high code
    CHECK(cv.Tick(true, 36, false, 60, pitch, gate));
    CHECK(pitch == 1638); // one octave = one volt, inverting stage
    // Glide: 60 ms linear to the next note.
    cv.Tick(true, 48, true, 60, pitch, gate);
    for(int i = 0; i < 29; ++i)
        cv.Tick(true, 48, false, 60, pitch, gate);
    CHECK(pitch > 1229 + 100 && pitch < 1638);
    for(int i = 0; i < 31; ++i)
        cv.Tick(true, 48, false, 60, pitch, gate);
    CHECK(pitch == 1229);
    // Enable withdrawn with an open gate: gate-low once, then nothing.
    CHECK(cv.Enable(false));
    CHECK(cv.Tick(true, 48, false, 60, pitch, gate) && gate == 2048);
    CHECK(!cv.Tick(true, 48, false, 60, pitch, gate));
    // New calibration withdraws the enable.
    CHECK(cv.Enable(true));
    cv.SetCal(0, GoodCal());
    CHECK(!cv.Enabled());
    int low = 0, high = 0;
    OutputCal valid = GoodCal();
    valid.valid = true;
    NoteRange(valid, low, high);
    CHECK(low == 0 && high == 84); // -5 V .. 5 V around C1
    // DAC adapter: data holding registers, then the software trigger of both channels.
    struct Regs {
        uint32_t SWTRIGR = 0, DHR12R1 = 0, DHR12R2 = 0;
    } regs;
    DacWriter<Regs> dac(&regs);
    dac.Write(1234, 4095);
    CHECK(regs.DHR12R1 == 1234 && regs.DHR12R2 == 4095 && regs.SWTRIGR == 3);
    dac.Write(5000, 0);
    CHECK(regs.DHR12R1 == (5000 & 4095));
}

// ----- SQ codec and device (PROTOCOL.md) -----
struct TestHost : sq::Host {
    std::vector<std::vector<uint8_t>> sent;
    bool busy = false;
    int params = 0, patterns = 0, cals = 0, enables = 0;
    bool PushParam(uint8_t, uint16_t, int32_t, uint16_t) override { return !busy && ++params; }
    bool PushPattern(const Pattern&, uint16_t) override { return !busy && ++patterns; }
    bool PushStep(uint8_t, const Step&, uint16_t) override { return !busy && ++patterns; }
    bool PushTransport(uint8_t) override { return true; }
    bool PushCal(uint8_t, const OutputCal&) override { return ++cals; }
    bool PushCvEnable(bool) override { return ++enables; }
    void Send(const uint8_t* b, std::size_t n) override { sent.emplace_back(b, b + n); }
};

static std::vector<uint8_t> Request(uint8_t type, uint8_t rid, const std::vector<uint8_t>& payload) {
    uint8_t buffer[sq::kMaxFrame];
    sq::Writer w(buffer, sizeof(buffer));
    w.Begin(type, rid);
    for(uint8_t b : payload)
        w.U7(b);
    const std::size_t n = w.End();
    return std::vector<uint8_t>(buffer, buffer + n);
}

static void TestSqVectors() {
    CHECK(sq::Crc8(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xF4);
    const std::string text = vectors::ReadFile(Contract("sq_vectors.json"));
    int cases = 0;
    for(const std::string& block : vectors::Blocks(text.substr(text.find("\"cases\"")), "name")) {
        std::string frame, verdict, error, stream, payload, type, rid;
        vectors::Field(block, "frame", frame);
        vectors::Field(block, "verdict", verdict);
        std::vector<uint8_t> bytes = vectors::Hex(frame);
        if(vectors::Field(block, "stream", stream)) {
            // Real-time bytes leave the SysEx in the parser (libDaisy MidiParser).
            std::vector<uint8_t> assembled;
            for(uint8_t b : vectors::Hex(stream))
                if(b < 0xF8)
                    assembled.push_back(b);
            CHECK(assembled == bytes);
        }
        const sq::Parsed p = sq::Classify(bytes.data(), bytes.size());
        const char* names[] = {"foreign", "ignore", "nak", "ok"};
        CHECK(verdict == names[int(p.verdict)]);
        if(p.verdict == sq::Verdict::Nak) {
            vectors::Field(block, "error", error);
            CHECK(p.error == std::atoi(error.c_str()));
        }
        if(vectors::Field(block, "payload", payload) && p.verdict == sq::Verdict::Ok) {
            vectors::Field(block, "type", type);
            vectors::Field(block, "rid", rid);
            CHECK(p.type == std::atoi(type.c_str()) && p.rid == std::atoi(rid.c_str()));
            CHECK(std::vector<uint8_t>(p.payload, p.payload + p.size) == vectors::Hex(payload));
            // The C++ writer produces the same bytes as the reference codec.
            CHECK(Request(p.type, p.rid, vectors::Hex(payload)) == bytes);
        }
        ++cases;
    }
    CHECK(cases >= 25);
}

static void TestDevice() {
    TestHost host;
    sq::Device dev(host);
    static int32_t values[sq::kStepSeqComponent.count];
    Settings defaults;
    for(uint8_t i = 0; i < sq::kStepSeqComponent.count; ++i)
        ReadParam(defaults, sq::kStepSeqComponent.params[i].id, values[i]);
    static sq::Component comps[] = {{"step-seq", sq::KindSequencer, "0.1.0", &sq::kStepSeqComponent, values}};
    dev.components = comps;
    dev.component_count = 1;
    dev.uid[0] = 1;
    dev.uid[1] = 2;
    dev.uid[2] = 3;
    dev.boot_id = 0xBEEF;
    auto send = [&](const std::vector<uint8_t>& f) {
        host.sent.clear();
        dev.Handle(f.data(), f.size(), 0);
        // An empty reply reads as zeros, so a missing answer fails the checks, not the test.
        return host.sent.empty() ? std::vector<uint8_t>(16, 0) : host.sent.back();
    };
    // Defaults match the contract.
    for(uint8_t i = 0; i < sq::kStepSeqComponent.count; ++i)
        CHECK(values[i] == sq::kStepSeqComponent.params[i].def);
    // IDENTIFY: product, boot ID, UID, components.
    std::vector<uint8_t> reply = send(Request(sq::Identify, 1, {}));
    sq::Parsed p = sq::Classify(reply.data(), reply.size());
    CHECK(p.verdict == sq::Verdict::Ignore && reply[5] == (sq::Identify | sq::kResponse) && reply[6] == 1);
    // Echo guard: our own replies sent back are never answered.
    send(reply);
    CHECK(host.sent.empty() && dev.counters[sq::CntEcho] == 1);
    // SET_PARAM: clamp and round, revision; read-only refused; busy -> NAK 5.
    std::vector<uint8_t> payload = {0};
    auto u14 = [&](uint16_t v) { payload.push_back(v & 0x7F); payload.push_back(v >> 7); };
    auto i32 = [&](int32_t v) {
        for(int k = 0; k < 5; ++k)
            payload.push_back(uint8_t((uint32_t(v) >> (7 * k)) & 0x7F));
    };
    u14(3);
    i32(20400); // length 20.4 -> 16
    reply = send(Request(sq::SetParam, 2, payload));
    CHECK(reply.size() > 10 && reply[5] == (sq::SetParam | sq::kResponse));
    sq::Reader rd(reply.data() + 8, reply.size() - 11);
    CHECK(rd.U7() == 0 && rd.U14() == 3 && rd.I32() == 16000 && rd.U14() == 1);
    CHECK(values[2] == 16000 && host.params == 1);
    // Duplicate (same rid, same bytes): cached reply, nothing runs twice.
    reply = send(Request(sq::SetParam, 2, payload));
    CHECK(host.params == 1 && dev.counters[sq::CntDuplicate] == 1 && !reply.empty());
    payload = {0};
    u14(5);
    i32(1000);
    reply = send(Request(sq::SetParam, 3, payload));
    CHECK(reply[5] == sq::kNak && reply[9] == sq::ErrBadArgument);
    host.busy = true;
    payload = {0};
    u14(1);
    i32(99000);
    reply = send(Request(sq::SetParam, 4, payload));
    CHECK(reply[5] == sq::kNak && reply[9] == sq::ErrBusy);
    host.busy = false;
    // Pattern: a whole pattern in one frame; invalid steps refuse the frame.
    std::vector<uint8_t> pattern;
    for(int i = 0; i < 16; ++i) {
        pattern.push_back(uint8_t(i));
        pattern.push_back(50);
        pattern.push_back(1);
    }
    reply = send(Request(sq::SetPattern, 5, pattern));
    CHECK(reply[5] == (sq::SetPattern | sq::kResponse) && host.patterns == 1);
    pattern[3 * 4] = 60; // note 60 > 59
    reply = send(Request(sq::SetPattern, 6, pattern));
    CHECK(reply[5] == sq::kNak && reply[9] == sq::ErrBadArgument && host.patterns == 1);
    reply = send(Request(sq::GetPattern, 7, {}));
    CHECK(reply.size() == 11 + 52);
    // Calibration bound to the kit's UID, separate enable.
    std::vector<uint8_t> cal;
    auto u32 = [&](std::vector<uint8_t>& v, uint32_t x) {
        for(int k = 0; k < 5; ++k)
            v.push_back(uint8_t((x >> (7 * k)) & 0x7F));
    };
    auto cal_frame = [&](uint32_t uid0, uint8_t output, uint16_t gate_high) {
        std::vector<uint8_t> v;
        u32(v, uid0);
        u32(v, 2);
        u32(v, 3);
        v.push_back(output);
        u32(v, 2048000);
        u32(v, uint32_t(-409600));
        for(uint16_t code : {uint16_t(2048), gate_high, uint16_t(0), uint16_t(4095)}) {
            v.push_back(code & 0x7F);
            v.push_back(uint8_t(code >> 7));
        }
        return v;
    };
    std::vector<uint8_t> enable;
    u32(enable, 1);
    u32(enable, 2);
    u32(enable, 3);
    enable.push_back(1);
    reply = send(Request(sq::CvEnable, 8, enable));
    CHECK(reply[5] == sq::kNak && reply[9] == sq::ErrNotCalibrated);
    reply = send(Request(sq::SetCal, 9, cal_frame(99, 0, 410)));
    CHECK(reply[5] == sq::kNak && reply[9] == sq::ErrUidMismatch);
    reply = send(Request(sq::SetCal, 10, cal_frame(1, 0, 2048)));
    CHECK(reply[5] == sq::kNak && reply[9] == sq::ErrBadArgument);
    CHECK(send(Request(sq::SetCal, 11, cal_frame(1, 0, 410)))[5] == (sq::SetCal | sq::kResponse));
    CHECK(send(Request(sq::SetCal, 12, cal_frame(1, 1, 410)))[5] == (sq::SetCal | sq::kResponse));
    reply = send(Request(sq::CvEnable, 13, enable));
    CHECK(reply[5] == (sq::CvEnable | sq::kResponse) && host.enables == 1);
    // Unknown type, broken frames, too long: NAK with the documented codes.
    reply = send(Request(0x3E, 14, {}));
    CHECK(reply[5] == sq::kNak && reply[9] == sq::ErrUnknownType);
    std::vector<uint8_t> broken = Request(sq::GetState, 15, {});
    broken[8] ^= 1;
    reply = send(broken);
    CHECK(reply[5] == sq::kNak && reply[9] == sq::ErrCrc && dev.counters[sq::CntBadCrc] == 1);
    std::vector<uint8_t> status = Request(sq::GetState, 16, {});
    status.insert(status.begin() + 8, 0x90);
    status[7] += 1;
    send(status);
    CHECK(dev.counters[sq::CntStatusInFrame] == 1);
    std::vector<uint8_t> big(130, 0);
    big[0] = 0xF0; big[1] = 0x7D; big[2] = 0x53; big[3] = 0x51; big[4] = 1; big[5] = 0x11; big.back() = 0xF7;
    send(big);
    CHECK(dev.counters[sq::CntTooLong] == 1);
    // Monitor: event frames at most every 20 ms, ended 3 s after the last request.
    host.sent.clear();
    send(Request(sq::Monitor, 17, {1}));
    host.sent.clear();
    for(uint32_t ms = 0; ms < 1000; ++ms) {
        dev.view.step = int8_t(ms % 16);
        dev.Service(ms);
    }
    CHECK(host.sent.size() >= 40 && host.sent.size() <= 51);
    for(auto& f : host.sent)
        CHECK(f[5] == sq::kMonitorEvent);
    host.sent.clear();
    for(uint32_t ms = 3100; ms < 3500; ++ms) {
        dev.view.step = int8_t(ms % 16);
        dev.Service(ms);
    }
    CHECK(host.sent.empty());
    // GET_STATS lists all counters.
    reply = send(Request(sq::GetStats, 18, {}));
    CHECK(reply[8] == sq::kCounters && reply.size() == 11 + 1 + 5 * sq::kCounters);
    // Every response fits the 120-byte limit; descriptors cover every parameter.
    for(uint8_t i = 0; i < sq::kStepSeqComponent.count; ++i) {
        reply = send(Request(sq::GetParamDesc, uint8_t(20 + i), {0, i}));
        CHECK(reply.size() <= sq::kMaxFrame && reply[5] == (sq::GetParamDesc | sq::kResponse));
    }
    reply = send(Request(sq::GetParams, 60, {0, 0}));
    CHECK(reply.size() <= sq::kMaxFrame && reply[11] == 14);
}

// ----- parameters -----
static void TestParams() {
    Settings s;
    for(uint8_t i = 0; i < sq::kStepSeqComponent.count; ++i) {
        const sq::ParamDesc& d = sq::kStepSeqComponent.params[i];
        int32_t v = 0;
        CHECK(ReadParam(s, d.id, v) && v == d.def);
        if(!(d.flags & sq::FlagReadonly)) {
            CHECK(WriteParam(s, d.id, d.max + 5000));
            CHECK(ReadParam(s, d.id, v) && v == d.max);
            CHECK(WriteParam(s, d.id, d.def));
        }
    }
    CHECK(!WriteParam(s, 5, 1000)); // play_mode follows SW18
    CHECK(!WriteParam(s, 99, 0));
    CHECK(WriteParam(s, 4, -7400) && s.transpose == -7);
}

// ----- golden: FNV-1a over a fixed event sequence, 64 steps, all three modes -----
static uint32_t Fnv(uint32_t h, uint32_t v) {
    for(int i = 0; i < 4; ++i) {
        h ^= (v >> (8 * i)) & 0xFF;
        h *= 16777619u;
    }
    return h;
}

static void TestGolden() {
    uint32_t hash = 2166136261u;
    for(Mode mode : {Mode::Forward, Mode::Reverse, Mode::Random}) {
        Rig r;
        r.engine.Init(0xC0FFEEu);
        for(int i = 0; i < kSteps; ++i) {
            Step& s = r.engine.pattern[std::size_t(i)];
            s.note = uint8_t((i * 7) % 60);
            s.gate_percent = uint8_t(10 + (i * 13) % 91);
            s.gate = i % 5 != 4;
            s.accent = i % 3 == 0;
            s.slide = i % 4 == 1;
        }
        r.engine.settings.mode = mode;
        r.engine.settings.tempo = 142.f;
        r.engine.settings.swing = 61.f;
        r.engine.settings.length = 13;
        r.Run();
        while(r.steps.size() < 64)
            r.Tick();
        for(auto& e : r.events) {
            hash = Fnv(hash, e.tick);
            hash = Fnv(hash, uint32_t(e.e.kind) | uint32_t(e.e.note) << 8 | uint32_t(e.e.velocity) << 16);
        }
    }
    std::printf("Step Seq golden event hash: 0x%08X\n", unsigned(hash));
    CHECK(hash == STEP_SEQ_GOLDEN);
}

int main() {
    TestTransportVectors();
    TestLengths();
    TestTransportInternal();
    TestTransportGate();
    TestTransportMidi();
    TestTransportAuto();
    TestGate2Run();
    TestNoDrift();
    TestSwing();
    TestGateBounds();
    TestExternalTiming();
    TestNotes();
    TestSlides();
    TestPreview();
    TestKeys();
    TestPots();
    TestMidiOut();
    TestCv();
    TestSqVectors();
    TestDevice();
    TestParams();
    TestGolden();
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: playback vectors, transport table, time base, gates, slides, preview, keys, pots, "
                "MIDI tracking, CV/DAC, SQ vectors and device, parameters, golden\n");
    return 0;
}
