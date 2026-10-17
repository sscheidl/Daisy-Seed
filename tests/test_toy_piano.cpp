#include "spsc_queue.h"
#include "toy_piano.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

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
            sum += single.ProcessRaw();
        const float actual = poly.ProcessRaw();
        assert(Near(actual, sum)); // Independent phases and envelopes per key.
        assert(std::isfinite(actual) && std::fabs(actual) <= kOutputCeiling + 0.00001f);
        peak = std::max(peak, std::fabs(actual));
    }
    for(std::size_t key = 0; key < kNumKeys; ++key)
        assert(!poly.Envelope(key).Active());
    assert(poly.ProcessRaw() == 0 && poly.Process() == 0);
    std::cout << "16-key render peak: " << peak << " (ceiling " << kOutputCeiling << ")\n";
}

// Magnitude of one DFT bin (Goertzel) of a Hann-windowed block.
float BinMagnitude(const std::vector<float>& x, float frequency) {
    const double w = 2.0 * 3.14159265358979 * frequency / kSampleRate;
    double re = 0, im = 0;
    for(std::size_t n = 0; n < x.size(); ++n) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * n / x.size());
        re += x[n] * hann * std::cos(w * n);
        im -= x[n] * hann * std::sin(w * n);
    }
    return static_cast<float>(std::sqrt(re * re + im * im));
}

std::vector<float> Render(ToyPiano& piano, int samples) {
    std::vector<float> out(samples);
    for(auto& sample : out)
        sample = piano.ProcessRaw();
    return out;
}

void TestEnvelopePeak() {
    AdEnvelope envelope;
    envelope.Trigger(0.01f, 0.1f, kSampleRate, 0.5f);
    float previous = 0;
    for(int i = 0; i < 480; ++i) {
        const float level = envelope.Process();
        assert(level >= previous && level <= 0.5f);
        previous = level;
    }
    assert(envelope.Level() == 0.5f);
    for(int i = 0; i < 4800; ++i) {
        const float level = envelope.Process();
        assert(level >= 0 && level <= previous);
        previous = level;
    }
    assert(envelope.Level() == 0 && !envelope.Active());
    // Retrigger from a high level towards a lower peak: continuous, then falling.
    envelope.Trigger(0.01f, 1.0f, kSampleRate);
    for(int i = 0; i < 1000; ++i)
        envelope.Process();
    const float before = envelope.Level();
    envelope.Trigger(kAttackMin, 0.1f, kSampleRate, 0.2f);
    previous = before;
    for(int i = 0; i < 96; ++i) {
        const float level = envelope.Process();
        assert(level <= previous && level >= 0.2f && previous - level < 0.02f);
        previous = level;
    }
    assert(envelope.Level() == 0.2f);
}

void TestMidiPitchAndVelocity() {
    for(uint8_t note : {24, 36, 60, 69, 96, 108}) {
        ToyPiano piano;
        piano.Init();
        piano.NoteOn(note, 127, kAttackMin, kDecayMax);
        float previous = 0.0f;
        int crossings = 0;
        for(int i = 0; i < 48000; ++i) {
            const float sample = piano.Process();
            if(previous < 0.0f && sample >= 0.0f)
                ++crossings;
            previous = sample;
        }
        assert(std::fabs(crossings - MidiToFrequency(note)) < 1.1f);
    }
    float peaks[2];
    int index = 0;
    for(uint8_t velocity : {127, 64}) {
        ToyPiano piano;
        piano.Init();
        piano.NoteOn(60, velocity, kAttackMin, kDecayMax);
        float peak = 0;
        for(float sample : Render(piano, 4800))
            peak = std::max(peak, std::fabs(sample));
        peaks[index++] = peak;
    }
    assert(std::fabs(peaks[1] / peaks[0] - 64.0f / 127.0f) < 0.002f); // Linear velocity.
    ToyPiano piano;
    piano.Init();
    piano.NoteOn(60, 0, kAttackMin, kDecayMin);   // Velocity 0 is a note-off: ignored.
    piano.NoteOn(128, 100, kAttackMin, kDecayMin);
    piano.NoteOn(60, 128, kAttackMin, kDecayMin);
    for(std::size_t i = 0; i < kNumMidiVoices; ++i)
        assert(piano.MidiVoiceNote(i) == -1);
    assert(piano.ProcessRaw() == 0 && piano.Process() == 0);
}

void TestMidiVoiceAllocation() {
    ToyPiano piano;
    piano.Init();
    for(uint8_t i = 0; i < kNumMidiVoices; ++i) {
        piano.NoteOn(40 + i, 100, kAttackMin, kDecayMax);
        piano.ProcessRaw();
    }
    for(std::size_t i = 0; i < kNumMidiVoices; ++i)
        assert(piano.MidiVoiceNote(i) == 40 + static_cast<int>(i));
    // Pool exhausted: the oldest voices are taken, one after the other.
    piano.NoteOn(100, 100, kAttackMin, kDecayMax);
    assert(piano.MidiVoiceNote(0) == 100 && piano.MidiVoiceNote(1) == 41);
    piano.NoteOn(101, 100, kAttackMin, kDecayMax);
    assert(piano.MidiVoiceNote(1) == 101 && piano.MidiVoiceNote(2) == 42);
    // Re-striking a sounding note reuses its voice instead of stealing another.
    piano.NoteOn(45, 100, kAttackMin, kDecayMax);
    assert(piano.MidiVoiceNote(5) == 45 && piano.MidiVoiceNote(2) == 42);
    // A stolen voice continues from its current level.
    ToyPiano level_piano;
    level_piano.Init();
    for(uint8_t i = 0; i < kNumMidiVoices; ++i)
        level_piano.NoteOn(40 + i, 127, kAttackMin, kDecayMax);
    Render(level_piano, 5000);
    const float level_before = level_piano.MidiEnvelope(0).Level();
    assert(level_before > 0.5f && level_before < 1.0f);
    level_piano.NoteOn(100, 127, kAttackMin, kDecayMax);
    assert(level_piano.MidiVoiceNote(0) == 100 && level_piano.MidiEnvelope(0).Level() == level_before);
    level_piano.ProcessRaw();
    assert(std::fabs(level_piano.MidiEnvelope(0).Level() - level_before) < 0.001f);
    // Everything decays to silence and frees all voices.
    Render(piano, 3 * 48000 + 200);
    for(std::size_t i = 0; i < kNumMidiVoices; ++i)
        assert(piano.MidiVoiceNote(i) == -1);
    assert(piano.Process() == 0);
}

void TestKeysAndMidiAreIndependent() {
    ToyPiano both, keys_only, midi_only;
    both.Init();
    keys_only.Init();
    midi_only.Init();
    for(std::size_t key : {0u, 7u, 8u, 15u}) {
        both.Trigger(key, 0.01f, 0.5f);
        keys_only.Trigger(key, 0.01f, 0.5f);
    }
    float peak = 0;
    for(int i = 0; i < 100000; ++i) {
        if(i == 2000) { // MIDI notes arrive mid-way, one of them equal to a key's pitch.
            for(uint8_t note : {60, 61, 72, 90}) {
                both.NoteOn(note, 90, 0.005f, 0.8f);
                midi_only.NoteOn(note, 90, 0.005f, 0.8f);
            }
        }
        const float expected = keys_only.ProcessRaw() + midi_only.ProcessRaw();
        const float actual = both.ProcessRaw();
        assert(Near(actual, expected));
        peak = std::max(peak, std::fabs(actual));
    }
    assert(peak > 0.05f);
}

void TestSoftLimiter() {
    assert(ToyPiano::SoftLimit(0) == 0);
    float previous = ToyPiano::SoftLimit(-10);
    for(int i = -9999; i <= 10000; ++i) {
        const float x = i / 1000.0f, y = ToyPiano::SoftLimit(x);
        assert(y > previous && std::fabs(y) < kOutputCeiling);
        assert(ToyPiano::SoftLimit(-x) == -y);
        previous = y;
    }
    for(float x : {1e3f, 1e6f, 1e12f})
        assert(std::fabs(ToyPiano::SoftLimit(x)) <= kOutputCeiling);
    assert(std::fabs(ToyPiano::SoftLimit(0.05f) - 0.05f) < 0.05f * 0.005f);
    // 16 keys and 16 full-velocity MIDI voices sounding together (the sum can reach
    // 2 * kOutputCeiling in theory; the limiter bounds it regardless).
    ToyPiano piano;
    piano.Init();
    for(std::size_t key = 0; key < kNumKeys; ++key)
        piano.Trigger(key, kAttackMin, kDecayMax);
    for(uint8_t i = 0; i < kNumMidiVoices; ++i)
        piano.NoteOn(60 + i, 127, kAttackMin, kDecayMax);
    float raw_peak = 0, limited_peak = 0;
    ToyPiano reference = piano;
    for(int i = 0; i < 200000; ++i) {
        raw_peak = std::max(raw_peak, std::fabs(reference.ProcessRaw()));
        const float y = piano.Process();
        assert(std::isfinite(y) && std::fabs(y) <= kOutputCeiling);
        limited_peak = std::max(limited_peak, std::fabs(y));
    }
    assert(raw_peak > limited_peak);
    std::cout << "32-voice dense chord peak: raw " << raw_peak << ", limited " << limited_peak
              << " (ceiling " << kOutputCeiling << ")\n";
}

void TestMidiRetriggerPhaseContinuity() {
    ToyPiano piano;
    piano.Init();
    piano.NoteOn(57, 127, 0.05f, 2.0f); // A3: small step per sample, large swing.
    float previous = 0, max_step = 0, amplitude = 0;
    for(int i = 0; i < 3000; ++i) {
        const float sample = piano.ProcessRaw();
        if(i > 2700) {
            max_step = std::max(max_step, std::fabs(sample - previous));
            amplitude = std::max(amplitude, std::fabs(sample));
        }
        previous = sample;
    }
    while(std::fabs(previous) < 0.95f * amplitude) // Retrigger near a wave crest.
        previous = piano.ProcessRaw();
    piano.NoteOn(57, 127, kAttackMin, 2.0f); // Same voice, same pitch: no phase jump.
    const float next = piano.ProcessRaw();
    assert(std::fabs(previous) > 10.0f * max_step); // A phase reset would be a big jump.
    assert(std::fabs(next - previous) < 2.0f * max_step);
}

void TestNoAliasedHarmonic() {
    const std::vector<std::pair<uint8_t, bool>> cases = {{100, true}, {127, false}};
    for(const auto& [note, has_harmonic] : cases) {
        ToyPiano piano;
        piano.Init();
        piano.NoteOn(note, 127, kAttackMin, kDecayMax);
        const auto block = Render(piano, 4800);
        const float fundamental = MidiToFrequency(note);
        const float harmonic = BinMagnitude(block, std::fmod(2.0f * fundamental, kSampleRate));
        const float alias = BinMagnitude(block, kSampleRate - 2.0f * fundamental);
        const float reference = BinMagnitude(block, fundamental);
        if(has_harmonic) // 2f is below Nyquist: the 12 % partial must be there.
            assert(harmonic > 0.08f * reference && harmonic < 0.16f * reference);
        else // 2f is above Nyquist: nothing may fold back below it.
            assert(alias < 0.01f * reference);
    }
}

void TestSpscQueue() {
    SpscQueue<int, 4> queue;
    int value = 0;
    assert(!queue.Pop(value));
    for(int i = 0; i < 4; ++i)
        assert(queue.Push(i));
    assert(!queue.Push(99)); // Full: dropped, nothing overwritten.
    for(int i = 0; i < 4; ++i)
        assert(queue.Pop(value) && value == i);
    assert(!queue.Pop(value));
    for(int round = 0; round < 10; ++round) { // Index wrap-around.
        assert(queue.Push(round) && queue.Push(round + 100));
        assert(queue.Pop(value) && value == round);
        assert(queue.Pop(value) && value == round + 100);
    }
    SpscQueue<uint32_t, 64> shared;
    constexpr uint32_t kCount = 300000;
    std::thread producer([&] {
        for(uint32_t i = 0; i < kCount; ++i)
            while(!shared.Push(i)) {}
    });
    for(uint32_t expected = 0; expected < kCount;) {
        uint32_t item;
        if(shared.Pop(item)) {
            assert(item == expected);
            ++expected;
        }
    }
    producer.join();
}
}

int main() {
    TestNotesAndPitch();
    TestTimesAndFilters();
    TestEnvelopes();
    TestPolyphony();
    TestEnvelopePeak();
    TestMidiPitchAndVelocity();
    TestMidiVoiceAllocation();
    TestKeysAndMidiAreIndependent();
    TestSoftLimiter();
    TestMidiRetriggerPhaseContinuity();
    TestNoAliasedHarmonic();
    TestSpscQueue();
    std::cout << "PASS: keys, log ranges/filter, AD timing/retrigger, polyphony, "
                 "MIDI pitch/velocity/allocation, limiter, aliasing, queue\n";
}
