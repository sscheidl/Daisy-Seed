/* Host fake: only the HAL/CMSIS parts used by common/sd/ro_sd.c. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { HAL_SD_STATE_RESET = 0, HAL_SD_STATE_READY = 1, HAL_SD_STATE_BUSY = 3 } HAL_SD_StateTypeDef;
typedef struct { HAL_SD_StateTypeDef State; } SD_HandleTypeDef;
#define SDMMC1_IRQn 49
uint32_t HAL_GetTick(void);
void HAL_NVIC_SetPriority(int irq, uint32_t preempt, uint32_t sub);
int HAL_SD_Abort(SD_HandleTypeDef* hsd);
int HAL_SD_DeInit(SD_HandleTypeDef* hsd);
void SCB_CleanInvalidateDCache_by_Addr(uint32_t* addr, int32_t size);
void SCB_InvalidateDCache_by_Addr(uint32_t* addr, int32_t size);
#ifdef __cplusplus
}
#endif
