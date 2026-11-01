// Host test of the editor's Streichwurst path (SQ 1.1 + Mod on Daisy on one port) against
// the firmware's own SQ device, parameter register and Mod on Daisy controller
// (StreichwurstSim). No JUCE. make -C editors/mod-on-daisy test
//
// Covered: connect reads first, identity/components/descriptors, no pattern requests,
// SET_PARAM with ACK and readback, rasterized values, read-only refusal, kit changes in
// the monitor, stale and duplicate replies, lost replies with an idempotent retry,
// reconnect after a restart, the MD session alongside on the same link, matrix edits that
// leave the register untouched, the .swpatch with embedded matrix, compatibility, and
// unchanged Acid Seq behaviour.
#include "../src/core/AcidPatch.h"
#include "../src/core/PatchFile.h"
#include "../src/core/SqSimulator.h"
#include "../src/core/StreichwurstSim.h"
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
using namespace editor;

// One USB port: the editor's SQ and MD sessions share it; frames can be lost or replayed.
struct Port : Transport {
    StreichwurstSim sim;
    std::deque<Frame> to_device, to_editor;
    std::vector<Frame> editor_sent, device_sent;
    int drop_to_editor = 0;
    Port() {
        sim.SetOut([this](const uint8_t* b, std::size_t n) {
            device_sent.emplace_back(b, b + n);
            if(drop_to_editor > 0)
                --drop_to_editor;
            else
                to_editor.emplace_back(b, b + n);
        });
    }
    bool Send(const uint8_t* b, std::size_t n) override {
        editor_sent.emplace_back(b, b + n);
        to_device.emplace_back(b, b + n);
        return true;
    }
    int SentOfType(uint8_t type) const {
        int count = 0;
        for(const Frame& f : editor_sent)
            count += f.size() > 6 && f[2] == 0x53 && f[5] == type;
        return count;
    }
};

struct Rig {
    Port port;
    SqSession sq{port};
    Model model;
    Session md{model, 9};
    double now = 1000;
    void Run(int ms) {
        for(int i = 0; i < ms; ++i) {
            while(!port.to_device.empty()) {
                Frame f = port.to_device.front();
                port.to_device.pop_front();
                port.sim.Receive(f.data(), f.size(), uint32_t(now));
            }
            port.sim.Advance(uint32_t(now));
            while(!port.to_editor.empty()) {
                Frame f = port.to_editor.front();
                port.to_editor.pop_front();
                // The editor's dispatcher: SQ to the SQ session, the rest to Mod on Daisy.
                if(ClassifySysEx(f.data(), f.size()) == Protocol::Sq)
                    sq.OnMessage(f.data(), f.size(), now);
                else
                    md.OnMessage(f.data(), f.size());
            }
            sq.Tick(now);
            md.Tick(uint32_t(now));
            now += 1;
        }
    }
    const SqParam* Param(const char* component, const char* key) {
        const int c = sq.ComponentIndex(component);
        return c < 0 ? nullptr : sq.Components()[std::size_t(c)].Find(key);
    }
};

static void TestConnectReadsFirst(Rig& r) {
    r.sq.Connect(r.now);
    r.Run(1500);
    CHECK(r.sq.Ready());
    const SqIdentity& id = r.sq.Identity();
    CHECK(id.product == "streichwurst" && id.version == "0.1.0");
    CHECK(id.protocol_major == 1 && id.protocol_minor == 1);
    CHECK(id.HasModMatrix() && !id.HasPattern() && !id.HasCalibration());
    CHECK(r.sq.Components().size() == 3);
    CHECK(r.sq.ComponentIndex("streichwurst-instrument") == 0); // 23 characters, SQ 1.1
    CHECK(r.sq.Components()[0].kind == 1 && r.sq.Components()[1].kind == 2 && r.sq.Components()[2].kind == 4);
    CHECK(std::string(SqKindName(4)) == "Product");
    CHECK(r.sq.Components()[0].parameters.size() == 18 && r.sq.Components()[1].parameters.size() == 8 &&
          r.sq.Components()[2].parameters.size() == 5);
    // Values and enum labels as the device holds them (contract defaults).
    CHECK(r.Param("streichwurst-instrument", "registration")->value == 50000);
    CHECK(r.Param("streichwurst-fx", "ensemble_mode")->enum_keys.size() == 3);
    CHECK(r.Param("streichwurst", "pot_page")->Readonly());
    // Reading only: no SET request, and no pattern request without capability bit 3.
    CHECK(r.port.SentOfType(sq::SetParam) == 0 && r.port.SentOfType(sq::GetPattern) == 0);
    CHECK(r.port.sim.Writes() == 0);
}

static void TestMatrixAlongside(Rig& r) {
    // SQ said: Mod on Daisy on the same port. The MD session connects on the same link.
    r.md.Connect(r.port, uint32_t(r.now));
    r.Run(3000);
    CHECK(r.md.Connected());
    CHECK(r.md.Device().key == "streichwurst");
    CHECK(r.model.columns[0].name == "Registr." && r.model.columns[7].name == "Pitch");
    // A matrix edit (LFO 1 -> Registration) is applied by the device; the SQ register
    // value of the registration stays what the app set.
    r.md.EditDepth(int(mod::CellIndex(0, 0)), 80, true);
    r.Run(500);
    CHECK(r.port.sim.Matrix().CurrentCrc() == r.model.Crc());
    float span = 0;
    for(int i = 0; i < 2000; ++i) {
        r.Run(1);
        span = std::max(span, std::fabs(r.port.sim.MatrixOffset(0)));
    }
    CHECK(span > 0.1f);
    CHECK(r.port.sim.Value(0, 1) == 50000);
    CHECK(r.Param("streichwurst-instrument", "registration")->value == 50000);
    // Both sessions stay healthy: SQ answers while MD monitors.
    CHECK(r.sq.Ready() && r.sq.Lost() == 0);
}

static void TestSetParamAndFeedback(Rig& r) {
    bool acked = false;
    CHECK(r.sq.SetParam(0, "registration", 75.0, [&](bool ok, uint8_t) { acked = ok; }));
    r.Run(50);
    CHECK(acked && r.Param("streichwurst-instrument", "registration")->value == 75000);
    CHECK(r.port.sim.Value(0, 1) == 75000);
    // Whole-number parameters are rasterized; the editor shows the device's value.
    CHECK(r.sq.SetParam(0, "split_key", 61.4));
    r.Run(50);
    CHECK(r.Param("streichwurst-instrument", "split_key")->value == 61000);
    // Out of range is clamped before sending; read-only parameters are not sent at all.
    CHECK(r.sq.SetParam(2, "master", 400.0));
    r.Run(50);
    CHECK(r.port.sim.Value(2, 2) == 100000);
    CHECK(!r.sq.SetParam(2, "pot_page", 1.0));
    // A pot on the kit: the monitor brings the change into the app.
    r.port.sim.TurnKnob(1, 5, 42000); // phaser 42 %
    r.Run(100);
    CHECK(r.Param("streichwurst-fx", "phaser")->value == 42000);
    // Accepted and applied revision follow (GET_STATE through Pull, monitor event).
    r.sq.Pull();
    r.Run(1500);
    CHECK(r.sq.State().applied == r.sq.State().accepted && r.sq.State().accepted >= 3);
}

static void TestLossAndStale(Rig& r) {
    // The reply to a change is lost: the retry carries the same request ID, the device
    // answers from its cache and applies the change once.
    const unsigned writes = r.port.sim.Writes();
    r.port.drop_to_editor = 1;
    bool acked = false;
    r.sq.SetParam(1, "reverb", 33.0, [&](bool ok, uint8_t) { acked = ok; });
    r.Run(600);
    CHECK(acked);
    CHECK(r.port.sim.Writes() == writes + 1);
    CHECK(r.port.sim.Device().counters[sq::CntDuplicate] >= 1);
    // A stale reply (old request ID) is ignored and counted.
    const unsigned ignored = r.sq.Ignored();
    Frame old;
    for(const Frame& f : r.port.device_sent)
        if(f.size() > 8 && f[2] == 0x53 && f[5] == (sq::SetParam | sq::kResponse)) {
            old = f;
            break;
        }
    CHECK(!old.empty());
    r.sq.OnMessage(old.data(), old.size(), r.now);
    CHECK(r.sq.Ignored() == ignored + 1);
    // Restart of the device (new boot ID): the next read reports it.
    r.port.sim.Restart();
    r.sq.Pull();
    r.Run(1500);
    CHECK(r.sq.Restarted());
}

static void TestProductPatch(Rig& r) {
    AcidPatch p = PatchFromSession(r.sq);
    p.matrix = SavePatch(r.model, &r.md.Device());
    const std::string text = SerializeAcidPatch(p);
    CHECK(text.find("\"streichwurst-patch\"") != std::string::npos);
    CHECK(text.find("\"pot_page\"") == std::string::npos); // read-only kit state stays out
    CHECK(std::string(PatchExtension("streichwurst")) == "swpatch");
    std::string error;
    auto back = ParseAcidPatch(text, error);
    CHECK(back.has_value());
    if(!back)
        return;
    CHECK(back->product == "streichwurst" && back->components.size() == 3 && !back->has_pattern);
    CHECK(back->components[0].params.at("registration") == 75.0);
    CHECK(back->components[1].params.at("reverb") == 33.0);
    // The embedded matrix reads back with the .modpatch reader of the editor.
    const LoadedPatch loaded = LoadPatch(back->matrix, r.model);
    CHECK(loaded.ok);
    CHECK(loaded.patch.depth[0][0] == 80);
    // Compatible with the connected device; schema mismatch and unknown keys are listed.
    CHECK(CheckAcidPatch(*back, r.sq).Clean());
    AcidPatch odd = *back;
    odd.components[1].schema = 9;
    odd.components[0].params["no_such_key"] = 1;
    odd.components.push_back({"streichwurst-delay", "9.9.9", 1, {}});
    const AcidCompat c = CheckAcidPatch(odd, r.sq);
    CHECK(c.incompatible.size() == 1 && c.unknown.size() == 1 && c.missing.size() == 1);
    // Upload (explicit action) sends the compatible values only.
    back->components[0].params["registration"] = 10.0;
    CHECK(UploadAcidPatch(*back, r.sq) == 18 + 8 + 3); // all writable parameters
    r.Run(3000);
    CHECK(r.port.sim.Value(0, 1) == 10000);
    // A Streichwurst patch must name its product; broken matrices are refused.
    std::string bad = text;
    bad.replace(bad.find("\"product\": \"streichwurst\""), 25, "\"product\": \"acid-seq\"   ");
    CHECK(!ParseAcidPatch(bad, error).has_value());
    std::string broken = text.substr(0, text.find("\"matrix\"")) + "\"matrix\": 7\n}\n";
    CHECK(!ParseAcidPatch(broken, error).has_value());
}

static void TestAcidSeqUnchanged() {
    // An Acid Seq device: no matrix capability, the pattern is read as before, and its
    // patch keeps the acid-seq-patch format without a matrix.
    struct AcidPort : Transport {
        SqSimulator sim{"acid-seq"};
        std::deque<Frame> to_editor;
        int patterns = 0;
        AcidPort() {
            sim.SetOut([this](const uint8_t* b, std::size_t n) { to_editor.emplace_back(b, b + n); });
        }
        bool Send(const uint8_t* b, std::size_t n) override {
            patterns += n > 6 && b[5] == sq::GetPattern;
            sim.Receive(b, n, 0);
            return true;
        }
    } port;
    SqSession s(port);
    s.Connect(0);
    double now = 0;
    for(int i = 0; i < 2000 && !s.Ready(); ++i) {
        port.sim.Advance(uint32_t(now));
        while(!port.to_editor.empty()) {
            Frame f = port.to_editor.front();
            port.to_editor.pop_front();
            s.OnMessage(f.data(), f.size(), now);
        }
        s.Tick(now);
        now += 1;
    }
    CHECK(s.Ready() && !s.Identity().HasModMatrix() && s.Identity().protocol_minor == 0);
    CHECK(port.patterns == 1);
    const std::string text = SerializeAcidPatch(PatchFromSession(s));
    CHECK(text.find("\"acid-seq-patch\"") != std::string::npos && text.find("\"matrix\"") == std::string::npos);
}

int main() {
    Rig r;
    TestConnectReadsFirst(r);
    TestMatrixAlongside(r);
    TestSetParamAndFeedback(r);
    TestLossAndStale(r);
    TestProductPatch(r);
    TestAcidSeqUnchanged();
    if(failures) {
        std::printf("editor Streichwurst: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("PASS: editor Streichwurst path (SQ 1.1 + Mod on Daisy on one port, .swpatch)\n");
    return 0;
}
