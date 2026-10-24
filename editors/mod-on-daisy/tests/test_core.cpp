// Host tests of the editor core (src/core) against the firmware's Controller and
// Engine (GUI.md § 3: "core/ wird mit einem simulierten Gerät getestet"). No JUCE.
//   make -C editors/mod-on-daisy test
#include "../src/core/PatchFile.h"
#include "../src/core/SimInstruments.h"
#include "../src/core/Simulator.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

using namespace editor;

// Counts what the session sends; can be cut to model an unplugged cable.
struct Wire : Transport {
    Simulator& sim;
    int frames = 0, set_cell = 0;
    bool cut = false;
    explicit Wire(Simulator& s) : sim(s) {}
    bool Send(const uint8_t* bytes, std::size_t size) override {
        ++frames;
        if(size > 6 && bytes[6] == proto::SetCell)
            ++set_cell;
        return cut ? true : sim.Send(bytes, size);
    }
};

struct Rig {
    Simulator sim;
    Wire wire{sim};
    Model model;
    Session session{model, 77};
    uint32_t now = 1000;
    std::vector<std::pair<LogKind, std::string>> logs;
    int monitor_frames = 0;
    MonitorValues last_monitor;

    explicit Rig(const mod::InstrumentInfo& info, uint32_t seed = 1) : sim(info, seed) {
        session.log = [this](LogKind k, const std::string& t) { logs.emplace_back(k, t); };
        session.monitor = [this](const MonitorValues& m) {
            ++monitor_frames;
            last_monitor = m;
        };
    }
    void Run(uint32_t ms) {
        for(uint32_t i = 0; i < ms; ++i) {
            sim.Advance(1, [this](const uint8_t* b, std::size_t n) { session.OnMessage(b, n); });
            session.Tick(++now);
        }
    }
    bool RunUntilIdle(uint32_t max_ms = 20000) {
        for(uint32_t i = 0; i < max_ms; ++i) {
            Run(1);
            if(session.Idle() && session.State() != LinkState::Connecting)
                return true;
        }
        return false;
    }
    bool Connect() {
        session.Connect(wire, now);
        return RunUntilIdle() && session.Connected();
    }
    bool InSync() const { return sim.Device().CurrentCrc() == model.Crc(); }
    bool Logged(const std::string& part) const {
        for(const auto& l : logs)
            if(l.second.find(part) != std::string::npos)
                return true;
        return false;
    }
};

int Cell(int s, int t) { return s * 8 + t; }

std::vector<uint8_t> MonitorFrameFrom(uint8_t device) {
    uint8_t payload[36]{};
    for(std::size_t i = 0; i < sizeof(payload); i += 2)
        payload[i + 1] = 64; // s14 8192 = 0.0
    uint8_t frame[proto::kMaxFrame];
    const std::size_t n = proto::BuildFrame(frame, device, proto::MonitorFrame, 0, payload, sizeof(payload));
    return {frame, frame + n};
}

void TestConnect() {
    Rig rig(toy_piano::MakeModInfo());
    CHECK(rig.Connect());
    const DeviceInfo& d = rig.session.Device();
    CHECK(d.key == "toy-piano");
    CHECK(d.protocol_major == 1 && d.max_active == 8 && d.tick_hz == 1000);
    CHECK(d.schema == rig.sim.Device().Schema());
    CHECK(rig.model.columns[0].name == "Pitch" && rig.model.columns[0].per_voice);
    CHECK(rig.model.columns[0].SpanLabel() == "\xC2\xB1" "2 st");
    CHECK(rig.model.columns[3].name == "Attack" && rig.model.columns[3].latched);
    CHECK(!rig.model.columns[5].present);
    CHECK(rig.model.allowed == rig.sim.Device().Allowed());
    CHECK(!rig.model.Allowed(Cell(mod::PolyAt, 3)) && rig.model.Allowed(Cell(mod::PolyAt, 1)));
    CHECK(!rig.model.Allowed(Cell(0, 6)));
    CHECK(rig.model.BlockedReason(Cell(mod::PolyAt, 3)).find("Anschlag") != std::string::npos);
    CHECK(rig.model.Crc() == 0x61C264C4u); // default patch, as in the browser codec test
    CHECK(rig.session.StatusText().find("toy-piano") != std::string::npos);
    rig.Run(500);
    CHECK(rig.monitor_frames >= 5); // ~20 Hz after the subscription
    const int frames = rig.monitor_frames;
    const std::vector<uint8_t> other = MonitorFrameFrom(5); // another device ID on the port
    rig.session.OnMessage(other.data(), other.size());
    const std::vector<uint8_t> ours = MonitorFrameFrom(0);
    rig.session.OnMessage(ours.data(), ours.size());
    CHECK(rig.monitor_frames == frames + 1);
    CHECK(!rig.last_monitor.HasBase(0)); // Toy Piano v0.3 reports no base values
    std::printf("PASS connect: IDENTIFY, columns, allowed cells, CRC 61C264C4, monitor\n");
}

void TestEditsAndCoalescing() {
    Rig rig(toy_piano::MakeModInfo());
    CHECK(rig.Connect());
    rig.session.EditDepth(Cell(0, 0), 30);
    rig.session.EditDepth(Cell(5, 1), -50);
    CHECK(rig.session.Pending(Cell(0, 0)) && rig.session.Pending(Cell(5, 1)));
    CHECK(rig.RunUntilIdle());
    CHECK(rig.InSync());
    CHECK(rig.sim.Device().CurrentPatch().Active(0, 0) && rig.sim.Device().CurrentPatch().depth[5][1] == -50);
    CHECK(rig.Logged("Gerät bestätigt"));
    // A drag: 100 values within 100 ms reach the device as a few SET_CELLs, ending on the last value.
    const int before = rig.wire.set_cell;
    for(int i = 1; i <= 100; ++i) {
        rig.session.EditDepth(Cell(7, 1), i);
        rig.Run(1);
    }
    CHECK(rig.RunUntilIdle());
    CHECK(rig.sim.Device().CurrentPatch().depth[7][1] == 100);
    CHECK(rig.wire.set_cell - before < 30);
    CHECK(rig.InSync());
    // Switch off keeps the depth, on again.
    rig.session.EditDepth(Cell(5, 1), -50, false);
    CHECK(rig.RunUntilIdle());
    CHECK(!rig.sim.Device().CurrentPatch().Enabled(5, 1) && rig.sim.Device().CurrentPatch().depth[5][1] == -50);
    CHECK(rig.session.EditDepth(Cell(7, 3), 10) == Model::Edit::None); // greyed out: nothing sent
    CHECK(rig.InSync());
    std::printf("PASS edits: SET_CELL per cell, drag coalesced (%d frames for 100 values), on/off, greyed cells\n",
                rig.wire.set_cell - before);
}

void TestLimit() {
    Rig rig(toy_piano::MakeModInfo());
    CHECK(rig.Connect());
    const int cells[] = {Cell(0, 0), Cell(0, 1), Cell(0, 2), Cell(1, 0), Cell(1, 1), Cell(2, 2), Cell(2, 3), Cell(3, 4)};
    for(int c : cells)
        rig.session.EditDepth(c, 10);
    CHECK(rig.RunUntilIdle());
    CHECK(rig.model.ActiveCount() == 8 && rig.InSync());
    CHECK(rig.session.EditDepth(Cell(4, 4), 10) == Model::Edit::Limited);
    CHECK(rig.Logged("Höchstens 8"));
    CHECK(rig.RunUntilIdle());
    CHECK(!rig.model.Enabled(Cell(4, 4)) && rig.model.Depth(Cell(4, 4)) == 10 && rig.InSync());
    // Codex finding on PR #23: a zeroed but enabled cell is the 8th again, not a 9th.
    rig.session.EditDepth(Cell(3, 4), 0);
    CHECK(rig.model.ActiveCount() == 7 && rig.model.Enabled(Cell(3, 4)));
    CHECK(rig.session.EditDepth(Cell(3, 4), 20) == Model::Edit::Changed);
    CHECK(rig.RunUntilIdle());
    CHECK(rig.model.ActiveCount() == 8 && rig.InSync());
    std::printf("PASS limit: 9th connection stays off with a message, zeroed cell counts as the 8th\n");
}

void TestSourcesNameClear() {
    Rig rig(toy_piano::MakeModInfo());
    CHECK(rig.Connect());
    rig.session.EditSource(0, mod::ParamShape, mod::SampleHold);
    rig.session.EditSource(0, mod::ParamSync, mod::MidiClock);
    rig.session.EditSource(0, mod::ParamDivision, 6);
    rig.session.EditSource(mod::Random, mod::ParamShape, mod::Smooth);
    rig.session.EditSource(mod::Random, mod::ParamSync, mod::GateClock);
    rig.session.EditSource(mod::Random, mod::ParamGate, 1);
    for(int i = 0; i < 50; ++i)
        rig.session.EditSource(1, mod::ParamRate, unsigned(1000 + i * 100));
    CHECK(rig.model.patch.random.division == mod::kDefaultGateDivision);
    rig.session.EditName("Vibrato Test");
    CHECK(rig.RunUntilIdle());
    const mod::Patch& p = rig.sim.Device().CurrentPatch();
    CHECK(p.lfo[0].shape == mod::SampleHold && p.lfo[0].sync == mod::MidiClock && p.lfo[0].division == 6);
    CHECK(p.random.mode == mod::Smooth && p.random.sync == mod::GateClock && p.random.gate == 1);
    CHECK(p.random.division == mod::kDefaultGateDivision && p.lfo[1].rate == 5900);
    CHECK(std::string(p.name) == "Vibrato Test" && rig.InSync());
    rig.session.EditDepth(Cell(0, 0), 40);
    CHECK(rig.RunUntilIdle());
    rig.session.ClearMatrix();
    CHECK(rig.RunUntilIdle());
    CHECK(rig.sim.Device().CurrentPatch().ActiveMask() == 0 && rig.InSync());
    std::printf("PASS sources: shape, sync with default division, gate, rate, name, clear\n");
}

void TestUploadAndResync() {
    Rig rig(toy_piano::MakeModInfo());
    CHECK(rig.Connect());
    mod::Patch p{};
    p.depth[0][0] = 25;
    p.depth[7][1] = 80;
    p.SetEnabled(0, 0, true);
    p.SetEnabled(7, 1, true);
    p.lfo[1].sync = mod::GateClock;
    p.lfo[1].division = 0;
    std::snprintf(p.name, sizeof(p.name), "Upload");
    rig.session.Upload(p);
    CHECK(rig.RunUntilIdle());
    CHECK(rig.sim.Device().CurrentCrc() == mod::PatchCrc(p) && rig.InSync());
    // Codex finding on PR #23: the device refuses a value half way through; the editor
    // must end on the device's state, not on the file's.
    mod::Patch bad = p;
    bad.depth[4][4] = 40;
    bad.SetEnabled(4, 4, false);
    bad.lfo[1].shape = 9;
    rig.session.Upload(bad);
    CHECK(rig.RunUntilIdle());
    CHECK(rig.InSync());
    CHECK(rig.model.patch.depth[4][4] == 40); // SET_MATRIX did arrive
    CHECK(rig.model.patch.lfo[1].shape != 9);
    CHECK(rig.Logged("teilweise"));
    std::printf("PASS upload: SET_MATRIX + sources + name; refused value reloads the device state\n");
}

void TestDisconnect() {
    Rig rig(toy_piano::MakeModInfo());
    CHECK(rig.Connect());
    rig.wire.cut = true; // cable pulled: nothing answers any more
    rig.session.EditDepth(Cell(0, 0), 30);
    rig.Run(100);
    const int sent = rig.wire.frames;
    const std::size_t logs = rig.logs.size();
    rig.session.Disconnect("Gerät getrennt");
    rig.Run(3000);
    // Codex finding on PR #23: no retries to the closed port, no late handlers.
    CHECK(rig.wire.frames == sent);
    CHECK(rig.logs.size() == logs);
    CHECK(rig.session.State() == LinkState::Offline && rig.session.StatusText() == "Gerät getrennt");
    // Codex finding on PR #24: a monitor frame arriving after Disconnect (e.g. later in
    // the same MIDI batch) must not repopulate the live display.
    const int frames = rig.monitor_frames;
    const std::vector<uint8_t> late = MonitorFrameFrom(0);
    rig.session.OnMessage(late.data(), late.size());
    CHECK(rig.monitor_frames == frames);
    // Edits go on offline and reach the device after reconnecting via the CRC check.
    rig.session.EditDepth(Cell(1, 1), -20);
    rig.wire.cut = false;
    CHECK(rig.Connect());
    CHECK(rig.model.Depth(Cell(1, 1)) == 0 && rig.InSync()); // the device is the source of truth
    std::printf("PASS disconnect: open request cancelled, no late retries or handlers, reconnect\n");
}

void TestNoAnswer() {
    Rig rig(toy_piano::MakeModInfo());
    rig.sim.drop_requests = 1.0;
    rig.session.Connect(rig.wire, rig.now);
    rig.Run(2000);
    CHECK(rig.session.State() == LinkState::Offline);
    CHECK(rig.wire.frames == 1 + Session::kRetries);
    CHECK(rig.Logged("keine Antwort"));
    std::printf("PASS no answer: IDENTIFY sent %d times, then offline with a message\n", rig.wire.frames);
}

void TestFaults() {
    for(uint32_t seed : {3u, 11u, 29u}) {
        Rig rig(physical_modeling::MakeModInfo(), seed);
        CHECK(rig.Connect());
        rig.sim.drop_requests = 0.05;
        rig.sim.drop_replies = 0.05;
        rig.sim.corrupt_replies = 0.05;
        rig.sim.latency_ms = 3;
        uint32_t x = seed;
        auto rnd = [&x](uint32_t n) {
            x = x * 1664525u + 1013904223u;
            return (x >> 8) % n;
        };
        for(int i = 0; i < 400; ++i) {
            const int cell = int(rnd(64));
            switch(rnd(6)) {
                case 0: rig.session.EditSource(int(rnd(3)), mod::ParamRate, rnd(16384)); break;
                case 1: rig.session.EditSource(int(rnd(2)), mod::ParamShape, rnd(mod::kShapeCount)); break;
                case 2: rig.session.EditDepth(cell, rig.model.Depth(cell), !rig.model.Enabled(cell)); break;
                default: rig.session.EditDepth(cell, int(rnd(201)) - 100); break;
            }
            rig.Run(rnd(15));
        }
        rig.sim.drop_requests = rig.sim.drop_replies = rig.sim.corrupt_replies = 0;
        CHECK(rig.RunUntilIdle(60000));
        CHECK(rig.InSync()); // converged by itself (CRC check, resync on mismatch)
        rig.session.Pull(); // what the device has is what the editor shows
        CHECK(rig.RunUntilIdle());
        CHECK(rig.InSync());
        CHECK(rig.session.Connected());
        CHECK(rig.session.Retries() > 0);
        CHECK(rig.model.ActiveCount() <= 8);
        std::printf("PASS faults (seed %u): 400 edits with 5%% lost/corrupted frames, %u retries, device and "
                    "editor agree\n",
                    unsigned(seed), unsigned(rig.session.Retries()));
    }
}

void TestPhysicalModeling() {
    Rig rig(physical_modeling::MakeModInfo());
    rig.sim.bases = kSimInstruments[1].bases;
    rig.sim.EnableExtensions(kSimInstruments[1].extensions);
    rig.sim.bases[1] = 0.9f; // Structure pot near the top
    CHECK(rig.Connect());
    CHECK(rig.session.Device().key == "physical-modeling");
    CHECK(rig.model.columns[5].name == "Excitation" && rig.model.columns[5].latched);
    CHECK(!rig.model.Allowed(Cell(mod::PolyAt, 5)) && rig.model.Allowed(Cell(mod::PolyAt, 2)));
    rig.session.EditDepth(Cell(mod::PolyAt, 2), 10);
    CHECK(rig.RunUntilIdle() && rig.InSync());
    CHECK(rig.model.columns[6].name == "Level" && !rig.model.columns[6].latched);
    CHECK((rig.session.Device().caps & 0x70) == 0x70);
    // Base values (hardware test 2026-10-23: the pot positions decide where a modulation lands).
    rig.session.EditDepth(Cell(mod::ModWheel, 1), 50);
    rig.sim.Midi(0xB0, 1, 127); // wheel full: +0.5 on Structure
    CHECK(rig.RunUntilIdle());
    rig.Run(300);
    CHECK(rig.last_monitor.HasBase(1) && std::fabs(rig.last_monitor.base[1] - 0.9f) < 1e-3f);
    CHECK(rig.last_monitor.offset[1] > 0.45f && rig.last_monitor.Result(1) == 1.0f); // clipped at the top
    CHECK(std::fabs(rig.last_monitor.base[6] - 0.5f) < 1e-3f);
    std::printf("PASS physical-modeling: 8 columns, Poly AT per voice, Excitation latched\n");
}

// Hardware test 2026-10-24: the active model in the connection area and the current value
// in each column header instead of the fixed span.
void TestModeAndValueLabels() {
    Rig rig(physical_modeling::MakeModInfo());
    rig.sim.bases = kSimInstruments[1].bases;
    rig.sim.mode = kSimInstruments[1].mode;
    rig.sim.EnableExtensions(kSimInstruments[1].extensions);
    CHECK(rig.Connect());
    CHECK(rig.session.Device().Modes() && rig.session.Device().version == "0.1.0-mod4");
    rig.Run(300); // GET_MODE 0..5 after connecting, monitor frames
    const DeviceInfo& d = rig.session.Device();
    CHECK(d.modes.size() == 6 && d.modes[4].name == "Western Chords" && d.modes[4].hint == "Bank B, SW18 Mitte");
    CHECK(rig.last_monitor.mode == 0 && rig.last_monitor.mode_origin == 0);
    rig.sim.mode = 4;
    rig.sim.mode_origin = 1; // CC28
    rig.Run(200);
    CHECK(rig.last_monitor.mode == 4 && rig.last_monitor.mode_origin == 1);
    // Toy Piano v0.3: no capabilities 2, no GET_MODE, 36-byte frames without a mode.
    Rig toy(toy_piano::MakeModInfo());
    CHECK(toy.Connect());
    toy.Run(300);
    CHECK(!toy.session.Device().Modes() && toy.session.Device().modes.empty() && toy.last_monitor.mode == -1);

    // Value labels: unipolar 0..100 %, bipolar spans in their unit around 0.5.
    const auto& cols = rig.model.columns;
    CHECK(cols[1].ValueLabel(0.63f) == "63 %" && cols[1].ValueLabel(0.0f) == "0 %" && cols[1].ValueLabel(1.0f) == "100 %");
    CHECK(cols[0].ValueLabel(0.5f) == "+0.0 st" && cols[0].ValueLabel(0.75f) == "+12.0 st"
          && cols[0].ValueLabel(0.0f) == "-24.0 st"); // VR1: -24..+24 semitones
    CHECK(cols[6].ValueLabel(0.5f) == "+0 %" && cols[6].ValueLabel(1.0f) == "+100 %"); // Level: gain 1 = +0 %
    std::printf("PASS modes and value labels: 6 models with hints, origin MIDI, Toy Piano without modes, "
                "63 %% / +12.0 st / +0 %%\n");
}

void TestPatchFile() {
    Model m;
    m.columns[0].name = "Pitch";
    m.SetDepth(Cell(0, 0), 30);
    m.SetDepth(Cell(7, 1), -45);
    m.SetDepth(Cell(5, 1), 60, false);
    m.patch.lfo[0].sync = mod::MidiClock;
    m.patch.lfo[0].division = 6;
    m.patch.random.mode = mod::Smooth;
    m.SetName("Pad \"Drift\"");
    DeviceInfo d;
    d.key = "toy-piano";
    d.schema = 0x1234ABCD;
    const std::string text = SavePatch(m, &d);
    CHECK(text.find("\"schema_hash\": \"1234ABCD\"") != std::string::npos);
    LoadedPatch l = LoadPatch(text, m);
    CHECK(l.ok && l.instrument_key == "toy-piano");
    CHECK(mod::PatchCrc(l.patch) == m.Crc());

    // Exactly what the browser editor writes (JSON.stringify(doc, null, 2), abridged rows).
    const std::string browser = R"({
  "format": "mod-on-daisy-patch",
  "version": 1,
  "instrument_key": "toy-piano",
  "schema_hash": "0A1B2C3D",
  "name": "Vibrato Test",
  "columns": ["Pitch", "Level", "Harmonic", "Attack", "Decay", null, null, null],
  "depth": [
    [30, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, -50, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0],
    [0, 80, 0, 0, 0, 0, 0, 0]
  ],
  "enabled": [
    [true, false, false, false, false, false, false, false],
    [false, false, false, false, false, false, false, false],
    [false, false, false, false, false, false, false, false],
    [false, false, false, false, false, false, false, false],
    [false, false, false, false, false, false, false, false],
    [false, false, false, false, false, false, false, false],
    [false, false, false, false, false, false, false, false],
    [false, true, false, false, false, false, false, false]
  ],
  "lfo": [
    {"rate": 6000, "shape": 5, "sync": 1, "gate": 0, "division": 6, "retrigger": 0},
    {"rate": 6000, "shape": 0, "sync": 0, "gate": 0, "division": 4, "retrigger": 0}
  ],
  "random": {"rate": 8000, "mode": 1, "sync": 2, "gate": 1, "division": 2}
})";
    Model toy;
    l = LoadPatch(browser, toy);
    CHECK(l.ok);
    CHECK(l.patch.Active(0, 0) && l.patch.depth[0][0] == 30 && !l.patch.Enabled(5, 1) && l.patch.depth[5][1] == -50);
    CHECK(l.patch.lfo[0].shape == 5 && l.patch.lfo[0].division == 6 && l.patch.random.gate == 1);
    CHECK(std::string(l.patch.name) == "Vibrato Test");
    // Greyed-out cells are switched off for the connected instrument.
    toy.allowed &= ~Bit(Cell(7, 1));
    l = LoadPatch(browser, toy);
    CHECK(l.ok && !l.patch.Enabled(7, 1) && l.patch.depth[7][1] == 80);

    CHECK(!LoadPatch("{\"format\": \"other\", \"version\": 1}", toy).ok);
    // Codex findings on PR #24: incomplete files and out-of-range values are refused,
    // never turned into a default patch or wrapped into another value.
    l = LoadPatch("{\"format\": \"mod-on-daisy-patch\", \"version\": 1}", toy);
    CHECK(!l.ok && l.error.find("unvollständig") != std::string::npos);
    std::string short_grid = browser;
    short_grid.replace(short_grid.find("    [0, 0, 0, 0, 0, 0, 0, 0],\n"), 30, "");
    CHECK(!LoadPatch(short_grid, toy).ok);
    for(const char* bad : {"\"shape\": 256", "\"shape\": -1", "\"rate\": 65536", "\"rate\": 16384"}) {
        std::string s = browser;
        const std::string key = std::string(bad).substr(0, std::string(bad).find(':'));
        const std::size_t at = s.find(key + ": ");
        s.replace(at, s.find(',', at) - at, bad);
        l = LoadPatch(s, toy);
        CHECK(!l.ok);
        if(l.ok)
            std::fprintf(stderr, "  accepted %s\n", bad);
    }
    CHECK(!LoadPatch("{not json", toy).ok);
    std::string too_many = browser;
    too_many.replace(too_many.find("[false, false, false, false, false, false, false, false],"), 57,
                     "[true, true, true, true, true, true, true, true],");
    std::string depth_row = "[0, 0, 0, 0, 0, 0, 0, 0],";
    too_many.replace(too_many.find(depth_row), depth_row.size(), "[1, 1, 1, 1, 1, 1, 1, 1],");
    l = LoadPatch(too_many, Model{});
    CHECK(!l.ok && l.error.find("mehr als 8") != std::string::npos);
    std::string bad_shape = browser;
    bad_shape.replace(bad_shape.find("\"shape\": 5"), 10, "\"shape\": 9");
    l = LoadPatch(bad_shape, Model{});
    CHECK(!l.ok && l.error.find("ungültige") != std::string::npos);
    std::printf("PASS patch files: round trip, browser editor format, greyed cells off, refusals\n");
}
// Correction order 2026-10-24: extended monitor (F3), aftertouch (F6), LFO cross modulation.
void TestExtendedMonitor() {
    Rig rig(physical_modeling::MakeModInfo());
    rig.sim.EnableExtensions(kSimInstruments[1].extensions);
    CHECK(rig.Connect());
    CHECK(rig.session.Device().ExtendedMonitor() && rig.session.Device().LfoFm());
    CHECK(rig.model.patch.format == 2 && rig.InSync()); // the CRC covers the format
    // LFO 1 saw on MIDI clock 1/32 at 150 BPM = 20 Hz: the 20 Hz samples alias, the
    // window range does not (review: "20 Hz display range 0.000000").
    rig.session.EditDepth(Cell(mod::Lfo1, 1), 50);
    rig.session.EditSource(0, mod::ParamShape, mod::SawUp);
    rig.session.EditSource(0, mod::ParamSync, mod::MidiClock);
    rig.session.EditSource(0, mod::ParamDivision, 10);
    CHECK(rig.RunUntilIdle() && rig.InSync());
    rig.sim.Midi(0xFA, 0, 0);
    float sample_low = 2, sample_high = -2;
    int frames = rig.monitor_frames;
    for(int t = 0; t < 2000; ++t) {
        if(t % 17 == 0) // 24 PPQN at ~147 BPM
            rig.sim.Midi(0xF8, 0, 0);
        rig.Run(1);
        if(t > 500 && rig.monitor_frames != frames) {
            frames = rig.monitor_frames;
            sample_low = std::fmin(sample_low, rig.last_monitor.source[0]);
            sample_high = std::fmax(sample_high, rig.last_monitor.source[0]);
        }
    }
    const MonitorValues& m = rig.last_monitor;
    CHECK(m.has_range && m.max[0] - m.min[0] > 1.5f);
    CHECK(m.min[0] <= m.source[0] && m.source[0] <= m.max[0]);
    CHECK((m.clocks & 1) != 0); // MIDI clock running
    rig.Run(5000);             // clock stopped
    CHECK((rig.last_monitor.clocks & 1) == 0);
    CHECK(rig.last_monitor.max[0] - rig.last_monitor.min[0] < 0.01f); // LFO holds without clock
    rig.session.EditSource(0, mod::ParamSync, mod::Free); // back to free: the display runs again
    rig.session.EditSource(0, mod::ParamRate, HzToRate(2.0f));
    CHECK(rig.RunUntilIdle());
    rig.Run(500);
    CHECK(rig.last_monitor.max[0] - rig.last_monitor.min[0] > 0.1f);

    // Aftertouch: one combined display, the device says which kind arrived.
    rig.sim.Midi(0x90, 60, 100);
    rig.sim.Midi(0xA0, 60, 90);
    rig.Run(200);
    CHECK(rig.last_monitor.at_kind == 2 && rig.last_monitor.at_note == 60 && rig.last_monitor.at_value == 90);
    CHECK(rig.last_monitor.at_age < 127);
    rig.sim.Midi(0xD0, 50, 0);
    rig.Run(200);
    CHECK(rig.last_monitor.at_kind == 1 && rig.last_monitor.at_value == 50);
    rig.Run(3000);
    CHECK(rig.last_monitor.at_age == 127); // old after ~2.5 s
    std::printf("PASS extended monitor: window range on 20 Hz LFO, MIDI clock state, aftertouch kind/note/value\n");

    // Toy Piano v0.3: no extensions, 36/52-byte frames, editor degrades.
    Rig toy(toy_piano::MakeModInfo());
    CHECK(toy.Connect());
    CHECK(!toy.session.Device().ExtendedMonitor() && !toy.session.Device().LfoFm());
    CHECK(toy.model.patch.format == 1 && toy.InSync());
    toy.Run(300);
    CHECK(toy.monitor_frames > 0 && !toy.last_monitor.has_range && toy.last_monitor.at_kind == 0);
    std::printf("PASS extended monitor: Toy Piano v0.3 without extensions stays format 1, no range data\n");
}

void TestLfoFm() {
    Rig rig(physical_modeling::MakeModInfo());
    rig.sim.EnableExtensions(kSimInstruments[1].extensions);
    CHECK(rig.Connect());
    rig.session.EditSource(0, mod::ParamFm, unsigned(40 + mod::kMaxFm));
    rig.session.EditSource(1, mod::ParamFm, unsigned(-25 + mod::kMaxFm));
    CHECK(rig.RunUntilIdle() && rig.InSync());
    CHECK(rig.sim.Device().CurrentPatch().lfo[0].fm == 40 && rig.sim.Device().CurrentPatch().lfo[1].fm == -25);
    rig.model.patch.lfo[0].fm = 0; // forget locally; GET_SOURCE must bring it back
    rig.session.Pull();
    CHECK(rig.RunUntilIdle() && rig.model.patch.lfo[0].fm == 40 && rig.InSync());
    // Upload carries fm; a file for a device without FM loses it, the CRCs still agree.
    mod::Patch p = rig.model.patch;
    p.lfo[1].fm = 70;
    rig.session.Upload(p);
    CHECK(rig.RunUntilIdle() && rig.InSync() && rig.sim.Device().CurrentPatch().lfo[1].fm == 70);
    Rig toy(toy_piano::MakeModInfo());
    CHECK(toy.Connect());
    toy.session.Upload(p);
    CHECK(toy.RunUntilIdle() && toy.InSync());
    CHECK(toy.model.patch.format == 1 && toy.model.patch.lfo[1].fm == 0);
    std::printf("PASS LFO FM: SET_SOURCE fm, GET_SOURCE read back, upload, dropped for Toy Piano v0.3\n");
}

// Review finding F5: silence must show, and recovery must reload the device state.
void TestHealth() {
    Rig rig(physical_modeling::MakeModInfo());
    rig.sim.EnableExtensions(kSimInstruments[1].extensions);
    CHECK(rig.Connect());
    rig.Run(300);
    CHECK(rig.session.LinkHealth() == Health::Ok);
    rig.sim.drop_replies = 1; // e.g. Studio One took the input
    rig.Run(1600);
    CHECK(rig.session.LinkHealth() == Health::NoLiveData);
    CHECK(rig.session.StatusText().find("keine Live-Daten") != std::string::npos);
    rig.Run(3600);
    CHECK(rig.session.LinkHealth() == Health::NoReplies);
    CHECK(rig.session.StatusText().find("keine Antwort") != std::string::npos);
    CHECK(rig.Logged("Keine Antwort vom Gerät seit 5 s"));
    rig.session.EditDepth(Cell(mod::Lfo1, 1), 55);
    rig.Run(5000);
    int timeouts = 0;
    for(const auto& l : rig.logs)
        timeouts += l.second.find("keine Antwort") != std::string::npos ? 1 : 0;
    CHECK(timeouts <= 2); // repeats within 3 s are shown once
    rig.sim.drop_replies = 0;
    CHECK(rig.RunUntilIdle());
    rig.Run(500);
    CHECK(rig.session.LinkHealth() == Health::Ok && rig.Logged("antwortet wieder"));
    CHECK(rig.RunUntilIdle() && rig.InSync());

    // A MIDI output that refuses to send ends the session instead of pretending.
    Rig dead(toy_piano::MakeModInfo());
    CHECK(dead.Connect());
    struct Refuse : Transport {
        bool Send(const uint8_t*, std::size_t) override { return false; }
    } refuse;
    dead.session.Connect(refuse, dead.now);
    dead.Run(50);
    CHECK(dead.session.State() == LinkState::Offline && dead.Logged("verweigert"));
    std::printf("PASS health: no live data after 1.5 s, no replies after 5 s, deduplicated log, reload on "
                "recovery, refused send disconnects\n");
}

// Review finding F7: what the transfer of zeros, clears and whole patches must do.
void TestTransferRegressions() {
    Rig rig(physical_modeling::MakeModInfo());
    rig.sim.EnableExtensions(kSimInstruments[1].extensions);
    CHECK(rig.Connect());
    rig.session.EditDepth(Cell(mod::Lfo1, 1), 50);
    CHECK(rig.RunUntilIdle() && rig.InSync());
    int before = rig.wire.set_cell;
    rig.session.EditDepth(Cell(mod::Lfo1, 1), 0); // +50 -> 0 is sent, not swallowed
    CHECK(rig.RunUntilIdle() && rig.InSync() && rig.wire.set_cell > before);
    CHECK(!rig.sim.Device().CurrentPatch().Active(0, 1));
    rig.Run(100);
    CHECK(rig.last_monitor.offset[1] == 0.0f);
    before = rig.wire.set_cell;
    rig.session.EditDepth(Cell(mod::Lfo1, 1), 0); // 0 -> 0: nothing to send
    rig.Run(100);
    CHECK(rig.wire.set_cell == before);
    rig.session.EditDepth(Cell(mod::Lfo1, 1), 0); // zero, then another cell
    rig.session.EditDepth(Cell(mod::Lfo2, 2), -20);
    CHECK(rig.RunUntilIdle() && rig.InSync() && rig.sim.Device().CurrentPatch().depth[1][2] == -20);
    // Zero while the previous value is still on the way.
    rig.session.EditDepth(Cell(mod::Lfo2, 2), 60);
    rig.Run(1);
    rig.session.EditDepth(Cell(mod::Lfo2, 2), 0);
    CHECK(rig.RunUntilIdle() && rig.InSync() && !rig.sim.Device().CurrentPatch().Active(1, 2));

    mod::Patch file;
    file.format = 2;
    file.depth[0][1] = 40;
    file.SetEnabled(0, 1, true);
    file.lfo[0].rate = 12345;
    file.lfo[0].fm = 30;
    std::snprintf(file.name, sizeof(file.name), "FilePatch");
    rig.session.Upload(file); // matrix, sources incl. fm and name
    CHECK(rig.RunUntilIdle() && rig.InSync() && rig.sim.Device().CurrentCrc() == mod::PatchCrc(file));
    rig.session.ClearMatrix(); // keeps sources and name
    CHECK(rig.RunUntilIdle() && rig.InSync());
    const mod::Patch cleared = rig.sim.Device().CurrentPatch();
    CHECK(cleared.ActiveMask() == 0 && cleared.EnabledMask() == 0 && cleared.lfo[0].rate == 12345);
    CHECK(cleared.lfo[0].fm == 30 && std::string(cleared.name) == "FilePatch");
    mod::Patch empty;
    empty.format = 2;
    rig.session.Upload(empty); // the default patch, all zeros, is transferred completely
    CHECK(rig.RunUntilIdle() && rig.InSync() && rig.sim.Device().CurrentCrc() == mod::PatchCrc(empty));

    // Partial refusal: Poly AT x Excitation is not allowed; the device state wins.
    mod::Patch refused = file;
    refused.depth[mod::PolyAt][5] = 50;
    refused.SetEnabled(mod::PolyAt, 5, true);
    rig.session.Upload(refused);
    CHECK(rig.RunUntilIdle() && rig.InSync());
    CHECK(!rig.sim.Device().CurrentPatch().Active(mod::PolyAt, 5));

    // No answer during a transfer: nothing is reported as confirmed.
    rig.sim.drop_replies = 1;
    rig.session.Upload(file);
    rig.Run(3000);
    CHECK(!rig.Logged("FilePatch bestätigt"));
    rig.sim.drop_replies = 0;
    CHECK(rig.RunUntilIdle());
    rig.Run(300);
    CHECK(rig.RunUntilIdle() && rig.InSync());

    // Offline load, then connect: the device patch replaces the offline one.
    rig.session.Disconnect("");
    rig.session.Upload(file);
    CHECK(rig.model.Crc() == mod::PatchCrc(file));
    CHECK(rig.Connect() && rig.InSync());
    std::printf("PASS transfer regressions: +50->0, 0->0, zero then other cell, zero in flight, upload with "
                "fm, clear keeps sources/name, default patch, partial refusal, no answer, offline load\n");
}

// Review finding F1: a file whose columns mean something else must not load silently.
void TestCompatibility() {
    Rig rig(physical_modeling::MakeModInfo());
    rig.sim.EnableExtensions(kSimInstruments[1].extensions);
    CHECK(rig.Connect());
    const DeviceInfo& device = rig.session.Device();

    // The review's legacy-reverb.modpatch: mod1 schema, ModWheel -> Reverb -100.
    auto legacy_info = physical_modeling::MakeModInfo();
    legacy_info.targets[6] = {"Reverb", "100%", 1.0f, false, false};
    Model legacy = rig.model;
    legacy.patch = mod::Patch(); // "= mod::Patch{}" trips an internal error in GCC here
    legacy.columns[6].name = "Reverb";
    legacy.SetDepth(Cell(mod::ModWheel, 6), -100);
    legacy.SetDepth(Cell(mod::Lfo1, 1), 30);
    DeviceInfo legacy_device = device;
    legacy_device.schema = mod::SchemaHash(legacy_info);
    const std::string text = SavePatch(legacy, &legacy_device);
    LoadedPatch f = LoadPatch(text, rig.model);
    CHECK(f.ok && f.schema == Hex32(legacy_device.schema) && f.columns[6] == "Reverb");
    Compatibility c = CheckCompatibility(f, rig.model, device);
    CHECK(c.schema_changed && c.NeedsDecision());
    CHECK(c.affected == Bit(Cell(mod::ModWheel, 6))); // Structure kept its meaning
    CHECK(!c.notes.empty() && c.notes[0].find("Reverb") != std::string::npos
          && c.notes[0].find("Level") != std::string::npos);
    const mod::Patch safe = SafePart(f.patch, c);
    CHECK(!safe.Enabled(mod::ModWheel, 6) && safe.depth[mod::ModWheel][6] == -100 && safe.Active(mod::Lfo1, 1));
    rig.session.Upload(safe);
    CHECK(rig.RunUntilIdle() && rig.InSync() && !rig.sim.Device().CurrentPatch().Active(mod::ModWheel, 6));

    // Same schema: no question.
    f = LoadPatch(SavePatch(rig.model, &device), rig.model);
    CHECK(!CheckCompatibility(f, rig.model, device).NeedsDecision());
    // No metadata (hand-written or very old file): active columns are unverifiable.
    std::string bare = text;
    bare.replace(bare.find("\"schema_hash\""), bare.find('\n', bare.find("\"schema_hash\"")) - bare.find("\"schema_hash\""),
                 "\"schema_hash\": null,");
    bare.replace(bare.find("\"columns\""), bare.find('\n', bare.find("\"columns\"")) - bare.find("\"columns\""),
                 "\"columns\": [null, null, null, null, null, null, null, null],");
    f = LoadPatch(bare, rig.model);
    c = CheckCompatibility(f, rig.model, device);
    CHECK(f.ok && c.schema_unknown && c.affected == (Bit(Cell(mod::ModWheel, 6)) | Bit(Cell(mod::Lfo1, 1))));
    // Another instrument: everything is affected.
    Model toy_model;
    toy_model.columns[0].name = "Pitch";
    toy_model.SetDepth(Cell(0, 0), 20);
    DeviceInfo toy_device;
    toy_device.key = "toy-piano";
    f = LoadPatch(SavePatch(toy_model, &toy_device), rig.model);
    c = CheckCompatibility(f, rig.model, device);
    CHECK(c.key_mismatch && c.affected == Bit(Cell(0, 0)));
    // Saved offline after an offline load: the file's origin is kept, not lost.
    Model offline;
    f = LoadPatch(text, offline);
    offline.patch = f.patch;
    offline.origin = f.Origin();
    const LoadedPatch again = LoadPatch(SavePatch(offline, nullptr), offline);
    CHECK(again.schema == f.schema && again.columns[6] == "Reverb" && again.instrument_key == f.instrument_key);
    // FM in a file for a device without it.
    Rig toy(toy_piano::MakeModInfo());
    CHECK(toy.Connect());
    Model fm_model = rig.model;
    fm_model.patch.lfo[0].fm = 50;
    DeviceInfo pm_as_toy = toy.session.Device();
    f = LoadPatch(SavePatch(fm_model, &pm_as_toy), toy.model);
    c = CheckCompatibility(f, toy.model, toy.session.Device());
    CHECK(f.has_fm && c.fm_unsupported && SafePart(f.patch, c).lfo[0].fm == 0);
    std::printf("PASS compatibility: legacy Reverb file (%s -> %s) flags only Level, safe load, same schema, "
                "no metadata, other instrument, offline origin, FM unsupported\n",
                Hex32(legacy_device.schema).c_str(), Hex32(device.schema).c_str());
}
// Acceptance of order item 4 (pot 0.1 / 0.5 / 0.9 with about +-0.5 LFO) and item 6
// (aftertouch kinds, zero messages, first of two held notes, note off, all notes off).
void TestGoalSpanAndAftertouch() {
    const float expect[][3] = {{0.1f, 0.0f, 0.6f}, {0.5f, 0.0f, 1.0f}, {0.9f, 0.4f, 1.0f}};
    for(const auto& e : expect) {
        Rig rig(physical_modeling::MakeModInfo());
        rig.sim.EnableExtensions(kSimInstruments[1].extensions);
        rig.sim.bases = kSimInstruments[1].bases;
        rig.sim.bases[1] = e[0];
        CHECK(rig.Connect());
        rig.session.EditDepth(Cell(mod::Lfo1, 1), 50);
        rig.session.EditSource(0, mod::ParamRate, HzToRate(20.0f)); // a full cycle per window
        CHECK(rig.RunUntilIdle());
        rig.Run(500);
        const GoalSpan g = GoalSpanOf(rig.model, rig.last_monitor, 1);
        CHECK(g.has_base && std::fabs(g.lo - e[1]) < 0.03f && std::fabs(g.hi - e[2]) < 0.03f);
        CHECK(g.clipped_low == (e[0] < 0.5f) && g.clipped_high == (e[0] > 0.5f) && !g.voice_part);
        CHECK(g.lo <= rig.last_monitor.Result(1) + 1e-4f && rig.last_monitor.Result(1) <= g.hi + 1e-4f);
        if(!(std::fabs(g.lo - e[1]) < 0.03f && std::fabs(g.hi - e[2]) < 0.03f))
            std::fprintf(stderr, "  pot %.1f: %.3f .. %.3f\n", double(e[0]), double(g.lo), double(g.hi));
    }

    Rig rig(physical_modeling::MakeModInfo());
    rig.sim.EnableExtensions(kSimInstruments[1].extensions);
    CHECK(rig.Connect());
    rig.Run(100);
    CHECK(rig.last_monitor.at_kind == 0); // nothing received yet, matrix empty
    rig.sim.Midi(0xD0, 0, 0);            // a zero message also decides the kind
    rig.Run(100);
    CHECK(rig.last_monitor.at_kind == 1 && rig.last_monitor.at_value == 0);
    rig.sim.Midi(0x90, 60, 100);
    rig.sim.Midi(0x90, 64, 100);
    rig.sim.Midi(0xA0, 60, 127); // pressure on the first of two held notes
    rig.Run(100);
    CHECK(rig.last_monitor.at_kind == 2 && rig.last_monitor.at_note == 60 && rig.last_monitor.at_value == 127);
    CHECK(rig.last_monitor.source[mod::PolyAt] < 0.01f); // the row keeps rule N1 (last note 64)
    rig.sim.Midi(0x80, 60, 0); // released: no stale pressure
    rig.Run(100);
    CHECK(rig.last_monitor.at_kind == 2 && rig.last_monitor.at_value == 0);
    rig.sim.Midi(0xA0, 64, 80);
    rig.sim.Midi(0xD0, 30, 0); // both in one tick: the last one received decides
    rig.Run(100);
    CHECK(rig.last_monitor.at_kind == 1 && rig.last_monitor.at_value == 30);
    rig.sim.Midi(0xA0, 64, 80);
    rig.Run(100);
    CHECK(rig.last_monitor.at_kind == 2 && rig.last_monitor.at_value == 80);
    rig.sim.Midi(0xB0, 123, 0); // All Notes Off
    rig.Run(100);
    CHECK(rig.last_monitor.at_value == 0);
    // A selected Channel AT cell stays the edited cell while the shown kind changes.
    rig.model.selected = Cell(mod::ChannelAt, 2);
    rig.session.EditDepth(rig.model.selected, 20);
    rig.sim.Midi(0xA0, 64, 50);
    rig.Run(50);
    rig.session.EditDepth(rig.model.selected, 25);
    CHECK(rig.RunUntilIdle() && rig.InSync() && rig.model.selected == Cell(mod::ChannelAt, 2));
    CHECK(rig.sim.Device().CurrentPatch().depth[mod::ChannelAt][2] == 25 && !rig.model.Active(Cell(mod::PolyAt, 2)));
    std::printf("PASS goal span (pot 0.1/0.5/0.9 +-0.5 -> 0..0.6 / 0..1 / 0.4..1, clipped) and aftertouch "
                "(zero message, first of two notes, note off, both kinds, all notes off, editing stays put)\n");
}
}


int main() {
    TestConnect();
    TestEditsAndCoalescing();
    TestLimit();
    TestSourcesNameClear();
    TestUploadAndResync();
    TestDisconnect();
    TestNoAnswer();
    TestFaults();
    TestPhysicalModeling();
    TestModeAndValueLabels();
    TestPatchFile();
    TestExtendedMonitor();
    TestLfoFm();
    TestHealth();
    TestTransferRegressions();
    TestCompatibility();
    TestGoalSpanAndAftertouch();
    if(failures) {
        std::fprintf(stderr, "FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: editor core against the firmware controller\n");
}
