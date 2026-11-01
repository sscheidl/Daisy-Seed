#pragma once
// SPDX-License-Identifier: MIT
// Own replacement stages of the Streichwurst FX component where the original is OPEN
// (docs/FIDELITY.md): Animate (registration modulation + forming), Phaser and Reverb.
#include "params.h"
#include <cmath>
#include <cstddef>
#include <cstring>

namespace streichwurst_fx {

constexpr float kPi = 3.14159265358979f;
constexpr int kControlHop = 16;

// One-pole smoother on the control hop: 20 ms for audible amounts (draft 0.2).
struct Smoothed {
    float value = 0.f, target = 0.f, alpha = 1.f;
    void Prepare(float sample_rate, float seconds) { alpha = 1.f - std::exp(-float(kControlHop) / (sample_rate * seconds)); }
    void Snap() { value = target; }
    float Step() {
        value += (target - value) * alpha;
        if(std::fabs(target - value) < 1e-6f)
            value = target;
        return value;
    }
};

// Animate (SF-D09 PLAUSIBLE: setter changes forming/LFO fields, forming before the
// ensemble; SF-F03: the registration takes an Animate offset m). Own design:
//  * a parabolic LFO at an own rate drives the published registration offset
//    m = amount * kSpan * h(phase) (native units of z = 256 r + m);
//  * an own forming band emphasis on the summed strings before the ensemble:
//    x + 0.3 d (LP_fc(x) - LP_200(x)), f_c = 700 exp(0.4 d h) Hz (draft 0.2).
// amount 0 gives an exactly unchanged signal and m = 0.
class Animate {
  public:
    static constexpr float kSpan = 8000.f; // own: +-8000 of 32498 native registration units

    void Prepare(float sample_rate) {
        fs_ = sample_rate;
        amount_.Prepare(fs_, 0.02f);
        lp200_a_ = std::exp(-2.f * kPi * 200.f / fs_);
        Reset();
    }
    void Reset() {
        phase_ = 0.f;
        lp_fc_ = lp_200_ = 0.f;
        hop_ = 0;
        offset_ = 0.f;
        amount_.Snap();
        Control();
    }
    void Set(float amount, float rate_hz) {
        amount_.target = amount;
        inc_ = rate_hz * float(kControlHop) / fs_;
    }
    float Process(float x) {
        if(hop_ == 0)
            Control();
        if(++hop_ == kControlHop)
            hop_ = 0;
        lp_fc_ = fc_a_ * lp_fc_ + (1.f - fc_a_) * x;
        lp_200_ = lp200_a_ * lp_200_ + (1.f - lp200_a_) * x;
        const float d = amount_.value;
        if(d == 0.f)
            return x;
        return x + 0.3f * d * (lp_fc_ - lp_200_);
    }
    // Filter states to zero (All Sound Off); the LFO keeps its phase.
    void Clear() { lp_fc_ = lp_200_ = 0.f; }
    float RegistrationOffset() const { return offset_; }
    float Amount() const { return amount_.value; }

  private:
    void Control() {
        amount_.Step();
        phase_ = Wrap(phase_ + inc_);
        const float h = Parabola(phase_);
        const float d = amount_.value;
        offset_ = d * kSpan * h;
        const float fc = 700.f * std::exp(0.4f * d * h);
        fc_a_ = std::exp(-2.f * kPi * fc / fs_);
    }
    float fs_ = 48000.f, phase_ = 0.f, inc_ = 0.f;
    float lp_fc_ = 0.f, lp_200_ = 0.f, fc_a_ = 0.f, lp200_a_ = 0.f;
    float offset_ = 0.f;
    int hop_ = 0;
    Smoothed amount_;
};

// Own phaser (SF-D10: separate stage on the common ensemble sum; its stage count,
// coefficient table and feedback topology are OPEN). Four first-order all-passes swept
// exponentially between 80 Hz and 3 kHz by a parabolic LFO, feedback 0.5 a, output
// x + 0.5 a (y - x). amount 0 passes the input exactly.
class Phaser {
  public:
    static constexpr int kStages = 4;
    void Prepare(float sample_rate) {
        fs_ = sample_rate;
        amount_.Prepare(fs_, 0.02f);
        Reset();
    }
    void Reset() {
        std::memset(x1_, 0, sizeof(x1_));
        std::memset(y1_, 0, sizeof(y1_));
        last_ = 0.f;
        phase_ = 0.f;
        hop_ = 0;
        amount_.Snap();
        Control();
    }
    void Set(float amount, float rate_hz) {
        amount_.target = amount;
        inc_ = rate_hz * float(kControlHop) / fs_;
    }
    // All-pass and feedback states to zero (All Sound Off); the LFO keeps its phase.
    void Clear() {
        std::memset(x1_, 0, sizeof(x1_));
        std::memset(y1_, 0, sizeof(y1_));
        last_ = 0.f;
    }
    float Process(float x) {
        if(hop_ == 0)
            Control();
        if(++hop_ == kControlHop)
            hop_ = 0;
        const float a = amount_.value;
        float u = x + 0.5f * a * last_;
        for(int s = 0; s < kStages; ++s) {
            const float y = g_ * u + x1_[s] - g_ * y1_[s];
            x1_[s] = u;
            y1_[s] = y;
            u = y;
        }
        last_ = u;
        if(last_ > -1e-20f && last_ < 1e-20f)
            last_ = 0.f;
        if(a == 0.f)
            return x;
        return x + 0.5f * a * (u - x);
    }

  private:
    void Control() {
        amount_.Step();
        phase_ = Wrap(phase_ + inc_);
        const float sweep = 0.5f + 0.5f * Parabola(phase_);
        float f = 80.f * std::pow(3000.f / 80.f, sweep);
        if(f > 0.45f * fs_)
            f = 0.45f * fs_;
        const float t = std::tan(kPi * f / fs_);
        g_ = (t - 1.f) / (t + 1.f);
    }
    float fs_ = 48000.f, phase_ = 0.f, inc_ = 0.f, g_ = 0.f, last_ = 0.f;
    float x1_[kStages]{}, y1_[kStages]{};
    int hop_ = 0;
    Smoothed amount_;
};

// Own reverb (SF-D11: the original has a 16384-float ring, eight + two sub-stages and two
// modulated phases; topology and decay curve OPEN). Two input all-pass diffusers and a
// four-line feedback delay network with a normalized Hadamard matrix, one-pole damping per
// line and feedback g = 0.1 + 0.83 decay (|g| <= 0.93 < 1, contractive). No delay modulation.
struct ReverbStorage {
    static constexpr float kMaxRate = 48000.f;
    static constexpr int kLineCap[4] = {1440, 1792, 1984, 2112}; // >= 29.7/37.1/41.1/43.7 ms at 48 kHz
    static constexpr int kDiffCap[2] = {256, 384};               // >= 5.3 / 7.9 ms
    float line0[1440], line1[1792], line2[1984], line3[2112];
    float diff0[256], diff1[384];
};

class Reverb {
  public:
    bool Prepare(float sample_rate, ReverbStorage& storage) {
        if(!(sample_rate >= 8000.f && sample_rate <= ReverbStorage::kMaxRate))
            return false;
        fs_ = sample_rate;
        s_ = &storage;
        lines_[0] = storage.line0;
        lines_[1] = storage.line1;
        lines_[2] = storage.line2;
        lines_[3] = storage.line3;
        static constexpr float kMs[4] = {29.7f, 37.1f, 41.1f, 43.7f};
        for(int i = 0; i < 4; ++i) {
            len_[i] = int(kMs[i] * 0.001f * fs_ + 0.5f);
            if(len_[i] > ReverbStorage::kLineCap[i])
                return false;
        }
        diff_len_[0] = int(0.0053f * fs_ + 0.5f);
        diff_len_[1] = int(0.0079f * fs_ + 0.5f);
        float fc = 6500.f < 0.2f * fs_ ? 6500.f : 0.2f * fs_;
        damp_a_ = std::exp(-2.f * kPi * fc / fs_);
        Clear();
        return true;
    }
    void Clear() {
        std::memset(s_, 0, sizeof(ReverbStorage));
        for(int i = 0; i < 4; ++i) {
            pos_[i] = 0;
            lp_[i] = 0.f;
        }
        diff_pos_[0] = diff_pos_[1] = 0;
    }
    void SetDecay(float decay) { g_ = 0.1f + 0.83f * decay; }
    float Feedback() const { return g_; }

    void Process(float in_l, float in_r, float& wet_l, float& wet_r) {
        float x = 0.5f * (in_l + in_r);
        x = Diffuse(0, s_->diff0, x);
        x = Diffuse(1, s_->diff1, x);
        float d[4];
        for(int i = 0; i < 4; ++i) {
            d[i] = lines_[i][pos_[i]];
            lp_[i] = (1.f - damp_a_) * d[i] + damp_a_ * lp_[i];
        }
        // H/2 (Sylvester 4x4) applied to the damped outputs.
        const float h0 = 0.5f * (lp_[0] + lp_[1] + lp_[2] + lp_[3]);
        const float h1 = 0.5f * (lp_[0] - lp_[1] + lp_[2] - lp_[3]);
        const float h2 = 0.5f * (lp_[0] + lp_[1] - lp_[2] - lp_[3]);
        const float h3 = 0.5f * (lp_[0] - lp_[1] - lp_[2] + lp_[3]);
        const float in = 0.5f * x;
        const float v[4] = {in + g_ * h0, in + g_ * h1, in + g_ * h2, in + g_ * h3};
        for(int i = 0; i < 4; ++i) {
            lines_[i][pos_[i]] = Flush(v[i]);
            if(++pos_[i] == len_[i])
                pos_[i] = 0;
        }
        wet_l = 0.5f * (d[0] + d[1] - d[2] - d[3]);
        wet_r = 0.5f * (d[0] - d[1] + d[2] - d[3]);
    }

  private:
    static float Flush(float v) { return (v > -1e-20f && v < 1e-20f) ? 0.f : v; }
    float Diffuse(int k, float* buf, float x) {
        constexpr float g = 0.6f;
        const float delayed = buf[diff_pos_[k]];
        const float w = x + g * delayed;
        buf[diff_pos_[k]] = Flush(w);
        if(++diff_pos_[k] == diff_len_[k])
            diff_pos_[k] = 0;
        return delayed - g * w;
    }
    float fs_ = 48000.f, damp_a_ = 0.f, g_ = 0.475f;
    ReverbStorage* s_ = nullptr;
    float* lines_[4]{};
    int len_[4]{}, pos_[4]{}, diff_len_[2]{}, diff_pos_[2]{};
    float lp_[4]{};
};

} // namespace streichwurst_fx
