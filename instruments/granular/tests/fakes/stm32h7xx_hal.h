#pragma once
#include <cstdint>
struct SD_HandleTypeDef {};
uint32_t HAL_GetTick();
void HAL_SD_Abort(SD_HandleTypeDef*);
void SCB_CleanInvalidateDCache_by_Addr(uint32_t*,int);
void SCB_InvalidateDCache_by_Addr(uint32_t*,int);
inline void HAL_NVIC_SetPriority(unsigned,unsigned,unsigned) {}
constexpr unsigned SDMMC1_IRQn=0;
