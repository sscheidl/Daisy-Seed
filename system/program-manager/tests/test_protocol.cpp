// PM* commands through diag::App: identity, catalog listing, start orders with
// session/generation/duplicate rules, mode gate for diagnostic commands, status.
#include "check.h"
#include "fakes/fake_diag.h"
#include "fakes/fake_storage.h"
#include "pm_protocol.h"

using namespace pm;
using namespace fakes;

namespace {

std::string HexOf(const uint8_t* d, size_t n) {
    static const char* hx = "0123456789ABCDEF";
    std::string s;
    for(size_t i = 0; i < n; ++i) {
        s += hx[d[i] >> 4];
        s += hx[d[i] & 15];
    }
    return s;
}

struct Rig {
    FakeStorage storage;
    FakeTarget target;
    FakeCommit commit;
    std::vector<std::string> log;
    Manager manager;
    ProtocolConfig config;
    Protocol protocol;
    diag::Identity identity;
    FakeBoard board;
    FakeUsb usb;
    diag::App app;
    ImageHeader toy, reverb;
    uint32_t now = 1000;

    explicit Rig(bool sd = true)
        : manager(storage, target, commit, sd), protocol(manager, MakeConfig(sd)),
          identity(MakeIdentity(sd)), app(board, usb, "v6_1", 1000, &identity) {
        target.log = &log;
        commit.log = &log;
        app.SetHook(&protocol);
        storage.source.files["PROGRAMS/toy-piano/ToyPiano-v0.4.0.dsy"].bytes = MakePackage("toy-piano", "0.4.0", 20000, &toy);
        storage.source.files["PROGRAMS/reverb-fx/AmbientReverb-v0.3.1.dsy"].bytes = MakePackage("reverb-fx", "0.3.1", 3000, &reverb);
        storage.catalog = "{\"format\":1,\"generation\":9,\"hardware\":\"seed3-desktop\",\"programs\":["
                          "{\"slot\":1,\"id\":\"toy-piano\",\"name\":\"Toy Piano (v0.4) \\u00e4\",\"category\":\"instrument\",\"version\":\"0.4.0\","
                          "\"path\":\"PROGRAMS/toy-piano/ToyPiano-v0.4.0.dsy\",\"payload_sha256\":\"" + HexOf(toy.payload_sha256, 32) + "\"},"
                          "{\"slot\":3,\"id\":\"reverb-fx\",\"name\":\"Ambient Reverb\",\"category\":\"effect\",\"version\":\"0.3.1\","
                          "\"path\":\"PROGRAMS/reverb-fx/AmbientReverb-v0.3.1.dsy\",\"payload_sha256\":\"" + HexOf(reverb.payload_sha256, 32) + "\"}]}";
        board.inputs.sd_card = true;
    }
    static ProtocolConfig MakeConfig(bool sd) {
        ProtocolConfig c;
        c.app = sd ? "DaisyProgramManager" : "DaisyProgramManagerProbe";
        c.fw = "0.1.0";
        c.profile = sd ? "manager" : "probe";
        c.build = "abc1234";
        return c;
    }
    static diag::Identity MakeIdentity(bool sd) {
        diag::Identity id;
        id.app = sd ? "DaisyProgramManager" : "DaisyProgramManagerProbe";
        id.fw = "0.1.0";
        id.boot = "BOOT_QSPI";
        id.extra_caps = sd ? ",pm,pmsd" : ",pm";
        return id;
    }
    void Host(const std::string& body) {
        const std::string f = Frame(body);
        app.OnUsbReceive(reinterpret_cast<const uint8_t*>(f.data()), static_cast<uint32_t>(f.size()));
    }
    // One combined main-loop step as main.cpp does it: manager, then the diag App.
    void Run(uint32_t ms, uint16_t keys = 0) {
        for(uint32_t i = 0; i < ms; ++i) {
            ++now;
            ++board.ticks;
            board.inputs.buttons = keys;
            manager.Step(now, keys, board.inputs.sd_card);
            app.Step(now);
        }
    }
    void Boot() { Run(Manager::kCardSettleMs + 2); }
    std::vector<std::string> Ask(const std::string& body) {
        usb.TakeBodies();
        Host(body);
        Run(1);
        return usb.TakeBodies();
    }
    std::string Reply(const std::string& body, const std::string& prefix) { return First(Ask(body), prefix); }
};

} // namespace

int main() {
    {   // Identity and info without a session.
        Rig rig;
        rig.Boot();
        const std::string hello = rig.Reply("HELLO proto=1", "HELLO");
        CHECK_EQ(Field(hello, "app"), "DaisyProgramManager");
        CHECK_EQ(Field(hello, "boot"), "BOOT_QSPI");
        CHECK(Field(hello, "caps").find(",pm,pmsd") != std::string::npos);
        rig.Host("BYE");
        rig.Run(1);
        const std::string info = rig.Reply("PMINFO", "PMINFO");
        CHECK_EQ(Field(info, "app"), "DaisyProgramManager");
        CHECK_EQ(Field(info, "fw"), "0.1.0");
        CHECK_EQ(Field(info, "profile"), "manager");
        CHECK_EQ(Field(info, "build"), "abc1234");
        CHECK_EQ(Field(info, "mode"), "SELECT");
        CHECK_EQ(Field(info, "gen"), "1");
        CHECK_EQ(Field(info, "cat"), "OK");
        CHECK_EQ(Field(info, "cgen"), "9");
        CHECK_EQ(Field(info, "n"), "2");
        CHECK_EQ(Field(info, "sd"), "1");
        CHECK_EQ(Field(info, "rootbin"), "0");
        CHECK_EQ(Field(info, "slots"), "15");
    }
    {   // Listing: summary mask and per-slot entries with encoded names.
        Rig rig;
        rig.Boot();
        const std::string list = rig.Reply("PMLIST", "PMLIST");
        CHECK_EQ(Field(list, "n"), "2");
        CHECK_EQ(Field(list, "mask"), "0005");
        CHECK_EQ(Field(list, "cat"), "OK");
        const std::string e1 = rig.Reply("PMLIST s=1", "PMENT");
        CHECK_EQ(Field(e1, "slot"), "1");
        CHECK_EQ(Field(e1, "id"), "toy-piano");
        CHECK_EQ(Field(e1, "name"), "Toy%20Piano%20%28v0.4%29%20%C3%A4");
        CHECK_EQ(Field(e1, "cat"), "instrument");
        CHECK_EQ(Field(e1, "ver"), "0.4.0");
        CHECK_EQ(Field(e1, "hash"), HexOf(rig.toy.payload_sha256, 32));
        CHECK_EQ(Field(e1, "gen"), "1");
        CHECK(e1.size() <= ddp::kMaxBody);
        CHECK(rig.Reply("PMLIST s=2", "ERR").find("e=NOENTRY") != std::string::npos);
        CHECK(rig.Reply("PMLIST s=16", "ERR").find("e=ARG") != std::string::npos);
        CHECK(rig.Reply("PMLIST s=x", "ERR").find("e=ARG") != std::string::npos);
    }
    {   // Longest possible entry still fits one frame (64-byte name of %XX bytes).
        Rig rig;
        std::string name;
        for(int i = 0; i < 32; ++i)
            name += "\\u00e4";  // 2 UTF-8 bytes each = 64 bytes, all percent-encoded
        rig.storage.catalog = "{\"format\":1,\"generation\":1,\"hardware\":\"seed3-desktop\",\"programs\":["
                              "{\"slot\":15,\"id\":\"" + std::string(32, 'x') + "\",\"name\":\"" + name + "\",\"category\":\"instrument\","
                              "\"version\":\"" + std::string(16, '9') + "\",\"path\":\"PROGRAMS/a/b.dsy\",\"payload_sha256\":\"" + HexOf(rig.toy.payload_sha256, 32) + "\"}]}";
        rig.Boot();
        CHECK(rig.manager.catalog_state() == CatalogState::Ok);
        const std::string e = rig.Reply("PMLIST s=15", "PMENT");
        CHECK(!e.empty());
        CHECK(e.size() <= ddp::kMaxBody);
        CHECK_EQ(Field(e, "name").size(), 64u * 3u);
    }
    {   // Start orders: session, arguments, generation, duplicates, loading state.
        Rig rig;
        rig.Boot();
        const std::string hash = HexOf(rig.toy.payload_sha256, 32);
        std::string start = "PMSTART slot=1 id=toy-piano hash=" + hash + " gen=1 rid=77";
        CHECK(rig.Reply(start, "ERR").find("c=PMSTART e=SESSION") != std::string::npos);
        rig.Ask("HELLO proto=1");
        CHECK(rig.Reply("PMSTART slot=1 id=toy-piano hash=" + hash.substr(0, 15) + " gen=1 rid=1", "ERR").find("e=ARG") != std::string::npos);
        CHECK(rig.Reply("PMSTART slot=1 id=toy-piano hash=" + hash + " gen=1", "ERR").find("e=ARG") != std::string::npos);
        CHECK(rig.Reply("PMSTART slot=0 id=toy-piano hash=" + hash + " gen=1 rid=1", "ERR").find("e=ARG") != std::string::npos);
        CHECK(rig.Reply("PMSTART slot=1 id=toy-piano hash=" + hash + " gen=2 rid=1", "ERR").find("e=GENERATION") != std::string::npos);
        CHECK(rig.Reply("PMSTART slot=1 id=reverb-fx hash=" + hash + " gen=1 rid=1", "ERR").find("e=NOENTRY") != std::string::npos);
        CHECK(rig.Reply("PMSTART slot=1 id=toy-piano hash=" + HexOf(rig.reverb.payload_sha256, 8) + " gen=1 rid=1", "ERR").find("e=HASH") != std::string::npos);
        CHECK(rig.manager.mode() == Mode::Select);
        // Lower-case hash prefix of 16 digits is accepted.
        std::string lower = hash.substr(0, 16);
        for(auto& c : lower) c = static_cast<char>(std::tolower(c));
        const std::string ack = rig.Reply("PMSTART slot=1 id=toy-piano hash=" + lower + " gen=1 rid=77", "ACK");
        CHECK_EQ(Field(ack, "rid"), "77");
        CHECK_EQ(Field(ack, "slot"), "1");
        CHECK(rig.manager.mode() == Mode::Loading);
        std::string st = rig.Reply("PMSTATUS", "PMST");
        CHECK_EQ(Field(st, "mode"), "LOADING");
        CHECK_EQ(Field(st, "rid"), "77");
        CHECK_EQ(Field(st, "slot"), "1");
        // While loading: diagnostics commands, mode changes, rescans, BOOT and new starts are refused.
        CHECK(rig.Reply("LED m=ON", "ERR").find("c=LED e=MODE") != std::string::npos);
        CHECK(rig.Reply("PMMODE m=DIAG", "ERR").find("e=BUSY") != std::string::npos);
        CHECK(rig.Reply("BOOT", "ERR").find("c=BOOT e=BUSY") != std::string::npos);
        CHECK_EQ(rig.board.boot_entries, 0);
        CHECK(rig.Reply(start, "ACK").find("dup=1") != std::string::npos);  // same rid
        CHECK(rig.Reply("PMSTART slot=3 id=reverb-fx hash=" + HexOf(rig.reverb.payload_sha256, 32) + " gen=1 rid=78", "ERR").find("e=BUSY") != std::string::npos);
        rig.Run(50);
        CHECK_EQ(rig.commit.commits, 1);
        st = rig.Reply("PMSTATUS", "PMST");
        CHECK_EQ(Field(st, "state"), "RESTARTING");
        CHECK_EQ(Field(st, "pct"), "100");
    }
    {   // Mode gate and PMMODE; BOOT passes through to the diagnostics layer.
        Rig rig;
        rig.Boot();
        rig.Ask("HELLO proto=1");
        for(const char* cmd : {"LED m=ON", "CVOUT m=FIX c1=1", "TONE m=SINE", "SCOPE", "SDTEST", "READER a=STATUS"}) {
            const std::string err = rig.Reply(cmd, "ERR");
            CHECK(err.find("e=MODE") != std::string::npos);
        }
        CHECK_EQ(rig.board.cv_writes.size(), 1u);  // only the initial 0/0 from the App
        CHECK_EQ(rig.board.sd_tests, 0);
        const std::string mode = rig.Reply("PMMODE m=DIAG", "ACK");
        CHECK_EQ(Field(mode, "m"), "DIAG");
        CHECK(rig.manager.mode() == Mode::Diagnostics);
        CHECK(rig.Reply("LED m=ON", "ACK").find("c=LED") != std::string::npos);
        CHECK(rig.Reply("CVOUT m=FIX c1=100 c2=200", "ACK").find("c=CVOUT") != std::string::npos);
        rig.Run(2);
        CHECK(rig.board.cv_writes.back().first == 100 && rig.board.cv_writes.back().second == 200);
        CHECK(rig.Reply("PMMODE m=SELECT", "ACK").find("m=SELECT") != std::string::npos);
        CHECK(rig.Reply("PMMODE m=LOADING", "ERR").find("e=ARG") != std::string::npos);
        CHECK(rig.Reply("PMMODE", "ERR").find("e=ARG") != std::string::npos);
        CHECK(rig.Reply("PMRESCAN", "ACK").find("c=PMRESCAN") != std::string::npos);
        rig.Run(1);
        CHECK_EQ(rig.storage.catalog_reads, 2);
        CHECK_EQ(rig.manager.generation(), 2u);
        CHECK(rig.Reply("PMFOO", "ERR").find("c=PMFOO e=UNKNOWN") != std::string::npos);
        // BOOT: normal diagnostics path (ACK, then Board::EnterBootloader).
        CHECK(rig.Reply("BOOT", "ACK").find("c=BOOT") != std::string::npos);
        rig.Run(diag::App::kBootFlushMs + 2);
        CHECK_EQ(rig.board.boot_entries, 1);
    }
    {   // Session-only commands without a session.
        Rig rig;
        rig.Boot();
        CHECK(rig.Reply("PMMODE m=DIAG", "ERR").find("e=SESSION") != std::string::npos);
        CHECK(rig.Reply("PMRESCAN", "ERR").find("e=SESSION") != std::string::npos);
        CHECK(rig.manager.mode() == Mode::Select);
        // Read-only commands work without a session.
        CHECK(!rig.Reply("PMSTATUS", "PMST").empty());
        CHECK(!rig.Reply("PMLIST", "PMLIST").empty());
    }
    {   // Status after a failed load and catalog details.
        Rig rig;
        rig.storage.catalog = "{nope";
        rig.Boot();
        std::string st = rig.Reply("PMSTATUS", "PMST");
        CHECK_EQ(Field(st, "cat"), "INVALID");
        CHECK_EQ(Field(st, "ce"), "SYNTAX");
        CHECK_EQ(Field(st, "state"), "IDLE");
        rig.Ask("HELLO proto=1");
        CHECK(rig.Reply("PMSTART slot=1 id=toy-piano hash=" + HexOf(rig.toy.payload_sha256, 32) + " gen=1 rid=5", "ERR").find("e=NOCATALOG") != std::string::npos);
        rig.storage.catalog_missing = true;
        rig.Ask("PMRESCAN");
        rig.Run(1);
        st = rig.Reply("PMSTATUS", "PMST");
        CHECK_EQ(Field(st, "cat"), "MISSING");
        CHECK_EQ(Field(st, "cd"), "4");
    }
    {   // Probe profile: no SD start, diagnostics reachable.
        Rig rig(false);
        rig.Boot();
        const std::string hello = rig.Reply("HELLO proto=1", "HELLO");
        CHECK_EQ(Field(hello, "app"), "DaisyProgramManagerProbe");
        CHECK(Field(hello, "caps").find(",pmsd") == std::string::npos);
        const std::string info = rig.Reply("PMINFO", "PMINFO");
        CHECK_EQ(Field(info, "profile"), "probe");
        CHECK_EQ(Field(info, "cat"), "UNSUPPORTED");
        CHECK(rig.Reply("PMSTART slot=1 id=toy-piano hash=" + HexOf(rig.toy.payload_sha256, 32) + " gen=1 rid=1", "ERR").find("e=UNSUPPORTED") != std::string::npos);
        CHECK(rig.Reply("PMMODE m=DIAG", "ACK").find("m=DIAG") != std::string::npos);
    }
    {   // Name encoding helper.
        char buf[64];
        ddp::Writer w(buf, sizeof(buf));
        Protocol::EncodeName("A b_c.d-e%f\xC3\xA4", w);
        CHECK_STREQ(w.CStr(), "A%20b_c.d-e%25f%C3%A4");
    }
    return check::Finish("test_protocol");
}
