#pragma once
// SPDX-License-Identifier: MIT
// Variable-rate 48 kHz host adapter for the FV-1 interpreter: the program runs at an adjustable internal rate
// (kMinRate..kMaxRate, 8..32 kHz) between two band-limited fractional resamplers. Lower rates stretch delay times,
// lower generator pitches and filter corners and cost proportionally less CPU (fewer interpreter samples).
// Rate changes glide like a tape-speed change; the interpreter state is kept.
//
// Contract: seed::audio FX interface 1.0 at 48 kHz. Prepare({48000, max_block <= 48}, storage); Process is
// wet-only, bounded (at most kMaxVmBlock interpreter samples per block) and allocation-free.
// Resampler: Kaiser-windowed sinc, kZeros zero crossings per side, cutoff kCutoff x the internal Nyquist.
// Down: the kernel is stretched by q = 48000 / rate (anti-aliasing), read from a full symmetric table with linear
// interpolation. Up: unit spacing at the internal rate (anti-imaging), so the 2 * kZeros taps of one fractional
// position are a precomputed, unity-gain row; two neighbouring rows are interpolated. The hot loops hold no
// range checks, no absolute values and no per-tap normalisation (the Seed3 budget is what limits this adapter).
#include "fv1_vm.h"
#include <cmath>
#include <cstdint>

namespace seed::fx::fv1 {

template <typename T> class FxVar {
  public:
    static constexpr unsigned kHostRate = 48000;
    static constexpr float kMinRate = 8000.f, kMaxRate = 32000.f;
    static constexpr std::size_t kMaxBlock = 48;
    static constexpr unsigned kZeros = 8, kPhases = 64;
    static constexpr float kCutoff = .87f, kBeta = 7.f;
    static constexpr std::size_t kMaxVmBlock = kMaxBlock * 2 / 3 + 2; // 34 at 32 kHz

    bool Prepare(const seed::audio::Config& config, Storage<T>& storage) {
        if(config.sample_rate != kHostRate || config.max_block == 0 || config.max_block > kMaxBlock) return false;
        config_ = config;
        BuildTables();
        rate_ = target_rate_ = kMaxRate;
        ResetResamplers();
        return vm_.Prepare({unsigned(kMaxRate), kMaxVmBlock}, storage);
    }
    void LoadProgram(const uint32_t* words) { vm_.LoadProgram(words); }
    // Clears the interpreter state, its delay RAM and the resampler histories (not real-time cheap).
    void Reset() {
        vm_.Reset();
        ResetResamplers();
    }
    void SetParameters(const Parameters& p) { vm_.SetParameters(p); }
    // Internal rate in Hz, clamped to kMinRate..kMaxRate; approached over a few milliseconds.
    void SetRate(float hz) { target_rate_ = hz < kMinRate ? kMinRate : hz > kMaxRate ? kMaxRate : hz; }
    float Rate() const { return rate_; }
    // Wet only; any block length up to max_block.
    bool Process(const seed::audio::StereoBlock& b) {
        if(!seed::audio::Valid(config_, b)) return false;
        if(!b.frames) return true;
        // One-pole glide per block (time constant about 5 ms at 1 kHz blocks).
        rate_ += (target_rate_ - rate_) * .2f;
        if(std::fabs(rate_ - target_rate_) < .5f) rate_ = target_rate_;
        const double q = double(kHostRate) / double(rate_); // host samples per interpreter sample
        const double inv_q = 1.0 / q;
        // 1) Down: append the host block, emit interpreter input samples whose kernels are complete.
        float vl[kMaxVmBlock], vr[kMaxVmBlock], ol[kMaxVmBlock], orr[kMaxVmBlock];
        std::size_t m = 0;
        for(std::size_t i = 0; i < b.frames; ++i) {
            in_l_[in_pos_ & kInMask] = b.left[i];
            in_r_[in_pos_ & kInMask] = b.right[i];
            ++in_pos_;
            newest_ += 1.0;
            // Fixed lead (not kZeros * q): production and the up side's read position then advance by
            // exactly the same 1/q per host sample, so their distance stays constant through rate changes.
            while(center_ + kDownLead <= newest_ && m < kMaxVmBlock) {
                Down(q, inv_q, vl[m], vr[m]);
                ++m;
                center_ += q;
            }
        }
        // 2) Interpreter at the internal rate.
        if(m && !vm_.Process({vl, vr, ol, orr, m})) return false;
        for(std::size_t k = 0; k < m; ++k) {
            vm_l_[vm_pos_ & kVmMask] = ol[k];
            vm_r_[vm_pos_ & kVmMask] = orr[k];
            ++vm_pos_;
        }
        vm_count_ += m;
        // 3) Up: one host output per frame, kZeros + kUpMargin interpreter samples behind production.
        for(std::size_t i = 0; i < b.frames; ++i) {
            Up(b.out_left[i], b.out_right[i]);
            sigma_ += inv_q;
        }
        return true;
    }
    const Vm<T>& Interpreter() const { return vm_; }
    uint64_t InterpreterSamples() const { return vm_count_; }

  private:
    static constexpr unsigned kInSize = 512, kInMask = kInSize - 1;  // > 2 * kDownLead + kMaxBlock
    static constexpr unsigned kVmSize = 128, kVmMask = kVmSize - 1;  // > 2 * kZeros + kUpMargin + kMaxVmBlock
    static constexpr unsigned kTaps = 2 * kZeros;
    static constexpr unsigned kCentre = kZeros * kPhases;  // table index of kernel argument 0
    static constexpr unsigned kTable = 2 * kCentre + 2;    // +-kZeros, plus one guard point for the interpolation
    // Down emits an interpreter sample once kDownLead host samples past its centre exist (the widest
    // kernel, at kMinRate). Up starts kZeros + kUpMargin samples behind the newest produced sample.
    static constexpr double kDownLead = kZeros * (double(kHostRate) / double(kMinRate));
    static constexpr unsigned kUpMargin = 2;

    static double KernelValue(double u) {
        constexpr double kPi = 3.14159265358979323846;
        if(std::fabs(u) >= kZeros) return 0;
        auto i0 = [](double x) {
            double sum = 1, term = 1;
            for(int k = 1; k < 40; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; }
            return sum;
        };
        const double x = kPi * kCutoff * u;
        const double sinc = u == 0 ? 1.0 : std::sin(x) / x;
        const double r = u / kZeros;
        return kCutoff * sinc * i0(kBeta * std::sqrt(std::fmax(0.0, 1 - r * r))) / i0(kBeta);
    }
    void BuildTables() {
        for(unsigned j = 0; j < kTable; ++j) table_[j] = float(KernelValue((double(j) - kCentre) / kPhases));
        // One row per fractional position f = r / kPhases: taps d = 1 - kZeros .. kZeros, argument f - d, unity sum.
        for(unsigned r = 0; r <= kPhases; ++r) {
            double sum = 0, h[kTaps];
            for(unsigned t = 0; t < kTaps; ++t) {
                const int d = int(t) + 1 - int(kZeros);
                h[t] = KernelValue(double(r) / kPhases - d);
                sum += h[t];
            }
            for(unsigned t = 0; t < kTaps; ++t) rows_[r][t] = float(h[t] / sum);
        }
    }
    void ResetResamplers() {
        for(auto& x : in_l_) x = 0;
        for(auto& x : in_r_) x = 0;
        for(auto& x : vm_l_) x = 0;
        for(auto& x : vm_r_) x = 0;
        in_pos_ = vm_pos_ = 0;
        newest_ = -1;
        center_ = 0;
        // Distance between production and the up side's read position (kept from now on): kZeros + kUpMargin.
        const double q = double(kHostRate) / double(rate_);
        sigma_ = -(kDownLead / q + kZeros + kUpMargin);
    }
    // Interpreter input at center_. Ring slots before the start hold zeros, so no start-up case is needed.
    void Down(double q, double inv_q, float& l, float& r) const {
        const double reach = kZeros * q;
        const double first_d = std::ceil(center_ - reach);
        const int count = int(std::floor(center_ + reach) - first_d) + 1;
        const float step = float(inv_q * kPhases);
        float x = float((center_ - first_d) * inv_q * kPhases) + float(kCentre); // table position, falls per tap
        uint32_t idx = uint32_t(int32_t(int64_t(first_d)));
        float sl = 0, sr = 0;
        for(int k = 0; k < count; ++k, x -= step, ++idx) {
            const unsigned j = unsigned(x);
            const float h = table_[j] + (x - float(j)) * (table_[j + 1] - table_[j]);
            sl += h * in_l_[idx & kInMask];
            sr += h * in_r_[idx & kInMask];
        }
        const float g = float(inv_q); // the stretched kernel sums to q
        l = sl * g; r = sr * g;
    }
    // Host output at sigma_.
    void Up(float& l, float& r) const {
        const double floor_sigma = std::floor(sigma_);
        const float x = float(sigma_ - floor_sigma) * float(kPhases);
        const unsigned row = unsigned(x);
        const float a = x - float(row);
        const float* lo = rows_[row];
        const float* hi = rows_[row + 1];
        uint32_t idx = uint32_t(int32_t(int64_t(floor_sigma))) + 1u - kZeros;
        float sl = 0, sr = 0;
        for(unsigned t = 0; t < kTaps; ++t, ++idx) {
            const float h = lo[t] + a * (hi[t] - lo[t]);
            sl += h * vm_l_[idx & kVmMask];
            sr += h * vm_r_[idx & kVmMask];
        }
        l = sl; r = sr;
    }

    seed::audio::Config config_{};
    Vm<T> vm_;
    float table_[kTable] = {};
    float rows_[kPhases + 1][kTaps] = {};
    float in_l_[kInSize] = {}, in_r_[kInSize] = {}, vm_l_[kVmSize] = {}, vm_r_[kVmSize] = {};
    uint32_t in_pos_ = 0, vm_pos_ = 0; // ring write positions (wrap harmlessly)
    uint64_t vm_count_ = 0;            // interpreter samples since Prepare (diagnostic)
    double newest_ = -1, center_ = 0, sigma_ = 0;
    float rate_ = kMaxRate, target_rate_ = kMaxRate;
};

} // namespace seed::fx::fv1
