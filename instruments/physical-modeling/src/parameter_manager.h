#pragma once
#include "config.h"
#include <cmath>
#include <algorithm>
namespace physical_modeling {
enum Parameter : unsigned {
    Frequency, Structure, Brightness, Damping, Position, Excitation,
    Decay, Wet, Model, Polyphony, ReverbDamping, Diffusion, InputGain, Master, Count
};
class PotFilter {
  public:
    bool Process(float input, float& value) {
        input = Unit(input);
        if(!initialized_) { smoothed_ = stable_ = input; initialized_ = true; value = Snap(input); return true; }
        smoothed_ += (input - smoothed_) * 0.0487705755f; // 20 ms at 1 kHz.
        const float next = Snap(smoothed_);
        if(std::fabs(next - stable_) >= 0.002f || (next != stable_ && (next == 0 || next == 1))) {
            stable_ = value = next; return true;
        }
        return false;
    }
  private:
    static float Snap(float x) { return x < 0.003f ? 0.0f : x > 0.997f ? 1.0f : x; }
    float smoothed_ = 0, stable_ = 0;
    bool initialized_ = false;
};
class ParameterManager {
  public:
    ParameterManager() {
        target_.fill(0.5f);
        target_[Excitation] = 0.8f; target_[Wet] = 0.25f;
        target_[Model] = 0; target_[Polyphony] = 1;
        target_[Diffusion] = 0.625f / 0.9f; target_[InputGain] = 0.4f; target_[Master] = 1;
        current_ = target_;
    }
    void Set(unsigned id, float value) { if(id < Count && id != Polyphony) target_[id] = Unit(value); }
    void Pot(unsigned id, float value) { float next = 0; if(id < 8 && pots_[id].Process(value, next)) Set(id, next); }
    float Target(unsigned id) const { return target_[id]; }
    float Get(unsigned id) const { return current_[id]; }
    unsigned DesiredModel() const { return std::min(5u, unsigned(target_[Model] * 6.0f)); }
    void SelectModel(unsigned model) { Set(Model, float(std::min(5u, model)) / 5.0f); }
    void SetBend(unsigned raw) { bend_target_ = Bend(raw); }
    float PitchOffset() const { return Transpose(current_[Frequency]) + bend_; }
    void Advance() {
        // 5 ms at 4 kHz; snap small residuals so dry/wet endpoints are exact.
        for(unsigned i = 0; i < Count; ++i) {
            current_[i] += 0.0487705755f * (target_[i] - current_[i]);
            if(std::fabs(target_[i] - current_[i]) < 0.000001f) current_[i] = target_[i];
        }
        bend_ += 0.0487705755f * (bend_target_ - bend_);
    }
  private:
    std::array<float, Count> target_{}, current_{};
    std::array<PotFilter, 8> pots_{};
    float bend_ = 0, bend_target_ = 0;
};
// Original Torus CV scaler: nearest of 11 chords with +/-0.1 chord hysteresis.
class ChordSelector {
  public:
    int Process(float structure) {
        const float chord = Unit(structure) * 10.0f;
        const float hysteresis = chord > chord_ ? -0.1f : 0.1f;
        chord_ = std::clamp(int(chord + hysteresis + 0.5f), 0, 10);
        return chord_;
    }
  private:
    int chord_ = 0;
};
class ModelSwitch {
  public:
    bool Process(bool bank, int position, unsigned& model) {
        const unsigned raw = SwitchModel(bank, position);
        if(raw != candidate_) { candidate_ = raw; count_ = 1; }
        else if(count_ < 20) ++count_;
        if(count_ == 20 && int(candidate_) != accepted_) {
            accepted_ = int(candidate_); model = candidate_; return true;
        }
        return false;
    }
  private:
    unsigned candidate_ = 0, count_ = 0;
    int accepted_ = -1;
};
}
