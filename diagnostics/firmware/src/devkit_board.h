// Seed3 Desktop hardware adapter shared by the diagnostic firmware and the program
// manager: control sampling in the audio callback, test tone / peak meters / snapshot
// capture, the USB CDC sink and the diag::Board methods that only need the kit itself.
// Card access and the bootloader entry stay with each firmware (SD driver and modes
// differ), so firmwares derive from DevkitBoardBase and implement those.
//
// Everything here needs libDaisy; the portable logic stays in diag_app/diag_core.
#pragma once
#include "desktop_devkit.h"
#include "usbd_core.h"
#include "diag_app.h"
#include <atomic>
#include <cmath>

extern "C" USBD_HandleTypeDef hUsbDeviceHS;  // libDaisy src/hid/usb.cpp

namespace diag::devkit {

// libDaisy configures USB and audio DMA with priority 0; the USB interrupt is moved
// to 1 so audio preempts it (see SetUsbPriority()).
constexpr uint32_t kUsbIrqPriority = 1;

inline Sw18 MapSwitch3(int pos) {
    switch(pos) {
        case daisy::Switch3::POS_UP: return Sw18::Up;
        case daisy::Switch3::POS_DOWN: return Sw18::Down;
        default: return Sw18::Center;
    }
}

inline const char* BootloaderName(daisy::System::BootInfo::Version version) {
    switch(version) {
        case daisy::System::BootInfo::Version::LT_v6_0: return "LT_v6_0";
        case daisy::System::BootInfo::Version::NONE: return "NONE";
        case daisy::System::BootInfo::Version::v6_0: return "v6_0";
        case daisy::System::BootInfo::Version::v6_1: return "v6_1";
        default: return "unknown";
    }
}

inline void SetUsbPriority() {
    NVIC_SetPriority(OTG_HS_IRQn, kUsbIrqPriority);
    NVIC_SetPriority(OTG_HS_EP1_IN_IRQn, kUsbIrqPriority);
    NVIC_SetPriority(OTG_HS_EP1_OUT_IRQn, kUsbIrqPriority);
}

// ---- Audio callback side: controls at 1 kHz, tone, meters, snapshot --------------
// The main loop writes the tone settings; the callback reads them once per block.
// `tone_until_ms` is refreshed by the main loop while a host session is active: a
// blocked or dead main loop (or a vanished host) therefore mutes the tone by itself.
class AudioDiagnostics {
  public:
    // Call once per audio block (48 samples at 48 kHz = 1 kHz control rate).
    void Process(daisy::desktop_devkit::Hardware& hw, daisy::AudioHandle::InputBuffer in,
                 daisy::AudioHandle::OutputBuffer out, size_t size, uint32_t now_ms) {
        hw.UpdateAllControls();
        for(size_t i = 0; i < kNumButtons; ++i)
            buttons_.Update(i, hw.button[i].RisingEdge(), hw.button[i].FallingEdge());
        sw18_.Update(MapSwitch3(hw.tog_sw18.Read()));

        callback_inputs_.buttons = buttons_.Held();
        for(size_t i = 0; i < kNumButtons; ++i)
            callback_inputs_.press_count[i] = buttons_.PressCount(i);
        callback_inputs_.sw17_up = hw.tog_sw17.Pressed();
        callback_inputs_.sw18 = sw18_.Position();
        callback_inputs_.sd_card = hw.sd_detect.Pressed();
        callback_inputs_.jack_left = hw.left_detect.Pressed();
        callback_inputs_.jack_right = hw.right_detect.Pressed();
        // One read per gate and tick: state and edge come from the same sample.
        for(size_t i = 0; i < kNumGates; ++i)
            gates_.Update(i, hw.gate_in[i].State());
        callback_inputs_.gates = gates_.States();
        for(size_t i = 0; i < kNumGates; ++i)
            callback_inputs_.gate_count[i] = gates_.Count(i);
        held_.store(callback_inputs_.buttons, std::memory_order_relaxed);
        card_.store(callback_inputs_.sd_card, std::memory_order_relaxed);
        control_ticks_.fetch_add(1, std::memory_order_relaxed);

        // Test tone (TONE command). Silent unless the main loop keeps refreshing the deadline.
        const bool tone_alive = static_cast<int32_t>(tone_until_ms_.load(std::memory_order_relaxed) - now_ms) > 0;
        const float amplitude = tone_alive ? tone_amplitude_.load(std::memory_order_relaxed) / 10000.f : 0.f;
        const uint8_t channels = tone_channels_.load(std::memory_order_relaxed);
        const float phase_step = 2.f * 3.14159265f * tone_freq_hz_.load(std::memory_order_relaxed)
                                 / hw.seed.AudioSampleRate();
        float peak_l = 0.f, peak_r = 0.f, peak_ol = 0.f, peak_or = 0.f;
        for(size_t i = 0; i < size; ++i) {
            float sample = 0.f;
            if(amplitude > 0.f) {
                sample = amplitude * sinf(tone_phase_);
                tone_phase_ += phase_step;
                if(tone_phase_ >= 2.f * 3.14159265f)
                    tone_phase_ -= 2.f * 3.14159265f;
            }
            out[0][i] = channels != 1 ? sample : 0.f;
            out[1][i] = channels != 0 ? sample : 0.f;
            const float l = fabsf(in[0][i]), r = fabsf(in[1][i]);
            if(l > peak_l) peak_l = l;
            if(r > peak_r) peak_r = r;
            if(fabsf(out[0][i]) > peak_ol) peak_ol = fabsf(out[0][i]);
            if(fabsf(out[1][i]) > peak_or) peak_or = fabsf(out[1][i]);
        }
        if(amplitude <= 0.f)
            tone_phase_ = 0.f;  // next tone starts at a zero crossing
        RaisePeak(peak_in_[0], PeakUnits(peak_l));
        RaisePeak(peak_in_[1], PeakUnits(peak_r));
        RaisePeak(peak_out_[0], PeakUnits(peak_ol));
        RaisePeak(peak_out_[1], PeakUnits(peak_or));

        // Snapshot capture of the inputs (SCOPE command).
        uint8_t state = scope_state_.load(std::memory_order_acquire);
        if(state == kScopeArmed) {
            size_t start = 0;
            bool found = !scope_trigger_.load(std::memory_order_relaxed)
                         || now_ms - scope_armed_ms_ >= kScopeTriggerTimeoutMs;
            if(!found) {
                float prev = scope_prev_sample_;
                for(size_t i = 0; i < size; ++i) {  // rising zero crossing on the left input
                    if(prev < 0.f && in[0][i] >= 0.f) {
                        start = i;
                        found = true;
                        break;
                    }
                    prev = in[0][i];
                }
            }
            if(found) {
                scope_fill_ = 0;
                state = kScopeCapturing;
                scope_state_.store(state, std::memory_order_relaxed);
                const uint16_t frames = scope_frames_.load(std::memory_order_relaxed);
                for(size_t i = start; i < size && scope_fill_ < frames; ++i, ++scope_fill_) {
                    scope_buffer_[scope_fill_ * 2] = ToInt16(in[0][i]);
                    scope_buffer_[scope_fill_ * 2 + 1] = ToInt16(in[1][i]);
                }
                if(scope_fill_ >= frames)
                    scope_state_.store(kScopeReady, std::memory_order_release);
            }
        } else if(state == kScopeCapturing) {
            const uint16_t frames = scope_frames_.load(std::memory_order_relaxed);
            for(size_t i = 0; i < size && scope_fill_ < frames; ++i, ++scope_fill_) {
                scope_buffer_[scope_fill_ * 2] = ToInt16(in[0][i]);
                scope_buffer_[scope_fill_ * 2 + 1] = ToInt16(in[1][i]);
            }
            if(scope_fill_ >= frames)
                scope_state_.store(kScopeReady, std::memory_order_release);
        }
        scope_prev_sample_ = size ? in[0][size - 1] : scope_prev_sample_;
    }

    // ---- main loop side ----
    Inputs Snapshot() {  // consistent copy of the callback's inputs
        Inputs in;
        __disable_irq();
        in = callback_inputs_;
        __enable_irq();
        return in;
    }
    uint16_t HeldKeys() const { return held_.load(std::memory_order_relaxed); }
    bool CardPresent() const { return card_.load(std::memory_order_relaxed); }
    uint32_t ControlTicks() const { return control_ticks_.load(std::memory_order_relaxed); }

    void ApplyTone(const ToneSettings& tone, uint32_t keep_until_ms) {
        tone_freq_hz_.store(tone.freq_hz, std::memory_order_relaxed);
        tone_channels_.store(static_cast<uint8_t>(tone.channels), std::memory_order_relaxed);
        tone_amplitude_.store(tone.mode == ToneMode::Sine ? tone.amplitude : 0, std::memory_order_relaxed);
        tone_until_ms_.store(keep_until_ms, std::memory_order_release);
    }
    void MuteTone() { tone_amplitude_.store(0, std::memory_order_release); }
    AudioLevels ReadLevels() {
        AudioLevels levels;
        for(size_t i = 0; i < kNumAudio; ++i) {
            levels.in[i] = peak_in_[i].exchange(0, std::memory_order_relaxed);
            levels.out[i] = peak_out_[i].exchange(0, std::memory_order_relaxed);
        }
        return levels;
    }
    bool StartScope(uint16_t frames, bool trigger, uint32_t now_ms) {
        const uint8_t state = scope_state_.load(std::memory_order_acquire);
        if(state == kScopeArmed || state == kScopeCapturing)
            return false;
        scope_frames_.store(frames, std::memory_order_relaxed);
        scope_trigger_.store(trigger, std::memory_order_relaxed);
        scope_armed_ms_ = now_ms;
        scope_state_.store(kScopeArmed, std::memory_order_release);
        return true;
    }
    const int16_t* ScopeData(uint16_t& frames) {
        if(scope_state_.load(std::memory_order_acquire) != kScopeReady) {
            frames = 0;
            return nullptr;
        }
        frames = scope_frames_.load(std::memory_order_relaxed);
        return scope_buffer_;  // stays valid until the next StartScope
    }

  private:
    enum ScopeState : uint8_t { kScopeIdle = 0, kScopeArmed, kScopeCapturing, kScopeReady };
    static uint16_t PeakUnits(float peak) {
        const float units = peak * 10000.f + 0.5f;
        return units >= 10000.f ? 10000u : static_cast<uint16_t>(units);
    }
    static void RaisePeak(std::atomic<uint16_t>& slot, uint16_t value) {
        if(value > slot.load(std::memory_order_relaxed))  // single writer: the callback
            slot.store(value, std::memory_order_relaxed);
    }
    static int16_t ToInt16(float sample) {
        const float scaled = sample * 32767.f;
        return scaled >= 32767.f ? 32767 : scaled <= -32768.f ? -32768 : static_cast<int16_t>(scaled);
    }

    ButtonTracker buttons_;
    Switch3Debouncer sw18_;
    GateTracker gates_;
    Inputs callback_inputs_;  // written in the callback, copied with IRQs masked
    std::atomic<uint16_t> held_{0};
    std::atomic<bool> card_{false};
    std::atomic<uint32_t> control_ticks_{0};
    std::atomic<uint16_t> tone_amplitude_{0};  // 1/10000 FS, 0 = off
    std::atomic<uint16_t> tone_freq_hz_{1000};
    std::atomic<uint8_t> tone_channels_{2};    // 0 = L, 1 = R, 2 = both
    std::atomic<uint32_t> tone_until_ms_{0};
    float tone_phase_ = 0.f;
    std::atomic<uint16_t> peak_in_[kNumAudio];
    std::atomic<uint16_t> peak_out_[kNumAudio];
    // Snapshot: interleaved L/R int16 of the inputs; CPU-only (no DMA), so DTCM is fine.
    std::atomic<uint8_t> scope_state_{kScopeIdle};
    std::atomic<uint16_t> scope_frames_{0};
    std::atomic<bool> scope_trigger_{true};
    uint32_t scope_armed_ms_ = 0;
    uint16_t scope_fill_ = 0;
    float scope_prev_sample_ = 0.f;
    int16_t scope_buffer_[kScopeMaxFrames * kNumAudio];
};

// ---- USB CDC transmit -------------------------------------------------------------
// libDaisy's CDC_Transmit_HS() dereferences the class data without a NULL check, and
// a USB reset handled in the OTG_HS interrupt frees that data (pClassData = NULL).
// The readiness check and the start of the transfer therefore run with the USB
// interrupt (priority 1) masked via BASEPRI; the audio interrupt (priority 0) still runs.
class CdcSink final : public UsbSink {
  public:
    explicit CdcSink(daisy::UsbHandle& usb) : usb_(usb) {}
    SendResult Send(const uint8_t* data, size_t len) override {
        const uint32_t saved = __get_BASEPRI();
        __set_BASEPRI(kUsbIrqPriority << (8u - __NVIC_PRIO_BITS));
        __DSB();
        __ISB();
        SendResult result;
        if(hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED || hUsbDeviceHS.pClassData == nullptr)
            result = SendResult::NotReady;
        else if(usb_.TransmitExternal(const_cast<uint8_t*>(data), len) == daisy::UsbHandle::Result::OK)
            result = SendResult::Ok;
        else
            result = SendResult::Busy;
        __set_BASEPRI(saved);
        return result;
    }

  private:
    daisy::UsbHandle& usb_;
};

// ---- Board: everything the kit itself provides ------------------------------------
class DevkitBoardBase : public Board {
  public:
    DevkitBoardBase(daisy::desktop_devkit::Hardware& hw, AudioDiagnostics& audio) : hw_(hw), audio_(audio) {}

    Inputs ReadInputs() override {
        Inputs in = audio_.Snapshot();
        for(size_t i = 0; i < kNumPots; ++i) {
            in.pot_norm[i] = NormalizePot(hw_.pot[i].Value());
            in.pot_raw[i] = hw_.seed.adc.GetMux(0, static_cast<uint8_t>(i));
        }
        // CV IN 1/2 are ADC channels 1/2 (channel 0 is the pot mux), see desktop_devkit.cpp.
        for(size_t i = 0; i < kNumCv; ++i) {
            in.cv_norm[i] = NormalizeCv(hw_.cv[i].Value());
            in.cv_raw[i] = hw_.seed.adc.Get(static_cast<uint8_t>(i + 1));
        }
        return in;
    }
    void ApplyCvOut(uint16_t code1, uint16_t code2) override {
        hw_.SetDacOut(daisy::DacHandle::Channel::ONE, code1);  // polling DAC: short register write
        hw_.SetDacOut(daisy::DacHandle::Channel::TWO, code2);
    }
    uint32_t ControlTicks() override { return audio_.ControlTicks(); }
    void ApplyLeds(uint16_t leds, uint8_t brightness_percent) override {
        const float on = brightness_percent / 100.f;
        for(size_t i = 0; i < kNumLeds; ++i)
            hw_.SetLed(static_cast<int>(i), (leds >> i) & 1u ? on : 0.f);
        hw_.UpdateLeds();  // DMA driver may wait for the previous transfer: main loop only
    }
    bool AudioSupported() override { return true; }
    void ApplyTone(const ToneSettings& tone, uint32_t keep_until_ms) override {
        audio_.ApplyTone(tone, keep_until_ms);
    }
    AudioLevels ReadAudioLevels() override { return audio_.ReadLevels(); }
    bool StartScope(uint16_t frames, bool trigger) override {
        return audio_.StartScope(frames, trigger, daisy::System::GetNow());
    }
    const int16_t* ScopeData(uint16_t& frames) override { return audio_.ScopeData(frames); }

  protected:
    daisy::desktop_devkit::Hardware& hw_;
    AudioDiagnostics& audio_;
};

} // namespace diag::devkit
