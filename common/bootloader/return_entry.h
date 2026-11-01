#pragma once
// Shared return from an application into the Daisy Program Manager (system/program-manager).
// Two triggers request it; the main loop finishes its own work (audio fade, note-offs,
// CV rest state, card writes) and then calls manager_return::Enter() (return_daisy.h),
// which resets with DAISY_SKIP_TIMEOUT: the bootloader starts the QSPI application
// (= the program manager) after its short window. The service entry (S9+S16, BL SysEx,
// DDP BOOT -> DAISY_INFINITE_TIMEOUT / DFU) is unchanged and lives in bootloader_entry.h.
//
//   * Keys: S1 and S16, and no other key, held together for 3 s.
//   * USB-MIDI SysEx: F0 7D 50 4D 01 01 4F 50 45 4E F7 ("PM", OPEN, "OPEN").
//     The firmware answers F0 7D 50 4D 01 41 F7 before it resets.
//
// Documented in common/bootloader/README.md (section "Rückkehr zur Verwaltung").
// No libDaisy dependency; host-tested in system/program-manager/tests.
#include "bootloader_entry.h"
#include <cstddef>
#include <cstdint>

// Component version for the manifest (components/registry.json), three-part SemVer.
#define MANAGER_RETURN_VERSION "1.0.0"

namespace manager_return {

// S1 and S16: the outer keys of the first row. Chosen apart from S9+S16 (service) and
// from any single-key action; a sustained two-key chord of exactly these keys for 3 s
// is the deliberate gesture. Their own press actions still run until the reset.
constexpr uint32_t kKeyMask = (1u << 0) | (1u << 15);
constexpr uint32_t kHoldMs = 3000;

// 0x7D: non-commercial manufacturer ID; "PM" (50 4D) is this namespace, distinct from
// "MD" (Mod on Daisy), "SQ" (Step Seq) and "BL" (bootloader). Parsers of the other
// namespaces pass unknown namespaces on untouched.
constexpr uint8_t kVersion = 0x01;
constexpr uint8_t kCommandOpen = 0x01;
constexpr uint8_t kReplyBit = 0x40;
constexpr uint8_t kRequestBody[] = {0x7D, 0x50, 0x4D, kVersion, kCommandOpen, 'O', 'P', 'E', 'N'};
constexpr uint8_t kRequest[] = {0xF0, 0x7D, 0x50, 0x4D, kVersion, kCommandOpen, 'O', 'P', 'E', 'N', 0xF7};
constexpr uint8_t kAck[] = {0xF0, 0x7D, 0x50, 0x4D, kVersion, kCommandOpen | kReplyBit, 0xF7};

// True for exactly the request body; any other SysEx belongs to someone else.
inline bool IsRequestBody(const uint8_t* body, std::size_t size) {
    if(size != sizeof(kRequestBody))
        return false;
    for(std::size_t i = 0; i < size; ++i)
        if(body[i] != kRequestBody[i])
            return false;
    return true;
}

// Raw-stream matcher (firmware that parses USB-MIDI bytes itself). Same rules as
// bootloader::SysExMatcher: real-time bytes are skipped, any other status aborts.
class SysExMatcher {
  public:
    bool Byte(uint8_t byte) {
        if(byte >= 0xF8)
            return false;
        if(byte == 0xF0) {
            position_ = 0;
            active_ = true;
            return false;
        }
        if(!active_)
            return false;
        if(byte == 0xF7) {
            active_ = false;
            return position_ == sizeof(kRequestBody);
        }
        if(byte & 0x80 || position_ >= sizeof(kRequestBody) || byte != kRequestBody[position_]) {
            active_ = false;
            return false;
        }
        ++position_;
        return false;
    }

  private:
    std::size_t position_ = 0;
    bool active_ = false;
};

// Key gesture: call with the debounced held-key mask (bit 0 = S1) once per control
// tick. Returns true once when S1+S16 have been held alone for kHoldMs; the keys
// must be released before it can fire again. Any other key resets the timer.
class KeyCombo {
  public:
    bool Process(uint32_t held, uint32_t now_ms) {
        if(held != kKeyMask) {
            holding_ = fired_ = false;
            return false;
        }
        if(!holding_) {
            holding_ = true;
            since_ = now_ms;
        }
        if(fired_ || now_ms - since_ < kHoldMs)
            return false;
        fired_ = true;
        return true;
    }
    bool Holding() const { return holding_ && !fired_; }

  private:
    uint32_t since_ = 0;
    bool holding_ = false, fired_ = false;
};

// Same lock-free request flags as the bootloader entry (kKeys / kSysEx).
using Request = bootloader::Request;

} // namespace manager_return
