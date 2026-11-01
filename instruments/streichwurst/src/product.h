#pragma once
// SPDX-License-Identifier: MIT
// Streichwurst product audio graph (MODULAR_ARCHITECTURE.md), host-tested without board:
//
//   instrument strings ─> FX string stage (Animate forming, Ensemble sum, Phaser,
//                         stereo tap correction) ─┐
//   instrument soli (mono, bypass the string FX) ─┴─> Balance (SF-F07 shape)
//        ─> global slot (component reverb, or any FX-contract 1.0 wet stage)
//        ─> ensemble gain word (SF-F06) x master ─> panic ramp ─> output protection
//
// The product owns the mix: the dry signal is added once (the slots are wet-only), the
// original's relative ensemble gain acts after solo and reverb, the own headroom constant
// and limiter stay separate from it. Matrix offsets and the Animate offset reach the DSP
// as typed values; the register state itself is never modulated.
#include "../../../common/audio/fx_contract.h"
#include "register.h"
#include "streichwurst/instrument.h"
#include "streichwurst_fx/fx.h"
#include <cmath>
#include <cstddef>

namespace streichwurst::product {

constexpr std::size_t kMaxBlock = 48;

// Default global slot: the FX component's own reverb with its wet parameter.
class ComponentReverbSlot {
  public:
    bool Prepare(const seed::audio::Config&, streichwurst_fx::StreichwurstFx& fx) {
        fx_ = &fx;
        return true;
    }
    bool Process(const float* l, const float* r, float* wl, float* wr, std::size_t n) {
        return fx_->ProcessReverb(l, r, wl, wr, n);
    }
    float Wet() const { return fx_->ReverbWet(); }
    void Clear() {}

  private:
    streichwurst_fx::StreichwurstFx* fx_ = nullptr;
};

// Any wet-only stage of the shared FX contract 1.0 (e.g. the stereo delay) in the global
// slot. The host keeps the dry/wet amount; the FX component's reverb then idles. Storage
// must be attached before Product::Prepare().
template <class Stage>
class ContractSlot {
  public:
    using Storage = typename Stage::Storage;
    Stage stage;
    Storage* storage = nullptr;
    float wet = 0.25f;
    bool Prepare(const seed::audio::Config& config, streichwurst_fx::StreichwurstFx&) {
        return storage && stage.Prepare(config, *storage);
    }
    bool Process(const float* l, const float* r, float* wl, float* wr, std::size_t n) {
        return stage.Process(seed::audio::StereoBlock{l, r, wl, wr, n});
    }
    float Wet() const { return wet; }
    void Clear() {}
};

struct ProductStats {
    uint32_t non_finite = 0; // output samples replaced by zero
    uint32_t panics = 0;     // completed All Sound Off ramps
};

template <class Slot = ComponentReverbSlot>
class Product {
  public:
    // Own gain plan (docs/FIDELITY.md): fixed mix headroom before the gain word; the
    // limiter is the last stage.
    static constexpr float kMixHeadroom = 0.35f;
    static constexpr float kPanicSeconds = 0.002f;
    Slot slot;

    bool Prepare(float sample_rate, std::size_t max_block, InstrumentStorage& inst_storage,
                 streichwurst_fx::FxStorage& fx_storage) {
        if(max_block == 0 || max_block > kMaxBlock || !inst_.Prepare(sample_rate, max_block, inst_storage) ||
           !fx_.Prepare(streichwurst_fx::FxConfig{sample_rate, max_block}, fx_storage) ||
           !slot.Prepare(seed::audio::Config{unsigned(sample_rate), max_block}, fx_))
            return false;
        fs_ = sample_rate;
        max_block_ = max_block;
        balance_alpha_ = 1.f - std::exp(-1.f / (fs_ * 0.0253995f)); // SF-F07 approximation
        master_alpha_ = 1.f - std::exp(-1.f / (fs_ * 0.051f));     // SF-F07: 1/128 smoothing ~51 ms
        panic_step_ = 1.f / (kPanicSeconds * fs_);
        prepared_ = true;
        Reset();
        return true;
    }
    void Reset() {
        inst_.Reset();
        fx_.Reset();
        slot.Clear();
        Apply(true);
        mute_ = 1.f;
        panic_ = PanicState::Idle;
    }

    // Register state and matrix offsets (nullptr: none) for the next block.
    void Update(const State& s, const float* offsets) {
        state_ = s;
        for(int t = 0; t < kMatrixTargets; ++t)
            mod_[t] = offsets && std::isfinite(offsets[t]) ? offsets[t] : 0.f;
        Apply(false);
    }
    // All Sound Off with a 2 ms ramp: down, clear instrument, line and reverb, up again.
    void Panic() {
        if(panic_ == PanicState::Idle)
            panic_ = PanicState::Down;
    }

    bool Render(const Event* events, std::size_t count, float* out_l, float* out_r, std::size_t n) {
        if(!prepared_ || n > max_block_ || (n && (!out_l || !out_r || out_l == out_r)))
            return false;
        inst_.SetAnimateOffset(fx_.AnimateRegistrationOffset()); // control cadence: one block
        if(!inst_.RenderSections(events, count, SectionBuses{strings_, solo_, n}))
            return false;
        if(n == 0)
            return true;
        fx_.ProcessStrings(strings_, sl_, sr_, gain_, n);
        for(std::size_t i = 0; i < n; ++i) {
            balance_ += (balance_target_ - balance_) * balance_alpha_;
            const float gs = balance_ < 1.f ? 1.f : 2.f - balance_;
            const float go = balance_ < 1.f ? balance_ : 1.f;
            ml_[i] = (sl_[i] * gs + solo_[i] * go) * kMixHeadroom;
            mr_[i] = (sr_[i] * gs + solo_[i] * go) * kMixHeadroom;
        }
        slot.Process(ml_, mr_, wl_, wr_, n);
        const float w = slot.Wet();
        bool ramp_done = false;
        for(std::size_t i = 0; i < n; ++i) {
            master_ += (master_target_ - master_) * master_alpha_;
            float l = ml_[i] + (wl_[i] - ml_[i]) * w;
            float r = mr_[i] + (wr_[i] - mr_[i]) * w;
            const float g = gain_[i] * master_ * Mute(ramp_done);
            out_l[i] = Protect(l * g);
            out_r[i] = Protect(r * g);
        }
        if(ramp_done) {
            Event off = streichwurst::AllSoundOff();
            inst_.ApplyEvent(off);
            fx_.ClearTails();
            slot.Clear();
            ++stats_.panics;
        }
        return true;
    }

    // Own output protection (draft 0.2): identity up to 0.8, tanh knee above (|y| < 1),
    // value and slope continuous; non-finite samples become 0 and are counted.
    float Protect(float x) {
        if(!std::isfinite(x)) {
            ++stats_.non_finite;
            return 0.f;
        }
        const float a = std::fabs(x);
        if(a <= 0.8f)
            return x;
        const float y = 0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f);
        const float bounded = y < 0.99997f ? y : 0.99997f; // |y| < 1 also where float tanh is 1
        return x < 0.f ? -bounded : bounded;
    }

    const State& GetState() const { return state_; }
    const Instrument& GetInstrument() const { return inst_; }
    const streichwurst_fx::StreichwurstFx& GetFx() const { return fx_; }
    const ProductStats& Stats() const { return stats_; }
    float BalanceValue() const { return balance_; }
    bool Muting() const { return panic_ != PanicState::Idle; }

    // Effective values after matrix offsets (also used for the MD monitor base values).
    static float BalanceCurve(float u) {
        // SF-F07: b = r/64 up to r = 64, then linear to exactly 2 at r = 127 (r = 127 u).
        const float r = u * 127.f;
        return r <= 64.f ? r / 64.f : 1.f + (r - 64.f) / 63.f;
    }

  private:
    enum class PanicState { Idle, Down, Up };
    float Mute(bool& done) {
        switch(panic_) {
            case PanicState::Down:
                mute_ -= panic_step_;
                if(mute_ <= 0.f) {
                    mute_ = 0.f;
                    panic_ = PanicState::Up;
                    done = true;
                }
                break;
            case PanicState::Up:
                if(done)
                    break; // stay silent until the clear at the end of this block
                mute_ += panic_step_;
                if(mute_ >= 1.f) {
                    mute_ = 1.f;
                    panic_ = PanicState::Idle;
                }
                break;
            case PanicState::Idle: break;
        }
        return mute_;
    }
    static float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
    void Apply(bool snap) {
        InstrumentParams ip = state_.inst;
        ip.string_registration = Clamp01(ip.string_registration + mod_[TargetRegistration]);
        ip.solo_tone = Clamp01(ip.solo_tone + mod_[TargetSoloTone]);
        ip.solo_tremolo_depth = Clamp01(ip.solo_tremolo_depth + mod_[TargetTremolo]);
        inst_.SetParameters(ip);
        inst_.SetPitchModulation(mod_[TargetPitch] * kPitchModSemitones);
        streichwurst_fx::FxParams fp = state_.fx;
        fp.animate_amount = Clamp01(fp.animate_amount + mod_[TargetAnimate]);
        fp.phaser_amount = Clamp01(fp.phaser_amount + mod_[TargetPhaser]);
        fp.reverb_wet = Clamp01(fp.reverb_wet + mod_[TargetReverb]);
        fx_.SetParameters(fp);
        balance_target_ = BalanceCurve(Clamp01(state_.prod.balance + mod_[TargetBalance]));
        const float m = Clamp01(state_.prod.master);
        master_target_ = m * m; // own taper
        if(snap) {
            balance_ = balance_target_;
            master_ = master_target_;
        }
    }

    bool prepared_ = false;
    float fs_ = 48000.f;
    std::size_t max_block_ = kMaxBlock;
    State state_;
    float mod_[kMatrixTargets]{};
    Instrument inst_;
    streichwurst_fx::StreichwurstFx fx_;
    float strings_[kMaxBlock]{}, solo_[kMaxBlock]{}, sl_[kMaxBlock]{}, sr_[kMaxBlock]{}, gain_[kMaxBlock]{};
    float ml_[kMaxBlock]{}, mr_[kMaxBlock]{}, wl_[kMaxBlock]{}, wr_[kMaxBlock]{};
    float balance_ = 1.f, balance_target_ = 1.f, balance_alpha_ = 0.f;
    float master_ = 0.64f, master_target_ = 0.64f, master_alpha_ = 0.f;
    float mute_ = 1.f, panic_step_ = 0.f;
    PanicState panic_ = PanicState::Idle;
    ProductStats stats_;
};

} // namespace streichwurst::product
