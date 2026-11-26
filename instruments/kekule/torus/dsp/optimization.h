#pragma once
#include <cstdint>
#include <cstring>
#include "dsp/profile.h"

// Keep a source-level reference path for differential renders and ARM A/B builds.
#ifndef TORUS_OPTIMIZE
#define TORUS_OPTIMIZE 1
#endif

namespace torus {
inline uint32_t FloatBits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
inline uint32_t PhaseWord(float value) {
    // Preserve the previous modulo conversion at the upper endpoint and for
    // negative inputs. The fast path never converts an out-of-range float.
    return value >= 0.0f && value < 4294967296.0f
        ? static_cast<uint32_t>(value)
        : static_cast<uint32_t>(static_cast<int64_t>(value));
}
}
