#pragma once
// SPDX-License-Identifier: MIT
// Stack high-water measurement for the Cumulonimbus diagnostic images (PR #108 review: check the
// DTCM stack headroom before the soak). Needs ld/toy_piano_axi.lds: the free gap is the DTCM
// between the end of the reserved-for-stack section and _estack. ARM only.
#include "stage_meter.h"
#include <cstdint>

#if defined(__arm__)
extern "C" uint32_t __reserved_for_stack_end__[], _estack[];
namespace cumulonimbus::board {
// Main only, before the audio starts: paints up to 256 bytes below the current stack pointer.
inline void PaintFreeStack() {
    uint32_t* sp;
    asm volatile("mov %0, sp" : "=r"(sp));
    diag::PaintStack(__reserved_for_stack_end__, sp - 64);
}
// Bytes never written since the paint (any context: main, audio and other interrupts).
inline uint32_t StackFree() { return diag::UntouchedStack(__reserved_for_stack_end__, _estack); }
inline uint32_t StackSize() { return uint32_t(_estack - __reserved_for_stack_end__) * 4u; }
} // namespace cumulonimbus::board
#else
namespace cumulonimbus::board { // host builds: not measured (reported as 0)
inline void PaintFreeStack() {}
inline uint32_t StackFree() { return 0; }
inline uint32_t StackSize() { return 0; }
} // namespace cumulonimbus::board
#endif
