#pragma once
// DDP v1 session over USB CDC: discovery of the kit's COM port, HELLO handshake with
// retry, keep-alive, timeouts, reconnect, and the card-reader commands with their ownership
// guards. Port of diagnostics/windows/daisy_diag/session.py (DeviceSession) reduced to what
// the SD share needs (SD_SHARE_PORT.md § 1, § 5, § 10).
//
// Single-threaded and clock-injected: Tick(now_ms) runs on the GUI thread; the serial link
// is an interface whose Read() must never block (the Windows transport uses its own thread).
// Card safety: a timed-out READER command makes the reader state unknown, never "ejected";
// connecting only asks for STATUS, never SHARE or RETURN; Disconnect sends BYE and nothing else.
#include "Ddp.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace editor::ddp {
struct PortInfo {
    std::string device;      // "COM7"
    int vid = -1, pid = -1;  // -1 unknown
    std::string description;
    bool IsDaisyCandidate() const { return vid == kUsbVid && pid == kUsbPid; }
};

// An open serial port. Read() returns what arrived since the last call without waiting;
// false from Read() or Write() means the link is gone (unplugged, re-enumerated).
class SerialLink {
  public:
    virtual ~SerialLink() = default;
    virtual bool Read(std::vector<uint8_t>& out) = 0;
    virtual bool Write(const uint8_t* data, std::size_t n) = 0;
    virtual void Close() = 0;
};

class SerialPorts {
  public:
    virtual ~SerialPorts() = default;
    virtual std::vector<PortInfo> List() = 0;
    // nullptr with `error` set if the port cannot be opened.
    virtual std::unique_ptr<SerialLink> Open(const std::string& device, std::string& error) = 0;
};

enum class ConnState { Searching, Handshake, Connected, Incompatible, Error };

inline const char* ConnStateText(ConnState s) {
    switch(s) {
        case ConnState::Searching: return "Disconnected – searching for a device";
        case ConnState::Handshake: return "Connecting …";
        case ConnState::Connected: return "Connected";
        case ConnState::Incompatible: return "Incompatible protocol";
        case ConnState::Error: return "Connection error";
    }
    return "?";
}

class DdpSession {
  public:
    static constexpr uint32_t kHandshakeTimeoutMs = 1000, kHelloRetryMs = 300, kRxTimeoutMs = 1500,
                              kPingIntervalMs = 500, kScanIntervalMs = 1000, kFailedPortBackoffMs = 5000,
                              kErrorDisplayMs = 3000;
    // SD initialization may block the firmware; never implies eject or ownership.
    static constexpr uint32_t kReaderCommandTimeoutMs = 20000;
    // While Windows owns the card one polling write + programming wait can take 2 s.
    static constexpr uint32_t kReaderIoRxTimeoutMs = 5000;

    std::function<void(const std::string&)> log;
    std::function<void()> changed;

    explicit DdpSession(SerialPorts* ports) : ports_(ports) {}
    // The port provider may be swapped (real COM ports or a simulated cable).
    void SetPorts(SerialPorts* ports) { ports_ = ports; }

    // ------------------------------------------------------------------ state
    bool Connected() const { return state_ == ConnState::Connected; }
    ConnState State() const { return state_; }
    const std::string& StatusDetail() const { return detail_; }
    const std::string& Port() const { return port_; }
    const std::optional<DeviceInfo>& Device() const { return device_; }
    const std::optional<ReaderInfo>& Reader() const { return reader_; }
    const std::optional<MgmtInfo>& Mgmt() const { return mgmt_; }
    std::optional<bool> SdCard() const { return sd_card_; } // from ST
    bool ReaderSupported() const { return Connected() && device_ && device_->Has("sdmsc"); }
    bool ReaderPending() const { return reader_deadline_.has_value(); }
    int Reconnects() const { return reconnects_; }
    int FramesOk() const { return frames_ok_; }
    int FrameErrors() const { return frame_errors_; }
    int ContentErrors() const { return content_errors_; }
    bool LinkOpen() const { return link_ != nullptr; }

    // ------------------------------------------------------------------ actions
    bool Send(const std::string& body) {
        if(!link_ || state_ != ConnState::Connected)
            return false;
        return Write(body);
    }

    // READER a=STATUS|SHARE|RETURN with the ownership guards of the Python session.
    bool RequestReader(const std::string& action) {
        if(action != "STATUS" && action != "SHARE" && action != "RETURN")
            return false;
        if(!ReaderSupported() || ReaderPending())
            return false;
        if(action == "SHARE" && (!reader_ || reader_->state != ReaderState::Local || !sd_card_ || !*sd_card_))
            return false;
        if(action == "RETURN"
           && (!reader_
               || (reader_->state != ReaderState::Ejected && reader_->state != ReaderState::Interrupted
                   && reader_->state != ReaderState::Error)
               || reader_->busy))
            return false;
        if(!Send("READER a=" + action))
            return false;
        reader_deadline_ = now_ + kReaderCommandTimeoutMs;
        reader_action_ = action;
        return true;
    }

    // BYE while connected, then the link is dropped. Never returns or reshares the card.
    void Disconnect(const std::string& reason = "Disconnected by user") {
        if(link_ && state_ == ConnState::Connected) {
            std::vector<uint8_t> frame;
            BuildFrame("BYE", frame);
            link_->Write(frame.data(), frame.size()); // a failed write is irrelevant here
        }
        DropLink(reason, ConnState::Searching);
    }

    // ------------------------------------------------------------------ polling
    void Tick(uint32_t now_ms) {
        now_ = now_ms;
        if(!link_) {
            if(Elapsed(next_scan_)) {
                next_scan_ = now_ + kScanIntervalMs;
                TryConnect();
            }
            return;
        }
        std::vector<uint8_t> data;
        if(!link_->Read(data)) {
            DropLink("USB connection lost", ConnState::Searching);
            return;
        }
        if(!data.empty()) {
            const int errors_before = parser_.Errors();
            for(const std::string& body : parser_.Feed(data.data(), data.size())) {
                last_rx_ = now_;
                ++frames_ok_;
                Handle(body);
                if(!link_)
                    return;
            }
            frame_errors_ += parser_.Errors() - errors_before;
        }
        if(state_ == ConnState::Handshake) {
            if(Elapsed(handshake_deadline_)) {
                failed_ports_[port_] = now_ + kFailedPortBackoffMs;
                DropLink(port_ + ": no DDP reply to HELLO", ConnState::Searching);
                return;
            }
            if(Elapsed(next_hello_)) { // the first HELLO may arrive while the firmware boots
                next_hello_ = now_ + kHelloRetryMs;
                Write(CmdHello());
            }
        }
        if(state_ == ConnState::Connected) {
            if(reader_deadline_ && Elapsed(*reader_deadline_ + 1)) {
                reader_deadline_.reset();
                reader_action_.clear();
                reader_.reset(); // state unknown; do not infer ownership or a clean eject
                Log("Card reader command timed out; ownership is unknown");
                Changed();
            }
            const uint32_t rx_timeout =
                (ReaderSupported() && reader_ && reader_->state == ReaderState::Shared) ? kReaderIoRxTimeoutMs : kRxTimeoutMs;
            if(now_ - last_rx_ > rx_timeout && !reader_deadline_) {
                DropLink("Timeout: no data for " + std::to_string(rx_timeout / 1000) + "." + std::to_string(rx_timeout / 100 % 10) + " s",
                         ConnState::Error);
                return;
            }
            if(now_ - last_ping_ >= kPingIntervalMs) {
                last_ping_ = now_;
                ++ping_n_;
                Write(CmdPing(ping_n_));
            }
        }
    }

  private:
    bool Elapsed(uint32_t at) const { return int32_t(now_ - at) >= 0; }

    void Log(const std::string& text) {
        if(log)
            log(text);
    }
    void Changed() {
        if(changed)
            changed();
    }
    void SetState(ConnState state, const std::string& detail) {
        if(state == ConnState::Error)
            error_until_ = now_ + kErrorDisplayMs;
        if(state != state_ || detail != detail_) {
            state_ = state;
            detail_ = detail;
            Changed();
        }
    }

    std::vector<PortInfo> Candidates() {
        if(!ports_)
            return {};
        std::vector<PortInfo> all = ports_->List(), out;
        for(const PortInfo& p : all)
            if(p.IsDaisyCandidate())
                out.push_back(p);
        return out;
    }

    void TryConnect() {
        const std::vector<PortInfo> candidates = Candidates();
        for(const PortInfo& p : candidates) {
            auto backoff = failed_ports_.find(p.device);
            if(backoff != failed_ports_.end() && !Elapsed(backoff->second))
                continue;
            std::string error;
            std::unique_ptr<SerialLink> link = ports_->Open(p.device, error);
            if(!link) {
                failed_ports_[p.device] = now_ + kFailedPortBackoffMs;
                SetState(ConnState::Error, p.device + " cannot be opened: " + error);
                Log(p.device + ": open failed (" + error + ")");
                continue;
            }
            link_ = std::move(link);
            port_ = p.device;
            parser_ = FrameParser();
            handshake_deadline_ = now_ + kHandshakeTimeoutMs;
            last_rx_ = now_;
            SetState(ConnState::Handshake, p.device + ": HELLO sent");
            next_hello_ = now_ + kHelloRetryMs;
            if(Write(CmdHello()))
                return;
            // Writing HELLO failed: the port is already backed off, try the next one.
        }
        if(state_ == ConnState::Incompatible && !candidates.empty())
            return;
        if(state_ == ConnState::Error && !Elapsed(error_until_))
            return;
        SetState(ConnState::Searching, "No Daisy (VID 0483 / PID 5740) found");
    }

    bool Write(const std::string& body) {
        if(!link_)
            return false;
        std::vector<uint8_t> frame;
        if(!BuildFrame(body, frame))
            return false;
        if(!link_->Write(frame.data(), frame.size())) {
            DropLink("Send failed", ConnState::Error);
            return false;
        }
        return true;
    }

    void DropLink(const std::string& reason, ConnState state) {
        if(link_) {
            link_->Close();
            link_.reset();
        }
        if(state_ == ConnState::Handshake && !port_.empty())
            failed_ports_[port_] = now_ + kFailedPortBackoffMs; // other candidates get their turn
        device_.reset();
        reader_.reset();
        mgmt_.reset();
        sd_card_.reset();
        reader_deadline_.reset();
        reader_action_.clear();
        next_scan_ = now_ + kScanIntervalMs;
        SetState(state, reason);
        Log(reason);
    }

    void Handle(const std::string& body) {
        const Message msg = ParseMessage(body);
        std::string error;
        if(msg.type == "HELLO") {
            DeviceInfo info;
            if(!ParseHello(msg, info, error))
                return Invalid(error);
            device_ = info;
            if(info.protocol != kProtocolVersion) {
                failed_ports_[port_] = now_ + kFailedPortBackoffMs;
                DropLink("Device reports protocol " + std::to_string(info.protocol) + ", expected "
                             + std::to_string(kProtocolVersion),
                         ConnState::Incompatible);
                return;
            }
            if(state_ == ConnState::Connected)
                return; // answer to a repeated HELLO
            if(ever_connected_)
                ++reconnects_;
            ever_connected_ = true;
            last_ping_ = now_;
            SetState(ConnState::Connected, port_);
            Log("Connected to " + port_ + ": " + info.app + " " + info.firmware + ", protocol "
                + std::to_string(info.protocol) + (info.simulated ? " (SIMULATION)" : ""));
            reader_.reset();
            if(info.Has("sdmsc"))
                RequestReader("STATUS"); // STATUS only: never SHARE or RETURN on connect
            Changed();
        } else if(msg.type == "ST") {
            if(state_ != ConnState::Connected)
                return;
            bool sd = false;
            if(!ParseStSdCard(msg, sd))
                return Invalid("ST: field 'sd' missing or invalid");
            const bool was = sd_card_.has_value() && *sd_card_ == sd;
            sd_card_ = sd;
            if(!was)
                Changed();
        } else if(msg.type == "READER") {
            if(!ReaderSupported())
                return Invalid("READER without advertised sdmsc capability");
            ReaderInfo info;
            if(!ParseReader(msg, info, error))
                return Invalid(error);
            reader_ = info;
            if(reader_deadline_ && info.action == reader_action_) { // only the matching action completes the command
                reader_deadline_.reset();
                reader_action_.clear();
            }
            Changed();
        } else if(msg.type == "NEB" || msg.type == "LOOPER") {
            const bool nebulae = msg.type == "NEB";
            if(!(device_ && device_->Has(nebulae ? "nebmgmt" : "loopmgmt")))
                return Invalid(msg.type + " without advertised capability");
            MgmtInfo info;
            if(!ParseMgmt(msg, nebulae, info, error))
                return Invalid(error);
            mgmt_ = info;
            if(info.action == "PERFORM")
                Log(std::string(nebulae ? "Nebulae" : "Looper") + " accepted the return to performance");
            Changed();
        } else if(msg.type == "ERR") {
            const std::string* c = msg.Get("c");
            const std::string* e = msg.Get("e");
            if(c && *c == "READER") {
                reader_deadline_.reset();
                reader_action_.clear();
                reader_.reset();
                Changed();
            }
            if(state_ == ConnState::Handshake && e && *e == "PROTO") {
                failed_ports_[port_] = now_ + kFailedPortBackoffMs;
                DropLink("Firmware does not support protocol " + std::to_string(kProtocolVersion), ConnState::Incompatible);
                return;
            }
            Log("Firmware error: " + body);
        }
        // PONG, SYS, ACK and unknown message types are ignored (forward compatibility).
    }

    void Invalid(const std::string& error) {
        ++content_errors_;
        Log("Discarded invalid message: " + error);
    }

    SerialPorts* ports_;
    std::unique_ptr<SerialLink> link_;
    FrameParser parser_;
    ConnState state_ = ConnState::Searching;
    std::string detail_ = "Disconnected – searching for a device", port_;
    std::optional<DeviceInfo> device_;
    std::optional<ReaderInfo> reader_;
    std::optional<MgmtInfo> mgmt_;
    std::optional<bool> sd_card_;
    std::optional<uint32_t> reader_deadline_;
    std::string reader_action_;
    std::map<std::string, uint32_t> failed_ports_;
    uint32_t now_ = 0, next_scan_ = 0, handshake_deadline_ = 0, next_hello_ = 0, last_rx_ = 0, last_ping_ = 0,
             error_until_ = 0, ping_n_ = 0;
    int reconnects_ = 0, frames_ok_ = 0, frame_errors_ = 0, content_errors_ = 0;
    bool ever_connected_ = false;
};
}
