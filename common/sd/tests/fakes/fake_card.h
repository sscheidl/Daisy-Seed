// Host fake of the SDMMC1 card under common/sd's disk layer (ro_sd.c): an in-memory
// image behind libDaisy's BSP/HAL calls, with fault injection. Header-only; include it
// in exactly one translation unit of a test. Used by common/sd/tests/test_rw_sd.cpp and
// instruments/looper/tests/test_card.cpp.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include "stm32h7xx_hal.h"
#include "util/bsp_sd_diskio.h"

extern "C" SD_HandleTypeDef hsd1;
SD_HandleTypeDef hsd1;

namespace card {
enum Mode { Ok, WriteStartFails, WriteError, WriteNeverComplete, ProgrammingHangs };
std::vector<uint8_t> image;
Mode mode = Ok;
bool present = true;
int busy_polls = 0;          // card reports BUSY for this many CMD13 polls
int busy_after_write = 3;    // programming time in polls after every block write
long writes_left = -1;       // the card goes away after this many block writes; -1 = never
uint32_t tick = 0;
unsigned dma_writes = 0, dma_reads = 0, blocks_written = 0, aborts = 0, cleans = 0, services = 0;
std::vector<unsigned> write_counts;
const uint8_t* last_src = nullptr;
inline void Reset(const std::vector<uint8_t>& img) {
    image = img; mode = Ok; present = true; writes_left = -1; busy_polls = 0; busy_after_write = 3;
}
}  // namespace card

extern "C" {
uint32_t HAL_GetTick(void) { return ++card::tick; }
void HAL_NVIC_SetPriority(int, uint32_t, uint32_t) {}
int HAL_SD_Abort(SD_HandleTypeDef*) { ++card::aborts; return 0; }
int HAL_SD_DeInit(SD_HandleTypeDef* h) { h->State = HAL_SD_STATE_RESET; return 0; }
void SCB_CleanInvalidateDCache_by_Addr(uint32_t*, int32_t) {}
void SCB_InvalidateDCache_by_Addr(uint32_t*, int32_t) {}
void SCB_CleanDCache_by_Addr(uint32_t*, int32_t) { ++card::cleans; }
uint8_t BSP_SD_Init(void) {
    if(!card::present) return MSD_ERROR;
    hsd1.State = HAL_SD_STATE_READY;
    return MSD_OK;
}
uint8_t BSP_SD_GetCardState(void) {
    if(card::mode == card::ProgrammingHangs && card::busy_polls) return SD_TRANSFER_BUSY;
    if(card::busy_polls > 0) { --card::busy_polls; return SD_TRANSFER_BUSY; }
    return SD_TRANSFER_OK;
}
void BSP_SD_GetCardInfo(DSY_SD_CardInfoTypeDef* info) { info->LogBlockNbr = uint32_t(card::image.size() / 512); }
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t* data, uint32_t sector, uint32_t count) {
    ++card::dma_reads;
    if(!card::present || uint64_t(sector + count) * 512 > card::image.size()) { BSP_SD_ErrorCallback(); return MSD_OK; }
    std::memcpy(data, card::image.data() + uint64_t(sector) * 512, size_t(count) * 512);
    BSP_SD_ReadCpltCallback();
    return MSD_OK;
}
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t* data, uint32_t sector, uint32_t count) {
    ++card::dma_writes; card::write_counts.push_back(count);
    card::last_src = reinterpret_cast<const uint8_t*>(data);
    if(card::mode == card::WriteStartFails) return MSD_ERROR;
    if(card::mode == card::WriteNeverComplete) return MSD_OK;
    if(!card::present || uint64_t(sector + count) * 512 > card::image.size()) { BSP_SD_ErrorCallback(); return MSD_OK; }
    // Blocks are programmed one after the other; a removal cuts the transfer between blocks.
    for(uint32_t i = 0; i < count; ++i) {
        if(card::writes_left == 0) { card::present = false; BSP_SD_ErrorCallback(); return MSD_OK; }
        if(card::writes_left > 0) --card::writes_left;
        std::memcpy(card::image.data() + (uint64_t(sector) + i) * 512,
                    reinterpret_cast<const uint8_t*>(data) + size_t(i) * 512, 512);
        ++card::blocks_written;
    }
    if(card::mode == card::WriteError) { BSP_SD_ErrorCallback(); return MSD_OK; }
    card::busy_polls = card::mode == card::ProgrammingHangs ? 1 : card::busy_after_write;
    BSP_SD_WriteCpltCallback();
    return MSD_OK;
}
}
