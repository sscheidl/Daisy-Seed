// Production main.cpp (performance personality) with fake board/USB and the REAL
// read-only SD stack: common/sd ro_sd.c + FatFs R0.16 over a card image that the
// e2e "Windows" step wrote through MSC. Shared by test_callback, test_golden and
// test_modulation; each is one translation unit around this file.
#pragma once
#include <cstring>
#include <fstream>
#include <iterator>
#define main firmware_main
#include "../src/main.cpp"
#undef main
#include "stm32h7xx_hal.h"
#include "util/bsp_sd_diskio.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)) {std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}} while(0)
extern "C" {
USBD_HandleTypeDef hUsbDeviceHS;
uint8_t __neb_file_start[1],__neb_file_end[1];
SD_HandleTypeDef hsd1;
}
// ---- fake SDMMC/BSP under the production disk layer ----
static std::vector<uint8_t> image;
static unsigned dma_reads=0, aborts=0, releases=0;
static uint32_t tick=0;
extern "C" {
uint32_t HAL_GetTick(void) {return ++tick;}
void HAL_NVIC_SetPriority(int,uint32_t,uint32_t) {}
int HAL_SD_Abort(SD_HandleTypeDef*) {std::abort();}  // Nebulae passes its own abort hook
int HAL_SD_DeInit(SD_HandleTypeDef* h) {h->State=HAL_SD_STATE_RESET;return 0;}
void SCB_CleanInvalidateDCache_by_Addr(uint32_t*,int32_t) {}
void SCB_InvalidateDCache_by_Addr(uint32_t*,int32_t) {}
uint8_t BSP_SD_Init(void) {hsd1.State=HAL_SD_STATE_READY;return MSD_OK;}
uint8_t BSP_SD_GetCardState(void) {return SD_TRANSFER_OK;}
void BSP_SD_GetCardInfo(DSY_SD_CardInfoTypeDef* info) {info->LogBlockNbr=uint32_t(image.size()/512);}
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t* data,uint32_t sector,uint32_t count) {
    ++dma_reads;
    if(uint64_t(sector+count)*512>image.size()) {BSP_SD_ErrorCallback();return MSD_OK;}
    std::memcpy(data,image.data()+uint64_t(sector)*512,size_t(count)*512);
    BSP_SD_ReadCpltCallback();return MSD_OK;
}
void neb_sd_abort(void) {++aborts;hsd1.State=HAL_SD_STATE_RESET;}
void neb_sd_release(void) {++releases;hsd1.State=HAL_SD_STATE_RESET;}
}
static volatile uint32_t intent_words[4];
volatile uint32_t* neb_intent_words() {return intent_words;}
void neb_run_sd_management(daisy::desktop_devkit::Hardware&,const neb::BootIntent&) {throw std::runtime_error("management");}

static std::array<float,neb::kBlock> input_l{},input_r{},output_l{},output_r{};
// Optional per-block hooks: the golden test feeds input and hashes the output.
inline void (*block_input)()=nullptr;
inline void (*block_output)()=nullptr;
inline void Block() {
    if(block_input) block_input();
    const float* input[]={input_l.data(),input_r.data()};float* output[]={output_l.data(),output_r.data()};
    const unsigned previous=dma_reads;
    Audio(input,output,neb::kBlock);
    CHECK(previous==dma_reads); // the audio callback never touches the SD card
    for(float x:output_l) CHECK(std::isfinite(x) && std::abs(x)<=.95f);
    if(block_output) block_output();
}
inline void Main() {PerformanceStep(daisy::System::GetNow());}
inline void Advance(unsigned n) {
    for(unsigned i=0;i<n;++i) {++daisy::System::now;PollControls(nullptr);if(i%5==4) Block();Main();}
    Block();
}
inline void Send(std::initializer_list<uint8_t> data) {std::vector<uint8_t> b(data);ReceiveUsb(b.data(),b.size(),nullptr);ServiceUsb();Block();}
inline std::vector<uint8_t> LastReply() {
    for(auto it=usb.sent.rbegin();it!=usb.sent.rend();++it)
        if(it->size()>6 && (*it)[0]==0xF0 && (*it)[2]==0x4E) return *it;
    return {};
}
inline void Settle(unsigned limit=20000) {
    for(unsigned i=0;i<limit && (banks.Busy() || loader.state!=neb::Loader::State::Idle || perf.mount_pending || perf.pending);++i) Advance(1);
    Advance(20);
}

inline void LoadCard(const char* path) {
    std::ifstream in(path,std::ios::binary);image.assign(std::istreambuf_iterator<char>(in),{});
    CHECK(!image.empty());
}
inline void InitSd() {
    RoSdConfig ro{};ro.bounce=sector_buffer;ro.bounce_bytes=sizeof sector_buffer;ro.transfer_timeout_ms=20;
    ro.card_present=[] {return CardPresent()?1:0;};ro.service=[] {ServiceUsb();};ro.abort=neb_sd_abort;
    CHECK(ro_sd_init(&ro)==0);
}
// Construction on poisoned SDRAM and a small file buffer, as in v0.1.
inline void BuildEngine(int16_t* file_buffer,size_t file_bytes,uint32_t live_capacity) {
    std::memset(engine_storage,0xa5,sizeof engine_storage);
    engine=new(engine_storage) neb::Engine;
    engine->Init(live_banks[0],live_banks[1],live_capacity,file_buffer,bridge);
    loader.Init(file,file_buffer,file_bytes,bridge);
}
// The part of InitPerformance() that does not need the linker's SDRAM symbols.
inline void Boot(int16_t* file_buffer,size_t file_bytes,uint32_t live_capacity) {
    BuildEngine(file_buffer,file_bytes,live_capacity);
    hw.Init();hw.pot[neb::Speed].value=.5f;hw.pot[neb::Pitch].value=.5f;hw.pot[neb::Size].value=1;
    InitSd();
#if NEBULAE_MOD
    InitModulation();
#endif
    usb_ready=true;usb.StartRx(ReceiveUsb,nullptr);hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;
    perf.mount_pending=true;perf.initial=true;
    Advance(30);CHECK(engine->ui.mode==1);
}
