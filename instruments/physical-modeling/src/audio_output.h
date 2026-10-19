#pragma once
#include "config.h"
#include <cmath>
namespace physical_modeling {
inline float Mix(float dry, float wet, float amount) { return dry + (wet - dry) * Unit(amount); }
inline float Output(float signal, float master) {
    if(!std::isfinite(signal)) return 0;
    const float x = signal * (0.8f * Unit(master));
    const float magnitude = std::fabs(x);
    // Linear through 0.6 FS, smooth knee asymptotic to 0.8; final roundoff guard.
    if(magnitude <= 0.6f) return x;
    const float excess = magnitude - 0.6f;
    const float limited = std::min(0.799999f, 0.6f + 0.2f * (excess / (0.2f + excess)));
    return std::copysign(limited, x);
}
}
