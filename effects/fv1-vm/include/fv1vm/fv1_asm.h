#pragma once
// SPDX-License-Identifier: MIT
// Minimal FV-1 machine-word encoders for own test and demo programs (no vendor program data).
// Field layout as decoded by fv1_vm.h. Coefficients are rounded half away from zero.
#include <cstdint>

namespace seed::fx::fv1::assemble {

inline int Round(double x) { return x >= 0 ? int(x + 0.5) : -int(-x + 0.5); }
inline uint32_t Field(int v, unsigned bits) { return uint32_t(v) & ((1u << bits) - 1u); }

inline uint32_t MemOp(unsigned op, unsigned addr, double c) {
    return (Field(Round(c * 512), 11) << 21) | ((addr & 0x7FFFu) << 5) | op;
}
inline uint32_t Rda(unsigned addr, double c) { return MemOp(0, addr, c); }
inline uint32_t Wra(unsigned addr, double c) { return MemOp(2, addr, c); }
inline uint32_t Wrap(unsigned addr, double c) { return MemOp(3, addr, c); }
inline uint32_t Rmpa(double c) { return (Field(Round(c * 512), 11) << 21) | 1u; }
inline uint32_t RegOp(unsigned op, unsigned reg, double c) {
    return (Field(Round(c * 16384), 16) << 16) | ((reg & 0x3Fu) << 5) | op;
}
inline uint32_t Rdax(unsigned reg, double c) { return RegOp(4, reg, c); }
inline uint32_t Rdfx(unsigned reg, double c) { return RegOp(5, reg, c); }
inline uint32_t Wrax(unsigned reg, double c) { return RegOp(6, reg, c); }
inline uint32_t Wrhx(unsigned reg, double c) { return RegOp(7, reg, c); }
inline uint32_t Wrlx(unsigned reg, double c) { return RegOp(8, reg, c); }
inline uint32_t Mulx(unsigned reg) { return ((reg & 0x3Fu) << 5) | 10u; }
inline uint32_t Sof(double c, double d) {
    return (Field(Round(c * 16384), 16) << 16) | (Field(Round(d * 1024), 11) << 5) | 13u;
}
inline uint32_t Log(double c, double d) { return (Sof(c, d) & ~0x1Fu) | 11u; }
inline uint32_t Exp(double c, double d) { return (Sof(c, d) & ~0x1Fu) | 12u; }
inline uint32_t And(uint32_t m) { return ((m & 0xFFFFFFu) << 8) | 14u; }
inline uint32_t Skp(unsigned cond, unsigned n) { return ((cond & 0x1Fu) << 27) | ((n & 0x3Fu) << 21) | 17u; }
inline uint32_t Nop() { return Skp(0, 0); }
inline uint32_t Wlds(unsigned lfo, unsigned f, unsigned a) {
    return ((lfo & 1u) << 29) | ((f & 0x1FFu) << 20) | ((a & 0x7FFFu) << 5) | 18u;
}
inline uint32_t Wldr(unsigned lfo, int f, unsigned code) {
    return (1u << 30) | ((lfo & 1u) << 29) | (Field(f, 16) << 13) | ((code & 3u) << 5) | 18u;
}
inline uint32_t Jam(unsigned lfo) { return (1u << 7) | ((lfo & 1u) << 6) | 19u; }
inline uint32_t Cho(unsigned type, unsigned lfo, unsigned flags, unsigned addr) {
    return (type << 30) | ((flags & 0x3Fu) << 24) | ((lfo & 3u) << 21) | ((addr & 0xFFFFu) << 5) | 20u;
}

// Register numbers and SKP conditions.
enum Reg : unsigned {
    SIN0_RATE = 0, SIN0_RANGE = 1, SIN1_RATE = 2, SIN1_RANGE = 3, RMP0_RATE = 4, RMP0_RANGE = 5,
    RMP1_RATE = 6, RMP1_RANGE = 7, POT0 = 16, POT1 = 17, POT2 = 18, ADCL = 20, ADCR = 21, DACL = 22,
    DACR = 23, ADDR_PTR = 24, REG0 = 32
};
enum Cond : unsigned { RUN = 0x10, ZRC = 0x08, ZRO = 0x04, GEZ = 0x02, NEG = 0x01 };

} // namespace seed::fx::fv1::assemble
