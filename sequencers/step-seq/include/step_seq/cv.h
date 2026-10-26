#pragma once
// SPDX-License-Identifier: MIT
// CV/Gate outputs and inputs (ARBEITSAUFTRAG 3.3, 4.5): calibration with validation and
// a separate enable, pitch glide, CV IN quantization, and the DAC register adapter that
// replaces hw.SetDacOut() in the audio callback (R01: no HAL call, no lock).
#include <cmath>
#include <cstdint>

namespace step_seq {

constexpr uint16_t kDacMax = 4095;
constexpr int32_t kMinScale = 100000, kMaxScale = 2000000; // |milli-code per volt|
constexpr int kPitchZeroNote = 24; // 0 V = C1 (step note 0)

// Calibration of one output; codes 0..4095, offset/scale in milli-codes (PROTOCOL.md 3.5).
struct OutputCal {
    bool valid = false;
    int32_t offset = 0;  // milli-code at 0 V
    int32_t scale = 0;   // milli-code per volt, negative for an inverting stage
    uint16_t gate_low = 0, gate_high = 0, clamp_min = 0, clamp_max = kDacMax;
};

inline bool ValidCal(const OutputCal& c) {
    const int32_t magnitude = c.scale < 0 ? -c.scale : c.scale;
    if(magnitude < kMinScale || magnitude > kMaxScale)
        return false;
    if(c.offset < 0 || c.offset > int32_t(kDacMax) * 1000)
        return false;
    if(c.clamp_min >= c.clamp_max || c.clamp_max > kDacMax)
        return false;
    if(c.gate_low == c.gate_high)
        return false;
    const auto inside = [&](uint16_t code) { return code >= c.clamp_min && code <= c.clamp_max; };
    return inside(c.gate_low) && inside(c.gate_high);
}

inline uint16_t CodeOf(const OutputCal& c, double volts) {
    double code = (double(c.offset) + volts * double(c.scale)) / 1000.0;
    if(code < c.clamp_min)
        code = c.clamp_min;
    if(code > c.clamp_max)
        code = c.clamp_max;
    return uint16_t(code + 0.5);
}

inline double VoltsOfNote(double note) { return (note - kPitchZeroNote) / 12.0; }

// MIDI note range that the calibrated pitch output can reach (for the note clamp, 4.2).
inline void NoteRange(const OutputCal& c, int& low, int& high) {
    low = 0;
    high = 127;
    if(!c.valid)
        return;
    const double a = (double(c.clamp_min) * 1000.0 - c.offset) / c.scale;
    const double b = (double(c.clamp_max) * 1000.0 - c.offset) / c.scale;
    const double vmin = a < b ? a : b, vmax = a < b ? b : a;
    const int lo = int(std::ceil(kPitchZeroNote + vmin * 12.0 - 1e-9));
    const int hi = int(std::floor(kPitchZeroNote + vmax * 12.0 + 1e-9));
    low = lo < 0 ? 0 : (lo > 127 ? 127 : lo);
    high = hi < 0 ? 0 : (hi > 127 ? 127 : hi);
    if(low > high)
        low = high;
}

// Pitch slide with constant time (4.2): linear in volts over glide_ms at 1 kHz.
class Glide {
  public:
    void Jump(double note) {
        current_ = target_ = note;
        step_ = 0;
        left_ = 0;
    }
    void To(double note, float glide_ms) {
        target_ = note;
        left_ = glide_ms < 1.f ? 1u : uint32_t(glide_ms + 0.5f);
        step_ = (target_ - current_) / double(left_);
    }
    double Tick() {
        if(left_ > 0) {
            current_ += step_;
            if(--left_ == 0)
                current_ = target_;
        }
        return current_;
    }
    double Current() const { return current_; }

  private:
    double current_ = 48, target_ = 48, step_ = 0;
    uint32_t left_ = 0;
};

// The two CV outputs. Locked (not enabled): no dynamic output, the DAC keeps the code
// that the start-up wrote. Withdrawing the enable with an open gate writes gate-low once.
class CvOut {
  public:
    OutputCal cal[2];

    // Returns true if the codes should be written this tick.
    bool Tick(bool gate, uint8_t pitch, bool glide, float glide_ms, uint16_t& pitch_code, uint16_t& gate_code) {
        if(pitch != last_pitch_) {
            if(glide)
                glide_.To(pitch, glide_ms);
            else
                glide_.Jump(pitch);
            last_pitch_ = pitch;
        }
        const double note = glide_.Tick();
        if(!enabled_) {
            if(release_gate_) {
                release_gate_ = false;
                pitch_code = pitch_code_;
                gate_code = gate_code_ = cal[1].valid ? cal[1].gate_low : gate_code_;
                return cal[1].valid;
            }
            return false;
        }
        pitch_code = pitch_code_ = CodeOf(cal[0], VoltsOfNote(note));
        gate_code = gate_code_ = gate ? cal[1].gate_high : cal[1].gate_low;
        return true;
    }
    // Enable needs valid data for both outputs (NAK 8 otherwise).
    bool Enable(bool on) {
        if(on && !(cal[0].valid && cal[1].valid))
            return false;
        if(!on && enabled_)
            release_gate_ = true;
        enabled_ = on;
        return true;
    }
    // New calibration withdraws an existing enable (PROTOCOL.md 3.5).
    void SetCal(int output, const OutputCal& c) {
        if(enabled_)
            Enable(false);
        cal[output & 1] = c;
        cal[output & 1].valid = ValidCal(c);
    }
    bool Enabled() const { return enabled_; }

  private:
    Glide glide_;
    uint8_t last_pitch_ = 0xFF;
    bool enabled_ = false, release_gate_ = false;
    uint16_t pitch_code_ = 0, gate_code_ = 0;
};

// Direct DAC writes for the callback: data holding registers, then the software trigger
// of both channels. Regs is the CMSIS DAC_TypeDef on the target and a fake in tests.
template <class Regs>
class DacWriter {
  public:
    static constexpr uint32_t kTriggerBoth = 0x3; // DAC_SWTRIGR_SWTRIG1 | SWTRIG2
    explicit DacWriter(Regs* regs) : regs_(regs) {}
    void Write(uint16_t channel1, uint16_t channel2) {
        regs_->DHR12R1 = uint32_t(channel1 & kDacMax);
        regs_->DHR12R2 = uint32_t(channel2 & kDacMax);
        regs_->SWTRIGR = kTriggerBoth;
    }

  private:
    Regs* regs_;
};

// CV IN 1 transpose: +-1 (+-5 V) to semitones with 0.25 semitone hysteresis, +-24.
class CvQuantizer {
  public:
    static constexpr float kVoltsPerUnit = 5.f;
    int Process(float value) {
        const float semis = value * kVoltsPerUnit * 12.f;
        if(std::fabs(semis - float(current_)) > 0.75f)
            current_ = int(std::lround(semis));
        if(current_ > 24)
            current_ = 24;
        if(current_ < -24)
            current_ = -24;
        return current_;
    }

  private:
    int current_ = 0;
};

} // namespace step_seq
