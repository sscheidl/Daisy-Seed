#pragma once
#include "physical_modeling.h"
#include "stmlib/utils/random.h"
#include <cstring>
#include <new>

namespace physical_modeling {
// Corpus v1: 24 seconds, six models, ordinary four-note chords, settled
// controls, two-knob sweeps, eight-knob automation and independent MOD voices.
// Identical source is used by host comparison and the optional ARM render.
template<class Sink>
uint64_t RenderPerformance(Synth& synth, Sink sink) {
    uint64_t hash = 1469598103934665603ull;
    float left[kBlockSize], right[kBlockSize];
    for(unsigned model = 0; model < 6; ++model) {
        synth.~Synth();
        std::memset(static_cast<void*>(&synth), 0, sizeof(synth));
        new(&synth) Synth{};
        synth.Init();
        stmlib::Random::Seed(0x12345678);
        synth.parameters.SelectModel(model);
        synth.parameters.Set(Decay, 0.9f);
        synth.parameters.Set(Wet, 0.35f);
        for(unsigned block = 0; block < 4000; ++block) {
            // Legal MIDI bounds also exercise the string SRC transition/state.
            if(block == 2200) synth.NoteOn(0, 80);
            if(block == 2500) synth.NoteOn(96, 80);
            if(block < 3000 && block % 1000 == 0)
                for(unsigned offset : {0u, 4u, 7u, 12u})
                    synth.NoteOn(48 + model * 2 + offset, 90);
            if(block >= 1000 && block < 3000 && block % 100 == 0) {
                const float x = float((block / 100) % 11) / 10.0f;
                synth.parameters.Set(Structure, x);
                synth.parameters.Set(Damping, 1.0f - x);
                if(block >= 2000)
                    for(unsigned control : {0u, 2u, 4u, 5u, 6u, 7u})
                        synth.parameters.Set(control, x);
#if PHYSICAL_MODELING_MOD
                synth.SetVoiceModulation(true, {0.1f, 0.2f, -0.3f, 0.4f, -0.2f},
                                         {0.0f, x, 0.5f * x, 1.0f - x});
#endif
            }
            synth.Process(left, right, kBlockSize);
            for(unsigned sample = 0; sample < kBlockSize; ++sample) {
                sink(left[sample], right[sample]);
                for(float value : {left[sample], right[sample]}) {
                    const uint32_t bits = torus::FloatBits(value);
                    for(unsigned shift = 0; shift < 32; shift += 8) {
                        hash ^= (bits >> shift) & 255u;
                        hash *= 1099511628211ull;
                    }
                }
            }
        }
    }
    return hash;
}
inline uint64_t RenderPerformance(Synth& synth) {
    return RenderPerformance(synth, [](float, float) {});
}
}
