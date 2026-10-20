#pragma once
#include <cstdint>
constexpr unsigned MSD_OK=0;
struct DSY_SD_CardInfoTypeDef {uint32_t LogBlockNbr=12345;};
unsigned BSP_SD_Init();
unsigned BSP_SD_ReadBlocks_DMA(uint32_t*,uint32_t,unsigned);
void BSP_SD_GetCardInfo(DSY_SD_CardInfoTypeDef*);
