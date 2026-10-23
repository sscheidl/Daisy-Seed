#pragma once
// Live MIDI state shared between main loop (only writer) and audio (reader):
// raw CC1/aftertouch, per-note pressure, last held note, clock and retrigger
// counters (ARCHITECTURE.md § 4.4, § 5).
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mod {
class LiveState {
    static_assert(std::atomic<uint8_t>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free,
                  "live MIDI state must be lock-free");
  public:
    static constexpr std::size_t kLastNotes = 16;

    // ---- main loop ----
    void ModWheel(uint8_t value) { mod_wheel_.store(value & 127, std::memory_order_relaxed); }
    void ChannelPressure(uint8_t value) { channel_at_.store(value & 127, std::memory_order_relaxed); }
    void NoteOn(uint8_t note) {
        note &= 127;
        pressure_[note].store(0, std::memory_order_relaxed);
        held_[note >> 5] |= 1u << (note & 31);
        Remove(note);
        if(count_ == kLastNotes) { // the oldest note falls out of the last-note list only
            for(std::size_t i = 1; i < kLastNotes; ++i)
                order_[i - 1] = order_[i];
            --count_;
        }
        order_[count_++] = note;
        UpdateLast();
        note_ons_.fetch_add(1, std::memory_order_relaxed);
    }
    void NoteOff(uint8_t note) {
        note &= 127;
        pressure_[note].store(0, std::memory_order_relaxed);
        held_[note >> 5] &= ~(1u << (note & 31));
        Remove(note);
        UpdateLast();
    }
    void PolyPressure(uint8_t note, uint8_t value) {
        note &= 127;
        if(!Held(note))
            return; // pressure without a held note has no voice to modulate
        pressure_[note].store(value & 127, std::memory_order_relaxed);
        UpdateLast();
    }
    void AllNotesOff() {
        for(auto& p : pressure_)
            p.store(0, std::memory_order_relaxed);
        for(auto& h : held_)
            h = 0;
        count_ = 0;
        UpdateLast();
    }
    void Clock() { clock_.fetch_add(1, std::memory_order_relaxed); }
    void Start() {
        clock_at_start_.store(clock_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        starts_.fetch_add(1, std::memory_order_release);
    }
    bool Held(uint8_t note) const { return (held_[(note & 127) >> 5] >> (note & 31)) & 1u; }
    int LastNote() const { return count_ ? order_[count_ - 1] : -1; }

    // ---- audio ----
    uint8_t ModWheelRaw() const { return mod_wheel_.load(std::memory_order_relaxed); }
    uint8_t ChannelPressureRaw() const { return channel_at_.load(std::memory_order_relaxed); }
    uint8_t PressureRaw(uint8_t note) const { return pressure_[note & 127].load(std::memory_order_relaxed); }
    uint8_t LastPressureRaw() const { return last_pressure_.load(std::memory_order_relaxed); }
    uint32_t NoteOns() const { return note_ons_.load(std::memory_order_relaxed); }
    uint32_t Clocks() const { return clock_.load(std::memory_order_relaxed); }
    uint32_t Starts() const { return starts_.load(std::memory_order_acquire); }
    uint32_t ClockAtStart() const { return clock_at_start_.load(std::memory_order_relaxed); }

  private:
    void Remove(uint8_t note) {
        std::size_t j = 0;
        for(std::size_t i = 0; i < count_; ++i)
            if(order_[i] != note)
                order_[j++] = order_[i];
        count_ = j;
    }
    void UpdateLast() {
        const uint8_t value = count_ ? pressure_[order_[count_ - 1]].load(std::memory_order_relaxed) : 0;
        last_pressure_.store(value, std::memory_order_relaxed);
    }
    std::atomic<uint8_t> pressure_[128]{};
    std::atomic<uint8_t> mod_wheel_{0}, channel_at_{0}, last_pressure_{0};
    std::atomic<uint32_t> note_ons_{0}, clock_{0}, starts_{0}, clock_at_start_{0};
    uint32_t held_[4]{};          // main loop only
    uint8_t order_[kLastNotes]{}; // main loop only, oldest first
    std::size_t count_ = 0;
};
}
