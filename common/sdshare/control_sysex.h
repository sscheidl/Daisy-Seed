// Control SysEx for firmwares with SD management (USB-MIDI, performance personality).
// Same frame as Nebulae on Daisy 0.2.0 (instruments/granular/docs/CONTROL_PROTOCOL.md),
// each firmware with its own two prefix bytes after the non-commercial ID 0x7D:
//
//   request  F0 7D <p0> <p1> <ver=01> <cmd> <payload...> F7
//   reply    F0 7D <p0> <p1> 01 <cmd|0x40> <payload...> F7
//
// STATUS payloads are firmware-specific; SHARE (confirm byte + 4-byte token, reply:
// result + token) and the error reply are common. Header-only, host-tested.
#pragma once
#include <cstddef>
#include <cstdint>

namespace sdshare::sysex {

constexpr uint8_t kManufacturer = 0x7D;
constexpr uint8_t kVersion = 0x01;
constexpr uint8_t kReplyBit = 0x40;
enum Command : uint8_t { kStatus = 0x01, kShare = 0x02, kError = 0x7F };
enum ShareResult : uint8_t {
    kAccepted = 0,         // restart into SD management follows the reply
    kConfirmRequired = 1,  // volatile content would be lost: resend with confirm=1
    kBusy = 2,             // recording, importing, loading ... : try again later
    kNoCard = 3,
    kBadRequest = 4,
    kPending = 5,          // accepted; the restart follows once the card is in sync (Looper:
                           // the current loop is saved first). STATUS shows the progress.
};
enum ErrorCode : uint8_t { kErrVersion = 1, kErrUnknown = 2, kErrLength = 3 };

struct Prefix {
    uint8_t a, b;  // e.g. 'N','B' (Nebulae), 'L','P' (Looper)
};

// Body = bytes between F0 and F7 (libDaisy MidiEvent::sysex_data). True if it is ours.
inline bool IsOurs(const uint8_t* body, size_t size, Prefix p) {
    return size >= 5 && body[0] == kManufacturer && body[1] == p.a && body[2] == p.b;
}

class Reply {
  public:
    Reply(Prefix p, uint8_t command) {
        Put(0xF0); Put(kManufacturer); Put(p.a); Put(p.b); Put(kVersion);
        Put(uint8_t(command | kReplyBit));
    }
    Reply& Put(uint8_t b) { if(size_ < sizeof(bytes_)) bytes_[size_++] = b; return *this; }
    Reply& Data(uint8_t b) { return Put(b & 0x7F); }
    Reply& U14(uint32_t v) { if(v > 16383) v = 16383; return Data(uint8_t(v)).Data(uint8_t(v >> 7)); }
    Reply& Mask16(uint16_t m) { return Data(uint8_t(m)).Data(uint8_t(m >> 7)).Data(uint8_t(m >> 14)); }
    const uint8_t* Finish() { Put(0xF7); return bytes_; }
    size_t Size() const { return size_; }

  private:
    uint8_t bytes_[48] = {};
    size_t size_ = 0;
};

struct ShareRequest {
    bool confirm = false;
    uint8_t token[4] = {};
};

// Validates version and command of `body` (which IsOurs accepted). Returns kStatus or
// kShare (with `share` filled) or kError with the error reply already in `reply`.
inline Command Parse(const uint8_t* body, size_t size, Prefix p, ShareRequest& share, Reply& reply) {
    const uint8_t version = body[3], cmd = body[4];
    if(version != kVersion) {
        reply = Reply(p, kError);
        reply.Data(cmd).Data(kErrVersion);
        return kError;
    }
    if(cmd == kStatus) {
        if(size != 5) { reply = Reply(p, kError); reply.Data(cmd).Data(kErrLength); return kError; }
        return kStatus;
    }
    if(cmd == kShare) {
        if(size != 10 || body[5] > 1) {
            reply = Reply(p, kShare);
            reply.Data(kBadRequest);
            for(size_t i = 0; i < 4; ++i) reply.Data(size == 10 ? body[6 + i] : 0);
            return kError;
        }
        share.confirm = body[5] == 1;
        for(size_t i = 0; i < 4; ++i) share.token[i] = body[6 + i];
        return kShare;
    }
    reply = Reply(p, kError);
    reply.Data(cmd).Data(kErrUnknown);
    return kError;
}

inline void ShareReply(Reply& reply, Prefix p, ShareResult result, const ShareRequest& share) {
    reply = Reply(p, kShare);
    reply.Data(result);
    for(uint8_t t : share.token) reply.Data(t);
}

} // namespace sdshare::sysex
