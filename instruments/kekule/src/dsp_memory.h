#pragma once
// Both NOLOAD placements use the existing explicit startup initialization.
#if defined(__arm__) && PM_MEMORY_AXI
#define PM_DSP_STORAGE __attribute__((section(".pm_axi_bss"), aligned(32)))
#elif defined(__arm__)
#define PM_DSP_STORAGE __attribute__((section(".sdram_bss")))
#else
#define PM_DSP_STORAGE
#endif
