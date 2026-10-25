#pragma once
// Asks the kit to restart into the Daisy bootloader for a firmware update
// (common/bootloader/README.md). Independent of the Mod on Daisy session: every
// firmware of this repository with USB-MIDI understands it, also one without the
// matrix (Nebulae). Single-threaded like the session (GUI thread).
#include "bootloader_entry.h"
#include <cstddef>
#include <cstdint>

namespace editor {
class BootRequest {
  public:
    enum class Result { None, Acknowledged, NoAnswer };
    // The firmware answers within a few milliseconds and resets 20 ms later.
    static constexpr uint32_t kTimeoutMs = 2000;

    // Sends the request; false if the port refused it (nothing is pending then).
    template <class Send>
    bool Start(uint32_t now_ms, Send send) {
        waiting_ = send(bootloader::kRequest, sizeof(bootloader::kRequest));
        since_ = now_ms;
        result_ = Result::None;
        return waiting_;
    }
    // Every received SysEx frame (F0 … F7). True for the reply; the session ignores it
    // anyway, its header is not "MD".
    bool OnMessage(const uint8_t* bytes, std::size_t size) {
        if(size != sizeof(bootloader::kAck))
            return false;
        for(std::size_t i = 0; i < size; ++i)
            if(bytes[i] != bootloader::kAck[i])
                return false;
        if(waiting_) {
            waiting_ = false;
            result_ = Result::Acknowledged;
        }
        return true;
    }
    // Reports the outcome once: Acknowledged, or NoAnswer after kTimeoutMs.
    Result Poll(uint32_t now_ms) {
        if(waiting_ && now_ms - since_ >= kTimeoutMs) {
            waiting_ = false;
            result_ = Result::NoAnswer;
        }
        const Result r = result_;
        result_ = Result::None;
        return r;
    }
    bool Waiting() const { return waiting_; }
    void Cancel() {
        waiting_ = false;
        result_ = Result::None;
    }

  private:
    uint32_t since_ = 0;
    bool waiting_ = false;
    Result result_ = Result::None;
};
}
