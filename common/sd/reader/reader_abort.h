// Main-loop error recovery. Never wait for a card response or HAL_SD_Abort.
#pragma once
#include "stm32h7xx_hal.h"

namespace diag {
inline void AbortReaderSd(SD_HandleTypeDef& sd) {
    if(sd.Instance != SDMMC1) return; // not initialized: no reader IDMA can be active
    // Quiesce only SDMMC1. Audio/USB and the system tick remain interruptible.
    // A completion immediately before this mask is harmless: reset is unconditional.
    NVIC_DisableIRQ(SDMMC1_IRQn);
    sd.Instance->MASK = 0;
    __DSB();
    // RCC reset stops the data path AND IDMA even when a removed card cannot
    // acknowledge CMD12. Merely clearing IDMACTRL while DPSM is active is unsafe.
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __DSB();
    __HAL_RCC_SDMMC1_RELEASE_RESET();
    __DSB();
    NVIC_ClearPendingIRQ(SDMMC1_IRQn);
    sd.Context = SD_CONTEXT_NONE;
    sd.Lock = HAL_UNLOCKED;
    sd.State = HAL_SD_STATE_RESET;
    // Preserve ErrorCode. RESET rejects new HAL transfers until full card init.
    // Leave the SD IRQ disabled; HAL_SD_MspInit enables it on explicit reopen.
}
} // namespace diag
