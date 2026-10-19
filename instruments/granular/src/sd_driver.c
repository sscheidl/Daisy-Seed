/* Application-specific, read-only SDMMC1 IDMA bridge.
 * FatFS buffers may live in DTCM; IDMA only sees this isolated AXI SRAM sector.
 * No copies from ST's sd_diskio driver. libDaisy BSP/HAL remain unchanged.
 */
#include "ff_gen_drv.h"
#include "util/bsp_sd_diskio.h"
#include "stm32h7xx_hal.h"
#include <string.h>
extern SD_HandleTypeDef hsd1;
extern void neb_sd_service(void);
extern int neb_card_present(void);
static uint8_t sector_buffer[512] __attribute__((section(".neb_sd"),aligned(32)));
static volatile uint32_t done, failed;
static DSTATUS status=STA_NOINIT;
void BSP_SD_ReadCpltCallback(void) { done=1; }
void BSP_SD_ErrorCallback(void) { failed=1; }
static DSTATUS Initialize(BYTE drive) {
    (void)drive;
    status=STA_NOINIT;
    if(neb_card_present() && BSP_SD_Init()==MSD_OK) {
        HAL_NVIC_SetPriority(SDMMC1_IRQn,2,0); // audio DMA remains priority 0
        status=0;
    }
    return status;
}
static DSTATUS Status(BYTE drive) {(void)drive;return neb_card_present()?status:STA_NOINIT;}
static DRESULT Read(BYTE drive,BYTE* buffer,DWORD sector,UINT count) {
    (void)drive;
    if(status || !count || count>128) return RES_NOTRDY;
    for(UINT i=0;i<count;++i) {
        if(!neb_card_present()) {status=STA_NOINIT;return RES_NOTRDY;}
        done=failed=0;
        SCB_CleanInvalidateDCache_by_Addr((uint32_t*)sector_buffer,512);
        if(BSP_SD_ReadBlocks_DMA((uint32_t*)sector_buffer,sector+i,1)!=MSD_OK) return RES_ERROR;
        const uint32_t start=HAL_GetTick();
        while(!done && !failed && HAL_GetTick()-start<20) {}
        if(!done || failed) {
            HAL_SD_Abort(&hsd1); status=STA_NOINIT;return RES_ERROR;
        }
        SCB_InvalidateDCache_by_Addr((uint32_t*)sector_buffer,512);
        memcpy(buffer+512*i,sector_buffer,512);
        neb_sd_service(); // USB service between sectors; never re-enters FatFS.
    }
    return RES_OK;
}
static DRESULT Write(BYTE d,const BYTE* b,DWORD s,UINT n) {(void)d;(void)b;(void)s;(void)n;return RES_WRPRT;}
static DRESULT Ioctl(BYTE d,BYTE cmd,void* p) {
    (void)d; DSY_SD_CardInfoTypeDef info;
    if(status) return RES_NOTRDY;
    if(cmd==CTRL_SYNC) return RES_OK;
    BSP_SD_GetCardInfo(&info);
    if(cmd==GET_SECTOR_COUNT) *(DWORD*)p=info.LogBlockNbr;
    else if(cmd==GET_SECTOR_SIZE) *(WORD*)p=512;
    else if(cmd==GET_BLOCK_SIZE) *(DWORD*)p=1;
    else return RES_PARERR;
    return RES_OK;
}
const Diskio_drvTypeDef SD_Driver={Initialize,Status,Read,Write,Ioctl};
