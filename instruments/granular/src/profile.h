// Diagnostic build only (make PROFILE=1, -DNEBULAE_PROFILE=1): cycle splits of one audio
// block, taken with the DWT cycle counter on the target. The regular and the MOD image
// compile every macro here to nothing and stay byte-identical (CHANGELOG, VALIDATION.md).
#pragma once
#include <cstdint>

namespace neb::profile {
#if NEBULAE_PROFILE
uint32_t Now();  // cycle counter: DWT->CYCCNT in main.cpp, an advancing fake on the host

// Written by the audio callback only; Engine::Process clears it at the start of a block
// and main.cpp folds it into its peaks afterwards.
struct Split {
    uint32_t vocoder = 0, grains = 0;       // Engine::Process: both vocoder channels, grains
    uint32_t fill = 0, fft = 0, phase = 0;  // inside the hops of this block: window fill
                                            // (interpolated SDRAM reads), FFTs, phase
                                            // loops plus overlap-add
    uint32_t hops = 0;                      // hop frames computed in this block (0 or 2)
    bool skipped = false;                   // vocoder left out by the overload protection
};
inline Split block;
#define NEB_PROFILE_BEGIN(tag) const uint32_t neb_profile_##tag = ::neb::profile::Now()
#define NEB_PROFILE_ADD(tag, field) ::neb::profile::block.field += ::neb::profile::Now() - neb_profile_##tag
#else
#define NEB_PROFILE_BEGIN(tag)
#define NEB_PROFILE_ADD(tag, field)
#endif
}
