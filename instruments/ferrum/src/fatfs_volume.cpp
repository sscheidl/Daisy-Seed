// SPDX-License-Identifier: MIT
// Board wiring of the card: libDaisy's SdmmcHandler (4-bit, STANDARD speed) and the
// shared writable SD driver (common/sd, FatFs R0.16 with exFAT; SD_WRITABLE = 1).
// SDMMC1 IDMA only ever sees `bounce`, an isolated, cache-line aligned SDRAM buffer
// (DTCM, where .bss lives, is not reachable by IDMA); FatFs objects may live anywhere.
// Faults use the RCC-reset abort that the Diagnostics card reader proved on the board.
#include "card_volume.h"
#include "fat_volume.h"
#include "reader_abort.h"
#include "ro_sd.h"
#include "daisy_seed.h"

extern SD_HandleTypeDef hsd1;  // libDaisy per/sdmmc.cpp

namespace looper {
namespace {

// 64 sectors per transfer: a 32 KB LoopStore chunk is one multi-block write.
alignas(32) uint8_t DSY_SDRAM_BSS bounce[32768];
daisy::SdmmcHandler sd;
FatVolume volume;

uint32_t Now() { return daisy::System::GetNow(); }
void Abort() { diag::AbortReaderSd(hsd1); }

}  // namespace

bool InitCard(bool (*present)()) {
    daisy::SdmmcHandler::Config config;
    config.Defaults();
    config.speed = daisy::SdmmcHandler::Speed::STANDARD;
    if (sd.Init(config) != daisy::SdmmcHandler::Result::OK)
        return false;
    static bool (*card_present)() = nullptr;
    card_present = present;
    RoSdConfig ro{};
    ro.bounce = bounce;
    ro.bounce_bytes = sizeof(bounce);
    ro.transfer_timeout_ms = 250;  // one 32 KB read including the card's transfer state
    ro.write_timeout_ms = 1000;    // one 32 KB write including programming
    ro.budget_ms = 0;
    ro.card_present = [] { return card_present && card_present() ? 1 : 0; };
    ro.service = nullptr;          // as before: USB MIDI is serviced between store steps
    ro.abort = Abort;
    if (ro_sd_init(&ro) != 0)
        return false;
    volume.Init(present, Now);
    return true;
}

Volume& CardVolume() { return volume; }

// main.cpp calls this from the main loop after StopAudio(): unmount first (closes any open
// file, so its data reaches the card), then the RCC reset, so the card reader starts from a
// clean SDMMC1. No transfer can be running at that point.
void ReleaseCard() {
    volume.Unmount();
    diag::AbortReaderSd(hsd1);
}

}  // namespace looper
