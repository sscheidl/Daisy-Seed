#pragma once
// SPDX-License-Identifier: MIT
// FV-1 instruction-set interpreter as an FX component (research / feasibility study).
//
// Executes one 128-word FV-1 program per sample. The semantics follow the L2A case model
// (fx-aid-catalog, scripts/fv1_model.py, assumptions A1-A7 and A5b) so that host results can be
// compared sample by sample with that model. This is NOT a certified chip emulation: delay-RAM
// compression, MULX/coefficient truncation and silicon LFO details are not modelled.
//
// Contract: seed::audio FX interface 1.0 - Prepare(Config, Storage&) at startup, SetParameters,
// Process(StereoBlock) wet-only, bounded, allocation/lock/I/O-free. Program words are supplied by the
// host; this component contains no program data.
#include "../../../../common/audio/fx_contract.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

#define SEED_FV1VM_VERSION "0.1.0"

namespace seed::fx::fv1 {

inline constexpr unsigned kProgramWords = 128;
inline constexpr unsigned kMemWords = 32768; // FV-1 delay RAM
inline constexpr unsigned kMemMask = kMemWords - 1;

enum class Op : uint8_t {
    Rda, Rmpa, Wra, Wrap, Rdax, Rdfx, Wrax, Wrhx, Wrlx, Maxx, Mulx, Log, Exp, Sof, And, Or, Xor, Skp,
    Wlds, Wldr, Jam, ChoRda, ChoSof, ChoRdal, Nop
};

template <typename T> struct Instr {
    Op op = Op::Nop;
    uint8_t reg = 0, lfo = 0, flags = 0, cond = 0, n = 0;
    uint32_t addr = 0, mask = 0;
    int32_t f = 0, a = 0;
    T c = 0, d = 0;
};

inline int32_t SignExtend(uint32_t v, unsigned bits) {
    v &= (1u << bits) - 1u;
    return (v >> (bits - 1)) ? int32_t(v) - int32_t(1u << bits) : int32_t(v);
}

// Field layout as in the L2A second decoder (fv1lib.decode), checked there against disfv1 on 30 720 words.
template <typename T> Instr<T> Decode(uint32_t w) {
    Instr<T> i;
    const unsigned op = w & 0x1Fu;
    switch(op) {
        case 0: case 2: case 3:
            i.op = op == 0 ? Op::Rda : op == 2 ? Op::Wra : Op::Wrap;
            i.addr = (w >> 5) & 0x7FFFu;
            i.c = T(SignExtend(w >> 21, 11)) / T(512);
            break;
        case 1: i.op = Op::Rmpa; i.c = T(SignExtend(w >> 21, 11)) / T(512); break;
        case 4: case 5: case 6: case 7: case 8: case 9: {
            constexpr Op ops[] = {Op::Rdax, Op::Rdfx, Op::Wrax, Op::Wrhx, Op::Wrlx, Op::Maxx};
            i.op = ops[op - 4];
            i.reg = uint8_t((w >> 5) & 0x3Fu);
            i.c = T(SignExtend(w >> 16, 16)) / T(16384);
            break;
        }
        case 10: i.op = Op::Mulx; i.reg = uint8_t((w >> 5) & 0x3Fu); break;
        case 11: case 12: case 13:
            i.op = op == 11 ? Op::Log : op == 12 ? Op::Exp : Op::Sof;
            i.c = T(SignExtend(w >> 16, 16)) / T(16384);
            i.d = T(SignExtend(w >> 5, 11)) / T(1024);
            break;
        case 14: case 15: case 16:
            i.op = op == 14 ? Op::And : op == 15 ? Op::Or : Op::Xor;
            i.mask = (w >> 8) & 0xFFFFFFu;
            break;
        case 17:
            i.op = Op::Skp;
            i.cond = uint8_t((w >> 27) & 0x1Fu);
            i.n = uint8_t((w >> 21) & 0x3Fu);
            break;
        case 18:
            if(((w >> 30) & 1u) == 0) {
                i.op = Op::Wlds; i.lfo = uint8_t((w >> 29) & 1u);
                i.f = int32_t((w >> 20) & 0x1FFu); i.a = int32_t((w >> 5) & 0x7FFFu);
            } else {
                constexpr int32_t amps[4] = {4096, 2048, 1024, 512};
                i.op = Op::Wldr; i.lfo = uint8_t(2 + ((w >> 29) & 1u));
                i.f = SignExtend(w >> 13, 16); i.a = amps[(w >> 5) & 3u];
            }
            break;
        case 19: i.op = Op::Jam; i.lfo = uint8_t(2 + ((w >> 6) & 1u)); break;
        case 20: {
            const unsigned t = (w >> 30) & 3u;
            i.flags = uint8_t((w >> 24) & 0x3Fu);
            i.lfo = uint8_t((w >> 21) & 3u);
            if(t == 2) { i.op = Op::ChoSof; i.d = T(SignExtend(w >> 5, 16)) / T(32768); }
            else { i.op = t == 0 ? Op::ChoRda : t == 3 ? Op::ChoRdal : Op::Nop; i.addr = (w >> 5) & 0xFFFFu; }
            break;
        }
        default: i.op = Op::Nop; break;
    }
    return i;
}

template <typename T> struct Storage {
    T mem[kMemWords];
};

struct Parameters {
    float pot[3] = {0.5f, 0.5f, 0.5f}; // POT0..POT2, 0..1
};

template <typename T> class Vm {
  public:
    static constexpr T kMax = T(1) - T(1) / T(8388608); // 1 - 2^-23 (A1)

    bool Prepare(const seed::audio::Config& config, Storage<T>& storage) {
        config_ = config;
        mem_ = storage.mem;
        Reset();
        return config.max_block > 0;
    }
    void Reset() {
        for(auto& r : r_) r = 0;
        if(mem_) for(unsigned i = 0; i < kMemWords; ++i) mem_[i] = 0;
        acc_ = pacc_ = lr_ = 0;
        head_ = 0; run_ = false;
        ss_[0] = ss_[1] = 0; cc_[0] = cc_[1] = -1; rp_[0] = rp_[1] = 0;
    }
    // Startup / program change only (not real-time safe w.r.t. state continuity: call Reset as needed).
    void LoadProgram(const uint32_t* words) {
        for(unsigned i = 0; i < kProgramWords; ++i) prog_[i] = Decode<T>(words[i]);
    }
    void SetParameters(const Parameters& p) {
        for(unsigned i = 0; i < 3; ++i) pot_[i] = T(p.pot[i] < 0.f ? 0.f : p.pot[i] > 1.f ? 1.f : p.pot[i]);
    }
    // Host cross-check helper: exact pot values written straight into POT0..2 (Step does not set them).
    void SetPotsExact(T p0, T p1, T p2) {
        pot_[0] = p0; pot_[1] = p1; pot_[2] = p2;
        r_[16] = p0; r_[17] = p1; r_[18] = p2;
    }
    bool Process(const seed::audio::StereoBlock& b) {
        if(!seed::audio::Valid(config_, b) || !mem_) return false;
        r_[16] = pot_[0]; r_[17] = pot_[1]; r_[18] = pot_[2];
        for(std::size_t t = 0; t < b.frames; ++t) {
            const T inl = T(b.left[t]), inr = T(b.right[t]);
            T outl, outr;
            Step(inl, inr, outl, outr);
            b.out_left[t] = float(outl);
            b.out_right[t] = float(outr);
        }
        return true;
    }
    // One sample; public for host tests.
    void Step(T inl, T inr, T& outl, T& outr) {
        T* const r = r_;
        r[20] = inl; r[21] = inr;
        const T ls[2] = {ss_[0] * r[1], ss_[1] * r[3]};
        const T lc[2] = {cc_[0] * r[1], cc_[1] * r[3]};
        T ramp_amp[2];
        for(unsigned k = 0; k < 2; ++k) ramp_amp[k] = T(4096) / T(1u << (unsigned(int(r[5 + 2 * k] * T(4))) & 3u));
        T acc = acc_, pacc = pacc_, lr = lr_;
        unsigned pc = 0;
        while(pc < kProgramWords) {
            const Instr<T>& in = prog_[pc];
            unsigned next = pc + 1;
            switch(in.op) {
                case Op::Rda: lr = mem_[(in.addr + head_) & kMemMask]; pacc = acc; acc = Sat(acc + lr * in.c); break;
                case Op::Rmpa: {
                    const uint32_t a = uint32_t(int64_t(r[24] * T(8388608)) >> 8);
                    lr = mem_[(a + head_) & kMemMask]; pacc = acc; acc = Sat(acc + lr * in.c); break;
                }
                case Op::Wra: mem_[(in.addr + head_) & kMemMask] = acc; pacc = acc; acc = Sat(acc * in.c); break;
                case Op::Wrap: mem_[(in.addr + head_) & kMemMask] = acc; pacc = acc; acc = Sat(acc * in.c + lr); break;
                case Op::Rdax: pacc = acc; acc = Sat(acc + r[in.reg] * in.c); break;
                case Op::Rdfx: pacc = acc; acc = Sat((acc - r[in.reg]) * in.c + r[in.reg]); break;
                case Op::Wrax: r[in.reg] = acc; pacc = acc; acc = Sat(acc * in.c); break;
                case Op::Wrhx: { r[in.reg] = acc; const T p = pacc; pacc = acc; acc = Sat(acc * in.c + p); break; }
                case Op::Wrlx: { r[in.reg] = acc; const T p = pacc; pacc = acc; acc = Sat((p - acc) * in.c + p); break; }
                case Op::Maxx: {
                    pacc = acc;
                    const T x = std::fabs(acc), y = std::fabs(r[in.reg] * in.c);
                    acc = Sat(x > y ? x : y); break;
                }
                case Op::Mulx: pacc = acc; acc = Sat(acc * r[in.reg]); break;
                case Op::Log: {
                    pacc = acc;
                    const T a = std::fabs(acc);
                    const T l = a > 0 ? T(std::log2(a)) / T(16) : T(-1);
                    acc = Sat(in.c * l + in.d); break;
                }
                case Op::Exp: {
                    pacc = acc;
                    const T e = acc >= 0 ? T(1) : T(std::exp2(T(16) * acc));
                    acc = Sat(in.c * e + in.d); break;
                }
                case Op::Sof: pacc = acc; acc = Sat(acc * in.c + in.d); break;
                case Op::And: case Op::Or: case Op::Xor: {
                    pacc = acc;
                    int32_t v = int32_t(int64_t(std::floor(acc * T(8388608)))) & 0xFFFFFF;
                    v = in.op == Op::And ? (v & int32_t(in.mask)) : in.op == Op::Or ? (v | int32_t(in.mask))
                                                                                   : (v ^ int32_t(in.mask));
                    if(v & 0x800000) v -= 0x1000000;
                    acc = T(v) / T(8388608); break;
                }
                case Op::Skp: {
                    if(in.cond && in.n) {
                        bool take = false; // only single flags occur in the catalogue (A3); several flags OR-ed here
                        if(in.cond & 0x10) take |= run_;
                        if(in.cond & 0x08) take |= ((acc < 0) != (pacc < 0));
                        if(in.cond & 0x04) take |= acc == 0;
                        if(in.cond & 0x02) take |= acc >= 0;
                        if(in.cond & 0x01) take |= acc < 0;
                        if(take) next = pc + 1 + in.n;
                    }
                    break;
                }
                case Op::Wlds: {
                    const unsigned k = in.lfo;
                    r[2 * k] = T(in.f) / T(512); r[2 * k + 1] = T(in.a) / T(32768);
                    ss_[k] = 0; cc_[k] = -1; break;
                }
                case Op::Wldr: { // A5b: rate/range only, phase kept
                    const unsigned k = in.lfo - 2u;
                    const unsigned code = in.a == 4096 ? 0 : in.a == 2048 ? 1 : in.a == 1024 ? 2 : 3;
                    r[4 + 2 * k] = T(in.f) / T(32768); r[5 + 2 * k] = T(code) * T(0.25); break;
                }
                case Op::Jam: rp_[in.lfo - 2u] = 0; break;
                case Op::ChoRda: case Op::ChoSof: case Op::ChoRdal: {
                    const unsigned k = in.lfo; const unsigned fl = in.flags;
                    T cf = 0; int off = 0;
                    if(k < 2) {
                        T val = (fl & 1u) ? lc[k] : ls[k];
                        if(in.op == Op::ChoRdal) { pacc = acc; acc = Sat(val); break; }
                        if(fl & 8u) val = -val;
                        const T o = val * T(8192), fi = std::floor(o);
                        cf = o - fi; off = int(fi);
                    } else {
                        const unsigned rk = k - 2u;
                        const T amp = ramp_amp[rk];
                        T p = rp_[rk];
                        if(in.op == Op::ChoRdal) { pacc = acc; acc = Sat(p / T(8192)); break; }
                        if(fl & 0x10u) p = PosMod(p + amp / T(2), amp);
                        if(fl & 8u) p = amp - p;
                        if(fl & 0x20u) {
                            const T u = p / amp, m = u < T(1) - u ? u : T(1) - u;
                            T w = T(4) * m - T(0.5);
                            cf = w < 0 ? T(0) : w > 1 ? T(1) : w; off = 0;
                        } else {
                            const T fi = std::floor(p); cf = p - fi; off = int(fi);
                        }
                    }
                    if(fl & 4u) cf = T(1) - cf;
                    if(in.op == Op::ChoRda) {
                        lr = mem_[(uint32_t(int32_t(in.addr) + off) + head_) & kMemMask];
                        pacc = acc; acc = Sat(acc + lr * cf);
                    } else {
                        pacc = acc; acc = Sat(acc * cf + in.d);
                    }
                    break;
                }
                case Op::Nop: break;
            }
            pc = next;
        }
        outl = r[22]; outr = r[23];
        acc_ = acc; pacc_ = pacc; lr_ = lr;
        run_ = true;
        head_ = (head_ - 1u) & kMemMask;
        for(unsigned k = 0; k < 2; ++k) {
            const T c = r[2 * k] / T(256);
            cc_[k] += ss_[k] * c;
            ss_[k] -= cc_[k] * c;
            rp_[k] = PosMod(rp_[k] - T(2) * r[4 + 2 * k], ramp_amp[k]);
        }
    }
    const T* Registers() const { return r_; }

  private:
    static T Sat(T x) { return x > kMax ? kMax : x < T(-1) ? T(-1) : x; }
    static T PosMod(T x, T m) { T y = std::fmod(x, m); return y < 0 ? y + m : y; }

    seed::audio::Config config_{};
    T* mem_ = nullptr;
    Instr<T> prog_[kProgramWords]{};
    T r_[64]{};
    T pot_[3] = {T(0.5), T(0.5), T(0.5)};
    T acc_ = 0, pacc_ = 0, lr_ = 0;
    uint32_t head_ = 0;
    bool run_ = false;
    T ss_[2]{}, cc_[2]{-1, -1}, rp_[2]{};
};

} // namespace seed::fx::fv1
