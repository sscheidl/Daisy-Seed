// Host timing of the Acid 303 voice (make bench): relative costs of the oscillator alone,
// the ladder filter and the whole voice. Not a Seed3 CPU measurement (R15).
#include "acid303/voice.h"
#include <chrono>
#include <cstdio>

template <class F>
static double NsPerSample(F&& f, int samples) {
    const auto start = std::chrono::steady_clock::now();
    float sink = 0.f;
    for(int i = 0; i < samples; ++i)
        sink += f();
    const auto end = std::chrono::steady_clock::now();
    volatile float keep = sink;
    (void)keep;
    return std::chrono::duration<double, std::nano>(end - start).count() / samples;
}

int main() {
    constexpr int kSamples = 48000 * 20;
    daisysp::Oscillator osc;
    osc.Init(48000.f);
    osc.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    osc.SetFreq(110.f);
    daisysp::LadderFilter ladder;
    ladder.Init(48000.f);
    acid303::Voice voice;
    acid303::Params p;
    p.resonance = 100.f;
    voice.SetParams(p);
    voice.Prepare();
    voice.NoteOn(36, 127, true, false, acid303::Source::Sequencer, 1);
    const double o = NsPerSample([&] { return osc.Process(); }, kSamples);
    const double l = NsPerSample([&] { return ladder.Process(osc.Process()); }, kSamples);
    const double v = NsPerSample([&] { return voice.Process(); }, kSamples);
    std::printf("host ns/sample: oscillator %.1f, oscillator+ladder %.1f, voice %.1f (relative %.2f x ladder)\n", o, l, v,
                v / l);
    return 0;
}
