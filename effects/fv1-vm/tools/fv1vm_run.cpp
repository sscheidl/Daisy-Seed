// SPDX-License-Identifier: MIT
// Host runner for cross-checks against an external reference model (e.g. the L2A case model).
// Usage: fv1vm_run <program.bin> <samples> <pot0> <pot1> <pot2> <amp> <seed> <out.bin> <double|float>
// The program file (512 bytes, big-endian words) stays outside this repository. Input: two independent
// LCG noise streams (x = (1103515245*x + 12345) mod 2^31; value = (x/2^31 - 0.5)*2*amp), L seed, R seed+1.
// Output: interleaved little-endian float64 L,R per sample (also for the float VM, after conversion).
#include "fv1vm/fv1_vm.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace seed::fx::fv1;

static double Lcg(uint64_t& x, double amp) {
    x = (1103515245ull * x + 12345ull) % 2147483648ull;
    return (double(x) / 2147483648.0 - 0.5) * 2.0 * amp;
}

template <typename T> static int Run(const uint32_t* words, long n, const double pots[3], double amp, uint64_t seed, FILE* out) {
    auto st = std::make_unique<Storage<T>>();
    Vm<T> vm; seed::audio::Config c; c.sample_rate = 32768; c.max_block = 1;
    vm.Prepare(c, *st); vm.LoadProgram(words);
    Parameters p; for(int i = 0; i < 3; ++i) p.pot[i] = float(pots[i]);
    vm.SetParameters(p);
    uint64_t sl = seed, sr = seed + 1;
    std::vector<double> buf; buf.reserve(2 * 4096);
    // Use Step with the exact double pot values (SetParameters stores floats; Step reads registers 16..18).
    for(long t = 0; t < n; ++t) {
        T ol, orr;
        vm.SetPotsExact(T(pots[0]), T(pots[1]), T(pots[2]));
        vm.Step(T(Lcg(sl, amp)), T(Lcg(sr, amp)), ol, orr);
        buf.push_back(double(ol)); buf.push_back(double(orr));
        if(buf.size() >= 2 * 4096) { std::fwrite(buf.data(), sizeof(double), buf.size(), out); buf.clear(); }
    }
    std::fwrite(buf.data(), sizeof(double), buf.size(), out);
    return 0;
}

int main(int argc, char** argv) {
    if(argc != 10) { std::fprintf(stderr, "usage: fv1vm_run prog.bin samples pot0 pot1 pot2 amp seed out.bin double|float\n"); return 2; }
    FILE* f = std::fopen(argv[1], "rb");
    if(!f) { std::perror("program"); return 2; }
    unsigned char b[512];
    if(std::fread(b, 1, 512, f) != 512) { std::fprintf(stderr, "program must be 512 bytes\n"); return 2; }
    std::fclose(f);
    uint32_t words[128];
    for(int i = 0; i < 128; ++i) words[i] = (uint32_t(b[4 * i]) << 24) | (uint32_t(b[4 * i + 1]) << 16) | (uint32_t(b[4 * i + 2]) << 8) | b[4 * i + 3];
    const long n = std::atol(argv[2]);
    const double pots[3] = {std::atof(argv[3]), std::atof(argv[4]), std::atof(argv[5])};
    const double amp = std::atof(argv[6]);
    const uint64_t seed = std::strtoull(argv[7], nullptr, 10);
    FILE* out = std::fopen(argv[8], "wb");
    if(!out) { std::perror("out"); return 2; }
    const std::string prec = argv[9];
    const int rc = prec == "float" ? Run<float>(words, n, pots, amp, seed, out) : Run<double>(words, n, pots, amp, seed, out);
    std::fclose(out);
    return rc;
}
