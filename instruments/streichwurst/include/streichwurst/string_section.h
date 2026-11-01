#pragma once
// SPDX-License-Identifier: MIT
// Streichwurst string section: own implementation of the documented structure
// (docs/FIDELITY.md, research SF-C01/C02, SF-B01, SF-F01..F05). No manufacturer data.
//
//  * Twelve free-running pitch-class clocks. Each clock ticks at twice the frequency of
//    the highest MIDI note of its class; a binary counter divides it down, so bit b is a
//    square wave of note top-12b (divide-down: PLAUSIBLE reading of SF-C02, own choice).
//    New notes never reset a phase.
//  * At each tick the amplitude step of all toggled octave bits (dA = new - old output of
//    that note, with its current level) is written into a 16-sample event ring through an
//    own 8-tap band-limited impulse kernel (256 fractional rows, own windowed sinc,
//    detail::kKernel). The original kernel coefficients are not known and not used.
//  * Output: the documented leaky integration z[n] = (1 - 1/256) z[n-1] + r[n] (SF-C02),
//    with the leak rescaled from 40 kHz to the actual sample rate (same time constant).
//  * 128 note levels follow 128 targets with the asymmetric one-pole of SF-F01 (crescendo
//    for rising, release for falling differences), updated every 16 samples; one index
//    per update is cleaned up cyclically below a small level (SF-F01).
//  * Targets: registration -> 8 footage weights by interpolating 13 frames (SF-F03, own
//    frame data from a formula), octave transform of SF-F04, footage offsets -12/0/+12...
//    in octave steps (SF-F05, PLAUSIBLE), activations of the held notes.
// Real-time safe: no allocation, no locks, fixed loops; Prepare() is start-up only.
#include "params.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace streichwurst {

constexpr int kNotes = 128;
constexpr int kFootages = 8;
constexpr int kRegistrationFrames = 13;
constexpr int kFootageOffset[kFootages] = {-12, 0, 12, 24, 36, 48, 60, 72};
constexpr int kKernelRows = 256, kKernelTaps = 8, kRingSize = 16;
constexpr int kControlHop = 16;                 // samples per control update (original: 16 at 40 kHz)
constexpr float kNativeRegistrationMax = 32498.f; // z clamp of SF-F03
constexpr float kOriginalRate = 40000.f;
constexpr float kMaxFootageWeight = 2.f; // SF-F04 upper bound 2^26 relative to weights <= 2^25
constexpr float kMaxTarget = 4.f;        // own saturation of accumulated targets
constexpr float kCleanupLevel = 8192.f / 67108864.f; // SF-F01 threshold 8192 relative to 2^26

inline float NoteHz(float note) { return 440.f * std::exp2((note - 69.f) / 12.f); }

// Own crescendo/release curve: log-linear through the three conditional calibration
// points of SF-F02 (native index 0, 64, 127 -> 0.577 ms, 151.3 ms, 4.369 s). The original
// index table is not used; the curve between the points is an assumption.
inline float EnvelopeTimeConstant(float u) {
    const float idx = (u < 0.f ? 0.f : (u > 1.f ? 1.f : u)) * 127.f;
    const float l0 = std::log(0.000577103f), l64 = std::log(0.151328236f), l127 = std::log(4.368866664f);
    const float l = idx <= 64.f ? l0 + (l64 - l0) * idx / 64.f : l64 + (l127 - l64) * (idx - 64.f) / 63.f;
    return std::exp(l);
}

// Own registration frames (no original frame values): a footage centre moving from the
// 16' side (frame 0, dark) to the high footages (frame 12, thin), weights falling by one
// octave per footage away from the centre (steeper below it), normalized to sum 1. A
// binary fall-off over octave-spaced squares approximates a staircase sawtooth.
inline void OwnRegistrationFrames(float frames[kRegistrationFrames][kFootages]) {
    for(int j = 0; j < kRegistrationFrames; ++j) {
        const float centre = 0.6f + 0.25f * float(j);
        float sum = 0.f;
        for(int i = 0; i < kFootages; ++i) {
            const float d = float(i) - centre;
            const float w = d >= 0.f ? std::exp2(-d) : std::exp2(1.5f * d);
            frames[j][i] = w;
            sum += w;
        }
        for(int i = 0; i < kFootages; ++i)
            frames[j][i] /= sum;
    }
}

// SF-F04 octave transform of the eight weights (real-valued form), then the upper bound.
inline void OctaveTransform(const float in[kFootages], int octave, float out[kFootages]) {
    for(int i = 0; i < kFootages; ++i) {
        const float below = i > 0 ? in[i - 1] : 0.f;
        float w = in[i];
        if(octave == OctaveBoth)
            w = 0.75f * (in[i] + below);
        else if(octave == OctaveUpper)
            w = below;
        out[i] = w > kMaxFootageWeight ? kMaxFootageWeight : w;
    }
}

// Own band-limited impulse kernel, computed at compile time (read-only data, not DTCM):
// sinc at 0.9 x Nyquist under a 4-term Blackman-Harris window spanning [-4, 4] samples,
// latency 4 samples; each of the 256 fractional rows sums to 1 so the integrated step
// equals dA exactly.
namespace detail {
constexpr double kPiD = 3.14159265358979323846;
constexpr double ConstSin(double x) {
    while(x > kPiD)
        x -= 2.0 * kPiD;
    while(x < -kPiD)
        x += 2.0 * kPiD;
    double term = x, sum = x;
    for(int n = 1; n < 14; ++n) {
        term *= -x * x / double((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}
constexpr double ConstCos(double x) { return ConstSin(x + kPiD / 2.0); }
struct KernelTable {
    float row[kKernelRows][kKernelTaps];
};
constexpr KernelTable MakeKernel() {
    KernelTable k{};
    for(int r = 0; r < kKernelRows; ++r) {
        const double d = (double(r) + 0.5) / double(kKernelRows);
        double v[kKernelTaps] = {};
        double sum = 0.0;
        for(int j = 0; j < kKernelTaps; ++j) {
            const double t = double(j) + d - 4.0; // time since the event, centred
            const double x = 0.9 * t;
            const double sinc = x == 0.0 ? 1.0 : ConstSin(kPiD * x) / (kPiD * x);
            const double w0 = (t + 4.0) / 8.0; // 0..1 across the window
            const double w = 0.35875 - 0.48829 * ConstCos(2.0 * kPiD * w0) + 0.14128 * ConstCos(4.0 * kPiD * w0) -
                             0.01168 * ConstCos(6.0 * kPiD * w0);
            v[j] = sinc * w;
            sum += v[j];
        }
        for(int j = 0; j < kKernelTaps; ++j)
            k.row[r][j] = float(v[j] / sum);
    }
    return k;
}
inline constexpr KernelTable kKernel = MakeKernel();
} // namespace detail

class StringSection {
  public:
    bool Prepare(float sample_rate) {
        if(!(sample_rate >= 8000.f && sample_rate <= 192000.f))
            return false;
        fs_ = sample_rate;
        leak_ = 1.f - (1.f / 256.f) * (kOriginalRate / fs_);
        hop_seconds_ = float(kControlHop) / fs_;
        OwnRegistrationFrames(frames_);
        for(int c = 0; c < 12; ++c) {
            const int top = c < 8 ? 120 + c : 108 + c; // highest MIDI note of the class
            top_[c] = top;
            bits_[c] = top / 12 + 1;                     // octave bits down to the lowest note >= 0
            base_inc_[c] = 2.f * NoteHz(float(top)) / fs_; // ticks per sample
        }
        prepared_ = true;
        Reset();
        return true;
    }

    void Reset() {
        std::memset(level_, 0, sizeof(level_));
        std::memset(target_, 0, sizeof(target_));
        std::memset(out_, 0, sizeof(out_));
        std::memset(activation_, 0, sizeof(activation_));
        std::memset(ring_, 0, sizeof(ring_));
        for(int c = 0; c < 12; ++c) {
            phase_[c] = 0.f;
            counter_[c] = 0;
        }
        z_ = 0.f;
        ring_pos_ = 0;
        hop_pos_ = 0;
        clean_index_ = 0;
        pitch_current_ = pitch_target_;
        animate_offset_ = 0.f;
        targets_dirty_ = true;
        UpdatePitch();
        UpdateCoefficients();
    }

    void SetParameters(float registration, int octave, float crescendo, float release) {
        if(registration != registration_ || octave != octave_) {
            registration_ = registration;
            octave_ = octave;
            targets_dirty_ = true;
        }
        if(crescendo != crescendo_ || release != release_) {
            crescendo_ = crescendo;
            release_ = release;
            UpdateCoefficients();
        }
    }
    // Pitch offset of all clocks in semitones (bend, tune, matrix); smoothed with 5 ms.
    void SetPitch(float semitones) { pitch_target_ = semitones; }
    // Typed modulation from the FX module's Animate stage: native registration offset m of
    // z = clamp(256 r + m, 0, 32498) (SF-F03). Applied at the next control update.
    void SetAnimateOffset(float native) {
        if(native != animate_offset_) {
            animate_offset_ = native;
            targets_dirty_ = true;
        }
    }
    // Activation strength 0..1 of a (transposed) MIDI note; 0 releases its contributions.
    void SetActivation(int note, float strength) {
        if(note < 0 || note >= kNotes || activation_[note] == strength)
            return;
        activation_[note] = strength;
        targets_dirty_ = true;
    }
    void ReleaseAll() {
        for(float& a : activation_)
            a = 0.f;
        targets_dirty_ = true;
    }
    // AllSoundOff: levels, square states, event ring and integrator to zero.
    void Silence() {
        ReleaseAll();
        std::memset(level_, 0, sizeof(level_));
        std::memset(target_, 0, sizeof(target_));
        std::memset(out_, 0, sizeof(out_));
        std::memset(ring_, 0, sizeof(ring_));
        z_ = 0.f;
    }

    float Process() {
        if(hop_pos_ == 0)
            Control();
        if(++hop_pos_ == kControlHop)
            hop_pos_ = 0;
        for(int c = 0; c < 12; ++c) {
            float ph = phase_[c] + inc_[c];
            while(ph >= 1.f) {
                ph -= 1.f;
                Tick(c, ph / inc_[c]);
            }
            phase_[c] = ph;
        }
        const float r = ring_[ring_pos_];
        ring_[ring_pos_] = 0.f;
        ring_pos_ = (ring_pos_ + 1) & (kRingSize - 1);
        z_ = z_ * leak_ + r;
        if(z_ > -1e-20f && z_ < 1e-20f)
            z_ = 0.f; // no denormals in the idle integrator
        return z_;
    }

    // Diagnostics and tests.
    float Level(int note) const { return level_[note]; }
    float Target(int note) const { return target_[note]; }
    float CrescendoAlpha() const { return alpha_up_; }
    float ReleaseAlpha() const { return alpha_down_; }
    float HopSeconds() const { return hop_seconds_; }
    float ClassTickRate(int c) const { return inc_[c] * fs_; }
    int TopNote(int c) const { return top_[c]; }
    int FirstAudibleBit(int c) const { return first_bit_[c]; }
    float Weight(int i) const { return weights_[i]; }
    static const float* Kernel(int row) { return detail::kKernel.row[row]; }
    bool Idle() const {
        for(int m = 0; m < kNotes; ++m)
            if(level_[m] != 0.f || target_[m] != 0.f || out_[m] != 0.f)
                return false;
        return true;
    }

  private:
    void UpdateCoefficients() {
        // alpha per control hop from the time constant: tau = -T / ln(1 - alpha) (SF-F01).
        alpha_up_ = 1.f - std::exp(-hop_seconds_ / EnvelopeTimeConstant(crescendo_));
        alpha_down_ = 1.f - std::exp(-hop_seconds_ / EnvelopeTimeConstant(release_));
    }

    void UpdatePitch() {
        const float factor = std::exp2(pitch_current_ / 12.f);
        const float limit = 0.45f * fs_;
        for(int c = 0; c < 12; ++c) {
            inc_[c] = base_inc_[c] * factor;
            // Octave bits whose square would lie above 0.45 fs stay silent.
            int b = 0;
            float f = NoteHz(float(top_[c])) * factor;
            while(f >= limit && b < bits_[c]) {
                f *= 0.5f;
                ++b;
            }
            first_bit_[c] = b;
        }
    }

    void UpdateTargets() {
        const float r = registration_ * 127.f;
        float z = 256.f * r + animate_offset_;
        z = z < 0.f ? 0.f : (z > kNativeRegistrationMax ? kNativeRegistrationMax : z);
        const float p = z * 355.f / 1048576.f; // c / 256 with c = 355 z / 4096 (SF-F03)
        int j = int(p);
        if(j > kRegistrationFrames - 2)
            j = kRegistrationFrames - 2;
        const float f = p - float(j);
        float w[kFootages];
        for(int i = 0; i < kFootages; ++i)
            w[i] = frames_[j][i] + (frames_[j + 1][i] - frames_[j][i]) * f;
        OctaveTransform(w, octave_, weights_);
        std::memset(target_, 0, sizeof(target_));
        for(int n = 0; n < kNotes; ++n) {
            const float a = activation_[n];
            if(a <= 0.f)
                continue;
            for(int i = 0; i < kFootages; ++i) {
                const int m = n + kFootageOffset[i];
                if(m >= 0 && m < kNotes)
                    target_[m] += weights_[i] * a;
            }
        }
        for(float& t : target_)
            if(t > kMaxTarget)
                t = kMaxTarget;
    }

    void Control() {
        if(pitch_current_ != pitch_target_) {
            const float a = 1.f - std::exp(-hop_seconds_ / 0.005f);
            pitch_current_ += (pitch_target_ - pitch_current_) * a;
            if(std::fabs(pitch_target_ - pitch_current_) < 1e-4f)
                pitch_current_ = pitch_target_;
            UpdatePitch();
        }
        if(targets_dirty_) {
            targets_dirty_ = false;
            UpdateTargets();
        }
        for(int m = 0; m < kNotes; ++m) {
            const float d = target_[m] - level_[m];
            if(d != 0.f)
                level_[m] += d * (d >= 0.f ? alpha_up_ : alpha_down_);
        }
        const int i = clean_index_;
        clean_index_ = (clean_index_ + 1) & (kNotes - 1);
        if(target_[i] == 0.f && std::fabs(level_[i]) < kCleanupLevel)
            level_[i] = 0.f;
    }

    void Tick(int c, float elapsed) {
        const uint32_t k = counter_[c], k1 = k + 1;
        counter_[c] = k1;
        uint32_t flipped = (k ^ k1) & ((1u << bits_[c]) - 1u);
        float delta = 0.f;
        while(flipped) {
            const int b = __builtin_ctz(flipped);
            flipped &= flipped - 1u;
            const int m = top_[c] - 12 * b;
            const float v = b < first_bit_[c] ? 0.f : (((k1 >> b) & 1u) ? level_[m] : -level_[m]);
            delta += v - out_[m];
            out_[m] = v;
        }
        if(delta == 0.f)
            return;
        int row = int(elapsed * float(kKernelRows));
        row = row < 0 ? 0 : (row >= kKernelRows ? kKernelRows - 1 : row);
        const float* h = detail::kKernel.row[row];
        for(int j = 0; j < kKernelTaps; ++j)
            ring_[(ring_pos_ + j) & (kRingSize - 1)] += delta * h[j];
    }

    bool prepared_ = false;
    float fs_ = 48000.f, leak_ = 1.f, hop_seconds_ = 1.f / 3000.f;
    float frames_[kRegistrationFrames][kFootages]{};
    float weights_[kFootages]{};
    int top_[12]{}, bits_[12]{}, first_bit_[12]{};
    float base_inc_[12]{}, inc_[12]{}, phase_[12]{};
    uint32_t counter_[12]{};
    float level_[kNotes]{}, target_[kNotes]{}, out_[kNotes]{}, activation_[kNotes]{};
    float ring_[kRingSize]{};
    float z_ = 0.f;
    int ring_pos_ = 0, hop_pos_ = 0, clean_index_ = 0;
    float registration_ = 0.5f, crescendo_ = 0.2f, release_ = 0.45f;
    int octave_ = OctaveBase;
    float alpha_up_ = 0.f, alpha_down_ = 0.f;
    float pitch_target_ = 0.f, pitch_current_ = 0.f;
    float animate_offset_ = 0.f;
    bool targets_dirty_ = true;
};

} // namespace streichwurst
