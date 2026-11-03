/* FatFs disk layer on libDaisy's BSP/HAL for SDMMC1.
 * Combines the Nebulae bridge (isolated IDMA bounce buffer, short per-transfer
 * timeout, card check and service hook between transfers) with the Diagnostics
 * driver (re-initialization per mount, overall time budget).
 * libDaisy's SD_Driver/ff_gen_drv are not used.
 *
 * Two configurations, chosen by the ffconf.h on the include path (sd.mk):
 *  - read-only (ffconf.h, FF_FS_READONLY 1): STA_PROTECT, no write path at all;
 *  - writable (rw/ffconf.h, FF_FS_READONLY 0): disk_write through the same bounce
 *    buffer with IDMA, then a bounded wait for programming completion (card back in
 *    transfer state) before RES_OK; CTRL_SYNC waits for the same state.
 */
#include "ro_sd.h"
#include "diskio.h"
#include "stm32h7xx_hal.h"
#include "util/bsp_sd_diskio.h"
#include <string.h>

#if FF_MIN_SS != 512 || FF_MAX_SS != 512 || FF_VOLUMES != 1 || !defined(RO_SD_FFCONF)
#error "common/sd requires its own ffconf.h (or rw/ffconf.h): 512-byte sectors, one volume"
#endif
#if FF_FS_READONLY
#define RO_SD_PROTECT STA_PROTECT
#else
#define RO_SD_PROTECT 0
#endif

#define RO_SD_SECTOR 512u

extern SD_HandleTypeDef hsd1; /* libDaisy src/per/sdmmc.cpp */

static RoSdConfig config;
static int configured = 0;
static DSTATUS status = STA_NOINIT | RO_SD_PROTECT;
static uint32_t deadline = 0;
static volatile uint32_t done, failed;

void BSP_SD_ReadCpltCallback(void) { done = 1; }
#if !FF_FS_READONLY
void BSP_SD_WriteCpltCallback(void) { done = 1; }
#endif
void BSP_SD_ErrorCallback(void) { failed = 1; }

static int Present(void) { return !config.card_present || config.card_present(); }

static int BudgetLeft(void) {
    return !config.budget_ms || (int32_t)(deadline - HAL_GetTick()) > 0;
}

static int Expired(uint32_t start) {
    return HAL_GetTick() - start >= config.transfer_timeout_ms || !BudgetLeft();
}

#if !FF_FS_READONLY
static uint32_t WriteTimeout(void) {
    return config.write_timeout_ms ? config.write_timeout_ms : 1000u;
}
/* Programming completion: the card answers CMD13 with the transfer state again. */
static int WaitProgrammed(uint32_t timeout) {
    const uint32_t start = HAL_GetTick();
    while(BSP_SD_GetCardState() != SD_TRANSFER_OK) {
        if(!Present() || failed || HAL_GetTick() - start >= timeout || !BudgetLeft()) return 0;
    }
    return 1;
}
#endif

static DRESULT Fail(void) {
    if(config.abort) config.abort();
    else HAL_SD_Abort(&hsd1);
    status |= STA_NOINIT;
    return RES_ERROR;
}

int ro_sd_init(const RoSdConfig* c) {
    configured = 0;
    if(!c || !c->bounce || ((uintptr_t)c->bounce & 31u) || c->bounce_bytes < RO_SD_SECTOR
       || c->bounce_bytes % RO_SD_SECTOR || !c->transfer_timeout_ms)
        return -1;
    config = *c;
    configured = 1;
    status = STA_NOINIT | RO_SD_PROTECT;
    return 0;
}

void ro_sd_restart_budget(void) { deadline = HAL_GetTick() + config.budget_ms; }

FRESULT ro_sd_mount(FATFS* fs) {
    if(!configured) return FR_NOT_READY;
    if(hsd1.State != HAL_SD_STATE_RESET) HAL_SD_DeInit(&hsd1); // fresh start after a swap
    status = STA_NOINIT | RO_SD_PROTECT;
    ro_sd_restart_budget();
    return f_mount(fs, "0:", 1);
}

void ro_sd_unmount(void) { f_mount(0, "0:", 0); }

DSTATUS ro_sd_status(void) {
    return Present() ? status : (DSTATUS)(STA_NOINIT | STA_NODISK | RO_SD_PROTECT);
}

DSTATUS disk_status(BYTE drive) {
    return drive ? (DSTATUS)(STA_NOINIT | RO_SD_PROTECT) : ro_sd_status();
}

DSTATUS disk_initialize(BYTE drive) {
    if(drive || !configured) return STA_NOINIT | RO_SD_PROTECT;
    status = STA_NOINIT | RO_SD_PROTECT;
    if(!Present()) return ro_sd_status();
    if(BSP_SD_Init() == MSD_OK) {
        HAL_NVIC_SetPriority(SDMMC1_IRQn, 2, 0); // audio DMA stays at priority 0
        status = RO_SD_PROTECT;
    }
    return status;
}

DRESULT disk_read(BYTE drive, BYTE* buffer, LBA_t sector, UINT count) {
    if(drive || !buffer || !count) return RES_PARERR;
    if(ro_sd_status() & STA_NOINIT) return RES_NOTRDY;
    const UINT chunk = config.bounce_bytes / RO_SD_SECTOR;
    while(count) {
        const UINT n = count < chunk ? count : chunk;
        const uint32_t bytes = n * RO_SD_SECTOR;
        if(!Present()) { status |= STA_NOINIT; return RES_NOTRDY; }
        if(!BudgetLeft()) return RES_ERROR; // nothing started; the card stays usable
        const uint32_t start = HAL_GetTick();
        while(BSP_SD_GetCardState() != SD_TRANSFER_OK)
            if(Expired(start)) return Fail();
        done = failed = 0;
        SCB_CleanInvalidateDCache_by_Addr((uint32_t*)config.bounce, (int32_t)bytes);
        if(BSP_SD_ReadBlocks_DMA((uint32_t*)config.bounce, (uint32_t)sector, n) != MSD_OK)
            return Fail();
        while(!done && !failed)
            if(Expired(start)) return Fail();
        if(failed) return Fail();
        SCB_InvalidateDCache_by_Addr((uint32_t*)config.bounce, (int32_t)bytes);
        memcpy(buffer, config.bounce, bytes);
        buffer += bytes; sector += n; count -= n;
        if(config.service) config.service();
    }
    return RES_OK;
}

#if !FF_FS_READONLY
DRESULT disk_write(BYTE drive, const BYTE* buffer, LBA_t sector, UINT count) {
    if(drive || !buffer || !count) return RES_PARERR;
    if(ro_sd_status() & STA_NOINIT) return RES_NOTRDY;
    const UINT chunk = config.bounce_bytes / RO_SD_SECTOR;
    while(count) {
        const UINT n = count < chunk ? count : chunk;
        const uint32_t bytes = n * RO_SD_SECTOR;
        if(!Present()) { status |= STA_NOINIT; return RES_NOTRDY; }
        if(!BudgetLeft()) return RES_ERROR; // nothing started
        const uint32_t start = HAL_GetTick();
        while(BSP_SD_GetCardState() != SD_TRANSFER_OK)
            if(Expired(start)) return Fail();
        memcpy(config.bounce, buffer, bytes);
        SCB_CleanDCache_by_Addr((uint32_t*)config.bounce, (int32_t)bytes);
        done = failed = 0;
        if(BSP_SD_WriteBlocks_DMA((uint32_t*)config.bounce, (uint32_t)sector, n) != MSD_OK)
            return Fail();
        while(!done && !failed)
            if(!Present() || HAL_GetTick() - start >= WriteTimeout() || !BudgetLeft()) return Fail();
        /* DMA completion is not programming completion: wait until the card is ready. */
        if(failed || !WaitProgrammed(WriteTimeout())) return Fail();
        buffer += bytes; sector += n; count -= n;
        if(config.service) config.service();
    }
    return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE drive, BYTE cmd, void* data) {
    if(drive) return RES_PARERR;
    if(ro_sd_status() & STA_NOINIT) return RES_NOTRDY;
    switch(cmd) {
#if FF_FS_READONLY
    case CTRL_SYNC: return RES_OK;
#else
    case CTRL_SYNC: failed = 0; return WaitProgrammed(WriteTimeout()) ? RES_OK : Fail();
#endif
    case GET_SECTOR_SIZE: *(WORD*)data = RO_SD_SECTOR; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD*)data = 1; return RES_OK;
    case GET_SECTOR_COUNT: {
        DSY_SD_CardInfoTypeDef info;
        BSP_SD_GetCardInfo(&info);
        *(LBA_t*)data = info.LogBlockNbr;
        return RES_OK;
    }
    default: return RES_PARERR;
    }
}
