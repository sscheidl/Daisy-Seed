// SPDX-License-Identifier: MIT
// Looper control SysEx over USB-MIDI (performance personality): STATUS and SHARE,
// used by the Windows control panel to hand the microSD card to Windows.
// Specification: docs/SD_MANAGEMENT.md. Header-only, no libDaisy, host-tested.
//
//   request  F0 7D 4C 50 <ver> <cmd> <payload...> F7
//   reply    F0 7D 4C 50 <ver> <cmd|0x40> <payload...> F7
//
// 0x7D is the non-commercial manufacturer ID shared with the bootloader entry
// ("BL", 42 4C), Mod on Daisy ("MD", 4D 44) and Nebulae ("NB", 4E 42); "LP" (4C 50)
// keeps them apart.
#pragma once
#include <cstddef>
#include <cstdint>

namespace looper::sysex {

constexpr uint8_t kVersion = 0x01;
constexpr uint8_t kPrefix[] = {0x7D, 0x4C, 0x50};
constexpr uint8_t kReplyBit = 0x40;
enum Command : uint8_t { kStatus = 0x01, kShare = 0x02, kError = 0x7F };
// SHARE results. Only kAccepted is followed by the restart into SD management.
enum ShareResult : uint8_t {
    kAccepted = 0,
    kConfirmRequired = 1,  // unsaved loop changes cannot be saved: resend with confirm=1
    kBusy = 2,             // recording, overdub, resample or a WAV import is running
    kNoCard = 3,
    kBadRequest = 4,
    kSaving = 5,  // the loop is being written to loop.wav now: ask again shortly
};
enum ErrorCode : uint8_t { kErrVersion = 1, kErrUnknown = 2, kErrLength = 3 };
constexpr size_t kMaxBody = 16;  // bytes after the prefix

// Body as libDaisy's MidiEvent::sysex_data reports it: the bytes between F0 and F7.
// True if it starts with this protocol's prefix (any other SysEx is someone else's).
inline bool IsControl(const uint8_t* data, size_t size) {
    if (size < sizeof(kPrefix))
        return false;
    for (size_t i = 0; i < sizeof(kPrefix); ++i)
        if (data[i] != kPrefix[i])
            return false;
    return true;
}

// Snapshot of the performance state for STATUS and the SHARE decision (main loop).
struct Status {
    uint8_t fw[3] = {0, 0, 0};
    bool card = false;        // a card is in the slot
    bool loop = false;        // a loop exists in RAM
    bool unsaved = false;     // the loop in RAM differs from loop.wav
    bool saving = false;      // loop.wav is being written
    bool recording = false;   // record/overdub/resample running or pending
    bool importing = false;   // a WAV import is running
    bool overrun = false;     // audio deadline overrun latched
    bool save_failed = false; // the last save failed or the card cannot be written
    uint8_t fault = 0;        // LoopStore::Fault of the last card operation
    uint32_t loop_ms = 0;     // loop length, saturates at 2^21 - 1
    uint16_t overruns = 0;    // saturates at 16383
    uint8_t peak_load = 0;    // percent of the block deadline, saturates at 127
};

// Bounded writer for reply frames.
class Reply {
  public:
    explicit Reply(uint8_t command) {
        Put(0xF0);
        for (uint8_t b : kPrefix)
            Put(b);
        Put(kVersion);
        Put(uint8_t(command | kReplyBit));
    }
    Reply& Put(uint8_t b) {
        if (size_ < sizeof(bytes_))
            bytes_[size_++] = b;
        return *this;
    }
    Reply& Data(uint8_t b) { return Put(b & 0x7F); }
    Reply& Data21(uint32_t v) {
        if (v > 0x1FFFFFu)
            v = 0x1FFFFFu;
        return Data(uint8_t(v)).Data(uint8_t(v >> 7)).Data(uint8_t(v >> 14));
    }
    const uint8_t* Finish() {
        Put(0xF7);
        return bytes_;
    }
    size_t Size() const { return size_; }

  private:
    uint8_t bytes_[32] = {};
    size_t size_ = 0;
};

constexpr size_t kStatusPayload = 13;

inline void WriteStatus(Reply& r, const Status& s) {
    r.Data(s.fw[0]).Data(s.fw[1]).Data(s.fw[2]).Data(0);  // personality 0 = performance
    r.Data(uint8_t((s.card ? 1 : 0) | (s.loop ? 2 : 0) | (s.unsaved ? 4 : 0) | (s.saving ? 8 : 0) |
                   (s.recording ? 16 : 0) | (s.importing ? 32 : 0) | (s.overrun ? 64 : 0)));
    r.Data(uint8_t(s.save_failed ? 1 : 0)).Data(s.fault);
    r.Data21(s.loop_ms);
    const uint16_t o = s.overruns > 16383 ? 16383 : s.overruns;
    r.Data(uint8_t(o)).Data(uint8_t(o >> 7)).Data(s.peak_load > 127 ? 127 : s.peak_load);
}

// Decision for a SHARE request; pure so every path is host-tested. The restart
// reloads loop.wav, so the loop in RAM must be saved first: kSaving asks the caller
// to save at once and the host to ask again. Only when saving is impossible does
// confirm=1 give up the unsaved changes.
inline ShareResult DecideShare(const Status& s, bool confirmed) {
    if (s.recording || s.importing)
        return kBusy;
    if (!s.card)
        return kNoCard;
    if (s.unsaved && !confirmed)
        return s.save_failed && !s.saving ? kConfirmRequired : kSaving;
    return kAccepted;
}

// Handles one request body (prefix included) and builds the reply. `result` is set
// for a well-formed SHARE; the caller saves on kSaving and restarts on kAccepted.
// Returns false if nothing is sent.
inline bool Handle(const uint8_t* data, size_t size, const Status& status, Reply& reply,
                   int& result) {
    result = -1;
    if (!IsControl(data, size))
        return false;
    const uint8_t* body = data + sizeof(kPrefix);
    size -= sizeof(kPrefix);
    if (size < 2 || size > kMaxBody)
        return false;
    const uint8_t cmd = body[1];
    if (body[0] != kVersion) {
        reply = Reply(kError);
        reply.Data(cmd).Data(kErrVersion);
        return true;
    }
    if (cmd == kStatus) {
        if (size != 2) {
            reply = Reply(kError);
            reply.Data(cmd).Data(kErrLength);
            return true;
        }
        reply = Reply(kStatus);
        WriteStatus(reply, status);
        return true;
    }
    if (cmd == kShare) {
        reply = Reply(kShare);
        if (size != 7 || body[2] > 1) {
            reply.Data(kBadRequest);
            for (size_t i = 0; i < 4; ++i)
                reply.Data(size == 7 ? body[3 + i] : 0);
            return true;
        }
        const ShareResult r = DecideShare(status, body[2] == 1);
        reply.Data(r);
        for (size_t i = 0; i < 4; ++i)
            reply.Data(body[3 + i]);
        result = r;
        return true;
    }
    reply = Reply(kError);
    reply.Data(cmd).Data(kErrUnknown);
    return true;
}

}  // namespace looper::sysex
