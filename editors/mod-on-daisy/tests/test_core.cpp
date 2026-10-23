// Host tests of the editor core (src/core) against the firmware's Controller and
// Engine (GUI.md § 3: "core/ wird mit einem simulierten Gerät getestet"). No JUCE.
//   make -C editors/mod-on-daisy test
#include "../src/core/PatchFile.h"
#include "../src/core/SimInstruments.h"
#include "../src/core/Simulator.h"
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
    CHECK(rig.Connect());
    CHECK(rig.session.Device().key == "physical-modeling");
    CHECK(rig.model.columns[5].name == "Excitation" && rig.model.columns[5].latched);
    CHECK(!rig.model.Allowed(Cell(mod::PolyAt, 5)) && rig.model.Allowed(Cell(mod::PolyAt, 2)));
    rig.session.EditDepth(Cell(mod::PolyAt, 2), 10);
    CHECK(rig.RunUntilIdle() && rig.InSync());
    std::printf("PASS physical-modeling: 8 columns, Poly AT per voice, Excitation latched\n");
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
    TestPatchFile();
    if(failures) {
        std::fprintf(stderr, "FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: editor core against the firmware controller\n");
}
