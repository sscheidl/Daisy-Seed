#pragma once
// SPDX-License-Identifier: MIT
// Variable-rate 48 kHz host adapter for the FV-1 interpreter: the program runs at an adjustable internal rate
// (kMinRate..kMaxRate, 8..32 kHz) between two band-limited fractional resamplers. Lower rates stretch delay times,
// lower generator pitches and filter corners and cost proportionally less CPU (fewer interpreter samples).
// Rate changes glide like a tape-speed change; the interpreter state is kept.
//
// Contract: seed::audio FX interface 1.0 at 48 kHz. Prepare({48000, max_block <= 48}, storage); Process is
// wet-only, bounded (at most kMaxVmBlock interpreter samples per block) and allocation-free.
// Resampler: Kaiser-windowed sinc, kZeros zero crossings per side, cutoff kCutoff x the internal Nyquist,
// linear interpolation in a table of kPhases points per zero crossing. Down: the kernel is stretched by
// q = 48000 / rate (anti-aliasing); up: unit spacing at the internal rate (anti-imaging).
#include "fv1_vm.h"
#include <cmath>
#include <cstdint>

namespace seed::fx::fv1 {

template <typename T> class FxVar {
  public:
    static constexpr unsigned kHostRate = 48000;
    static constexpr float kMinRate = 8000.f, kMaxRate = 32000.f;
    static constexpr std::size_t kMaxBlock = 48;
    static constexpr unsigned kZeros = 12, kPhases = 64;
    static constexpr float kCutoff = .82f, kBeta = 8.f;
    static constexpr std::size_t kMaxVmBlock = kMaxBlock * 2 / 3 + 2; // 34 at 32 kHz

    bool Prepare(const seed::audio::Config& config, Storage<T>& storage) {
        if(config.sample_rate != kHostRate || config.max_block == 0 || config.max_block > kMaxBlock) return false;
        config_ = config;
        BuildTable();
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
        // 1) Down: append the host block, emit interpreter input samples whose kernels are complete.
        float vl[kMaxVmBlock], vr[kMaxVmBlock], ol[kMaxVmBlock], orr[kMaxVmBlock];
        std::size_t m = 0;
        for(std::size_t i = 0; i < b.frames; ++i) {
            in_l_[in_count_ & kInMask] = b.left[i];
            in_r_[in_count_ & kInMask] = b.right[i];
            const double newest = double(in_count_++);
            // Fixed lead (not kZeros * q): production and the up side's read position then advance by
            // exactly the same 1/q per host sample, so their distance stays constant through rate changes.
            while(center_ + kDownLead <= newest && m < kMaxVmBlock) {
                Down(center_, q, 1.0 / q, vl[m], vr[m]);
                ++m;
                center_ += q;
            }
        }
        // 2) Interpreter at the internal rate.
        if(m && !vm_.Process({vl, vr, ol, orr, m})) return false;
        for(std::size_t k = 0; k < m; ++k) {
            vm_l_[vm_count_ & kVmMask] = ol[k];
            vm_r_[vm_count_ & kVmMask] = orr[k];
            ++vm_count_;
        }
        // 3) Up: one host output per frame, kZeros + kUpMargin interpreter samples behind production.
        for(std::size_t i = 0; i < b.frames; ++i) {
            Up(sigma_, b.out_left[i], b.out_right[i]);
            sigma_ += 1.0 / q;
        }
        return true;
    }
    const Vm<T>& Interpreter() const { return vm_; }
    uint64_t InterpreterSamples() const { return vm_count_; }

  private:
    static constexpr unsigned kInSize = 512, kInMask = kInSize - 1;  // > 2 * kDownLead + kMaxBlock
    static constexpr unsigned kVmSize = 128, kVmMask = kVmSize - 1;  // > 2 * kZeros + kUpMargin + kMaxVmBlock
    static constexpr unsigned kTable = kZeros * kPhases;
    // Down emits an interpreter sample once kDownLead host samples past its centre exist (the widest
    // kernel, at kMinRate). Up starts kZeros + kUpMargin samples behind the newest produced sample.
    static constexpr double kDownLead = kZeros * (double(kHostRate) / double(kMinRate));
    static constexpr unsigned kUpMargin = 2;

    void BuildTable() {
        constexpr double kPi = 3.14159265358979323846;
        auto i0 = [](double x) {
            double sum = 1, term = 1;
            for(int k = 1; k < 40; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; }
            return sum;
        };
        for(unsigned j = 0; j <= kTable; ++j) {
            const double u = double(j) / kPhases; // 0..kZeros
            const double x = kPi * kCutoff * u;
            const double sinc = j == 0 ? 1.0 : std::sin(x) / x;
            const double r = u / kZeros;
            const double w = i0(kBeta * std::sqrt(std::fmax(0.0, 1 - r * r))) / i0(kBeta);
            table_[j] = float(kCutoff * sinc * w);
        }
    }
    float Kernel(double u) const { // symmetric, zero outside +-kZeros
        u = std::fabs(u) * kPhases;
        if(u >= kTable) return 0.f;
        const unsigned j = unsigned(u);
        const float f = float(u - j);
        return table_[j] + f * (table_[j + 1] - table_[j]);
    }
    void ResetResamplers() {
        for(auto& x : in_l_) x = 0;
        for(auto& x : in_r_) x = 0;
        for(auto& x : vm_l_) x = 0;
        for(auto& x : vm_r_) x = 0;
        in_count_ = vm_count_ = 0;
        center_ = 0;
        // Distance between production and the up side's read position (kept from now on): kZeros + kUpMargin.
        const double q = double(kHostRate) / double(rate_);
        sigma_ = -(kDownLead / q + kZeros + kUpMargin);
    }
    void Down(double center, double q, double inv_q, float& l, float& r) const {
        const double reach = kZeros * q;
        const int64_t first = int64_t(std::ceil(center - reach)), last = int64_t(std::floor(center + reach));
        float sl = 0, sr = 0, norm = 0;
        for(int64_t i = first; i <= last; ++i) {
            if(i < 0) continue;
            const float h = Kernel((center - double(i)) * inv_q);
            sl += h * in_l_[uint64_t(i) & kInMask];
            sr += h * in_r_[uint64_t(i) & kInMask];
            norm += h;
        }
        // Exact unity gain at DC for every q (the stretched kernel's sum differs from 1 by its sampling).
        const float g = norm > 1e-6f ? 1.f / norm : 0.f;
        l = sl * g; r = sr * g;
    }
    void Up(double sigma, float& l, float& r) const {
        const int64_t first = int64_t(std::ceil(sigma - kZeros)), last = int64_t(std::floor(sigma + kZeros));
        const int64_t newest = int64_t(vm_count_) - 1;
        float sl = 0, sr = 0, norm = 0;
        for(int64_t k = first; k <= last; ++k) {
            const float h = Kernel(sigma - double(k));
            norm += h;
            if(k < 0 || k > newest) continue;
            sl += h * vm_l_[uint64_t(k) & kVmMask];
            sr += h * vm_r_[uint64_t(k) & kVmMask];
        }
        const float g = norm > 1e-6f ? 1.f / norm : 0.f;
        l = sl * g; r = sr * g;
    }

    seed::audio::Config config_{};
    Vm<T> vm_;
    float table_[kTable + 1] = {};
    float in_l_[kInSize] = {}, in_r_[kInSize] = {}, vm_l_[kVmSize] = {}, vm_r_[kVmSize] = {};
    uint64_t in_count_ = 0, vm_count_ = 0;
    double center_ = 0, sigma_ = 0;
    float rate_ = kMaxRate, target_rate_ = kMaxRate;
};

} // namespace seed::fx::fv1
