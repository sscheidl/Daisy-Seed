// Execution-context glue of the Nebulae Mod on Daisy build (docs/MODULATION.md). No
// libDaisy here, so all of it is host-tested.
//
// Nebulae reads USB MIDI as raw bytes in the USB interrupt and has no libDaisy event
// FIFO. MidiTap sits next to the existing NB, BL and note parsers on the same bytes and
// hands the matrix only what it needs; it consumes nothing, so the other three see
// exactly the bytes they saw before.
#pragma once
#include "control.h"
#include "mod_on_daisy/protocol.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace neb::modbridge {

// A live-layer message of PROTOCOL.md section 1. Real-time messages have no data bytes.
struct LiveEvent {
    uint8_t status = 0, data0 = 0, data1 = 0;
};

// One Mod on Daisy frame: the bytes between F0 and F7.
struct SysExFrame {
    uint8_t size = 0;
    uint8_t body[mod::proto::kMaxFrame - 2] = {};
};

// USB interrupt fills, main loop drains. Both hand-overs are single producer, single
// consumer and bounded: when full the message is dropped and Overflowed() reports it.
class MidiTap {
  public:
    static constexpr unsigned kLiveEvents = 128, kFrames = 4;

    // USB interrupt only.
    void Byte(uint8_t byte) {
        if(byte >= 0xF8) { // real-time bytes may appear anywhere, even inside SysEx
            if(byte == 0xF8 || byte == 0xFA)
                Push(LiveEvent{byte, 0, 0});
            return;
        }
        if(byte & 0x80) {
            used_ = 0;
            if(byte == 0xF7) {
                if(sysex_ && ours_ && !too_long_)
                    PushFrame();
                sysex_ = false;
                status_ = 0;
                return;
            }
            sysex_ = byte == 0xF0;
            if(sysex_) {
                frame_.size = 0;
                ours_ = true;
                too_long_ = false;
            }
            status_ = byte < 0xF0 ? byte : 0; // any other status aborts a running SysEx
            return;
        }
        if(sysex_) {
            SysExByte(byte);
            return;
        }
        if(!status_)
            return;
        const uint8_t type = status_ & 0xF0;
        if(type == 0xC0) // program change: one data byte, not for the matrix
            return;
        if(type == 0xD0) {
            Push(LiveEvent{status_, byte, 0});
            return;
        }
        if(!used_) {
            first_ = byte;
            used_ = 1;
            return;
        }
        used_ = 0;
        if(Wanted(type, first_))
            Push(LiveEvent{status_, first_, byte});
    }

    // Main loop only.
    bool Pop(LiveEvent& event) { return live_.Pop(event); }
    bool Pop(SysExFrame& frame) { return frames_.Pop(frame); }
    bool Overflowed() { return overflow_.exchange(false, std::memory_order_acq_rel); }

  private:
    // Notes, poly AT, CC1 and the all-notes CCs. Instrument CCs and pitch bend stay out,
    // so CC20-27 automation cannot crowd a note off out of the queue.
    static bool Wanted(uint8_t type, uint8_t first) {
        if(type == 0xB0)
            return first == 1 || first == 120 || first == 123;
        return type == 0x80 || type == 0x90 || type == 0xA0;
    }
    void SysExByte(uint8_t byte) {
        const std::size_t at = frame_.size;
        if(at < 3) {
            const uint8_t expected = at == 0 ? mod::proto::kManufacturer : mod::proto::kProject[at - 1];
            ours_ = ours_ && byte == expected;
        }
        if(!ours_)
            return;
        if(at >= sizeof(frame_.body)) { // longer than PROTOCOL.md allows: never a valid frame
            too_long_ = true;
            return;
        }
        frame_.body[at] = byte;
        frame_.size = uint8_t(at + 1);
    }
    void Push(const LiveEvent& event) {
        if(!live_.Push(event))
            overflow_.store(true, std::memory_order_release);
    }
    void PushFrame() {
        if(!frames_.Push(frame_))
            overflow_.store(true, std::memory_order_release);
    }

    Queue<LiveEvent, kLiveEvents> live_;
    Queue<SysExFrame, kFrames> frames_;
    std::atomic<bool> overflow_{false};
    SysExFrame frame_;
    uint8_t status_ = 0, first_ = 0, used_ = 0;
    bool sysex_ = false, ours_ = false, too_long_ = false;
};
static_assert(std::atomic<bool>::is_always_lock_free, "the overflow flag must be lock-free");

// Matrix ticks owed to the millisecond clock. TIM5 is the lowest interrupt priority and
// the audio callback runs above it for most of each 5.33 ms block, so timer interrupts
// merge. Counting interrupts would slow every LFO with the audio load; counting
// milliseconds keeps the control rate at 1 kHz on average. Timer context only.
class TickClock {
  public:
    static constexpr uint32_t kMaxCatchUp = 16; // three blocks; beyond that the time is dropped

    void Start(uint32_t now_ms) { last_ = now_ms; }
    uint32_t Due(uint32_t now_ms) {
        const uint32_t elapsed = now_ms - last_;
        last_ = now_ms;
        if(elapsed <= kMaxCatchUp)
            return elapsed;
        dropped_ += elapsed - kMaxCatchUp;
        return kMaxCatchUp;
    }
    uint32_t Dropped() const { return dropped_; }

  private:
    uint32_t last_ = 0, dropped_ = 0;
};

} // namespace neb::modbridge
