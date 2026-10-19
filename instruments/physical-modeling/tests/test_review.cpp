// Adapted from Claude's independent review of PR #13 (2026-10-19).
// Production DSP regressions; full-level limiter interaction is kept out of T2.
#include "physical_modeling.h"
#include "midi_control.h"
#include "stmlib/utils/random.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <new>
#include <random>
using namespace physical_modeling;
static Synth synth;
static float L[48], R[48];
static int failures = 0;
#define CHECK(c, ...) do { if(!(c)) { ++failures; printf("FAIL: " __VA_ARGS__); puts(""); } } while(0)

static void Reset(unsigned model) {
    synth.~Synth(); new(&synth) Synth{}; synth.Init();
    stmlib::Random::Seed(0x12345678);
    synth.parameters.SelectModel(model);
    for(int i = 0; i < 30; ++i) synth.Process(L, R, 48);
}
static std::vector<float> Run(unsigned blocks, float* peak = nullptr, size_t* knee = nullptr) {
    std::vector<float> s;
    for(unsigned b = 0; b < blocks; ++b) {
        synth.Process(L, R, 48);
        for(int i = 0; i < 48; ++i) {
            s.push_back(L[i]); s.push_back(R[i]);
            if(peak) *peak = std::max(*peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
            if(knee) *knee += (std::fabs(L[i]) > 0.6f) + (std::fabs(R[i]) > 0.6f);
        }
    }
    for(float v:s) CHECK(std::isfinite(v) && std::fabs(v)<0.8f, "finite protected sample");
    return s;
}
static double Hz(double note) { return 440.0 * std::pow(2.0, (note - 69) / 12); }
// Max Hann-windowed DFT power within +-1.5 % of f.
static double Power1(const std::vector<float>& s, double f);
static double Power(const std::vector<float>& st, double f) {
    std::vector<float> l, r; for(size_t i = 0; i < st.size(); i += 2) { l.push_back(st[i]); r.push_back(st[i + 1]); }
    return Power1(l, f) + Power1(r, f);
}
static double Power1(const std::vector<float>& s, double f) {
    double best = 0;
    for(double hz = f * 0.985; hz <= f * 1.015; hz += 0.5) {
        double re = 0, im = 0;
        for(size_t i = 0; i < s.size(); ++i) {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (s.size() - 1));
            const double p = 2 * M_PI * hz * i / 48000;
            re += s[i] * w * std::cos(p); im += s[i] * w * std::sin(p);
        }
        best = std::max(best, re * re + im * im);
    }
    return best;
}
static double dB(double a, double b) { return 10 * std::log10((a + 1e-30) / (b + 1e-30)); }

static float g_exc = 1;
static void SetCommon(float damping) {
    synth.parameters.Set(Wet, 0); synth.parameters.Set(Damping, damping);
    synth.parameters.Set(Structure, 0.25f); synth.parameters.Set(Brightness, 0.5f);
    synth.parameters.Set(Position, 0.5f); synth.parameters.Set(Excitation, g_exc);
    for(int i = 0; i < 20; ++i) synth.Process(L, R, 48);
}

// T1: four notes in the same callback -> four individual fundamentals.
static void ChordSpectrum() {
    const unsigned chord[4] = {60, 64, 67, 71};
    const char* names[6] = {"Modal", "Sympathetic", "Inharmonic", "FM", "Western", "String+Rev"};
    for(unsigned m = 0; m < 6; ++m) {
        double single[4], full[4];
        for(int pass = 0; pass < 2; ++pass) {
            Reset(m); SetCommon(0.8f);
            if(pass == 0) synth.NoteOn(60, 127);
            else for(auto n : chord) synth.NoteOn(n, 127);
            Run(10); // 10 ms
            auto s = Run(100); // 100 ms window
            for(int k = 0; k < 4; ++k) (pass ? full : single)[k] = Power(s, Hz(chord[k]));
        }
        printf("T1 %-12s chord vs single-note power gain at 60/64/67/71: %+6.1f %+6.1f %+6.1f %+6.1f dB\n",
               names[m], dB(full[0], single[0]), dB(full[1], single[1]), dB(full[2], single[2]), dB(full[3], single[3]));
        for(int k = 0; k < 4; ++k)
            CHECK(full[k] > 1e-6, "model %u chord fundamental %u absent", m, chord[k]);
        for(int k = 1; k < 4; ++k)
            CHECK(dB(full[k], single[k]) > 10, "model %u note %u not individually present", m, chord[k]);
    }
}

// T2: sequential strikes must not reset earlier voices; 5th strike steals voice of note 1.
static void VoicePreservation() {
    g_exc = 0.05f; // Isolate voice preservation from the original shared limiter.
    for(unsigned m : {0u, 2u, 3u}) {
        double ref, poly, after_steal, ref_late;
        { // reference: only note 60
            Reset(m); SetCommon(0.9f); synth.NoteOn(60, 127); Run(200);
            ref = Power(Run(100), Hz(60)); ref_late = Power(Run(100), Hz(60));
        }
        Reset(m); SetCommon(0.9f);
        synth.NoteOn(60, 127); Run(50);
        synth.NoteOn(64, 127); Run(50);
        synth.NoteOn(67, 127); Run(50);
        synth.NoteOn(71, 127); Run(50);
        poly = Power(Run(100), Hz(60));
        synth.NoteOn(74, 127); Run(1);
        after_steal = Power(Run(99), Hz(60));
        printf("T2 model %u: note60 power with 3 later strikes vs alone: %+5.1f dB; after 5th strike (steal): %+6.1f dB\n",
               m, dB(poly, ref), dB(after_steal, ref_late));
        CHECK(std::fabs(dB(poly, ref)) < 3, "model %u earlier voice disturbed", m);
        CHECK(dB(after_steal, ref_late) < -15, "model %u fifth strike did not steal first voice", m);
    }
    g_exc = 1;
}

// T3: bend on already-sounding voices moves all four fundamentals by +2 st.
static void BendOnSounding() {
    Reset(0); SetCommon(0.9f);
    for(auto n : {60u, 64u, 67u, 71u}) synth.NoteOn(n, 127);
    Run(50);
    HandleMidi(synth, {0xe0, 127, 127}); Run(30);
    auto s = Run(100);
    for(auto n : {60u, 64u, 67u, 71u}) {
        const double moved = Power(s, Hz(n + 2)), stay = Power(s, Hz(n));
        printf("T3 note %u: +2st %.3g vs original %.3g\n", n, moved, stay);
        CHECK(moved > 10 * stay, "bend did not move sounding voice %u", n);
    }
}

// T4: extreme level stress in all models.
static void Extremes() {
    std::mt19937 rng(1);
    for(unsigned m = 0; m < 6; ++m) for(int wet = 0; wet < 2; ++wet) {
        Reset(m);
        for(unsigned p : {Brightness, Damping, Excitation, Decay, InputGain, Master, Diffusion}) synth.parameters.Set(p, 1);
        synth.parameters.Set(Wet, float(wet)); synth.parameters.Set(Structure, 0.5f);
        float peak = 0; size_t knee = 0, total = 0;
        for(int t = 0; t < 400; ++t) { // 4 s, chord every 10 ms
            for(int k = 0; k < 4; ++k) synth.NoteOn(36 + rng() % 60, 127);
            Run(10, &peak, &knee); total += 960;
        }
        printf("T4 model %u wet=%d: peak %.6f, knee active on %.2f %% of samples\n", m, wet, peak, 100.0 * knee / total);
        CHECK(peak <= 0.8f, "peak bound");
    }
    // model switching under stress
    float peak = 0; size_t knee = 0;
    Reset(0);
    for(int t = 0; t < 600; ++t) {
        if(t % 25 == 0) synth.parameters.SelectModel((t / 25) % 6);
        for(int k = 0; k < 4; ++k) synth.NoteOn(rng() % 128, 127);
        Run(2, &peak, &knee);
    }
    printf("T4 switching stress peak %.6f\n", peak);
    CHECK(peak <= 0.8f, "switch peak bound");
}

// T5: Wet=1, long decay: does a model change interrupt the global reverb tail?
static std::vector<float> Tail(unsigned from, unsigned to) {
    Reset(from); SetCommon(0); // Short dry resonance; long independent reverb.
    synth.parameters.Set(Wet,1); synth.parameters.Set(Decay,1); Run(100);
    for(auto n:{60u,64u,67u,71u}) synth.NoteOn(n,127);
    Run(1000);
    synth.parameters.SelectModel(to);
    return Run(20);
}
static void ReverbTailOnModelChange() {
    for(unsigned from:{0u,5u}) {
        const auto reference=Tail(from,from);
        for(unsigned to:{0u,1u,4u,5u}) {
            if(from==to) continue;
            const auto changed=Tail(from,to);
            double reference_energy=0,error=0,minimum=1e9;
            for(size_t i=0;i<changed.size();i+=24) {
                double energy=0;
                for(unsigned k=0;k<24;++k) {
                    energy+=changed[i+k]*changed[i+k];
                    reference_energy+=reference[i+k]*reference[i+k];
                    const double delta=changed[i+k]-reference[i+k]; error+=delta*delta;
                }
                minimum=std::min(minimum,std::sqrt(energy/24));
            }
            const double rms=std::sqrt(reference_energy/reference.size());
            printf("T5 %u -> %u: min/reference tail RMS %.6f/%.6f, relative waveform error %.9g\n",from,to,minimum,rms,error/reference_energy);
            CHECK(rms>1e-5 && minimum>rms*.02,"global tail muted on %u -> %u",from,to);
            CHECK(error/reference_energy<.001,"global tail changed or flipped polarity on %u -> %u",from,to);
        }
    }
}

// T6: synthetic pot noise/ownership with a .5 one-pole prefilter, not a
// measurement or assertion about the pinned board ADC's actual filter/noise.
static void PotNoiseOwnership() {
    for(float amp : {0.001f, 0.002f, 0.004f, 0.008f, 0.016f}) {
        ParameterManager p; std::mt19937 rng(7); std::uniform_real_distribution<float> u(-amp, amp);
        float adc = 0.5f; p.Pot(Brightness, adc);
        p.Set(Brightness, 1.0f); // CC22 = 127
        int steals = 0;
        for(int i = 0; i < 60000; ++i) { // 60 s
            adc += 0.5f * (0.5f + u(rng) - adc);
            p.Pot(Brightness, adc);
            if(p.Target(Brightness) != 1.0f) { ++steals; p.Set(Brightness, 1.0f); }
        }
        printf("T6 raw ADC noise +-%.3f FS: MIDI value overwritten %d times in 60 s\n", amp, steals);
        if(amp<=.004f) CHECK(steals==0,"MIDI ownership lost within tested noise envelope");
        // Larger synthetic noise is diagnostic only until real ADC noise is measured.
    }
}

int main() {
    setbuf(stdout, nullptr);
    ChordSpectrum(); VoicePreservation(); BendOnSounding(); Extremes(); ReverbTailOnModelChange(); PotNoiseOwnership();
    printf("%s (%d failures)\n", failures ? "REVIEW TESTS FAILED" : "REVIEW TESTS PASSED", failures);
    return failures != 0;
}
