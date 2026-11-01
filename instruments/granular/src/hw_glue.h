// Board-only parts of Nebulae (hw_glue.cpp, never host-compiled): backup SRAM for the
// boot intent, the SDMMC1 abort/release, and the complete SD management personality.
// Host tests provide fakes for these functions.
#pragma once
#include "boot_intent.h"
#include "desktop_devkit.h"
#include <cstdint>

extern "C" void neb_sd_abort(void);    // RCC reset of SDMMC1 (common/sd/reader/reader_abort.h)
extern "C" void neb_sd_release(void);  // power the SD interface down before a restart
volatile uint32_t* neb_intent_words(); // backup SRAM at neb::kBootIntentAddress
// USB CDC (DDP) + MSC on J7, card owned by the shared card reader. Never returns.
[[noreturn]] void neb_run_sd_management(daisy::desktop_devkit::Hardware& hw, const neb::BootIntent& intent);
