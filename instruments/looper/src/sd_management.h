// SPDX-License-Identifier: MIT
// Board-only parts of the looper's SD management (sd_management.cpp, never
// host-compiled): backup SRAM for the boot intent and the complete SD management
// personality. Host tests provide fakes for these functions.
#pragma once
#include "boot_intent.h"
#include "desktop_devkit.h"
#include <cstdint>

// Backup SRAM at looper::boot::kBootIntentAddress (enabled on first use).
volatile uint32_t* looper_intent_words();
// USB CDC (DDP) + MSC on J7, card owned by the shared card reader. Does not return on
// the board.
void looper_run_sd_management(daisy::desktop_devkit::Hardware& hw);
