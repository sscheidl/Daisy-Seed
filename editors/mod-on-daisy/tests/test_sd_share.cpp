// Host tests of the SD share core (src/core/SdShare*.h, Ddp*.h) against the simulated
// Nebulae on Daisy and Looper. Every test maps to a Python test of Daisy Diagnostic 0.6.0
// (docs/dcp/SD_SHARE_PORT.md § 11); the byte vectors in sd_share_vectors.txt come from the
// Python reference itself. No JUCE.
//   make -C editors/mod-on-daisy test
#include "../src/core/SdShareSim.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if(!(cond)) {                                                                     \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while(0)

using namespace editor;
using namespace editor::sd;
using editor::ddp::ReaderState;

std::vector<uint8_t> Hex(const std::string& text) {
    std::vector<uint8_t> out;
    for(std::size_t i = 0; i + 1 < text.size(); i += 2)
        out.push_back(uint8_t(std::stoul(text.substr(i, 2), nullptr, 16)));
    return out;
}
std::string Hex(const std::vector<uint8_t>& bytes) {
    std::string out;
    char b[3];
    for(uint8_t v : bytes) {
        std::snprintf(b, sizeof(b), "%02X", unsigned(v));
        out += b;
    }
    return out;
}
bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// ------------------------------------------------------------------ rig: workflow over the simulator
struct Rig {
    uint32_t now = 1000000;
    SimInstrument dev;
    SimSerialPorts serial{dev};
    SimMidiPorts midi{dev};
    ddp::DdpSession session{&serial};
    int token_counter = 0;
    SdShareWorkflow wf;
    std::vector<std::string> logs;

    explicit Rig(const Profile& profile = kNebulae)
        : dev(profile, [this] { return now; }),
          wf(session, &midi, [this](uint8_t* t) {
              const int i = token_counter++;
              t[0] = uint8_t(i & 0x7F);
              t[1] = uint8_t((i + 1) & 0x7F);
              t[2] = uint8_t((i + 2) & 0x7F);
              t[3] = 0x03;
          }) {
        session.log = [this](const std::string& t) { logs.push_back("session: " + t); };
        wf.log = [this](const std::string& t) { logs.push_back(t); };
    }
    void Run(uint32_t ms, uint32_t step = 20) {
        for(uint32_t t = 0; t < ms; t += step) {
            now += step;
            session.Tick(now);
            wf.Tick(now);
        }
    }
    bool Until(const std::function<bool()>& predicate, uint32_t ms = 10000) {
        for(uint32_t t = 0; t < ms; t += 20) {
            if(predicate())
                return true;
            Run(20);
        }
        return predicate();
    }
    bool Logged(const char* part) const {
        for(const std::string& l : logs)
            if(Contains(l, part))
                return true;
        return false;
    }
};

// ------------------------------------------------------------------ codecs and vectors
void TestVectors() {
    std::ifstream in("tests/sd_share_vectors.txt");
    CHECK(in.good());
    std::string line;
    int checked = 0;
    while(std::getline(in, line)) {
        if(line.empty() || line[0] == '#')
            continue;
        std::istringstream s(line);
        std::string name, hex;
        s >> name >> hex;
        std::map<std::string, std::string> kv;
        std::string pair;
        while(s >> pair) {
            const std::size_t eq = pair.find('=');
            kv[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
        const std::vector<uint8_t> bytes = Hex(hex);
        const Profile& p = name.rfind("nebulae", 0) == 0 ? kNebulae : kLooper;
        if(name == "nebulae.status_request" || name == "looper.status_request") {
            CHECK(StatusRequest(p) == bytes);
        } else if(name == "nebulae.share_request" || name == "looper.share_request") {
            const std::vector<uint8_t> token = Hex(kv["token"]);
            std::vector<uint8_t> out;
            CHECK(ShareRequest(p, kv["confirm"] == "1", token.data(), out) && out == bytes);
        } else if(name == "nebulae.parse.share" || name == "looper.parse.share") {
            Reply r;
            CHECK(ParseReply(p, bytes.data(), bytes.size(), r) && r.kind == Reply::Share);
            CHECK(ShareResultName(r.result, r.result_code) == kv["result"]);
            CHECK(Hex(std::vector<uint8_t>(r.token, r.token + 4)) == kv["token"]);
            CHECK(ParseAny(bytes.data(), bytes.size(), r) == &p);
        } else if(name == "nebulae.parse.error" || name == "looper.parse.error") {
            Reply r;
            CHECK(ParseReply(p, bytes.data(), bytes.size(), r) && r.kind == Reply::Error);
            CHECK(std::to_string(r.error_command) == kv["command"] && ErrorCodeName(r.error_code) == kv["code"]);
        } else if(name == "nebulae.parse.status" || name == "looper.parse.status") {
            Reply r;
            CHECK(ParseReply(p, bytes.data(), bytes.size(), r) && r.kind == Reply::Status);
            const PerfStatus& st = r.status;
            auto i = [&](const char* k) { return std::stoi(kv[k]); };
            CHECK(st.firmware == kv["firmware"] && int(st.card) == i("card") && int(st.overrun) == i("overrun"));
            CHECK(int(st.recording) == i("recording") && st.overruns == i("overruns") && st.peak_load == i("peak_load"));
            CHECK(int(st.Busy()) == i("busy"));
            if(p.id == Instrument::Nebulae) {
                CHECK(int(st.live) == i("live") && int(st.loading) == i("loading") && int(st.scanning) == i("scanning"));
                CHECK(st.bank == i("bank") && st.banks == i("banks") && st.playing_bank == i("playing_bank"));
                CHECK(st.playing_slot == i("playing_slot") && st.bank_mask == i("bank_mask") && st.slot_mask == i("slot_mask"));
                CHECK(st.load_error == i("load_error"));
            } else {
                CHECK(int(st.loop) == i("loop") && int(st.unsaved) == i("unsaved") && int(st.saving) == i("saving"));
                CHECK(int(st.importing) == i("importing") && int(st.save_failed) == i("save_failed"));
                CHECK(st.fault == i("fault") && st.loop_ms == i("loop_ms"));
            }
        } else if(name == "reject") {
            Reply r;
            CHECK(!ParseReply(kNebulae, bytes.data(), bytes.size(), r));
            CHECK(!ParseReply(kLooper, bytes.data(), bytes.size(), r));
            CHECK(ParseAny(bytes.data(), bytes.size(), r) == nullptr);
        } else if(name.rfind("ddp.", 0) == 0) {
            std::string body = kv["body"];
            for(char& c : body)
                if(c == '_')
                    c = ' ';
            std::vector<uint8_t> frame;
            CHECK(ddp::BuildFrame(body, frame) && frame == bytes);
            ddp::FrameParser parser;
            const std::vector<std::string> bodies = parser.Feed(bytes.data(), bytes.size());
            CHECK(bodies.size() == 1 && bodies[0] == body && parser.Errors() == 0);
        } else {
            std::fprintf(stderr, "unknown vector %s\n", name.c_str());
            ++failures;
        }
        ++checked;
    }
    CHECK(checked >= 50);
    std::printf("PASS vectors: %d byte vectors from the Python reference (STATUS, SHARE, CONFIRM, SAVING, ERROR, RETURN, "
                "PERFORM, HELLO, PING, BYE)\n",
                checked);
}

void TestCodecs() {
    // test_requests_match_firmware_layout (both): a token byte above 0x7F is refused.
    std::vector<uint8_t> out;
    const uint8_t bad[4] = {0x80, 0, 0, 0};
    CHECK(!ShareRequest(kNebulae, false, bad, out) && !ShareRequest(kLooper, false, bad, out));
    // test_parse_status_reply (Nebulae): named fields and texts.
    const std::vector<uint8_t> status = Hex("F07D4E420141000200001102030210070000" "7F7F03047F7F7FF7");
    Reply r;
    CHECK(ParseReply(kNebulae, status.data(), status.size(), r) && r.kind == Reply::Status);
    CHECK(r.status.firmware == "0.2.0" && r.status.card && r.status.overrun && !r.status.live);
    CHECK(r.status.bank == 2 && r.status.banks == 3 && r.status.playing_bank == 2 && r.status.playing_slot == 16);
    CHECK(r.status.bank_mask == 7 && r.status.slot_mask == 0xFFFF && r.status.overruns == 16383);
    CHECK(r.status.LoadErrorText() == "SD I/O error");
    // test_parse_status_reply (Looper): fault text and busy flags.
    const std::vector<uint8_t> lstatus = Hex("F07D4C500141000500004701025827057F7F7FF7");
    CHECK(ParseReply(kLooper, lstatus.data(), lstatus.size(), r) && r.kind == Reply::Status);
    CHECK(r.status.firmware == "0.5.0" && r.status.card && r.status.loop && r.status.unsaved && r.status.overrun);
    CHECK(!r.status.saving && !r.status.Busy() && r.status.save_failed && r.status.FaultText() == "write failed");
    CHECK(r.status.loop_ms == 87000 && r.status.overruns == 16383 && r.status.peak_load == 127);
    std::string why;
    CHECK(!ParseReply(kNebulae, lstatus.data(), lstatus.size(), r, &why) && why == "not a Nebulae SysEx reply");
    // Nebulae result 5 is not SAVING but a refusal code.
    const std::vector<uint8_t> five = Hex("F07D4E4201420509080706F7");
    CHECK(ParseReply(kNebulae, five.data(), five.size(), r) && r.result == ShareResult::Unknown
          && ShareResultName(r.result, r.result_code) == "CODE5");
    CHECK(IsSdShareSysEx(status.data(), status.size()) && IsSdShareSysEx(lstatus.data(), lstatus.size()));
    const std::vector<uint8_t> md = Hex("F07D4D4401007E000200007EF7");
    CHECK(!IsSdShareSysEx(md.data(), md.size()));
    // DDP: HELLO, READER, NEB, LOOPER, ST parsing (test_management_status_line, test_card_reader parsers).
    ddp::Message m = ddp::ParseMessage("LOOPER mode=SDMGMT fw=0.5.0 card=1 ret=0 a=STATUS");
    ddp::MgmtInfo info;
    std::string err;
    CHECK(ddp::ParseMgmt(m, false, info, err) && info.firmware == "0.5.0" && info.card && !info.can_return && info.action == "STATUS");
    CHECK(!ddp::ParseMgmt(ddp::ParseMessage("LOOPER mode=PERF fw=0.5.0 a=STATUS"), false, info, err));
    CHECK(!ddp::ParseMgmt(ddp::ParseMessage("LOOPER mode=SDMGMT fw=0.5.0 card=1 ret=1 a=FROB"), false, info, err));
    CHECK(ddp::ParseMgmt(ddp::ParseMessage("NEB mode=SDMGMT fw=0.2.0 card=1 ret=1 bank=2 slot=5 a=STATUS"), true, info, err)
          && info.bank == 2 && info.slot == 5 && info.can_return);
    CHECK(!ddp::ParseMgmt(ddp::ParseMessage("NEB mode=SDMGMT fw=0.2.0 card=1 ret=1 bank=17 slot=5 a=STATUS"), true, info, err));
    ddp::ReaderInfo reader;
    CHECK(ddp::ParseReader(ddp::ParseMessage("READER state=SHARED sectors=62332928 busy=0 a=STATUS"), reader, err)
          && reader.state == ReaderState::Shared && reader.sectors == 62332928u && !reader.busy);
    CHECK(!ddp::ParseReader(ddp::ParseMessage("READER state=GONE sectors=0 busy=0 a=STATUS"), reader, err));
    CHECK(!ddp::ParseReader(ddp::ParseMessage("READER state=LOCAL sectors=0 busy=0 a=EJECT"), reader, err));
    CHECK(!ddp::ParseReader(ddp::ParseMessage("READER state=LOCAL sectors=0 busy=2 a=STATUS"), reader, err));
    CHECK(!ddp::ParseReader(ddp::ParseMessage("READER state=LOCAL sectors=0 busy=0 a=STATUS io=READ"), reader, err));
    CHECK(ddp::ParseReader(ddp::ParseMessage("READER state=ERROR sectors=0 busy=0 a=STATUS io=WRITE fault=3 he=16 lba=99 ms=12"),
                           reader, err)
          && reader.has_diagnostics && reader.fault == 3 && reader.lba == 99);
    ddp::DeviceInfo dev;
    CHECK(ddp::ParseHello(ddp::ParseMessage("HELLO proto=1 fw=0.2.0 app=NebulaeOnDaisy dev=seed3-desktop boot=BOOT_SRAM bl=v6_1 "
                                            "libdaisy=f715e48 caps=btn,pot,sd,boot,sdmsc,nebmgmt sim=1"),
                          dev, err)
          && dev.protocol == 1 && dev.app == "NebulaeOnDaisy" && dev.Has("nebmgmt") && dev.Has("sdmsc") && !dev.Has("led")
          && dev.simulated);
    CHECK(!ddp::ParseHello(ddp::ParseMessage("HELLO proto=x"), dev, err));
    bool sd = false;
    CHECK(ddp::ParseStSdCard(ddp::ParseMessage("ST n=1 t=2 sd=1 lm=OFF"), sd) && sd);
    CHECK(!ddp::ParseStSdCard(ddp::ParseMessage("ST n=1 t=2 sd=+1"), sd));
    // Framing: resynchronisation and CRC errors are counted, a CR LF ending is accepted.
    ddp::FrameParser parser;
    std::vector<uint8_t> good;
    ddp::BuildFrame("PONG n=1", good);
    const std::string good_text(good.begin(), good.end() - 1); // without the newline
    const std::string stream = "garbage" + good_text + "\r\n$PONG n=1*0000\n$" + good_text + "\n";
    const std::vector<std::string> bodies = parser.Feed(reinterpret_cast<const uint8_t*>(stream.data()), stream.size());
    CHECK(bodies.size() == 2 && bodies[0] == "PONG n=1" && parser.bad_crc == 1 && parser.bad_format == 1);
    std::vector<uint8_t> frame;
    CHECK(!ddp::BuildFrame("", frame) && !ddp::BuildFrame("A*B", frame) && !ddp::BuildFrame(std::string(401, 'A'), frame));
    std::printf("PASS codecs: Nebulae and Looper SysEx (requests, STATUS, SHARE, ERROR, rejects), DDP HELLO/READER/NEB/LOOPER/ST, "
                "framing\n");
}

// ------------------------------------------------------------------ Nebulae workflow
void TestNebulaeRoundTrip() {
    Rig r;
    r.Run(1500);
    CHECK(r.wf.GetPhase() == Phase::Performance && r.wf.Status() && r.wf.Status()->firmware == "0.2.1");
    CHECK(r.wf.CanShare() && !r.session.Connected()); // no COM port in performance
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Restarting; }, 1000));
    CHECK(r.dev.restarts == 1 && !r.wf.LinkOpen());
    // The restart takes kSimRestartMs; the panel continues the share exactly once.
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Management; }, kSimRestartMs + 5000));
    CHECK(r.session.Device() && r.session.Device()->app == "NebulaeOnDaisy" && r.session.Device()->Has("nebmgmt"));
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Shared; }, 5000));
    CHECK(!r.wf.ShareAfterRestart());
    CHECK(!r.wf.Ejected() && !r.wf.CanReturn()); // Windows owns the card
    CHECK(!r.wf.ReturnToPerformance());
    CHECK(r.Until([&] { return r.session.Mgmt() && !r.session.Mgmt()->can_return; }, 3000));
    CHECK(Contains(r.wf.Message(), "Windows owns the card"));
    r.dev.Management().ReaderEject(); // Windows "Safely remove"
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Ejected; }, 3000));
    CHECK(r.wf.Ejected() && r.wf.CanReturn() && Contains(r.wf.Message(), "ejected"));
    CHECK(r.wf.ReturnToPerformance());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Returning; }, 3000));
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance; }, 10000));
    CHECK(r.dev.GetPersonality() == SimInstrument::Personality::Perf && r.dev.restarts == 2);
    CHECK(Contains(r.wf.Message(), "rescanned") && r.Logged("is back in performance"));
    std::printf("PASS nebulae round trip: STATUS, SHARE, restart, CDC HELLO, READER SHARE once, eject, RETURN, NEB PERFORM, "
                "STATUS again\n");
}

void TestForeignSysExSkipped() {
    // Nebulae MOD streams monitor frames for up to 3 s after the editor closes, and a
    // keyboard may be playing. Only NB replies count; the rest is skipped, not an error.
    struct Noisy : MidiLink {
        std::unique_ptr<MidiLink> inner;
        bool Send(const uint8_t* b, std::size_t n) override { return inner->Send(b, n); }
        bool Poll(std::vector<std::vector<uint8_t>>& out) override {
            std::vector<std::vector<uint8_t>> real;
            if(!inner->Poll(real))
                return false;
            const std::vector<uint8_t> monitor = Hex("F07D4D4401007E000200007EF7");
            const std::vector<uint8_t> sq = Hex("F07D53510100F7");
            out = {monitor, sq};
            out.insert(out.end(), real.begin(), real.end());
            out.push_back(monitor);
            return true;
        }
        void Close() override { inner->Close(); }
    };
    struct NoisyPorts : MidiPorts {
        SimMidiPorts& inner;
        explicit NoisyPorts(SimMidiPorts& i) : inner(i) {}
        std::vector<std::string> List() override { return inner.List(); }
        std::unique_ptr<MidiLink> Open(const std::string& name, std::string& error) override {
            auto link = inner.Open(name, error);
            if(!link)
                return nullptr;
            auto noisy = std::make_unique<Noisy>();
            noisy->inner = std::move(link);
            return noisy;
        }
    };
    Rig r;
    NoisyPorts ports(r.midi);
    r.wf.SetMidiPorts(&ports);
    r.Run(3000);
    CHECK(r.wf.GetPhase() == Phase::Performance && r.wf.Error().empty());
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Restarting; }, 1000));
    std::printf("PASS foreign SysEx: Mod on Daisy monitor frames and SQ on the same port are skipped\n");
}

void TestNebulaeConfirmCancel() {
    Rig r;
    r.dev.live = true;
    r.Run(1500);
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Confirm; }, 2000));
    CHECK(r.wf.NeedsConfirmation() && Contains(r.wf.Message(), "LOST") && r.dev.restarts == 0);
    CHECK(!r.wf.RequestShare()); // an unconfirmed retry is refused (T9)
    CHECK(!r.wf.CanShare() || r.wf.NeedsConfirmation());
    r.wf.CancelConfirmation();
    CHECK(r.wf.GetPhase() == Phase::Performance && r.dev.restarts == 0 && Contains(r.wf.Message(), "nothing was shared"));
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Confirm; }, 2000));
    CHECK(r.wf.RequestShare(true));
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Management; }, 10000));
    CHECK(!r.dev.live); // the RAM loop is gone after the restart (simulated)
    std::printf("PASS nebulae confirmation: CONFIRM_REQUIRED, cancel keeps playing, confirm shares\n");
}

void TestNebulaeRefusals() {
    struct Case {
        void (*setup)(SimInstrument&);
        const char* error;
    } cases[] = {{[](SimInstrument& d) { d.recording = true; }, "recording"},
                 {[](SimInstrument& d) { d.card = false; }, "No microSD"}};
    for(const Case& c : cases) {
        Rig r;
        c.setup(r.dev);
        r.Run(1500);
        CHECK(r.wf.GetPhase() == Phase::Performance && !r.wf.CanShare());
        CHECK(!r.wf.RequestShare()); // the GUI button is disabled; the request is refused here too
        // A forced request (another client, a race) is refused by the firmware as well.
        std::unique_ptr<MidiLink> link;
        std::string error;
        link = r.midi.Open(kSimMidiPort, error);
        uint8_t token[4] = {1, 2, 3, 4};
        std::vector<uint8_t> frame;
        ShareRequest(kNebulae, true, token, frame);
        CHECK(link && link->Send(frame.data(), frame.size()));
        std::vector<std::vector<uint8_t>> replies;
        CHECK(link->Poll(replies) && replies.size() == 1);
        Reply reply;
        CHECK(ParseReply(kNebulae, replies[0].data(), replies[0].size(), reply) && reply.kind == Reply::Share
              && reply.result != ShareResult::Accepted && r.dev.restarts == 0);
        CHECK(reply.result == (Contains(c.error, "recording") ? ShareResult::Busy : ShareResult::NoCard));
    }
    // The workflow's own error texts for BUSY and NO_CARD (the device changes state after the probe).
    {
        Rig r;
        r.Run(1500);
        r.dev.recording = true;
        CHECK(r.wf.RequestShare()); // CanShare still true from the last STATUS
        r.Run(200);
        CHECK(r.wf.GetPhase() == Phase::Performance && Contains(r.wf.Error(), "recording") && r.dev.restarts == 0);
        r.dev.recording = false;
        r.dev.card = false;
        CHECK(r.wf.RequestShare());
        r.Run(200);
        CHECK(r.wf.GetPhase() == Phase::Performance && Contains(r.wf.Error(), "No microSD") && r.dev.restarts == 0);
        r.Run(1200);
        CHECK(!r.wf.CanShare()); // the next STATUS reports no card
    }
    std::printf("PASS nebulae refusals: recording and no card change nothing, texts shown\n");
}

void TestShareTimeoutAndLost() {
    Rig r;
    r.Run(1500);
    r.dev.answer_sysex = false; // the firmware stops answering
    CHECK(r.wf.RequestShare());
    r.Run(SdShareWorkflow::kShareTimeoutMs + 500);
    CHECK(Contains(r.wf.Error(), "No answer") && r.wf.GetPhase() == Phase::Performance);
    r.Run(SdShareWorkflow::kStatusTimeoutMs + 1000);
    CHECK(r.wf.GetPhase() == Phase::NotFound && !r.wf.Status() && r.Logged("stopped answering"));
    std::printf("PASS timeouts: SHARE reply 2 s, STATUS 3 s to lost\n");
}

void TestWrongPersonalityR1() {
    Rig r;
    r.Run(1500);
    r.dev.wrong_personality_on_restart = true; // boot intent lost, the device boots performance
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Restarting; }, 1000));
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance; }, 10000));
    CHECK(Contains(r.wf.Error(), "did not enter SD management") && !r.wf.ShareAfterRestart());
    CHECK(r.dev.Management().reader_state == ReaderState::Local); // nothing was shared
    std::printf("PASS R1: a restart that comes back in performance is reported, nothing shared\n");
}

void TestRestartTimeout() {
    Rig r;
    r.Run(1500);
    CHECK(r.wf.RequestShare());
    r.Run(100);
    r.dev.never_reenumerates = true;
    r.Run(SdShareWorkflow::kRestartTimeoutMs + 1000);
    CHECK(r.wf.GetPhase() == Phase::NotFound && Contains(r.wf.Error(), "did not reappear") && !r.wf.ShareAfterRestart());
    std::printf("PASS restart timeout: 20 s without SD management\n");
}

void TestInterruptionQuarantine() {
    Rig r;
    r.Run(1500);
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Shared; }, 10000));
    r.dev.Management().ReaderBusReset();
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Interrupted; }, 3000));
    CHECK(!r.wf.Ejected() && !r.wf.CanReturn() && r.wf.CanAcknowledge());
    CHECK(Contains(r.wf.Message(), "quarantined"));
    CHECK(!r.wf.ReturnToPerformance());
    r.Run(3000);
    CHECK(r.wf.ReaderState() == ReaderState::Interrupted); // no automatic reshare or return
    CHECK(r.wf.AcknowledgeInterruption());
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Local; }, 3000));
    CHECK(r.wf.CanReturn() && r.wf.ReturnToPerformance());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance; }, 10000));
    // The same for a card I/O error.
    Rig e;
    e.Run(1500);
    CHECK(e.wf.RequestShare());
    CHECK(e.Until([&] { return e.wf.ReaderState() == ReaderState::Shared; }, 10000));
    e.dev.Management().ReaderIoError();
    CHECK(e.Until([&] { return e.wf.ReaderState() == ReaderState::Error; }, 3000));
    CHECK(Contains(e.wf.Message(), "Card I/O failed") && e.wf.CanAcknowledge() && !e.wf.CanReturn());
    std::printf("PASS interruption: INTERRUPTED and ERROR are quarantined until acknowledged, then RETURN works\n");
}

void TestCloseNeverReturnsCard() {
    Rig r;
    r.Run(1500);
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Shared; }, 10000));
    r.session.Disconnect("GUI closed");
    CHECK(r.dev.Management().reader_state == ReaderState::Shared);
    r.Run(5000); // reconnects, queries STATUS, never repeats SHARE or RETURN
    CHECK(r.session.Connected() && r.wf.ReaderState() == ReaderState::Shared);
    CHECK(r.dev.GetPersonality() == SimInstrument::Personality::Mgmt && r.session.Reconnects() == 1);
    // Also a lost CDC link (cable pulled while SHARED): ownership unknown, nothing sent.
    r.dev.Cable().Unplug();
    r.Run(2000);
    CHECK(!r.session.Connected() && Contains(r.wf.Message(), "ownership is unknown"));
    CHECK(r.wf.GetPhase() == Phase::Management && !r.wf.CanReturn() && !r.wf.CanAcknowledge());
    std::printf("PASS close: BYE, reconnect and a pulled cable never return or reshare the card\n");
}

void TestFoundInManagement() {
    Rig r;
    r.dev.StartInManagement();
    r.Run(3000);
    CHECK(r.wf.GetPhase() == Phase::Management && r.wf.ReaderState() == ReaderState::Local);
    CHECK(r.wf.CanShareInManagement() && r.Logged("found in SD management mode"));
    r.Run(2000);
    CHECK(r.wf.ReaderState() == ReaderState::Local); // only an explicit request shares
    CHECK(r.wf.RequestShare() && r.Until([&] { return r.wf.ReaderState() == ReaderState::Shared; }, 3000));
    std::printf("PASS found in management: no automatic share, explicit READER SHARE works\n");
}

void TestDdpErrKeepsSession() {
    Rig r;
    r.dev.StartInManagement();
    r.Run(2000);
    CHECK(r.session.Connected() && !r.session.Device()->Has("led") && !r.session.Device()->Has("sdread"));
    CHECK(r.session.Send("LED m=CHASE"));
    r.Run(200);
    CHECK(r.session.Connected() && r.Logged("Firmware error: ERR c=LED e=UNSUPPORTED"));
    std::printf("PASS DDP: an ERR for an unsupported command keeps the session\n");
}

// ------------------------------------------------------------------ Looper workflow
void TestProbeProfiles() {
    Rig r(kLooper);
    r.Run(1500);
    CHECK(r.wf.GetPhase() == Phase::Performance && r.wf.GetProfile().id == Instrument::Looper);
    CHECK(r.wf.Status()->firmware == "0.6.0" && r.wf.CanShare() && r.wf.Name() == "Looper");
    Rig n;
    n.Run(1500);
    CHECK(n.wf.GetPhase() == Phase::Performance && n.wf.GetProfile().id == Instrument::Nebulae);
    CHECK(n.wf.Status()->firmware == "0.2.1");
    std::printf("PASS probe: both STATUS requests go out, the answering instrument selects the profile\n");
}

void TestLooperRoundTrip() {
    Rig r(kLooper);
    r.Run(1500);
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Restarting; }, 1000));
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Management; }, kSimRestartMs + 5000));
    CHECK(r.session.Device()->app == "LooperOnDaisy" && r.session.Device()->Has("loopmgmt"));
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Shared; }, 5000));
    CHECK(Contains(r.wf.Message(), "looper/loops"));
    CHECK(r.Until([&] { return r.session.Mgmt() && !r.session.Mgmt()->can_return; }, 3000));
    CHECK(!r.wf.ReturnToPerformance());
    r.dev.Management().ReaderEject();
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Ejected; }, 3000));
    CHECK(r.wf.ReturnToPerformance());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Returning; }, 3000));
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance; }, 10000));
    CHECK(r.dev.GetPersonality() == SimInstrument::Personality::Perf && r.dev.restarts == 2);
    CHECK(Contains(r.wf.Message(), "loop.wav") && r.wf.GetProfile().id == Instrument::Looper);
    std::printf("PASS looper round trip: LP SysEx, LOOPER status line, LOOPER PERFORM\n");
}

void TestLooperSaving() {
    Rig r(kLooper);
    r.dev.unsaved = true;
    r.Run(1500);
    CHECK(r.wf.Status()->unsaved && r.wf.CanShare());
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Saving; }, 1000));
    CHECK(Contains(r.wf.Message(), "loop.wav") && r.dev.Saving() && r.dev.restarts == 0);
    // The workflow asks again until the firmware accepts; no confirmation is needed.
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Restarting; }, kSimSaveMs + 2000));
    CHECK(!r.dev.unsaved && r.dev.restarts == 1);
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Shared; }, kSimRestartMs + 5000));
    std::printf("PASS looper saving: SAVING repeated every 0.5 s with the same token until ACCEPTED\n");
}

void TestLooperSaveFailedConfirm() {
    Rig r(kLooper);
    r.dev.unsaved = true;
    r.dev.save_fails = true;
    r.Run(1500);
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Confirm; }, kSimSaveMs + 3000));
    CHECK(r.wf.NeedsConfirmation() && Contains(r.wf.Message(), "LOSES") && r.dev.restarts == 0);
    CHECK(!r.wf.RequestShare()); // only an explicit confirmation continues
    r.wf.CancelConfirmation();
    CHECK(r.wf.GetPhase() == Phase::Performance && r.dev.restarts == 0);
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Confirm; }, 2000));
    CHECK(r.wf.RequestShare(true));
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Management; }, 10000));
    std::printf("PASS looper failed save: CONFIRM_REQUIRED, cancel, confirm\n");
}

void TestLooperSaveCancelTimeout() {
    Rig r(kLooper);
    r.dev.unsaved = true;
    r.Run(1500);
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Saving; }, 1000));
    r.wf.CancelConfirmation();
    CHECK(r.wf.GetPhase() == Phase::Performance && Contains(r.wf.Message(), "nothing was shared"));
    r.Run(2000);
    CHECK(r.dev.restarts == 0); // no further SHARE after the cancel
    // A save that never finishes: the panel gives up after kSaveTimeoutMs.
    r.dev.unsaved = true;
    r.dev.save_fails = false;
    // Keep the device "saving" forever: it reports SAVING as long as unsaved and saving; we
    // re-arm unsaved after every simulated save completes, which makes every retry answer SAVING.
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Saving; }, 1000));
    uint32_t elapsed = 0;
    while(elapsed < SdShareWorkflow::kSaveTimeoutMs - 1000) {
        r.dev.unsaved = true;
        r.Run(100);
        elapsed += 100;
    }
    CHECK(r.wf.GetPhase() == Phase::Saving);
    CHECK(r.Until([&] {
        r.dev.unsaved = true;
        return r.wf.GetPhase() == Phase::Performance;
    }, 3000));
    CHECK(Contains(r.wf.Error(), "not saved") && r.dev.restarts == 0);
    std::printf("PASS looper save cancel and timeout: cancel stops the retries, 180 s gives up\n");
}

void TestLooperRefusals() {
    struct Case {
        void (*setup)(SimInstrument&);
        const char* error;
    } cases[] = {{[](SimInstrument& d) { d.recording = true; }, "recording"},
                 {[](SimInstrument& d) { d.importing = true; }, "importing"},
                 {[](SimInstrument& d) { d.card = false; }, "No microSD"}};
    for(const Case& c : cases) {
        Rig r(kLooper);
        r.Run(1500);
        c.setup(r.dev);
        CHECK(r.wf.RequestShare()); // CanShare from the previous STATUS; the firmware refuses
        r.Run(200);
        CHECK(r.wf.GetPhase() == Phase::Performance && Contains(r.wf.Error(), c.error) && r.dev.restarts == 0);
        r.Run(1200);
        CHECK(!r.wf.CanShare());
    }
    std::printf("PASS looper refusals: recording, importing, no card\n");
}

void TestLooperFoundInManagement() {
    Rig r(kLooper);
    r.dev.StartInManagement();
    r.Run(3000);
    CHECK(r.wf.GetPhase() == Phase::Management && r.wf.GetProfile().id == Instrument::Looper);
    CHECK(r.wf.ReaderState() == ReaderState::Local && r.session.Mgmt() && r.session.Mgmt()->action == "STATUS");
    r.Run(2000);
    CHECK(r.wf.ReaderState() == ReaderState::Local);
    CHECK(r.wf.ReturnToPerformance());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance; }, 10000));
    std::printf("PASS looper found in management: LOCAL, return with LOOPER PERFORM\n");
}

void TestSimLooperRejectsNeb() {
    Rig r(kLooper);
    r.dev.StartInManagement();
    r.Run(2000);
    CHECK(r.session.Send("NEB a=PERFORM"));
    r.Run(300);
    CHECK(r.dev.GetPersonality() == SimInstrument::Personality::Mgmt && r.dev.restarts == 0);
    CHECK(r.Logged("Firmware error: ERR c=NEB e=UNKNOWN"));
    std::printf("PASS simulator: the looper rejects NEB, nothing restarts\n");
}

// ------------------------------------------------------------------ DDP session rules
void TestDdpReaderTimeoutUnknown() {
    Rig r;
    r.dev.StartInManagement();
    r.Run(2000);
    CHECK(r.wf.ReaderState() == ReaderState::Local);
    r.dev.Management().responsive = false; // the firmware hangs
    CHECK(r.wf.RequestShare() && r.session.ReaderPending());
    r.Run(ddp::DdpSession::kReaderCommandTimeoutMs + 100);
    CHECK(!r.session.ReaderPending() && !r.wf.ReaderState() && !r.wf.Ejected() && !r.wf.CanReturn());
    // The timed-out command and the following receive timeout both say so; the session
    // then reconnects and asks STATUS only.
    CHECK(r.Logged("Card reader command timed out; ownership is unknown") && Contains(r.wf.Message(), "ownership is unknown"));
    r.dev.Management().responsive = true;
    CHECK(r.Until([&] { return r.session.Connected() && r.wf.ReaderState().has_value(); }, 8000));
    CHECK(r.wf.ReaderState() == ReaderState::Shared); // the firmware had shared; nothing was repeated
    std::printf("PASS DDP: a timed-out READER command makes ownership unknown, never ejected\n");
}

void TestDdpSharedRxTolerance() {
    Rig r;
    r.dev.StartInManagement();
    r.Run(2000);
    r.dev.Management().reader_init_ms = 3000; // SD initialization blocks the main loop
    CHECK(r.wf.RequestShare());
    r.Run(3500);
    CHECK(r.session.Connected()); // 20 s reader tolerance while the command is pending
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Shared; }, 3000));
    // While SHARED a 4 s silence is tolerated (5 s), a 6 s one is not.
    r.dev.Management().responsive = false;
    r.Run(4000);
    CHECK(r.session.Connected());
    r.Run(2000);
    CHECK(!r.session.Connected() && r.wf.GetPhase() == Phase::Management && Contains(r.wf.Message(), "ownership is unknown"));
    r.dev.Management().responsive = true;
    CHECK(r.Until([&] { return r.session.Connected(); }, 8000));
    CHECK(r.wf.ReaderState() == ReaderState::Shared); // reconnect asked STATUS only
    std::printf("PASS DDP: 5 s receive tolerance while SHARED, 1.5 s otherwise, STATUS only after reconnect\n");
}

void TestDdpPendingActionMatch() {
    // A periodic READER STATUS must not complete a pending SHARE; only a=SHARE does.
    Rig r;
    r.dev.StartInManagement();
    r.Run(2000);
    r.dev.Management().share_reply_delay_ms = 2500; // the a=SHARE answer comes late, STATUS lines keep coming
    CHECK(r.wf.RequestShare() && r.session.ReaderPending());
    r.Run(2000);
    CHECK(r.session.ReaderPending());                                                 // two periodic STATUS lines arrived …
    CHECK(r.session.Reader() && r.session.Reader()->action == "STATUS" && r.wf.ReaderState() == ReaderState::Shared); // … and were parsed
    CHECK(!r.wf.CanShareInManagement() && !r.wf.CanReturn());                          // but nothing is decided while pending
    CHECK(r.Until([&] { return !r.session.ReaderPending(); }, 3000));
    CHECK(r.session.Reader() && r.wf.ReaderState() == ReaderState::Shared);
    r.Run(1200); // the state stays with the periodic lines
    CHECK(r.wf.ReaderState() == ReaderState::Shared && !r.session.ReaderPending());
    std::printf("PASS DDP: only the matching READER action completes a pending command\n");
}

void TestWfBusyReaderBlocksReturn() {
    // The Python reference refuses RETURN while the BOT transport is busy, even after EJECTED
    // (test_card_reader.py::test_busy_transport_prevents_return_even_after_eject).
    Rig r;
    r.dev.StartInManagement();
    r.Run(2000);
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Shared; }, 3000));
    r.dev.Management().reader_busy = true; // Windows is still inside a SCSI command
    r.dev.Management().ReaderEject();
    CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Ejected; }, 3000));
    CHECK(r.session.Reader() && r.session.Reader()->busy);
    CHECK(r.wf.Ejected() && !r.wf.CanReturn() && !r.wf.ReturnToPerformance());
    r.Run(2000);
    CHECK(r.dev.Management().reader_state == ReaderState::Ejected && r.dev.restarts == 0); // nothing was sent
    r.dev.Management().reader_busy = false;
    CHECK(r.Until([&] { return r.session.Reader() && !r.session.Reader()->busy; }, 3000));
    CHECK(r.wf.CanReturn() && r.wf.ReturnToPerformance());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance; }, 10000));
    std::printf("PASS busy reader: RETURN refused while busy=1 even after EJECTED, allowed once busy=0\n");
}

// T3: a port that does not answer STATUS is closed after 1 s and tried again after 5 s.
void TestProbeBackoff() {
    Rig r;
    r.dev.answer_sysex = false; // a Toy Piano: no NB/LP protocol at all
    r.Run(1500);
    CHECK(r.midi.opened == 1 && !r.wf.LinkOpen() && r.wf.GetPhase() == Phase::NotFound);
    r.Run(3000);
    CHECK(r.midi.opened == 1); // backed off
    r.Run(2500);
    CHECK(r.midi.opened == 2); // probed again after 5 s
    r.dev.answer_sysex = true;
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance; }, 8000));
    // A port that refuses the very first send is backed off the same way (finding 11).
    struct Refusing : MidiLink {
        bool Send(const uint8_t*, std::size_t) override { return false; }
        bool Poll(std::vector<std::vector<uint8_t>>&) override { return true; }
        void Close() override {}
    };
    struct RefusingPorts : MidiPorts {
        int opened = 0;
        std::vector<std::string> List() override { return {"Refusing"}; }
        std::unique_ptr<MidiLink> Open(const std::string&, std::string&) override {
            ++opened;
            return std::make_unique<Refusing>();
        }
    };
    Rig q;
    RefusingPorts ports;
    q.wf.SetMidiPorts(&ports);
    q.Run(3500);
    CHECK(ports.opened == 1);
    q.Run(2500);
    CHECK(ports.opened == 2);
    std::printf("PASS probe backoff: silent port closed after 1 s, retried after 5 s; refused send backed off too\n");
}

// T13/T14/T15 through the workflow: replies the simulator never produces on its own.
void TestStaleAndErrorReplies() {
    struct Rewriting : MidiLink {
        std::unique_ptr<MidiLink> inner;
        int mode = 0; // 1: foreign token, 2: ERROR reply, 3: BAD_REQUEST, 4: unknown code 5 (Nebulae)
        bool Send(const uint8_t* b, std::size_t n) override { return inner->Send(b, n); }
        bool Poll(std::vector<std::vector<uint8_t>>& out) override {
            if(!inner->Poll(out))
                return false;
            for(std::vector<uint8_t>& m : out) {
                if(m.size() != 12 || m[5] != (kCmdShare | kReplyBit))
                    continue;
                if(mode == 1)
                    m[7] ^= 0x01; // another host's token
                else if(mode == 2)
                    m = {0xF0, 0x7D, 0x4E, 0x42, 0x01, uint8_t(kCmdError | kReplyBit), kCmdShare, 3, 0xF7}; // LENGTH
                else if(mode == 3)
                    m[6] = 4;
                else if(mode == 4)
                    m[6] = 5;
            }
            return true;
        }
        void Close() override { inner->Close(); }
    };
    struct Ports : MidiPorts {
        SimMidiPorts& inner;
        Rewriting* last = nullptr;
        explicit Ports(SimMidiPorts& i) : inner(i) {}
        std::vector<std::string> List() override { return inner.List(); }
        std::unique_ptr<MidiLink> Open(const std::string& name, std::string& error) override {
            auto link = inner.Open(name, error);
            if(!link)
                return nullptr;
            auto w = std::make_unique<Rewriting>();
            w->inner = std::move(link);
            last = w.get();
            return w;
        }
    };
    Rig r;
    r.dev.live = true; // every SHARE answers CONFIRM_REQUIRED: no restart, the device stays
    Ports ports(r.midi);
    r.wf.SetMidiPorts(&ports);
    r.Run(1500);
    CHECK(r.wf.GetPhase() == Phase::Performance && ports.last);
    // T14: a reply with another token is ignored; the request times out after 2 s.
    ports.last->mode = 1;
    CHECK(r.wf.RequestShare());
    r.Run(1000);
    CHECK(r.wf.GetPhase() == Phase::Requesting);
    // (the next STATUS reply clears the error text again, as in the Python panel: check at once)
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance && Contains(r.wf.Error(), "No answer"); },
                  SdShareWorkflow::kShareTimeoutMs + 100));
    CHECK(r.dev.restarts == 0);
    // T15: an ERROR reply ends the request with the firmware's reason.
    ports.last->mode = 2;
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance && Contains(r.wf.Error(), "Firmware rejected command 0x02: LENGTH"); }, 500));
    // T13: BAD_REQUEST and an unknown code are refusals.
    ports.last->mode = 3;
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance && Contains(r.wf.Error(), "Request refused (BAD_REQUEST)"); }, 500));
    ports.last->mode = 4;
    CHECK(r.wf.RequestShare());
    CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance && Contains(r.wf.Error(), "Request refused (CODE5)"); }, 500));
    CHECK(r.dev.restarts == 0 && r.Logged("refused SD management: CODE5"));
    std::printf("PASS stale and error replies: foreign token ignored (timeout), ERROR, BAD_REQUEST, CODE5 refuse\n");
}

// The clock is milliseconds since boot and may start anywhere, also above 2^31 or just
// below the 32-bit wrap: every timer must still fire (finding 2).
void TestClockWrap() {
    for(uint32_t start : {0x80000000u, 0xFFFFF000u, 0xFFFFFFFFu - 500u}) {
        Rig r;
        r.now = start;
        r.Run(1500);
        CHECK(r.wf.GetPhase() == Phase::Performance);
        CHECK(r.wf.RequestShare());
        CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Shared; }, 25000));
        r.dev.Management().ReaderEject();
        CHECK(r.Until([&] { return r.wf.ReaderState() == ReaderState::Ejected; }, 3000));
        CHECK(r.wf.ReturnToPerformance());
        CHECK(r.Until([&] { return r.wf.GetPhase() == Phase::Performance; }, 25000));
        // The CDC side alone, found already in management, with the same start.
        Rig m;
        m.now = start;
        m.dev.StartInManagement();
        m.Run(3000);
        CHECK(m.wf.GetPhase() == Phase::Management && m.session.Connected());
    }
    std::printf("PASS clock wrap: full cycle and CDC discovery with a clock starting at 2^31 and across 2^32\n");
}

void TestDdpHandshake() {
    // No DDP device on the COM port: HELLO repeats, 1 s deadline, 5 s backoff, then retry.
    struct Silent : ddp::SerialLink {
        int* hellos;
        explicit Silent(int* h) : hellos(h) {}
        bool Read(std::vector<uint8_t>& out) override {
            out.clear();
            return true;
        }
        bool Write(const uint8_t* d, std::size_t n) override {
            if(std::string(d, d + n).find("HELLO") != std::string::npos)
                ++*hellos;
            return true;
        }
        void Close() override {}
    };
    struct SilentPorts : ddp::SerialPorts {
        int opened = 0, hellos = 0;
        std::vector<ddp::PortInfo> List() override {
            return {{"COM9", ddp::kUsbVid, ddp::kUsbPid, "x"}, {"COM3", 0x1234, 0x5678, "not a Daisy"}};
        }
        std::unique_ptr<ddp::SerialLink> Open(const std::string& device, std::string&) override {
            CHECK(device == "COM9"); // the non-Daisy VID/PID is never opened
            ++opened;
            return std::make_unique<Silent>(&hellos);
        }
    };
    SilentPorts ports;
    ddp::DdpSession session(&ports);
    uint32_t now = 5000;
    for(int i = 0; i < 60; ++i) // 1.2 s
        session.Tick(now += 20);
    CHECK(ports.opened == 1 && ports.hellos >= 3 && session.State() == ddp::ConnState::Searching);
    for(int i = 0; i < 200; ++i) // 4 s: still backed off
        session.Tick(now += 20);
    CHECK(ports.opened == 1);
    for(int i = 0; i < 100; ++i) // 2 s more: retried
        session.Tick(now += 20);
    CHECK(ports.opened == 2);
    // A device with another protocol version is incompatible and backed off.
    struct Wrong : Silent {
        bool pending = false;
        int ignored = 0;
        Wrong() : Silent(&ignored) {}
        bool Write(const uint8_t* d, std::size_t n) override {
            Silent::Write(d, n);
            pending = true;
            return true;
        }
        bool Read(std::vector<uint8_t>& out) override {
            out.clear();
            if(pending) {
                pending = false;
                ddp::BuildFrame("HELLO proto=2 fw=9.9.9 app=Future", out);
            }
            return true;
        }
    };
    struct WrongPorts : ddp::SerialPorts {
        std::vector<ddp::PortInfo> List() override { return {{"COM9", ddp::kUsbVid, ddp::kUsbPid, "x"}}; }
        std::unique_ptr<ddp::SerialLink> Open(const std::string&, std::string&) override { return std::make_unique<Wrong>(); }
    };
    WrongPorts wrong;
    ddp::DdpSession s2(&wrong);
    for(int i = 0; i < 100; ++i)
        s2.Tick(now += 20);
    CHECK(s2.State() == ddp::ConnState::Incompatible && !s2.Connected());
    std::printf("PASS DDP handshake: HELLO retry, 1 s deadline, 5 s backoff, VID/PID filter, incompatible protocol\n");
}

void TestButtonRules() {
    // The predicates the SD card page binds its buttons to (test_performance_profile_shows_status...).
    Rig r;
    CHECK(r.wf.GetPhase() == Phase::NotFound && !r.wf.CanShare() && !r.wf.CanReturn() && !r.wf.CanAcknowledge());
    r.Run(1500);
    CHECK(r.wf.CanShare() && !r.wf.NeedsConfirmation() && !r.wf.CanReturn() && !r.wf.CanAcknowledge() && !r.wf.Ejected());
    CHECK(!r.wf.ManagementConnected() && !r.wf.CanShareInManagement());
    CHECK(std::string(PhaseText(r.wf.GetPhase())) == "Performance (USB-MIDI)");
    // Disconnecting the MIDI side (user) drops to NotFound and never touches the reader.
    r.wf.ReleaseMidi();
    CHECK(r.wf.GetPhase() == Phase::NotFound && !r.wf.LinkOpen() && !r.wf.Status());
    r.Run(1500);
    CHECK(r.wf.GetPhase() == Phase::Performance); // the port is still listed: found again
    std::printf("PASS button rules: predicates in NotFound and Performance, MIDI release\n");
}
}

int main() {
    TestVectors();
    TestCodecs();
    TestNebulaeRoundTrip();
    TestForeignSysExSkipped();
    TestNebulaeConfirmCancel();
    TestNebulaeRefusals();
    TestShareTimeoutAndLost();
    TestWrongPersonalityR1();
    TestRestartTimeout();
    TestInterruptionQuarantine();
    TestCloseNeverReturnsCard();
    TestFoundInManagement();
    TestDdpErrKeepsSession();
    TestProbeProfiles();
    TestLooperRoundTrip();
    TestLooperSaving();
    TestLooperSaveFailedConfirm();
    TestLooperSaveCancelTimeout();
    TestLooperRefusals();
    TestLooperFoundInManagement();
    TestSimLooperRejectsNeb();
    TestDdpReaderTimeoutUnknown();
    TestDdpSharedRxTolerance();
    TestDdpPendingActionMatch();
    TestWfBusyReaderBlocksReturn();
    TestProbeBackoff();
    TestStaleAndErrorReplies();
    TestClockWrap();
    TestDdpHandshake();
    TestButtonRules();
    if(failures) {
        std::fprintf(stderr, "FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: SD share core against the simulated Nebulae and Looper\n");
}
