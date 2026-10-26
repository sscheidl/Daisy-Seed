#pragma once
// SQ protocol on the editor side (sequencers/step-seq/docs/PROTOCOL.md, ARBEITSAUFTRAG
// 4.9, R09): a dispatcher that keeps Mod on Daisy (7D 4D 44), SQ (7D 53 51) and the
// bootloader reply (7D 42 4C) apart, and the SQ handshake (IDENTIFY, then the component
// list). Phase 1 delivers the handshake only; the pages follow in phase 5. The device
// state has priority: nothing is sent to the device without an explicit action (D13).
//
// Single-threaded like Session: OnMessage(), Tick() and Connect() on the GUI thread.
#include "Session.h"
#include "step_seq/sq_protocol.h"
#include <cstdint>
#include <string>
#include <vector>

namespace editor {

enum class Protocol { Unknown, ModOnDaisy, Sq, Bootloader };

// Which conversation a complete SysEx F0 ... F7 belongs to.
inline Protocol ClassifySysEx(const uint8_t* m, std::size_t n) {
    if(n < 5 || m[0] != 0xF0 || m[1] != 0x7D)
        return Protocol::Unknown;
    if(m[2] == 0x4D && m[3] == 0x44)
        return Protocol::ModOnDaisy;
    if(m[2] == 0x53 && m[3] == 0x51)
        return Protocol::Sq;
    if(m[2] == 0x42 && m[3] == 0x4C)
        return Protocol::Bootloader;
    return Protocol::Unknown;
}

struct SqComponent {
    std::string id, version;
    uint8_t kind = 0, schema = 0, params = 0;
};

struct SqIdentity {
    uint8_t protocol_major = 0, protocol_minor = 0;
    std::string product, version;
    uint32_t boot_id = 0, uid[3] = {};
    uint16_t capabilities = 0;
    uint8_t component_count = 0;
};

class SqSession {
  public:
    static constexpr double kTimeoutMs = 200.0;
    static constexpr int kRetries = 3;
    enum class Phase { Offline, Identify, Components, Ready, Failed };

    explicit SqSession(Transport& transport) : transport_(transport) {}

    // Starts the handshake: IDENTIFY, then GET_COMPONENT for each component.
    void Connect(double now_ms) {
        identity_ = SqIdentity{};
        components_.clear();
        phase_ = Phase::Identify;
        lost_ = 0;
        Request(sq::Identify, {}, now_ms);
    }
    // One complete SQ frame from the device. Returns true if it was consumed.
    bool OnMessage(const uint8_t* m, std::size_t n, double now_ms) {
        if(ClassifySysEx(m, n) != Protocol::Sq)
            return false;
        const bool reply_class = n >= 6 && (m[5] & sq::kResponse);
        if(!reply_class || n < sq::kOverhead) {
            ++ignored_; // requests from the other side or broken frames
            return true;
        }
        if(m[n - 1] != 0xF7 || m[7] != n - sq::kOverhead) {
            ++ignored_;
            return true;
        }
        const uint8_t crc = sq::Crc8(m + 4, n - 7);
        if(m[n - 3] != (crc >> 7) || m[n - 2] != (crc & 0x7F)) {
            ++ignored_;
            return true;
        }
        if(m[5] == sq::kMonitorEvent) {
            ++monitor_frames_;
            return true;
        }
        if(!waiting_ || m[6] != rid_) {
            ++ignored_; // stale or duplicate reply
            return true;
        }
        sq::Reader r(m + 8, n - sq::kOverhead);
        if(m[5] == sq::kNak) {
            waiting_ = false;
            r.U7();
            last_error_ = r.U7();
            phase_ = Phase::Failed;
            return true;
        }
        if(m[5] != (request_type_ | sq::kResponse)) {
            ++ignored_;
            return true;
        }
        waiting_ = false;
        if(request_type_ == sq::Identify) {
            identity_.protocol_major = r.U7();
            identity_.protocol_minor = r.U7();
            identity_.product = Text(r);
            identity_.version = Text(r);
            identity_.boot_id = r.U32();
            for(auto& w : identity_.uid)
                w = r.U32();
            identity_.capabilities = r.U14();
            identity_.component_count = r.U7();
            if(!r.Ok() || identity_.protocol_major != sq::kVersion) {
                phase_ = Phase::Failed;
                return true;
            }
            NextComponent(now_ms);
        } else if(request_type_ == sq::GetComponent) {
            SqComponent c;
            r.U7();
            r.U7();
            c.id = Text(r);
            c.kind = r.U7();
            c.version = Text(r);
            c.schema = r.U7();
            c.params = r.U7();
            if(!r.Ok()) {
                phase_ = Phase::Failed;
                return true;
            }
            components_.push_back(c);
            NextComponent(now_ms);
        }
        return true;
    }
    // Retries after 200 ms with the same request ID (idempotent on the device).
    void Tick(double now_ms) {
        if(!waiting_ || now_ms - sent_at_ < kTimeoutMs)
            return;
        if(tries_ > kRetries) {
            waiting_ = false;
            ++lost_;
            phase_ = Phase::Failed;
            return;
        }
        Send(now_ms);
    }

    Phase GetPhase() const { return phase_; }
    const SqIdentity& Identity() const { return identity_; }
    const std::vector<SqComponent>& Components() const { return components_; }
    bool HasComponent(const std::string& id) const {
        for(const auto& c : components_)
            if(c.id == id)
                return true;
        return false;
    }
    unsigned Lost() const { return lost_; }
    unsigned Ignored() const { return ignored_; }
    unsigned MonitorFrames() const { return monitor_frames_; }
    uint8_t LastError() const { return last_error_; }

  private:
    static std::string Text(sq::Reader& r) {
        char buffer[32];
        r.Str(buffer, sizeof(buffer));
        return buffer;
    }
    void NextComponent(double now_ms) {
        if(components_.size() < identity_.component_count) {
            Request(sq::GetComponent, {uint8_t(components_.size())}, now_ms);
            phase_ = Phase::Components;
        } else {
            phase_ = Phase::Ready;
        }
    }
    void Request(uint8_t type, std::vector<uint8_t> payload, double now_ms) {
        rid_ = uint8_t((rid_ + 1) & 0x7F);
        request_type_ = type;
        uint8_t buffer[sq::kMaxFrame];
        sq::Writer w(buffer, sizeof(buffer));
        w.Begin(type, rid_);
        for(uint8_t b : payload)
            w.U7(b);
        frame_.assign(buffer, buffer + w.End());
        tries_ = 0;
        waiting_ = true;
        Send(now_ms);
    }
    void Send(double now_ms) {
        ++tries_;
        sent_at_ = now_ms;
        transport_.Send(frame_.data(), frame_.size());
    }

    Transport& transport_;
    Phase phase_ = Phase::Offline;
    SqIdentity identity_;
    std::vector<SqComponent> components_;
    std::vector<uint8_t> frame_;
    uint8_t rid_ = 0, request_type_ = 0, last_error_ = 0;
    bool waiting_ = false;
    int tries_ = 0;
    double sent_at_ = 0;
    unsigned lost_ = 0, ignored_ = 0, monitor_frames_ = 0;
};

} // namespace editor
