#include "performance_render.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cinttypes>
using namespace physical_modeling;
static Synth synth;
int main(int argc, char** argv) {
    assert(argc == 3);
    const bool write = std::strcmp(argv[1], "write") == 0;
    assert(write || std::strcmp(argv[1], "compare") == 0);
    std::FILE* file = std::fopen(argv[2], write ? "wb" : "rb");
    assert(file);
    uint64_t samples = 0, different = 0;
    double maximum_error = 0;
    const uint64_t hash = RenderPerformance(synth, [&](float left, float right) {
        float actual[2] = {left, right}, reference[2];
        for(float value : actual)
            assert(std::isfinite(value) && std::fabs(value) < 0.8f);
        if(write) {
            assert(std::fwrite(actual, sizeof(actual), 1, file) == 1);
        } else {
            assert(std::fread(reference, sizeof(reference), 1, file) == 1);
            for(unsigned i = 0; i < 2; ++i) {
                if(torus::FloatBits(actual[i]) != torus::FloatBits(reference[i])) ++different;
                maximum_error = std::fmax(maximum_error, std::fabs(double(actual[i]) - reference[i]));
            }
        }
        samples += 2;
    });
    if(!write) assert(std::fgetc(file) == EOF);
    std::fclose(file);
    for(float x : {-4294967296.0f, -0.5f, -0.0f, 0.0f, 1.0f, 4294967040.0f, 4294967296.0f})
        assert(torus::PhaseWord(x) == uint32_t(int64_t(x)));
    for(uint32_t i = 0; i < 100000; ++i) {
        const float x = (float(i) / 99999.0f) * 4294967296.0f;
        assert(torus::PhaseWord(x) == uint32_t(int64_t(x)));
    }
    std::printf("corpus v1: hash %016" PRIX64 ", samples %" PRIu64
                ", different %" PRIu64 ", max error %.9g\n",
                hash, samples, different, maximum_error);
    assert(different == 0);
}
