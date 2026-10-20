#include "../src/sd_driver.c"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
SD_HandleTypeDef hsd1;
static unsigned mode=0,reads=0,aborts=0,services=0,cleans=0,invalidates=0;
static bool present=true;
static uint32_t tick=0;
void neb_sd_service() {++services;}
int neb_card_present() {return present;}
unsigned BSP_SD_Init() {return 0;}
unsigned BSP_SD_ReadBlocks_DMA(uint32_t* buffer,uint32_t sector,unsigned count) {
    assert(count==1 && reinterpret_cast<uintptr_t>(buffer)%32==0);++reads;
    assert(reinterpret_cast<uint8_t*>(buffer)==sector_buffer);
    if(mode==3) return 1;
    if(mode==2) {BSP_SD_ErrorCallback();return 0;}
    if(mode==1) return 0; // timeout
    std::memset(buffer,int(sector&255),512);BSP_SD_ReadCpltCallback();return 0;
}
void BSP_SD_GetCardInfo(DSY_SD_CardInfoTypeDef* info) {*info=DSY_SD_CardInfoTypeDef{};}
uint32_t HAL_GetTick() {return tick++;}
void HAL_SD_Abort(SD_HandleTypeDef*) {++aborts;}
void SCB_CleanInvalidateDCache_by_Addr(uint32_t* p,int n) {assert(reinterpret_cast<uint8_t*>(p)==sector_buffer && n==512);++cleans;}
void SCB_InvalidateDCache_by_Addr(uint32_t* p,int n) {assert(reinterpret_cast<uint8_t*>(p)==sector_buffer && n==512);++invalidates;}
int main() {
    assert(SD_Driver.initialize(0)==0);
    std::vector<uint8_t> pcm(512*128+2,0xa5);
    assert(SD_Driver.read(0,pcm.data()+1,40,128)==RES_OK);
    assert(reads==128 && services==128 && cleans==128 && invalidates==128);
    assert(pcm.front()==0xa5 && pcm.back()==0xa5);
    for(unsigned i=0;i<128*512;++i) assert(pcm[i+1]==uint8_t(40+i/512));
    assert(SD_Driver.read(0,pcm.data(),0,129)==RES_NOTRDY);
    assert(SD_Driver.write(0,pcm.data(),0,1)==RES_WRPRT);
    for(unsigned m:{1u,2u,3u}) {
        mode=m;SD_Driver.initialize(0);
        const unsigned start=tick;assert(SD_Driver.read(0,pcm.data(),0,1)==RES_ERROR);
        assert(tick-start<25);
    }
    assert(aborts==2);
    present=false;assert(SD_Driver.status(0)==STA_NOINIT);assert(SD_Driver.initialize(0)==STA_NOINIT);
    std::puts("PASS production SD driver: aligned AXI bounce, 128-sector bounds, cache protocol, unaligned DTCM destination, USB service, timeout/error/absent-card and read-only writes");
}
