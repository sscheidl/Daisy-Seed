#pragma once
// Host stand-in for the parts of the Korg logue SDK unit.h that the verbatim
// drumlogue reference headers (tapevibe.h, mixforge.h) use. Test-only.
#include <cstdint>
#define fast_inline inline __attribute__((always_inline))
struct unit_runtime_desc_t {
    uint32_t samplerate = 48000;
    uint8_t input_channels = 2;
    uint8_t output_channels = 2;
};
enum { k_unit_err_none = 0, k_unit_err_samplerate = -1, k_unit_err_geometry = -2 };
