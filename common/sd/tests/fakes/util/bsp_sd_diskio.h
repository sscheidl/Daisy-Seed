/* Host fake: the libDaisy BSP SD declarations used by common/sd/ro_sd.c. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t LogBlockNbr; } DSY_SD_CardInfoTypeDef;
#define MSD_OK ((uint8_t)0x00)
#define MSD_ERROR ((uint8_t)0x01)
#define SD_TRANSFER_OK ((uint8_t)0x00)
#define SD_TRANSFER_BUSY ((uint8_t)0x01)
uint8_t BSP_SD_Init(void);
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t* data, uint32_t sector, uint32_t count);
uint8_t BSP_SD_GetCardState(void);
void BSP_SD_GetCardInfo(DSY_SD_CardInfoTypeDef* info);
void BSP_SD_ReadCpltCallback(void);
void BSP_SD_ErrorCallback(void);
#ifdef __cplusplus
}
#endif
