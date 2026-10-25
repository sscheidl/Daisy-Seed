#pragma once
// Software entry into the external Daisy bootloader (v6.x on J7), shared by all
// apps. Two triggers request it; the main loop then calls bootloader::Enter()
// (bootloader_daisy.h). This header has no libDaisy dependency and is host-tested.
//
//   * Keys: S9 and S16, and no other key, held together for 5 s.
//   * USB-MIDI SysEx: F0 7D 42 4C 01 01 42 4F 4F 54 F7 ("BL", ENTER, "BOOT").
//     The firmware answers F0 7D 42 4C 01 41 F7 before it resets.
//
// The format is documented in common/bootloader/README.md.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bootloader {

// S9 and S16 (bit 0 = S1): the outer keys of the second row, easy to find with the
// case closed. Their own press actions still run (a loop mode, sustain, C5/C6); the
// reset follows only after 5 s of holding exactly this pair (README.md).
constexpr uint32_t kKeyMask = (1u << 8) | (1u << 15);
constexpr uint32_t kHoldMs = 5000;

// 0x7D: non-commercial manufacturer ID, shared with Mod on Daisy ("MD", 4D 44).
// "BL" (42 4C) keeps the two protocols apart.
constexpr uint8_t kVersion = 0x01;
constexpr uint8_t kCommandEnter = 0x01;
constexpr uint8_t kReplyBit = 0x40;
// Bytes between F0 and F7, as libDaisy reports them in MidiEvent::sysex_data.
constexpr uint8_t kRequestBody[] = {0x7D, 0x42, 0x4C, kVersion, kCommandEnter, 'B', 'O', 'O', 'T'};
constexpr uint8_t kRequest[] = {0xF0, 0x7D, 0x42, 0x4C, kVersion, kCommandEnter, 'B', 'O', 'O', 'T', 0xF7};
constexpr uint8_t kAck[] = {0xF0, 0x7D, 0x42, 0x4C, kVersion, kCommandEnter | kReplyBit, 0xF7};

// True for exactly the request body; any other SysEx belongs to someone else.
inline bool IsRequestBody(const uint8_t* body, std::size_t size) {
    if(size != sizeof(kRequestBody))
        return false;
    for(std::size_t i = 0; i < size; ++i)
        if(body[i] != kRequestBody[i])
            return false;
    return true;
}

// For firmware that parses the raw USB-MIDI byte stream itself (Nebulae).
// Feed every received byte; returns true when a complete request has arrived.
// Real-time bytes (F8..FF) may appear inside SysEx and are skipped. No allocation,
// safe in an interrupt.
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
            active_ = false; // another status byte, a longer or a different message
            return false;
        }
        ++position_;
        return false;
    }

  private:
    std::size_t position_ = 0;
    bool active_ = false;
};

// Key gesture. Call with the debounced held-key mask (bit 0 = S1) and a
// millisecond clock, once per control tick. Returns true once when S9+S16 have
// been held alone for kHoldMs; the keys must be released before it can fire again.
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

// Request flags from the audio or USB interrupt to the main loop. kSysEx means a
// host asked and expects the reply.
class Request {
  public:
    enum : uint8_t { kNone = 0, kKeys = 1, kSysEx = 2 };
    void Set(uint8_t source) { pending_.fetch_or(source, std::memory_order_release); }
    // Main loop: returns the sources since the last call (kNone if none).
    uint8_t Take() { return pending_.exchange(kNone, std::memory_order_acquire); }
    bool Pending() const { return pending_.load(std::memory_order_acquire) != kNone; }

  private:
    std::atomic<uint8_t> pending_{kNone};
};
static_assert(std::atomic<uint8_t>::is_always_lock_free, "bootloader request must be lock-free");

} // namespace bootloader
