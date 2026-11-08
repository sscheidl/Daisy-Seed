// SPDX-License-Identifier: MIT
// Host tests for the FV-1 interpreter with hand-encoded programs (no vendor program data).
#include "fv1vm/fv1_vm.h"
#include "fv1vm/fv1_asm.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace seed::fx::fv1;

static int failures = 0;
#define CHECK(cond, msg)                                                                                 \
    do {                                                                                               \
        if(!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++failures; }          \
    } while(0)

// Encoders: fv1vm/fv1_asm.h (shared with the firmware demo programs).
using namespace seed::fx::fv1::assemble;

struct Rig {
    std::unique_ptr<Storage<double>> st = std::make_unique<Storage<double>>();
    Vm<double> vm;
    explicit Rig(std::vector<uint32_t> p) {
        p.resize(kProgramWords, Skp(0, 0));
        seed::audio::Config c; c.sample_rate = 32768; c.max_block = 64;
        vm.Prepare(c, *st);
        vm.LoadProgram(p.data());
    }
    void Run(const std::vector<double>& l, const std::vector<double>& r, std::vector<double>& ol, std::vector<double>& orr) {
        ol.resize(l.size()); orr.resize(l.size());
        for(std::size_t i = 0; i < l.size(); ++i) vm.Step(l[i], r[i], ol[i], orr[i]);
    }
};

static bool Near(double a, double b, double tol = 1e-12) { return std::fabs(a - b) <= tol; }

int main() {
    // Decoder round trip on representative words.
    {
        auto i = Decode<double>(Rda(4255, 1.0)); CHECK(i.op == Op::Rda && i.addr == 4255 && i.c == 1.0, "decode rda");
        auto j = Decode<double>(Wldr(0, -8192, 0)); CHECK(j.op == Op::Wldr && j.f == -8192 && j.a == 4096 && j.lfo == 2, "decode wldr");
        auto k = Decode<double>(Cho(0, 1, 6, 5874)); CHECK(k.op == Op::ChoRda && k.lfo == 1 && k.flags == 6 && k.addr == 5874, "decode cho");
        auto s = Decode<double>(Sof(-0.5, 0.25)); CHECK(s.op == Op::Sof && s.c == -0.5 && s.d == 0.25, "decode sof");
    }
    std::vector<double> x(400), z(400, 0.0), ol, orr;
    for(std::size_t i = 0; i < x.size(); ++i) x[i] = 0.3 * std::sin(0.05 * double(i)) + 0.1 * std::sin(0.31 * double(i));
    // 1 gain
    { Rig g({Rdax(ADCL, 0.5), Wrax(DACL, 0)}); g.Run(x, z, ol, orr);
      bool ok = true; for(std::size_t i = 0; i < x.size(); ++i) ok &= Near(ol[i], 0.5 * x[i]); CHECK(ok, "gain"); }
    // 2 delay of 100 samples (write at 0, read at 100)
    { Rig g({Rdax(ADCL, 1.0), Wra(0, 0), Rda(100, 1.0), Wrax(DACL, 0)}); g.Run(x, z, ol, orr);
      bool ok = true; for(std::size_t i = 0; i < x.size(); ++i) ok &= Near(ol[i], i >= 100 ? x[i - 100] : 0.0); CHECK(ok, "delay 100"); }
    // 3 saturation of ACC (A1)
    { std::vector<double> big(10, 0.8); Rig g({Rdax(ADCL, 1.99), Wrax(DACL, 0), Rdax(ADCL, -1.99), Wrax(DACR, 0)}); g.Run(big, big, ol, orr);
      CHECK(Near(ol[0], Vm<double>::kMax) && Near(orr[0], -1.0), "saturation"); }
    // 4 WRHX uses the ACC before the preceding RDFX as PACC (A2): high pass x - LP(x)
    { Rig g({Rdax(ADCL, 1.0), Rdfx(REG0, 0.5), Wrhx(REG0, -1.0), Wrax(DACL, 0)}); g.Run(x, z, ol, orr);
      double lp = 0; bool ok = true;
      for(std::size_t i = 0; i < x.size(); ++i) { lp = lp + 0.5 * (x[i] - lp); ok &= Near(ol[i], x[i] - lp); }
      CHECK(ok, "wrhx high pass"); }
    // 5 WRLX -1 gives the low pass
    { Rig g({Rdax(ADCL, 1.0), Rdfx(REG0, 0.25), Wrlx(REG0, -1.0), Wrax(DACL, 0)}); g.Run(x, z, ol, orr);
      double lp = 0; bool ok = true;
      for(std::size_t i = 0; i < x.size(); ++i) { lp = lp + 0.25 * (x[i] - lp); ok &= Near(ol[i], lp); }
      CHECK(ok, "wrlx low pass"); }
    // 6 SKP RUN skips only after the first sample
    { Rig g({Skp(RUN, 1), Sof(0, 0.5), Wrax(DACL, 0)}); std::vector<double> l(3, 0.0); g.Run(l, l, ol, orr);
      CHECK(Near(ol[0], 0.5) && Near(ol[1], 0.0), "skp run"); }
    // 6b SKP with an empty condition mask: unconditional jump; SKP 0,0 stays a no-op
    { Rig g({Sof(0, 0.5), Skp(0, 1), Sof(0, 0.25), Wrax(DACL, 0)}); std::vector<double> l(2, 0.0); g.Run(l, l, ol, orr);
      CHECK(Near(ol[0], 0.5) && Near(ol[1], 0.5), "SKP 0,1 jumps unconditionally"); }
    { Rig g({Sof(0, 0.5), Skp(0, 0), Sof(0, 0.25), Wrax(DACL, 0)}); std::vector<double> l(2, 0.0); g.Run(l, l, ol, orr);
      CHECK(Near(ol[0], 0.25), "SKP 0,0 is a no-op"); }
    // 7 SKP NEG/GEZ on the input sign
    { Rig g({Rdax(ADCL, 1.0), Skp(NEG, 1), Sof(0, 0.25), Wrax(DACL, 0)}); std::vector<double> l = {0.5, -0.5}; g.Run(l, l, ol, orr);
      CHECK(Near(ol[0], 0.25) && Near(ol[1], -0.5), "skp neg"); }
    // 8 LOG/EXP round trip and the zero endpoint (A6: log2(0) -> -16)
    { Rig g({Rdax(ADCL, 1.0), Log(1.0, 0), Exp(1.0, 0), Wrax(DACL, 0)}); std::vector<double> l = {0.25, 0.0}; g.Run(l, l, ol, orr);
      CHECK(Near(ol[0], 0.25, 1e-12), "log/exp round trip");
      CHECK(Near(ol[1], std::exp2(-16.0)), "log(0) endpoint"); }
    // 9 AND mask keeps the selected 24-bit fraction bits
    { Rig g({Rdax(ADCL, 1.0), And(0x7C0000), Wrax(DACL, 0)}); std::vector<double> l = {0.3}; g.Run(l, l, ol, orr);
      CHECK(Near(ol[0], std::floor(0.3 * 32) / 32), "and mask"); }
    // 10 WLDR every sample keeps the ramp phase (A5b); RDAL returns p/8192, p -= 2*RATE per sample
    { Rig g({Wldr(0, 16384, 0), Cho(3, 2, 2, 0), Wrax(DACL, 0)}); std::vector<double> l(5, 0.0); g.Run(l, l, ol, orr);
      // RATE = 16384/32768 = 0.5 -> p decreases by 1 per sample modulo 4096
      CHECK(Near(ol[0], 0.0) && Near(ol[1], 4095.0 / 8192) && Near(ol[2], 4094.0 / 8192), "wldr keeps phase"); }
    // 11 JAM resets the ramp
    { Rig g({Wldr(0, 16384, 0), Rdax(ADCL, 1.0), Skp(NEG, 1), Jam(0), Cho(3, 2, 2, 0), Wrax(DACL, 0)});
      std::vector<double> l = {-1, -1, 0.5, -1}; g.Run(l, l, ol, orr);
      CHECK(Near(ol[2], 0.0) && Near(ol[3], 4095.0 / 8192), "jam"); }
    // 12 RMPA reads the address in ADDR_PTR (fraction of 32768 words)
    { // ADDR_PTR = 1/1024 -> address 1/1024 * 32768 = 32 -> 32-sample delay
      Rig g({Rdax(ADCL, 1.0), Wra(0, 0), Sof(0, 1.0 / 1024), Wrax(ADDR_PTR, 0), Rmpa(1.0), Wrax(DACL, 0)});
      g.Run(x, z, ol, orr); bool ok = true;
      for(std::size_t i = 0; i < x.size(); ++i) ok &= Near(ol[i], i >= 32 ? x[i - 32] : 0.0);
      CHECK(ok, "rmpa delay 32"); }
    // 13 CHO RDA with a static SIN LFO (rate 0) reads base + floor(0) with coefficient 1 via COMPC
    { Rig g({Skp(RUN, 1), Wlds(0, 0, 100), Rdax(ADCL, 1.0), Wra(0, 0), Cho(0, 0, 2 | 4, 50), Cho(0, 0, 0, 51), Wrax(DACL, 0)}); g.Run(x, z, ol, orr);
      bool ok = true; for(std::size_t i = 0; i < x.size(); ++i) ok &= Near(ol[i], i >= 50 ? x[i - 50] : 0.0); CHECK(ok, "cho static interpolation"); }
    // 15 WRAP allpass: v = x + 0.5*d, y = d - 0.5*v; impulse 0.5 (below ACC saturation, A1)
    //    -> y[0] = -0.25, y[100] = 0.5 - 0.125 = 0.375, y[200] = 0.1875
    { Rig g({Rdax(ADCL, 1.0), Rda(100, 0.5), Wrap(0, -0.5), Wrax(DACL, 0)});
      std::vector<double> imp(201, 0.0); imp[0] = 0.5; g.Run(imp, imp, ol, orr);
      const bool ok = Near(ol[0], -0.25) && Near(ol[1], 0.0) && Near(ol[100], 0.375) && Near(ol[200], 0.1875);
      if(!ok) std::printf("wrap allpass values: %.17g %.17g %.17g %.17g\n", ol[0], ol[1], ol[100], ol[200]);
      CHECK(ok, "wrap allpass"); }
    // 16 MULX multiplies by a register (here the right input)
    { Rig g({Rdax(ADCL, 1.0), Mulx(ADCR), Wrax(DACL, 0)}); std::vector<double> l = {0.5, -0.4}, r = {0.5, 0.25}; g.Run(l, r, ol, orr);
      CHECK(Near(ol[0], 0.25) && Near(ol[1], -0.1), "mulx"); }
    // 14 Process rejects an invalid block and works on a valid one (contract)
    { Storage<float>* stf = new Storage<float>(); Vm<float> v; seed::audio::Config c; c.max_block = 8; v.Prepare(c, *stf);
      std::vector<uint32_t> p = {Rdax(ADCL, 0.5), Wrax(DACL, 0), Rdax(ADCR, 0.25), Wrax(DACR, 0)}; p.resize(128, Skp(0, 0)); v.LoadProgram(p.data());
      float l[8] = {1, 1, 1, 1, 1, 1, 1, 1}, r[8] = {1, 1, 1, 1, 1, 1, 1, 1}, a[8], b[8];
      CHECK(!v.Process({l, r, a, b, 9}), "reject oversize block");
      CHECK(v.Process({l, r, a, b, 8}) && a[7] == 0.5f && b[7] == 0.25f, "process block"); delete stf; }
    { // POT low-pass: 2^-12 per sample, a new program starts settled, and the shelving pot filter does not overshoot.
      uint32_t w[kProgramWords]; for(auto& x : w) x = Nop();
      unsigned n = 0;
      w[n++] = Rdax(POT0, 1.0); w[n++] = Wrax(DACR, 1.0);
      w[n++] = Rdfx(REG0, 1.0 / 1024); w[n++] = Wrhx(REG0, -0.75);
      w[n++] = Rdax(REG0 + 1, 0.75); w[n++] = Wrax(REG0 + 1, 1.0); w[n++] = Wrax(DACL, 0.0);
      auto stp = std::make_unique<Storage<double>>(); Vm<double> v;
      seed::audio::Config c; c.sample_rate = 32768; c.max_block = 1;
      v.Prepare(c, *stp); v.LoadProgram(w);
      Parameters p; p.pot[0] = .3f; v.SetParameters(p); v.Reset();
      const float z = 0.f; float l = 0.f, r = 0.f;
      for(int i = 0; i < 40000; ++i) v.Process({&z, &z, &l, &r, 1}); // the shelf settles on the constant pot
      CHECK(std::fabs(r - .3f) < 1e-6f, "pot starts settled");
      p.pot[0] = .7f; v.SetParameters(p);
      float peak = 0.f, at_tau = 0.f;
      for(int i = 0; i < 60000; ++i) { v.Process({&z, &z, &l, &r, 1}); peak = std::fmax(peak, l); if(i == 4095) at_tau = r; }
      CHECK(std::fabs(at_tau - (.7f - .4f * .3679f)) < 1e-3f, "pot time constant 4096 samples");
      CHECK(peak < .701f && l > .69f, "no overshoot through the shelving pot filter"); }
    if(failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("fv1-vm host tests: all passed\n");
    return 0;
}
