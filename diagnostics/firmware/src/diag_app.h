// Main-loop logic of the diagnostic firmware, independent of libDaisy so that the
// complete command/response/streaming behaviour can be tested on the host.
// main.cpp supplies the hardware through the Board and UsbSink interfaces.
#pragma once
#include "ddp.h"
#include "bootloader_entry.h"
#include "diag_core.h"
#include "ring_buffer.h"

namespace diag {

enum class SendResult : uint8_t {
    Ok,        // transfer started; the buffer must stay untouched until the next Ok
    Busy,      // previous IN transfer still running, try again later
    NotReady,  // device not configured by a host (no enumeration / USB reset)
};

// USB CDC transmit. Implementations must check readiness and start the transfer
// atomically with respect to the USB interrupt (see CdcSink in main.cpp).
class UsbSink {
  public:
    virtual SendResult Send(const uint8_t* data, size_t len) = 0;

  protected:
    ~UsbSink() = default;
};

class Board {
  public:
    virtual Inputs ReadInputs() = 0;
    virtual uint32_t ControlTicks() = 0;  // number of 1 kHz control updates so far
    virtual void ApplyLeds(uint16_t leds, uint8_t brightness_percent) = 0;
    // CV OUT 1/2 DAC codes 0..kDacMax; main loop only.
    virtual void ApplyCvOut(uint16_t, uint16_t) {}
    virtual SdResult RunSdTest(bool card_present) = 0;  // blocking, bounded
    virtual bool SdTestAllowed() { return true; }
    // Default board is the protected diagnostic build. Reader builds return
    // explicit status/error bodies and never release a card on BYE or timeout.
    virtual void ReaderCommand(uint32_t, ddp::Writer& out) {
        out.Str("ERR c=READER e=UNSUPPORTED");
    }
    // Resets into the Daisy bootloader (common/bootloader); does not return on hardware.
    virtual void EnterBootloader() = 0;

    // ---- audio (firmware 0.4.0): all optional; the default board has no audio ----
    virtual bool AudioSupported() { return false; }
    // Hands the tone to the audio callback. The callback mutes itself once its clock
    // passes `keep_until_ms`, so a blocked or dead main loop cannot leave a tone on.
    virtual void ApplyTone(const ToneSettings&, uint32_t /*keep_until_ms*/) {}
    // Peak levels since the previous call (the call resets them).
    virtual AudioLevels ReadAudioLevels() { return AudioLevels(); }
    // Arms a snapshot of `frames` stereo input frames; false when unsupported/busy.
    virtual bool StartScope(uint16_t /*frames*/, bool /*trigger*/) { return false; }
    // Interleaved L/R int16 samples once the capture is complete, else nullptr.
    virtual const int16_t* ScopeData(uint16_t& frames) { frames = 0; return nullptr; }

  protected:
    ~Board() = default;
};

// A hosting firmware (the program manager) sees every valid frame before the Session:
// it can add its own commands and refuse diagnostic ones in its other operating modes.
// Returning true consumes the frame: `reply` (possibly empty) is sent and `requests`
// is merged into the step's request mask. `session_active` is the host session state
// before this frame. The hook must not block.
class CommandHook {
  public:
    virtual bool Handle(const char* body, const char* type, bool session_active, uint32_t now_ms,
                        ddp::Writer& reply, uint32_t& requests) = 0;

  protected:
    ~CommandHook() = default;
};

// Frames waiting for the CDC IN endpoint. The driver reads a submitted buffer until
// its transfer completes, so new frames are collected in the other buffer; a
// successful Send() implies the previous transfer has finished.
class TxQueue {
  public:
    static constexpr size_t kSize = 1536;
    void Append(const char* body, size_t len, Stats& stats);
    void Pump(UsbSink& usb, Stats& stats);
    void Discard(Stats& stats);
    size_t Pending() const { return fill_; }
    size_t Room() const { return kSize - fill_; }

  private:
    uint8_t buf_[2][kSize] = {};
    size_t active_ = 0;
    size_t fill_ = 0;
};

class App {
  public:
    static constexpr uint32_t kRxSize = 1024;
    static constexpr uint32_t kLedMinIntervalMs = 10;
    // BOOT waits at most this long for the ACK to leave before the reset.
    static constexpr uint32_t kBootFlushMs = 50;
    static constexpr uint32_t kAudioFrameMs = 100;   // AU frames at 10 Hz
    static constexpr uint32_t kToneKeepAliveMs = 1000;  // callback mutes after this silence
    static constexpr uint32_t kToneRefreshMs = 200;

    App(Board& board, UsbSink& usb, const char* bootloader_name, uint32_t now_ms,
        const Identity* identity = nullptr);

    // Optional command hook (see CommandHook); nullptr removes it. Main loop only.
    void SetHook(CommandHook* hook) { hook_ = hook; }

    // USB receive interrupt: copy only.
    void OnUsbReceive(const uint8_t* data, uint32_t len) { rx_.Push(data, len); }

    // One main-loop iteration.
    void Step(uint32_t now_ms);

    bool HostActive(uint32_t now_ms) { return session_.HostActive(now_ms); }
    const Stats& stats() const { return stats_; }
    const Settings& settings() const { return session_.settings; }

  private:
    void Send(const ddp::Writer& body);
    void SilenceOutputs(uint32_t now_ms);  // CV OUT 0/0 and tone off, applied at once
    void PumpScope();

    Board& board_;
    UsbSink& usb_;
    CommandHook* hook_ = nullptr;
    Session session_;
    ddp::FrameParser parser_;
    SpscRing<kRxSize> rx_;
    TxQueue tx_;
    Stats stats_;
    Inputs last_sent_;
    uint32_t seq_ = 0;
    uint32_t start_ms_;
    uint32_t last_state_ms_;
    uint32_t last_sys_ms_;
    uint32_t last_led_ms_;
    uint32_t last_stat_ms_;
    uint32_t last_audio_ms_;
    uint32_t last_tone_ms_ = 0;
    uint32_t last_ticks_ = 0;
    uint16_t last_leds_ = 0;
    uint8_t last_brightness_ = 0;
    bool leds_dirty_ = true;
    uint16_t last_cv_[kNumCv] = {};
    bool cv_applied_ = false;
    bool tone_applied_ = false;
    ToneSettings last_tone_;  // what the callback currently has
    bool keepalive_after_block_ = false;
    bool boot_pending_ = false;
    uint32_t boot_since_ = 0;
    bool scope_active_ = false;   // capture armed or chunks still to send
    uint16_t scope_next_chunk_ = 0;
    bootloader::KeyCombo boot_keys_;  // S9+S16 for 5 s, as in every app
};

} // namespace diag
