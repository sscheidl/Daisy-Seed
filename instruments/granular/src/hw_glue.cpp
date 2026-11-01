// SD management personality and board glue. Reuses the Diagnostics card reader
// (common/sd/reader) exactly as its hardware-tested 0.3.2 image wires it: SDMMC1 IDMA
// into one isolated 512-byte buffer, polled programming completion, RCC-reset abort,
// CDC on interfaces 0/1 and MSC on interface 2 (V0_2_ARCHITECTURE.md, D2).
#include "hw_glue.h"
#include "admin.h"
#include "bootloader_daisy.h"
#include "card_reader.h"
#include "reader_abort.h"
#include "reader_dma.h"
#include "reader_usb.h"
#include "per/sdmmc.h"
extern "C" {
#include "util/bsp_sd_diskio.h"
}
#include "usbd_core.h"
#include "version.h"
#include <atomic>

extern "C" USBD_HandleTypeDef hUsbDeviceHS;
extern SD_HandleTypeDef hsd1;

extern "C" void neb_sd_abort(void) { diag::AbortReaderSd(hsd1); }
extern "C" void neb_sd_release(void) {
    diag::AbortReaderSd(hsd1);  // no transfer can be running here; reset is unconditional
}
volatile uint32_t* neb_intent_words() {
    static bool ready = false;
    if(!ready) { daisy::System::InitBackupSram(); ready = true; }
    return reinterpret_cast<volatile uint32_t*>(neb::kBootIntentAddress);
}

namespace {
constexpr uint32_t kUsbIrqPriority = 1;  // audio would be 0; SDMMC1 is 2
daisy::desktop_devkit::Hardware* board = nullptr;
daisy::UsbHandle cdc;
daisy::SdmmcHandler sdmmc;
std::atomic<uint16_t> buttons{0};
std::atomic<bool> card_present{false};
neb::BootIntent returning;

// IDMA cannot reach DTCM (.bss); this sector is AXI SRAM, cache-line aligned (memory.ld).
alignas(32) uint8_t reader_dma_buffer[diag::ReaderDma::kBytes] __attribute__((section(".neb_sd")));
class ReaderBackend final : public diag::ReaderDmaBackend {
  public:
    bool Available() override { return card_present.load(std::memory_order_relaxed) && !ReaderUsbInterrupted(); }
    uint32_t Now() override { return HAL_GetTick(); }
    bool StartRead(uint8_t* b, uint32_t lba) override { return HAL_SD_ReadBlocks_DMA(&hsd1, b, lba, 1) == HAL_OK; }
    bool StartWrite(uint8_t* b, uint32_t lba) override { return HAL_SD_WriteBlocks_DMA(&hsd1, b, lba, 1) == HAL_OK; }
    bool Busy() override { return hsd1.State == HAL_SD_STATE_BUSY; }
    uint32_t Error() override { return hsd1.ErrorCode; }
    bool CardReady() override { return HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER; }
    void Clean(uint8_t* b) override { SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(b), 512); }
    void Invalidate(uint8_t* b) override { SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(b), 512); }
    void Abort() override { diag::AbortReaderSd(hsd1); }
};
ReaderBackend backend;
diag::ReaderDma dma(backend, reader_dma_buffer);
class ReaderCard final : public diag::CardDevice {
  public:
    bool Open(uint32_t& sectors) override {
        dma.ResetStatus();
        if(hsd1.State != HAL_SD_STATE_RESET) HAL_SD_DeInit(&hsd1);
        daisy::SdmmcHandler::Config cfg; cfg.Defaults();
        cfg.speed = daisy::SdmmcHandler::Speed::STANDARD;
        sdmmc.Init(cfg);
        if(BSP_SD_Init() != MSD_OK || ReaderUsbInterrupted()) return false;
        NVIC_SetPriority(SDMMC1_IRQn, 2);
        HAL_SD_CardInfoTypeDef info{};
        if(HAL_SD_GetCardInfo(&hsd1, &info) != HAL_OK || info.LogBlockSize != 512) return false;
        sectors = info.LogBlockNbr;
        return sectors != 0;
    }
    bool Read(uint32_t lba, uint8_t* block) override { return dma.Read(lba, block); }
    bool Write(uint32_t lba, const uint8_t* block) override { return dma.Write(lba, block); }
    bool Sync() override { return dma.Sync(); }
    void Close() override {
        backend.Abort();
        if(hsd1.Instance == SDMMC1) HAL_SD_DeInit(&hsd1);
    }
};
ReaderCard reader_card;
diag::CardReader reader(reader_card);

class Port final : public neb::AdminPort {
  public:
    // libDaisy's CDC_Transmit_HS dereferences class data a USB reset may free:
    // check and start with the USB interrupt masked (as the Diagnostics CdcSink).
    neb::AdminSend Send(const uint8_t* data, size_t size) override {
        const uint32_t saved = __get_BASEPRI();
        __set_BASEPRI(kUsbIrqPriority << (8u - __NVIC_PRIO_BITS));
        __DSB(); __ISB();
        neb::AdminSend r;
        if(hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED || hUsbDeviceHS.pClassData == nullptr)
            r = neb::AdminSend::NotReady;
        else
            r = cdc.TransmitExternal(const_cast<uint8_t*>(data), size) == daisy::UsbHandle::Result::OK
                    ? neb::AdminSend::Ok : neb::AdminSend::Busy;
        __set_BASEPRI(saved);
        return r;
    }
    neb::AdminInputs Inputs() override {
        neb::AdminInputs in;
        in.buttons = buttons.load(std::memory_order_relaxed);
        for(int i = 0; i < 8; ++i) {
            const float v = board->pot[i].Value();
            in.pots[i] = uint16_t(v <= 0.f ? 0 : v >= 1.f ? 10000 : uint16_t(v * 10000.f + .5f));
            in.pots_raw[i] = board->seed.adc.GetMux(0, uint8_t(i));
        }
        in.sw17_up = board->tog_sw17.Pressed();
        const int p = board->tog_sw18.Read();
        in.sw18 = p == daisy::Switch3::POS_UP ? 0 : p == daisy::Switch3::POS_DOWN ? 2 : 1;
        in.card = card_present.load(std::memory_order_relaxed);
        return in;
    }
    bool ReaderIdle() override { return ReaderUsbIdle(); }
    neb::ReaderIo LastIo() override {
        const auto& s = dma.Status();
        neb::ReaderIo io;
        io.operation = s.operation; io.fault = uint8_t(s.fault); io.hal_error = s.hal_error;
        io.lba = s.lba; io.elapsed_ms = s.elapsed_ms;
        return io;
    }
    void ApplyLeds(uint16_t mask) override {
        for(int i = 0; i < 16; ++i) board->SetLed(i, mask & (1u << i) ? 1.f : 0.f);
        board->UpdateLeds();
    }
    void RestartPerformance() override {
        // Only reached from LOCAL: the card reader has closed the card already.
        for(int i = 0; i < 16; ++i) board->SetLed(i, 0.f);
        board->UpdateLeds(); board->UpdateLeds();
        if(hsd1.Instance == SDMMC1 && hsd1.State != HAL_SD_STATE_RESET) HAL_SD_DeInit(&hsd1);
        neb::StoreBootIntent(neb_intent_words(), returning);
        daisy::System::Delay(10);
        daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_SKIP_TIMEOUT);
    }
    void EnterBootloader() override { bootloader::Enter(*board); }
};
Port port;
neb::Admin* admin = nullptr;

void PollInputs(void*) {
    board->UpdateAllControls();
    uint16_t held = 0;
    for(int i = 0; i < 16; ++i) if(board->button[i].Pressed()) held |= uint16_t(1u << i);
    buttons.store(held, std::memory_order_relaxed);
    card_present.store(board->sd_detect.Pressed(), std::memory_order_relaxed);
}
void UsbReceive(uint8_t* buf, uint32_t* len) {
    if(admin && buf && len) admin->OnUsbReceive(buf, *len);  // copy only
}
const char* BootloaderName() {
    switch(daisy::System::GetBootloaderVersion()) {
    case daisy::System::BootInfo::Version::LT_v6_0: return "LT_v6_0";
    case daisy::System::BootInfo::Version::NONE: return "NONE";
    case daisy::System::BootInfo::Version::v6_0: return "v6_0";
    case daisy::System::BootInfo::Version::v6_1: return "v6_1";
    default: return "unknown";
    }
}
} // namespace

void neb_run_sd_management(daisy::desktop_devkit::Hardware& hw, const neb::BootIntent& intent) {
    board = &hw;
    returning.personality = neb::Personality::Performance;
    returning.bank = intent.bank;
    returning.slot = intent.slot;
    static neb::Admin responder(reader, port, NEBULAE_VERSION, BootloaderName(), intent.bank, intent.slot);
    admin = &responder;
    for(int i = 0; i < 16; ++i) hw.SetLed(i, 0.f);
    hw.UpdateLeds();
    for(int i = 0; i < 50; ++i) { hw.UpdateAllControls(); daisy::System::Delay(1); }
    card_present.store(hw.sd_detect.Pressed());
    // No audio, no FatFs in this personality. The MSC LUN reports NOT READY until the
    // control panel sends READER a=SHARE.
    if(!InitReaderUsb(reader)) while(true) { hw.seed.SetLed(true); }
    NVIC_SetPriority(OTG_HS_IRQn, kUsbIrqPriority);
    NVIC_SetPriority(OTG_HS_EP1_IN_IRQn, kUsbIrqPriority);
    NVIC_SetPriority(OTG_HS_EP1_OUT_IRQn, kUsbIrqPriority);
    cdc.SetReceiveCallback(UsbReceive, daisy::UsbHandle::FS_EXTERNAL);
    hw.StartLowPriorityCallback(PollInputs, 1000);
    while(true) {
        const uint32_t now = daisy::System::GetNow();
        PollReaderUsb();
        reader.Observe(card_present.load(std::memory_order_relaxed));
        responder.Step(now);
        hw.seed.SetLed(((now / 125u) & 1u) != 0);  // Seed LED: fast blink in SD management
    }
}
