// Host test of the editor's SQ client and Acid Seq patch (ARBEITSAUFTRAG Phase 5) against
// the firmware's own SQ device (SqSimulator wraps sequencers/step-seq/.../device.h).
// No JUCE. make -C editors/mod-on-daisy test
#include "../src/core/AcidPatch.h"
#include "../src/core/SqSimulator.h"
#include <cstdio>
#include <deque>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

using Frame = std::vector<uint8_t>;

// Editor <-> simulated device with optional loss of the next n frames in either direction.
struct Link : editor::Transport {
    editor::SqSimulator sim;
    std::deque<Frame> to_device, to_editor;
    std::vector<Frame> editor_sent;
    int drop_to_device = 0, drop_to_editor = 0;
    explicit Link(const std::string& product = "acid-seq") : sim(product) {
        sim.SetOut([this](const uint8_t* b, std::size_t n) {
            if(drop_to_editor > 0)
                --drop_to_editor;
            else
                to_editor.emplace_back(b, b + n);
        });
    }
    bool Send(const uint8_t* b, std::size_t n) override {
        editor_sent.emplace_back(b, b + n);
        if(drop_to_device > 0)
            --drop_to_device;
        else
            to_device.emplace_back(b, b + n);
        return true;
    }
};

static void Pump(Link& link, editor::SqSession& session, double& now, int ms) {
    for(int i = 0; i < ms; ++i) {
        while(!link.to_device.empty()) {
            Frame f = link.to_device.front();
            link.to_device.pop_front();
            link.sim.Receive(f.data(), f.size(), uint32_t(now));
        }
        link.sim.Advance(uint32_t(now));
        while(!link.to_editor.empty()) {
            Frame f = link.to_editor.front();
            link.to_editor.pop_front();
            session.OnMessage(f.data(), f.size(), now);
        }
        session.Tick(now);
        now += 1;
    }
}

static void TestDispatcher() {
    const uint8_t md[] = {0xF0, 0x7D, 0x4D, 0x44, 1, 0, 0x41, 0, 0, 0, 0xF7};
    const uint8_t sqf[] = {0xF0, 0x7D, 0x53, 0x51, 1, 0x41, 0, 0, 0, 0, 0xF7};
    const uint8_t bl[] = {0xF0, 0x7D, 0x42, 0x4C, 1, 0x41, 0xF7};
    const uint8_t other[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    CHECK(editor::ClassifySysEx(md, sizeof(md)) == editor::Protocol::ModOnDaisy);
    CHECK(editor::ClassifySysEx(sqf, sizeof(sqf)) == editor::Protocol::Sq);
    CHECK(editor::ClassifySysEx(bl, sizeof(bl)) == editor::Protocol::Bootloader);
    CHECK(editor::ClassifySysEx(other, sizeof(other)) == editor::Protocol::Unknown);
}

static void TestReadAndEdit() {
    Link link;
    editor::SqSession s(link);
    double now = 0;
    int changes = 0;
    s.changed = [&] { ++changes; };
    s.Connect(now);
    Pump(link, s, now, 400);
    CHECK(s.Ready());
    // Device first (D13): reading the device writes nothing.
    CHECK(link.sim.Writes() == 0);
    for(const Frame& f : link.editor_sent)
        CHECK(f[5] < 0x06 || (f[5] >= 0x10 && f[5] != 0x11 && f[5] != 0x12 && f[5] != 0x20 && f[5] != 0x29 && f[5] != 0x2A));
    CHECK(s.Identity().product == "acid-seq" && s.Components().size() == 4);
    const int seq = s.ComponentIndex("step-seq"), voice = s.ComponentIndex("acid-303"),
              drive = s.ComponentIndex("drive"), delay = s.ComponentIndex("stereo-delay");
    CHECK(seq == 0 && voice == 1 && drive == 2 && delay == 3);
    const editor::SqParam* tempo = s.Components()[0].Find("tempo");
    CHECK(tempo && tempo->Value() == 120.0 && tempo->min == 30000);
    const editor::SqParam* clock = s.Components()[0].Find("clock_source");
    CHECK(clock && clock->enum_keys.size() == 4 && clock->enum_keys[3] == "auto");
    CHECK(s.Components()[0].Find("play_mode")->Readonly());
    CHECK(s.Components()[3].Find("note_value")->enum_keys.size() == 18);
    CHECK(s.Components()[1].Find("cutoff")->Log() && s.Components()[1].Find("cutoff")->cc == 74);
    CHECK(s.Pattern()[0].note == link.sim.Pattern()[0].note && !s.Pattern()[3].gate);
    CHECK(s.State().state == 2 && s.State().sw17 == 1);
    CHECK(!s.Cal(0).valid);
    // Edits: one request each, confirmed by the device.
    bool ok = false;
    CHECK(s.SetParam(std::size_t(voice), "cutoff", 2500, [&](bool r, uint8_t) { ok = r; }));
    Pump(link, s, now, 5);
    CHECK(ok && link.sim.Value(1, 4) == 2500000 && s.Components()[1].Find("cutoff")->Value() == 2500.0);
    CHECK(!s.SetParam(std::size_t(seq), "play_mode", 1)); // read-only: follows SW18
    s.SetParam(std::size_t(seq), "length", 9.6);           // rounded by the device
    Pump(link, s, now, 5);
    CHECK(s.Components()[0].Find("length")->Value() == 10.0);
    step_seq::Step st;
    st.note = 59;
    st.gate_percent = 100;
    st.accent = st.slide = true;
    s.SetStep(5, st);
    Pump(link, s, now, 5);
    CHECK(link.sim.Pattern()[5] == st && s.Pattern()[5] == st);
    // Transport: stop, then start (PAUSE -> running).
    s.StartStop(false);
    Pump(link, s, now, 5);
    CHECK(link.sim.Device().view.state == 3);
    s.StartStop(true);
    Pump(link, s, now, 5);
    CHECK(link.sim.Device().view.state == 2);
    // Calibration bound to the UID, then the separate enable.
    editor::SqCal cal;
    cal.offset = 2048000;
    cal.scale = -409600;
    cal.gate_low = 2048;
    cal.gate_high = 410;
    bool refused = true;
    s.CvEnable(true, [&](bool r, uint8_t err) { refused = !r && err == 8; });
    Pump(link, s, now, 5);
    CHECK(refused && !link.sim.CvEnabled());
    s.SetCal(0, cal);
    s.SetCal(1, cal);
    s.CvEnable(true);
    Pump(link, s, now, 10);
    CHECK(link.sim.CvEnabled() && s.State().cv_enabled && s.Cal(1).valid);
    // Monitor: step changes and kit edits arrive without asking.
    const unsigned frames = s.MonitorFrames();
    link.sim.TurnKnob(0, 1, 140000); // VR5
    Pump(link, s, now, 600);
    CHECK(s.MonitorFrames() > frames + 3 && s.State().step >= 0);
    CHECK(s.Components()[0].Find("tempo")->Value() == 140.0);
    // Stats.
    s.ReadStats();
    Pump(link, s, now, 5);
    CHECK(s.Stats().size() == sq::kCounters);
    CHECK(changes > 10);
}

static void TestLossAndRestart() {
    Link link("step-seq");
    editor::SqSession s(link);
    double now = 0;
    // A lost request and a lost reply: retried with the same request ID; the device
    // answers the retry from its cache.
    link.drop_to_device = 1;
    s.Connect(now);
    Pump(link, s, now, 600);
    CHECK(s.Ready() && link.editor_sent[0] == link.editor_sent[1]);
    link.drop_to_editor = 1;
    s.SetParam(0, "swing", 60);
    Pump(link, s, now, 300);
    CHECK(s.Components()[0].Find("swing")->Value() == 60.0 && link.sim.Device().counters[sq::CntDuplicate] >= 1);
    CHECK(link.sim.Writes() == 1); // executed once
    // Restart detection by the boot ID.
    link.sim.Restart();
    s.Pull();
    Pump(link, s, now, 400);
    CHECK(s.Restarted());
    // A device that never answers: failed after three retries.
    link.drop_to_device = 1000;
    editor::SqSession t(link);
    t.Connect(now);
    Pump(link, t, now, 1200);
    CHECK(t.GetPhase() == editor::SqSession::Phase::Failed && t.Lost() == 1);
    // Broken and stale frames are ignored and counted.
    link.drop_to_device = 0;
    const unsigned ignored = s.Ignored();
    Frame stale = link.editor_sent.back();
    stale[5] |= sq::kResponse;
    s.OnMessage(stale.data(), stale.size(), now);
    CHECK(s.Ignored() == ignored + 1);
}

static void TestPatch() {
    Link link;
    editor::SqSession s(link);
    double now = 0;
    s.Connect(now);
    Pump(link, s, now, 400);
    s.SetParam(1, "resonance", 87.5);
    step_seq::Step st;
    st.note = 33;
    st.slide = true;
    s.SetStep(2, st);
    Pump(link, s, now, 10);
    // Save, parse back, identical.
    const editor::AcidPatch saved = editor::PatchFromSession(s);
    const std::string text = editor::SerializeAcidPatch(saved);
    std::string error;
    auto loaded = editor::ParseAcidPatch(text, error);
    CHECK(loaded && loaded->components.size() == 4 && loaded->has_pattern);
    CHECK(loaded->components[1].params.at("resonance") == 87.5 && loaded->pattern[2] == st);
    CHECK(loaded->components[0].params.count("play_mode") == 0); // read-only values are not stored
    CHECK(editor::SerializeAcidPatch(*loaded) == text);
    // Offline: a patch loads without any device.
    editor::Transport* none = &link;
    editor::SqSession offline(*none);
    CHECK(editor::ParseAcidPatch(text, error).has_value());
    // Upload on an explicit action only, to another kit with changed values.
    Link other;
    editor::SqSession o(other);
    double t2 = 0;
    o.Connect(t2);
    Pump(other, o, t2, 400);
    CHECK(other.sim.Writes() == 0);
    CHECK(editor::CheckAcidPatch(*loaded, o).Clean());
    const int queued = editor::UploadAcidPatch(*loaded, o);
    Pump(other, o, t2, 200);
    CHECK(queued > 40 && o.Idle());
    CHECK(other.sim.Value(1, 5) == 87500 && other.sim.Pattern()[2] == st);
    // Missing and incompatible components are listed and skipped.
    Link fx("drive-fx");
    editor::SqSession f(fx);
    double t3 = 0;
    f.Connect(t3);
    Pump(fx, f, t3, 400);
    editor::AcidPatch changed = *loaded;
    changed.components[2].schema = 9;
    changed.components[2].params["drive"] = 77;
    const editor::AcidCompat compat = editor::CheckAcidPatch(changed, f);
    CHECK(compat.missing.size() == 3 && compat.incompatible.size() == 1 && compat.pattern_skipped);
    CHECK(editor::UploadAcidPatch(changed, f) == 0);
    // Broken files are refused with a reason.
    CHECK(!editor::ParseAcidPatch("{\"format\": \"modpatch\"}", error) && !error.empty());
    CHECK(!editor::ParseAcidPatch("{\"format\": \"acid-seq-patch\", \"format_version\": 99}", error));
    CHECK(!editor::ParseAcidPatch("not json", error));
    std::string bad = text;
    bad.replace(bad.find("\"note\": 33"), 10, "\"note\": 70");
    CHECK(!editor::ParseAcidPatch(bad, error));
}

int main() {
    TestDispatcher();
    TestReadAndEdit();
    TestLossAndRestart();
    TestPatch();
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: SQ client against the firmware device (read-only connect, descriptors, enum labels, "
                "fragments, edits, transport, calibration/enable, monitor, retries, restart), Acid Seq patch "
                "(round trip, offline, compatibility, explicit upload)\n");
    return 0;
}
