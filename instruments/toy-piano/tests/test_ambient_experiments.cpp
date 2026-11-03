// MIT. Differential render for the diagnostic reverb memory switches: the build
// with AMBIENT_COMPACT_48K/AMBIENT_CONTIGUOUS_FIR must match the default build
// bit for bit. One continuous host: all six modes, Stretch endpoints and sweeps,
// long decay, maximum modulation depth and dense chords.
#include "ambient_host.h"
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
using namespace toy_piano;
static ToyPiano piano;
alignas(AmbientHost) static unsigned char storage[sizeof(AmbientHost)];
#if AMBIENT_COMPACT_48K
static ambient::FastMemory fast;
#endif
int main(int argc, char** argv) {
    assert(argc == 3);
    const bool write = std::strcmp(argv[1], "write") == 0;
    std::FILE* file = std::fopen(argv[2], write ? "wb" : "rb");
    assert(file);
    piano.Init(kSampleRate);
    AmbientHost* host = new(storage) AmbientHost{};
#if AMBIENT_COMPACT_48K
    assert(host->Prepare(&fast));
#else
    assert(host->Prepare());
#endif
    uint64_t samples = 0, different = 0;
    float mono[kBlockSize], left[kBlockSize], right[kBlockSize];
    unsigned block = 0;
    for(unsigned mode = 0; mode < 6; ++mode) {
        const bool bank = mode >= 3;
        const int position = mode % 3 == 0 ? 1 : mode % 3 == 1 ? 0 : 2;
        for(unsigned b = 0; b < 4000; ++b, ++block) {
            const float t = float(b) / 4000.f;
            // Stretch: full rate, sweep down, maximum stretch, sweep up.
            const float stretch = t < .25f ? 0.f : t < .5f ? (t - .25f) * 4 : t < .75f ? 1.f : 4 * (1 - t);
            const std::array<float, 8> knobs = {.5f, .7f, .9f, .4f + .5f * t, 1.f, .6f, 1.f - t, stretch};
            host->ReadControls(knobs, bank, position);
            host->SetModulated(.95f - .3f * t, .7f, t);
            if(b % 250 == 0)
                for(uint8_t n : {36, 48, 55, 60, 64, 67, 71, 74})
                    piano.NoteOn(uint8_t(n + (block / 250) % 7), 120, .002f, 2.5f);
            for(unsigned i = 0; i < kBlockSize; ++i) mono[i] = piano.Process();
            assert(host->Process(mono, left, right, kBlockSize));
            for(unsigned i = 0; i < kBlockSize; ++i) {
                float actual[2] = {left[i], right[i]}, reference[2];
                assert(std::isfinite(actual[0]) && std::isfinite(actual[1]));
                if(write) {
                    assert(std::fwrite(actual, sizeof(actual), 1, file) == 1);
                } else {
                    assert(std::fread(reference, sizeof(reference), 1, file) == 1);
                    different += std::memcmp(&actual[0], &reference[0], 4) != 0;
                    different += std::memcmp(&actual[1], &reference[1], 4) != 0;
                }
                samples += 2;
            }
        }
    }
    if(!write) assert(std::fgetc(file) == EOF);
    std::fclose(file);
    std::printf("PASS ambient experiments: %" PRIu64 " samples, %" PRIu64 " different\n", samples, different);
    return different == 0 ? 0 : 1;
}
