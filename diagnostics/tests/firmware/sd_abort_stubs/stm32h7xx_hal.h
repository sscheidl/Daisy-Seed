#pragma once
#include <cstdint>
#include <string>
struct SDMMC_TypeDef { uint32_t MASK=0, IDMACTRL=0, DCTRL=0, STA=0; };
inline SDMMC_TypeDef controller;
#define SDMMC1 (&controller)
constexpr int SDMMC1_IRQn=49;
enum HAL_SD_StateTypeDef { HAL_SD_STATE_RESET, HAL_SD_STATE_READY, HAL_SD_STATE_BUSY };
constexpr uint32_t SD_CONTEXT_NONE=0, HAL_UNLOCKED=0;
struct SD_HandleTypeDef {
    SDMMC_TypeDef* Instance=nullptr;
    HAL_SD_StateTypeDef State=HAL_SD_STATE_RESET;
    uint32_t Context=0, Lock=0, ErrorCode=0;
};
inline bool irq_enabled=true, pending=true, reset_held=false, complete_on_disable=false;
inline SD_HandleTypeDef* active_handle=nullptr;
inline std::string operations;
inline void NVIC_DisableIRQ(int) {
    operations+='I'; irq_enabled=false;
    if(complete_on_disable && active_handle) active_handle->State=HAL_SD_STATE_READY;
}
inline void __DSB() { operations+='B'; }
inline void NVIC_ClearPendingIRQ(int) { operations+='P'; pending=false; }
inline void ForceReset() {
    operations+='R'; reset_held=true; controller={};
}
inline void ReleaseReset() { operations+='r'; reset_held=false; }
#define __HAL_RCC_SDMMC1_FORCE_RESET() ForceReset()
#define __HAL_RCC_SDMMC1_RELEASE_RESET() ReleaseReset()
