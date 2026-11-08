#pragma once
// Simulated Nebulae on Daisy and Looper with both USB personalities: performance on a
// simulated USB-MIDI port (control SysEx as instruments/granular/src/control_sysex.h and
// instruments/looper/src/control_sysex.h), SD management on a simulated CDC cable (DDP v1
// with the card reader's ownership rules). Port of diagnostics/windows/daisy_diag/
// nebulae_sim.py, looper_sim.py and the reader part of simulator.py. Simulation only: no
// drive is created on Windows. Faults the tests and the self-test inject: Windows eject,
// USB bus reset, card removal, a restart into the wrong personality (R1), a restart that
// never completes, a firmware that stops answering.
#include "SdShare.h"
#include <cstdio>
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace editor::sd {
constexpr uint32_t kSimRestartMs = 800; // simulated reset + bootloader skip + enumeration
constexpr uint32_t kSimSaveMs = 600;    // simulated write of the loop to looper/loop.wav
constexpr uint32_t kSimHostTimeoutMs = 3000;
constexpr const char* kSimMidiPort = "Daisy Seed Built In (Sim)";
constexpr const char* kSimComPort = "SIM";

class SimInstrument;

// DDP side of the SD management personality (firmware/src behaviour, reduced to what the
// instruments advertise: no LED, read test, CV or audio).
class SimManagement {
  public:
    SimManagement(SimInstrument& owner, const Profile& profile, bool card)
        : owner_(owner), profile_(profile), card_(card) {}

    // ---- control surface for tests and the self-test --------------------------------
    ddp::ReaderState reader_state = ddp::ReaderState::Local;
    bool responsive = true;          // false: behaves like a hung firmware (no output)
    uint32_t reader_init_ms = 0;     // SHARE blocks the main loop this long (card init)
    bool starting_first = false;     // SHARE reports STARTING once before SHARED (PROTOCOL.md state)
    bool reader_busy = false;        // the BOT transport is handling a command (READER busy=1)
    uint32_t share_reply_delay_ms = 0; // the READER a=SHARE answer comes this much later (periodic STATUS lines first)
    int bootloader_entries = 0;
    bool in_bootloader = false;

    void SetCard(bool present) { card_ = present; }
    bool Card() const { return card_; }
    // Model a successful Windows eject. Does not emulate the USB MSC stack.
    void ReaderEject() {
        if(reader_state == ddp::ReaderState::Shared) {
            reader_state = ddp::ReaderState::Ejected;
            SendReader("STATUS");
        }
    }
    // An externally powered board can lose its USB link without rebooting.
    void ReaderBusReset() {
        if(reader_state == ddp::ReaderState::Shared)
            reader_state = ddp::ReaderState::Interrupted;
        SendReader("STATUS");
    }
    void ReaderIoError() {
        if(reader_state == ddp::ReaderState::Shared)
            reader_state = ddp::ReaderState::Error;
        SendReader("STATUS");
    }
    bool Streaming() const { return streaming_; }

    // ---- byte streams ------------------------------------------------------------
    void Receive(const uint8_t* data, std::size_t n, uint32_t now) {
        Boot(now);
        for(std::size_t i = 0; i < n; ++i) {
            if(rx_pending_.size() < kRxRing)
                rx_pending_.push_back(data[i]);
        }
        ProcessRx(now);
    }

    std::vector<uint8_t> Output(uint32_t now) {
        Boot(now);
        if(!card_ && reader_state == ddp::ReaderState::Shared)
            reader_state = ddp::ReaderState::Interrupted;
        if(Busy(now))
            return {}; // main loop blocked in the SD driver: nothing is sent
        ProcessRx(now);
        if(responsive)
            RunMainLoop(now);
        std::vector<uint8_t> out;
        out.swap(out_);
        return responsive ? out : std::vector<uint8_t>{};
    }

  private:
    static constexpr std::size_t kRxRing = 1024;
    // Busy spans are short (card init); anything longer is a test clock that jumped.
    bool Busy(uint32_t now) const { return busy_until_ - now - 1u < (1u << 30); }
    void Boot(uint32_t now) {
        if(!booted_) {
            booted_ = true;
            boot_time_ = now;
            busy_until_ = now; // the clock may start anywhere: "not busy" is relative to now
            last_state_ = last_sys_ = now - 1000;
        }
    }
    uint32_t Uptime(uint32_t now) const { return booted_ ? now - boot_time_ : 0; }

    void ProcessRx(uint32_t now) {
        while(!rx_pending_.empty() && !Busy(now)) {
            const uint8_t byte = rx_pending_.front();
            rx_pending_.pop_front();
            for(const std::string& body : parser_.Feed(&byte, 1))
                Handle(body, now);
        }
    }

    void Send(const std::string& body) {
        std::vector<uint8_t> frame;
        ddp::BuildFrame(body, frame);
        out_.insert(out_.end(), frame.begin(), frame.end());
        ++tx_ok_;
    }

    void Expire(uint32_t now) {
        if(streaming_ && now - last_rx_ >= kSimHostTimeoutMs)
            streaming_ = false; // only a new HELLO restarts the stream
    }

    std::string Firmware() const { return std::string(FirmwareOf(profile_)) + "-sim"; }
    static const char* FirmwareOf(const Profile& p) { return p.id == Instrument::Nebulae ? "0.2.1" : "0.6.0"; }

    std::string StatusLine(const char* action) const;

    void SendReader(const char* action) {
        char text[96];
        std::snprintf(text, sizeof(text), "READER state=%s sectors=%u busy=%d a=%s", ddp::ReaderStateName(reader_state),
                      unsigned(sectors_), reader_busy ? 1 : 0, action);
        Send(text);
    }

    // ST alone (the main loop's periodic frame, as the firmware sends it).
    void SendStateOnly(uint32_t now) {
        char text[200];
        std::snprintf(text, sizeof(text),
                      "ST n=%u t=%u b=0000 pc=00000000000000000000000000000000 p=0,0,0,0,0,0,0,0 r=0,0,0,0,0,0,0,0 "
                      "s17=D s18=C sd=%d jl=0 jr=0 lm=OFF l=0000 br=100",
                      unsigned(seq_), unsigned(Uptime(now)), card_ ? 1 : 0);
        Send(text);
        ++seq_;
        last_state_ = now;
        last_sent_card_ = card_;
    }
    // ST plus SYS and the status line (after HELLO and GET, like the Python simulator).
    void SendState(uint32_t now) {
        SendStateOnly(now);
        SendSys(now);
    }

    void SendSys(uint32_t now) {
        char text[120];
        std::snprintf(text, sizeof(text), "SYS up=%u cbhz=1000 txok=%u txbusy=0 txdrop=0 rxok=%d rxbad=%d host=1 rate=%u",
                      unsigned(Uptime(now) / 1000), unsigned(tx_ok_), parser_.ok, parser_.Errors(), unsigned(rate_hz_));
        Send(text);
        last_sys_ = now;
        Send(StatusLine("STATUS"));
    }

    void RunMainLoop(uint32_t now) {
        Expire(now);
        if(!streaming_)
            return;
        const uint32_t period = 1000 / rate_hz_;
        const bool changed = last_sent_card_ != card_ && now - last_state_ >= 10;
        if(now - last_state_ >= period || changed)
            SendStateOnly(now);
        if(now - last_sys_ >= 1000) { // once per second: SYS, the status line and READER
            SendSys(now);
            SendReader("STATUS");
        }
        if(share_reply_at_ && int32_t(now - *share_reply_at_) >= 0) {
            share_reply_at_.reset();
            SendReader("SHARE");
        }
    }

    void Handle(const std::string& body, uint32_t now);

    SimInstrument& owner_;
    const Profile& profile_;
    bool card_;
    uint32_t sectors_ = 0, busy_until_ = 0, boot_time_ = 0, last_rx_ = 0, last_state_ = 0, last_sys_ = 0, seq_ = 0,
             rate_hz_ = 50;
    bool booted_ = false, streaming_ = false, last_sent_card_ = false;
    std::optional<uint32_t> share_reply_at_;
    std::deque<uint8_t> rx_pending_;
    ddp::FrameParser parser_;
    std::vector<uint8_t> out_;
    int tx_ok_ = 0;
};

// The J7 cable in SD management: unplugging ends the CDC port (the restart into
// performance has no COM port at all).
class SimCable {
  public:
    explicit SimCable(std::function<std::unique_ptr<SimManagement>()> factory) : factory_(std::move(factory)) {
        device_ = factory_();
    }
    bool Plugged() const { return plugged_; }
    int Generation() const { return generation_; }
    SimManagement& Device() { return *device_; }
    void Unplug() {
        plugged_ = false;
        ++generation_;
    }
    void Plug() {
        if(!plugged_) {
            device_ = factory_(); // the firmware restarts: fresh state
            plugged_ = true;
        }
    }
    std::vector<ddp::PortInfo> Ports() const {
        if(!plugged_)
            return {};
        return {{kSimComPort, ddp::kUsbVid, ddp::kUsbPid, "Simulated Daisy Seed3 (J7)"}};
    }

  private:
    std::function<std::unique_ptr<SimManagement>()> factory_;
    std::unique_ptr<SimManagement> device_;
    bool plugged_ = false;
    int generation_ = 0;
};

class SimSerialLink : public ddp::SerialLink {
  public:
    SimSerialLink(SimCable& cable, std::function<uint32_t()> clock)
        : cable_(cable), clock_(std::move(clock)), generation_(cable.Generation()) {}
    bool Read(std::vector<uint8_t>& out) override {
        if(!Alive())
            return false;
        out = cable_.Device().Output(clock_());
        if(cable_.Device().in_bootloader)
            cable_.Unplug(); // the ACK is out; the CDC port disappears with the reset
        return true;
    }
    bool Write(const uint8_t* data, std::size_t n) override {
        if(!Alive())
            return false;
        cable_.Device().Receive(data, n, clock_());
        return true;
    }
    void Close() override { closed_ = true; }

  private:
    bool Alive() const { return !closed_ && cable_.Plugged() && generation_ == cable_.Generation(); }
    SimCable& cable_;
    std::function<uint32_t()> clock_;
    int generation_;
    bool closed_ = false;
};

// Whole instrument: personality, restart timing, control SysEx, the cable.
class SimInstrument {
  public:
    enum class Personality { Perf, Mgmt };

    SimInstrument(const Profile& profile, std::function<uint32_t()> clock)
        : profile_(profile), clock_(std::move(clock)),
          cable_([this] { return std::make_unique<SimManagement>(*this, profile_, card); }) {
        mgmt_app = profile_.app;
        if(profile_.id == Instrument::Looper)
            loop = true;
    }
    SimInstrument(const SimInstrument&) = delete;
    SimInstrument& operator=(const SimInstrument&) = delete;

    // ---- state the tests set ---------------------------------------------------------
    bool card = true;
    // Nebulae
    bool live = false, recording = false, loading = false;
    int bank = 1, banks = 3, slot = 1;
    // Looper
    bool loop = false, unsaved = false, importing = false;
    bool save_fails = false;   // the card cannot be written
    bool save_failed = false;  // the last save failed (firmware: Fault::Write)
    int loop_ms = 12500;
    // both
    int overruns = 0;
    int restarts = 0;
    bool answer_sysex = true;                     // false: the firmware stops answering control SysEx
    bool wrong_personality_on_restart = false;    // R1: the boot intent is lost, the kit boots performance
    bool never_reenumerates = false;              // the restart never completes
    std::string mgmt_app;                         // HELLO app= of the SD management personality (default: the profile's)

    const Profile& GetProfile() const { return profile_; }
    Personality GetPersonality() const { return personality_; }
    bool Restarting() const { return restarting_; }
    SimCable& Cable() { return cable_; }
    SimManagement& Management() { return cable_.Device(); }
    bool Saving() const { return saving_until_.has_value(); }
    void StartInManagement() {
        personality_ = Personality::Mgmt;
        cable_.Plug();
    }
    // Power off and on: performance personality, fresh RAM, no COM port, reader state gone.
    void PowerCycle() {
        switch_at_.reset();
        restarting_ = false;
        personality_ = Personality::Perf;
        live = recording = false;
        saving_until_.reset();
        cable_.Unplug();
        ++restarts;
    }
    const std::string& MgmtApp() const { return mgmt_app; }

    // ---- time and personality ---------------------------------------------------------
    void Advance() {
        const uint32_t now = clock_();
        if(switch_at_ && !never_reenumerates && int32_t(now - *switch_at_) >= 0) {
            switch_at_.reset();
            restarting_ = false;
            personality_ = target_;
            live = recording = false; // RAM content is gone after any reset
            if(personality_ == Personality::Mgmt) {
                cable_.Plug();
                cable_.Device().SetCard(card);
            }
        }
        if(saving_until_ && int32_t(now - *saving_until_) >= 0) {
            saving_until_.reset();
            save_failed = save_fails;
            if(!save_fails)
                unsaved = false;
        }
        if(personality_ == Personality::Mgmt)
            unsaved = false; // after a reset the loop is the one in loop.wav
    }

    void Restart(Personality target, uint32_t now) {
        ++restarts;
        restarting_ = true;
        target_ = wrong_personality_on_restart ? Personality::Perf : target;
        switch_at_ = now + kSimRestartMs;
        if(target_ == Personality::Perf)
            cable_.Unplug(); // no COM port in the performance personality
    }

    std::vector<std::string> MidiPortNames() {
        Advance();
        if(personality_ == Personality::Perf && !restarting_)
            return {kSimMidiPort};
        return {};
    }
    std::vector<ddp::PortInfo> CdcPorts() {
        Advance();
        return cable_.Ports();
    }
    std::unique_ptr<SimSerialLink> OpenCdc(const std::string& port, std::string& error) {
        Advance();
        if(port != kSimComPort || !cable_.Plugged()) {
            error = "cannot open " + port;
            return nullptr;
        }
        return std::make_unique<SimSerialLink>(cable_, clock_);
    }
    bool MidiAlive() {
        Advance();
        return personality_ == Personality::Perf && !restarting_;
    }

    // ---- control SysEx, as the firmware's control_sysex.h ------------------------------
    // Returns an empty vector for foreign SysEx (e.g. the other instrument's probe).
    std::vector<uint8_t> HandleSysEx(const uint8_t* m, std::size_t n) {
        Advance();
        if(!answer_sysex)
            return {};
        if(n < 7 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != profile_.owner[0] || m[3] != profile_.owner[1] || m[n - 1] != 0xF7)
            return {};
        const uint8_t* body = m + 4;
        const std::size_t body_length = n - 5;
        const uint8_t version = body[0], cmd = body[1];
        std::vector<uint8_t> head = {0xF0, 0x7D, profile_.owner[0], profile_.owner[1], kVersion};
        auto with = [&](std::initializer_list<uint8_t> tail) {
            std::vector<uint8_t> out = head;
            out.insert(out.end(), tail.begin(), tail.end());
            out.push_back(0xF7);
            return out;
        };
        if(version != kVersion)
            return with({uint8_t(kCmdError | kReplyBit), cmd, 1});
        if(cmd == kCmdStatus && body_length == 2) {
            std::vector<uint8_t> out = head;
            out.push_back(kCmdStatus | kReplyBit);
            AppendStatus(out);
            out.push_back(0xF7);
            return out;
        }
        if(cmd == kCmdShare) {
            if(body_length != 7 || body[2] > 1)
                return with({uint8_t(kCmdShare | kReplyBit), 4, 0, 0, 0, 0});
            const bool confirm = body[2] == 1;
            uint8_t result;
            if(profile_.id == Instrument::Nebulae) {
                if(recording || loading)
                    result = 2;
                else if(!card)
                    result = 3;
                else if(live && !confirm)
                    result = 1;
                else
                    result = 0;
            } else {
                if(recording || importing)
                    result = 2;
                else if(!card)
                    result = 3;
                else if(unsaved && !confirm) {
                    if(save_failed && !Saving())
                        result = 1;
                    else {
                        result = 5;
                        if(!Saving())
                            saving_until_ = clock_() + kSimSaveMs;
                    }
                } else
                    result = 0;
            }
            if(result == 0) {
                saving_until_.reset();
                Restart(Personality::Mgmt, clock_());
            }
            return with({uint8_t(kCmdShare | kReplyBit), result, body[3], body[4], body[5], body[6]});
        }
        return with({uint8_t(kCmdError | kReplyBit), cmd, 2});
    }

  private:
    void AppendStatus(std::vector<uint8_t>& out) const {
        // NEBULAE_VERSION 0.2.1 / LOOPER_VERSION 0.6.0, the same strings as FirmwareOf().
        const uint8_t version[3] = {0, uint8_t(profile_.id == Instrument::Nebulae ? 2 : 6), uint8_t(profile_.id == Instrument::Nebulae ? 1 : 0)};
        out.insert(out.end(), version, version + 3);
        out.push_back(0); // performance personality
        if(profile_.id == Instrument::Nebulae) {
            const uint8_t flags = uint8_t((card ? 1 : 0) | (live ? 2 : 0) | (recording ? 4 : 0) | (loading ? 8 : 0)
                                          | (overruns ? 16 : 0));
            const int mask = (1 << banks) - 1;
            const uint8_t data[] = {flags, uint8_t(bank), uint8_t(banks), uint8_t(bank), uint8_t(slot),
                                    uint8_t(mask & 0x7F), uint8_t((mask >> 7) & 0x7F), uint8_t((mask >> 14) & 3),
                                    0x7F, 0x7F, 3, 0, uint8_t(overruns & 0x7F), uint8_t((overruns >> 7) & 0x7F), 35};
            out.insert(out.end(), data, data + sizeof(data));
        } else {
            const uint8_t flags = uint8_t((card ? 1 : 0) | (loop ? 2 : 0) | (unsaved ? 4 : 0) | (Saving() ? 8 : 0)
                                          | (recording ? 16 : 0) | (importing ? 32 : 0) | (overruns ? 64 : 0));
            const int ms = loop ? loop_ms : 0;
            const uint8_t data[] = {flags, uint8_t(save_failed ? 1 : 0), uint8_t(save_failed ? 2 : 0),
                                    uint8_t(ms & 0x7F), uint8_t((ms >> 7) & 0x7F), uint8_t((ms >> 14) & 0x7F),
                                    uint8_t(overruns & 0x7F), uint8_t((overruns >> 7) & 0x7F), 22};
            out.insert(out.end(), data, data + sizeof(data));
        }
    }

    const Profile& profile_;
    std::function<uint32_t()> clock_;
    SimCable cable_;
    Personality personality_ = Personality::Perf, target_ = Personality::Perf;
    bool restarting_ = false;
    std::optional<uint32_t> switch_at_, saving_until_;
};

inline std::string SimManagement::StatusLine(const char* action) const {
    const int ret = reader_state == ddp::ReaderState::Local ? 1 : 0;
    char text[120];
    if(profile_.id == Instrument::Nebulae)
        std::snprintf(text, sizeof(text), "NEB mode=SDMGMT fw=%s card=%d ret=%d bank=%d slot=%d a=%s", Firmware().c_str(),
                      card_ ? 1 : 0, ret, owner_.bank, owner_.slot, action);
    else
        std::snprintf(text, sizeof(text), "LOOPER mode=SDMGMT fw=%s card=%d ret=%d a=%s", Firmware().c_str(), card_ ? 1 : 0,
                      ret, action);
    return text;
}

inline void SimManagement::Handle(const std::string& body, uint32_t now) {
    Expire(now);
    last_rx_ = now;
    const ddp::Message msg = ddp::ParseMessage(body);
    const std::string& t = msg.type;
    if(t == "HELLO") {
        const std::string* proto = msg.Get("proto");
        if(!proto || *proto != std::to_string(ddp::kProtocolVersion)) {
            Send("ERR c=HELLO e=PROTO");
            return;
        }
        streaming_ = true;
        const Profile* claimed = ProfileForApp(owner_.MgmtApp());
        Send("HELLO proto=1 fw=" + Firmware() + " app=" + owner_.MgmtApp()
             + " dev=seed3-desktop boot=BOOT_SRAM bl=v6_1 libdaisy=f715e48 caps=btn,pot,potraw,sw17,sw18,sd,boot,sdmsc,"
             + (claimed ? claimed->capability : profile_.capability) + " sim=1");
        SendState(now);
        SendReader("STATUS");
        return;
    }
    if(t == "PING") {
        const std::string* n = msg.Get("n");
        Send(n ? "PONG n=" + *n : "ERR c=PING e=ARG");
        return;
    }
    if(t == "GET") {
        SendState(now);
        return;
    }
    if(t == profile_.status_message) {
        const std::string* action = msg.Get("a");
        if(!streaming_)
            Send(std::string("ERR c=") + t + " e=SESSION");
        else if(action && *action == "STATUS")
            Send(StatusLine("STATUS"));
        else if(action && *action == "PERFORM") {
            if(reader_state != ddp::ReaderState::Local)
                Send(std::string("ERR c=") + t + " e=STATE");
            else {
                Send(StatusLine("PERFORM"));
                owner_.Restart(SimInstrument::Personality::Perf, now);
            }
        } else
            Send(std::string("ERR c=") + t + " e=ARG");
        return;
    }
    if(t == "READER") {
        const std::string* action = msg.Get("a");
        if(!streaming_)
            Send("ERR c=READER e=SESSION");
        else if(!action || (*action != "STATUS" && *action != "SHARE" && *action != "RETURN"))
            Send("ERR c=READER e=ARG");
        else if(*action == "SHARE") {
            if(reader_state != ddp::ReaderState::Local || !card_)
                Send("ERR c=READER e=STATE");
            else {
                reader_state = starting_first ? ddp::ReaderState::Starting : ddp::ReaderState::Shared;
                sectors_ = 30436u * 2048u;
                busy_until_ = now + reader_init_ms;
                if(share_reply_delay_ms) {
                    share_reply_at_ = now + share_reply_delay_ms; // answered from the main loop later
                } else {
                    SendReader("SHARE");
                    if(starting_first) {
                        reader_state = ddp::ReaderState::Shared;
                        SendReader("STATUS");
                    }
                }
            }
        } else if(*action == "RETURN") {
            if(reader_state != ddp::ReaderState::Ejected && reader_state != ddp::ReaderState::Interrupted
               && reader_state != ddp::ReaderState::Error)
                Send("ERR c=READER e=STATE");
            else {
                reader_state = ddp::ReaderState::Local;
                sectors_ = 0;
                SendReader("RETURN");
            }
        } else
            SendReader("STATUS");
        return;
    }
    if(t == "BOOT") {
        if(!streaming_)
            Send("ERR c=BOOT e=SESSION");
        else if(reader_state != ddp::ReaderState::Local)
            Send("ERR c=BOOT e=BUSY");
        else {
            Send("ACK c=BOOT");
            in_bootloader = true;
            ++bootloader_entries;
        }
        return;
    }
    if(t == "BYE") {
        streaming_ = false;
        Send("ACK c=BYE");
        return;
    }
    if(t == "LED" || t == "SDTEST" || t == "CVOUT" || t == "TONE" || t == "SCOPE") {
        Send("ERR c=" + t + " e=UNSUPPORTED");
        return;
    }
    Send("ERR c=" + t.substr(0, 12) + " e=UNKNOWN"); // includes the other instrument's NEB/LOOPER
}

// The performance personality on the simulated USB-MIDI port.
class SimMidiLink : public MidiLink {
  public:
    explicit SimMidiLink(SimInstrument& owner) : owner_(owner) {}
    bool Send(const uint8_t* bytes, std::size_t n) override {
        if(!Check())
            return false;
        std::vector<uint8_t> reply = owner_.HandleSysEx(bytes, n);
        if(!reply.empty())
            pending_.push_back(std::move(reply));
        return true;
    }
    bool Poll(std::vector<std::vector<uint8_t>>& out) override {
        // A reply sent just before the reset (the SHARE ACK) still reaches the host.
        if(!pending_.empty() && !closed_) {
            out.swap(pending_);
            pending_.clear();
            return true;
        }
        out.clear();
        return Check();
    }
    void Close() override { closed_ = true; }
    bool Closed() const { return closed_; }

  private:
    bool Check() { return !closed_ && owner_.MidiAlive(); }
    SimInstrument& owner_;
    std::vector<std::vector<uint8_t>> pending_;
    bool closed_ = false;
};

class SimMidiPorts : public MidiPorts {
  public:
    explicit SimMidiPorts(SimInstrument& owner) : owner_(owner) {}
    std::vector<std::string> List() override { return owner_.MidiPortNames(); }
    std::unique_ptr<MidiLink> Open(const std::string& name, std::string& error) override {
        for(const std::string& n : owner_.MidiPortNames())
            if(n == name) {
                ++opened;
                return std::make_unique<SimMidiLink>(owner_);
            }
        error = "cannot open " + name;
        return nullptr;
    }
    int opened = 0;

  private:
    SimInstrument& owner_;
};

class SimSerialPorts : public ddp::SerialPorts {
  public:
    explicit SimSerialPorts(SimInstrument& owner) : owner_(owner) {}
    std::vector<ddp::PortInfo> List() override { return owner_.CdcPorts(); }
    std::unique_ptr<ddp::SerialLink> Open(const std::string& device, std::string& error) override {
        return owner_.OpenCdc(device, error);
    }

  private:
    SimInstrument& owner_;
};
}
