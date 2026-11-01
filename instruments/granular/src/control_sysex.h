// Nebulae control SysEx over USB-MIDI (performance personality). Specification:
// docs/CONTROL_PROTOCOL.md. Header-only, no libDaisy, host-tested.
//
//   request  F0 7D 4E 42 <ver> <cmd> <payload...> F7
//   reply    F0 7D 4E 42 <ver> <cmd|0x40> <payload...> F7
//
// 0x7D is the non-commercial manufacturer ID shared with the bootloader entry
// ("BL", 42 4C) and Mod on Daisy ("MD", 4D 44); "NB" (4E 42) keeps them apart.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace neb::sysex {

constexpr uint8_t kVersion = 0x01;
constexpr uint8_t kPrefix[] = {0x7D, 0x4E, 0x42};
constexpr uint8_t kReplyBit = 0x40;
enum Command : uint8_t { kStatus = 0x01, kShare = 0x02, kError = 0x7F };
// SHARE results. Only kAccepted is followed by the restart into SD management.
enum ShareResult : uint8_t {
    kAccepted = 0,
    kConfirmRequired = 1,  // volatile Live audio would be lost: resend with confirm=1
    kBusy = 2,             // recording/finalizing or a sample load is running
    kNoCard = 3,
    kBadRequest = 4,
};
enum ErrorCode : uint8_t { kErrVersion = 1, kErrUnknown = 2, kErrLength = 3 };
constexpr size_t kMaxBody = 16;  // bytes after the prefix, before F7

// Collects one Nebulae request from the raw USB-MIDI byte stream (USB interrupt).
// Other SysEx, channel messages and real-time bytes pass untouched; the parsers for
// MIDI and the bootloader see the same bytes independently. One-slot mailbox: the
// main loop must Take() before the next request is accepted (the host retries).
class Collector {
  public:
    void Byte(uint8_t byte) {
        if(byte >= 0xF8) return;  // real-time bytes may appear inside SysEx
        if(byte == 0xF0) { active_ = true; used_ = 0; prefix_ = 0; return; }
        if(!active_) return;
        if(byte == 0xF7) {
            active_ = false;
            if(prefix_ == sizeof(kPrefix) && used_ >= 2 && !ready_.load(std::memory_order_acquire)) {
                for(size_t i = 0; i < used_; ++i) mailbox_[i] = body_[i];
                size_ = used_;
                ready_.store(true, std::memory_order_release);
            }
            return;
        }
        if(byte & 0x80) { active_ = false; return; }  // another status byte aborts
        if(prefix_ < sizeof(kPrefix)) {
            if(byte != kPrefix[prefix_]) { active_ = false; return; }
            ++prefix_;
            return;
        }
        if(used_ >= kMaxBody) { active_ = false; return; }  // too long: not ours
        body_[used_++] = byte;
    }
    // Main loop: copies a complete body (version, command, payload). False if none.
    bool Take(uint8_t* out, size_t& size) {
        if(!ready_.load(std::memory_order_acquire)) return false;
        size = size_;
        for(size_t i = 0; i < size_; ++i) out[i] = mailbox_[i];
        ready_.store(false, std::memory_order_release);
        return true;
    }

  private:
    uint8_t body_[kMaxBody] = {};
    uint8_t mailbox_[kMaxBody] = {};
    size_t used_ = 0, size_ = 0, prefix_ = 0;
    bool active_ = false;
    std::atomic<bool> ready_{false};
};
static_assert(std::atomic<bool>::is_always_lock_free, "SysEx mailbox must be lock-free");

// Snapshot of the performance state for STATUS and the SHARE decision (main loop).
struct Status {
    uint8_t fw[3] = {0, 0, 0};
    bool card = false, live = false, recording = false, loading = false;
    bool overrun = false, scanning = false;
    uint8_t bank = 0, banks = 0, playing_bank = 0, playing_slot = 0;
    uint16_t bank_mask = 0, slot_mask = 0;
    uint8_t load_error = 0;
    uint16_t overruns = 0;   // saturates at 16383
    uint8_t peak_load = 0;   // percent of the block deadline, saturates at 127
};

// Bounded writer for reply frames.
class Reply {
  public:
    explicit Reply(uint8_t command) {
        Put(0xF0);
        for(uint8_t b : kPrefix) Put(b);
        Put(kVersion);
        Put(uint8_t(command | kReplyBit));
    }
    Reply& Put(uint8_t b) { if(size_ < sizeof(bytes_)) bytes_[size_++] = b; return *this; }
    Reply& Data(uint8_t b) { return Put(b & 0x7F); }
    Reply& Mask16(uint16_t m) { return Data(uint8_t(m)).Data(uint8_t(m >> 7)).Data(uint8_t(m >> 14)); }
    const uint8_t* Finish() { Put(0xF7); return bytes_; }
    size_t Size() const { return size_; }

  private:
    uint8_t bytes_[48] = {};
    size_t size_ = 0;
};

constexpr size_t kStatusPayload = 19;

inline void WriteStatus(Reply& r, const Status& s) {
    r.Data(s.fw[0]).Data(s.fw[1]).Data(s.fw[2]).Data(0);  // personality 0 = performance
    r.Data(uint8_t((s.card ? 1 : 0) | (s.live ? 2 : 0) | (s.recording ? 4 : 0)
                   | (s.loading ? 8 : 0) | (s.overrun ? 16 : 0) | (s.scanning ? 32 : 0)));
    r.Data(s.bank).Data(s.banks).Data(s.playing_bank).Data(s.playing_slot);
    r.Mask16(s.bank_mask).Mask16(s.slot_mask).Data(s.load_error);
    const uint16_t o = s.overruns > 16383 ? 16383 : s.overruns;
    r.Data(uint8_t(o)).Data(uint8_t(o >> 7)).Data(s.peak_load > 127 ? 127 : s.peak_load);
}

// Decision for a SHARE request; pure so every path is host-tested.
inline ShareResult DecideShare(const Status& s, bool confirmed) {
    if(s.recording || s.loading) return kBusy;
    if(!s.card) return kNoCard;
    if(s.live && !confirmed) return kConfirmRequired;
    return kAccepted;
}

// Handles one request body and builds the reply. Sets `share` only for an accepted
// SHARE; the caller then stops audio and restarts. Returns false if nothing is sent.
inline bool Handle(const uint8_t* body, size_t size, const Status& status, Reply& reply,
                   bool& share) {
    share = false;
    if(size < 2) return false;
    const uint8_t cmd = body[1];
    if(body[0] != kVersion) {
        reply = Reply(kError);
        reply.Data(cmd).Data(kErrVersion);
        return true;
    }
    if(cmd == kStatus) {
        if(size != 2) { reply = Reply(kError); reply.Data(cmd).Data(kErrLength); return true; }
        reply = Reply(kStatus);
        WriteStatus(reply, status);
        return true;
    }
    if(cmd == kShare) {
        reply = Reply(kShare);
        if(size != 7 || body[2] > 1) {
            reply.Data(kBadRequest);
            for(size_t i = 0; i < 4; ++i) reply.Data(size == 7 ? body[3 + i] : 0);
            return true;
        }
        const ShareResult result = DecideShare(status, body[2] == 1);
        reply.Data(result);
        for(size_t i = 0; i < 4; ++i) reply.Data(body[3 + i]);
        share = result == kAccepted;
        return true;
    }
    reply = Reply(kError);
    reply.Data(cmd).Data(kErrUnknown);
    return true;
}

} // namespace neb::sysex
