#include "toy_piano.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace toy_piano;
namespace {
bool Near(float a, float b, float tolerance = 0.00001f) {
    return std::fabs(a - b) < tolerance;
}

void TestNotesAndPitch() {
    const std::array<uint8_t, 16> expected = {
        60, 62, 64, 65, 67, 69, 71, 72, 72, 74, 76, 77, 79, 81, 83, 84};
    assert(kMidiNotes == expected);
    assert(Near(MidiToFrequency(69), 440.0f));
    assert(Near(MidiToFrequency(60), 261.625565f, 0.001f));
    for(std::size_t key = 0; key < kNumKeys; ++key) {
        ToyPiano piano;
        piano.Init();
        piano.Trigger(key, kAttackMin, kDecayMax);
        float previous = 0.0f;
        int crossings = 0;
        for(int i = 0; i < 48000; ++i) {
            const float sample = piano.Process();
            if(previous < 0.0f && sample >= 0.0f)
                ++crossings;
            previous = sample;
        }
        // One-second zero-crossing estimate of actual rendered oscillator pitch.
        assert(std::fabs(crossings - MidiToFrequency(expected[key])) < 1.1f);
    }
}

void TestTimesAndFilters() {
    for(const auto bounds : {std::array<float, 2>{kAttackMin, kAttackMax},
                             std::array<float, 2>{kDecayMin, kDecayMax}}) {
        assert(Near(LogTime(0, bounds[0], bounds[1]), bounds[0]));
        assert(Near(LogTime(1, bounds[0], bounds[1]), bounds[1]));
        assert(Near(LogTime(0.5f, bounds[0], bounds[1]), std::sqrt(bounds[0] * bounds[1])));
        float previous = 0;
        for(int i = 0; i <= 1000; ++i) {
            const float current = LogTime(i / 1000.0f, bounds[0], bounds[1]);
            assert(current > previous);
            previous = current;
        }
    }
    PotFilter filter;
    assert(filter.Process(0.5f) == 0.5f);
    for(int i = 0; i < 1000; ++i)
        assert(filter.Process(0.5f + (i % 2 ? 0.0005f : -0.0005f)) == 0.5f);
    for(int i = 0; i < 1000; ++i)
        filter.Process(1);
    assert(filter.Process(1) == 1);
    for(int i = 0; i < 1000; ++i)
        filter.Process(0);
    assert(filter.Process(0) == 0);
}

void TestEnvelopes() {
    for(float attack : {kAttackMin, LogTime(0.5f, kAttackMin, kAttackMax), kAttackMax})
        for(float decay : {kDecayMin, LogTime(0.5f, kDecayMin, kDecayMax), kDecayMax}) {
            AdEnvelope envelope;
            assert(!envelope.Active() && envelope.Process() == 0);
            envelope.Trigger(attack, decay, kSampleRate);
            const int na = static_cast<int>(attack * kSampleRate + 0.5f);
            const int nd = static_cast<int>(decay * kSampleRate + 0.5f);
            float previous = 0;
            for(int i = 0; i < na; ++i) {
                const float level = envelope.Process();
                assert(std::isfinite(level) && level >= previous && level <= 1);
                assert(level - previous < 0.016f); // Even at the 2 ms minimum.
                previous = level;
            }
            assert(envelope.Level() == 1);
            for(int i = 0; i < nd; ++i) {
                const float level = envelope.Process();
                assert(std::isfinite(level) && level >= 0 && level <= previous);
                assert(previous - level < 0.002f);
                previous = level;
                if(i + 1 < nd)
                    assert(envelope.Active());
            }
            assert(envelope.Level() == 0 && !envelope.Active());
        }
    for(int elapsed : {30, 1000}) { // Retrigger during attack and decay.
        AdEnvelope envelope;
        envelope.Trigger(0.01f, 1.0f, kSampleRate);
        for(int i = 0; i < elapsed; ++i)
            envelope.Process();
        const float before = envelope.Level();
        envelope.Trigger(kAttackMin, kDecayMin, kSampleRate);
        assert(envelope.Level() == before);
        assert(std::fabs(envelope.Process() - before) < 0.001f);
    }
}

void TestPolyphony() {
    ToyPiano poly;
    poly.Init();
    std::array<ToyPiano, kNumKeys> singles;
    for(std::size_t key = 0; key < kNumKeys; ++key) {
        singles[key].Init();
        const float decay = LogTime(key / 15.0f, kDecayMin, kDecayMax);
        poly.Trigger(key, kAttackMin, decay);
        singles[key].Trigger(key, kAttackMin, decay);
    }
    float peak = 0;
    for(int i = 0; i < 160000; ++i) {
        if(i == 3000 || i == 10000) { // Retrigger only one of the two C5 keys.
            poly.Trigger(7, kAttackMin, 0.2f);
            singles[7].Trigger(7, kAttackMin, 0.2f);
        }
        float sum = 0;
        for(auto& single : singles)
            sum += single.Process();
        const float actual = poly.Process();
        assert(Near(actual, sum)); // Independent phases and envelopes per key.
        assert(std::isfinite(actual) && std::fabs(actual) <= kOutputCeiling + 0.00001f);
        peak = std::max(peak, std::fabs(actual));
    }
    for(std::size_t key = 0; key < kNumKeys; ++key)
        assert(!poly.Envelope(key).Active());
    assert(poly.Process() == 0);
    std::cout << "16-voice render peak: " << peak << " (ceiling " << kOutputCeiling << ")\n";
}
}

int main() {
    TestNotesAndPitch();
    TestTimesAndFilters();
    TestEnvelopes();
    TestPolyphony();
    std::cout << "PASS: 16 notes/pitches, log ranges/filter, AD timing/retrigger, polyphony/headroom\n";
}
