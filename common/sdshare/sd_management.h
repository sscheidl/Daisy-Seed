// Board side of SD management (ARM only, never host-compiled): backup SRAM for the boot
// intent, SDMMC1 abort/release, the restart, and the complete SD management personality
// (card reader CDC + MSC on J7, DDP responder, LEDs, S4 escape). Generalised from
// Nebulae on Daisy 0.2.0 (instruments/granular/src/hw_glue.cpp). The USB identity of the
// composite comes from the firmware's READER_USB_* defines (common/sd/reader/reader_usb.cpp).
#pragma once
#include "boot_intent.h"
#include "desktop_devkit.h"
#include <cstdint>

namespace sdshare {

struct ManagementConfig {
    const char* app = "?";         // HELLO app=
    const char* firmware = "?";    // HELLO fw=
    uint32_t magic = 0;            // boot intent magic of this firmware
    BootIntent returning{};        // written when returning to performance (a/b payload)
};

volatile uint32_t* IntentWords();  // backup SRAM at kBootIntentAddress (enables access)
void AbortSd();                    // RCC reset of SDMMC1, hardware-tested in the card reader
void ReleaseSd();                  // SD interface powered down before a restart
// Stores the intent and restarts through the external bootloader without its DFU wait.
// Returns only on host fakes.
void RestartInto(uint32_t magic, const BootIntent& intent);
[[noreturn]] void RunSdManagement(daisy::desktop_devkit::Hardware& hw, const ManagementConfig& config);

} // namespace sdshare
