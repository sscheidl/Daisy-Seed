// Daisy Seed3 Desktop Dev Kit - hardware diagnostic firmware.
// Hardware glue only: panel controls through the official desktop_devkit::Hardware
// class, USB CDC on carrier port J7 and a read-only microSD driver. The protocol and
// main-loop logic live in diag_app.* / diag_core.* and are tested on the host.
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "per/sdmmc.h"
#include "usbd_core.h"
#include "ff.h"
#include "ff_gen_drv.h"
#include "diag_app.h"
#include "reader_abort.h"
#include <atomic>
#include <cmath>
#include <cstring>
#ifdef DIAG_CARD_READER
#include "reader_usb.h"
#include "reader_dma.h"
#endif

extern "C" {
#include "util/bsp_sd_diskio.h"
}

using namespace daisy;

extern "C" USBD_HandleTypeDef hUsbDeviceHS;  // libDaisy src/hid/usb.cpp
extern SD_HandleTypeDef hsd1;                // libDaisy src/per/sdmmc.cpp

namespace {
desktop_devkit::Hardware hw;
UsbHandle usb;

// ---- Control sampling (audio callback, 1 kHz) -----------------------------------
diag::ButtonTracker buttons;
diag::Switch3Debouncer sw18;
diag::GateTracker gates;
diag::Inputs callback_inputs;  // written in the callback, copied with IRQs masked
std::atomic<uint32_t> control_ticks{0};

diag::Sw18 MapSwitch3(int pos) {
    switch(pos) {
        case Switch3::POS_UP: return diag::Sw18::Up;
        case Switch3::POS_DOWN: return diag::Sw18::Down;
        default: return diag::Sw18::Center;
    }
}

// ---- Audio: test tone, peak meters and snapshot capture (firmware 0.4.0) ----------
// The main loop writes the tone settings; the callback reads them once per block.
// `tone_until_ms` is refreshed by the main loop while a host session is active: a
// blocked or dead main loop (or a vanished host) therefore mutes the tone by itself.
std::atomic<uint16_t> tone_amplitude{0};  // 1/10000 FS, 0 = off
std::atomic<uint16_t> tone_freq_hz{1000};
std::atomic<uint8_t> tone_channels{2};    // 0 = L, 1 = R, 2 = both
std::atomic<uint32_t> tone_until_ms{0};
float tone_phase = 0.f;
// Peak |sample| per channel since the main loop last read them (1/10000 FS).
std::atomic<uint16_t> peak_in[diag::kNumAudio];
std::atomic<uint16_t> peak_out[diag::kNumAudio];
// Snapshot: interleaved L/R int16 of the inputs. States: 0 idle, 1 armed (waiting for
// the trigger), 2 capturing, 3 ready. The buffer is CPU-only (no DMA), so DTCM is fine.
enum ScopeState : uint8_t { kScopeIdle = 0, kScopeArmed, kScopeCapturing, kScopeReady };
std::atomic<uint8_t> scope_state{kScopeIdle};
std::atomic<uint16_t> scope_frames{0};
std::atomic<bool> scope_trigger{true};
uint32_t scope_armed_ms = 0;
uint16_t scope_fill = 0;
float scope_prev_sample = 0.f;
int16_t scope_buffer[diag::kScopeMaxFrames * diag::kNumAudio];

inline uint16_t PeakUnits(float peak) {
    const float units = peak * 10000.f + 0.5f;
    return units >= 10000.f ? 10000u : static_cast<uint16_t>(units);
}

inline void RaisePeak(std::atomic<uint16_t>& slot, uint16_t value) {
    if(value > slot.load(std::memory_order_relaxed))  // single writer: the callback
        slot.store(value, std::memory_order_relaxed);
}

inline int16_t ToInt16(float sample) {
    const float scaled = sample * 32767.f;
    return scaled >= 32767.f ? 32767 : scaled <= -32768.f ? -32768 : static_cast<int16_t>(scaled);
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    hw.UpdateAllControls();
    for(size_t i = 0; i < diag::kNumButtons; ++i)
        buttons.Update(i, hw.button[i].RisingEdge(), hw.button[i].FallingEdge());
    sw18.Update(MapSwitch3(hw.tog_sw18.Read()));

    callback_inputs.buttons = buttons.Held();
    for(size_t i = 0; i < diag::kNumButtons; ++i)
        callback_inputs.press_count[i] = buttons.PressCount(i);
    callback_inputs.sw17_up = hw.tog_sw17.Pressed();
    callback_inputs.sw18 = sw18.Position();
    callback_inputs.sd_card = hw.sd_detect.Pressed();
    callback_inputs.jack_left = hw.left_detect.Pressed();
    callback_inputs.jack_right = hw.right_detect.Pressed();
    // One read per gate and tick: state and edge come from the same sample.
    for(size_t i = 0; i < diag::kNumGates; ++i)
        gates.Update(i, hw.gate_in[i].State());
    callback_inputs.gates = gates.States();
    for(size_t i = 0; i < diag::kNumGates; ++i)
        callback_inputs.gate_count[i] = gates.Count(i);
    control_ticks.fetch_add(1, std::memory_order_relaxed);

    // Test tone (TONE command). Silent unless the main loop keeps refreshing the deadline.
    const uint32_t now_ms = System::GetNow();
    const bool tone_alive = static_cast<int32_t>(tone_until_ms.load(std::memory_order_relaxed) - now_ms) > 0;
    const float amplitude = tone_alive ? tone_amplitude.load(std::memory_order_relaxed) / 10000.f : 0.f;
    const uint8_t channels = tone_channels.load(std::memory_order_relaxed);
    const float phase_step = 2.f * 3.14159265f * tone_freq_hz.load(std::memory_order_relaxed)
                             / hw.seed.AudioSampleRate();
    float peak_l = 0.f, peak_r = 0.f, peak_ol = 0.f, peak_or = 0.f;
    for(size_t i = 0; i < size; ++i) {
        float sample = 0.f;
        if(amplitude > 0.f) {
            sample = amplitude * sinf(tone_phase);
            tone_phase += phase_step;
            if(tone_phase >= 2.f * 3.14159265f)
                tone_phase -= 2.f * 3.14159265f;
        }
        out[0][i] = channels != 1 ? sample : 0.f;
        out[1][i] = channels != 0 ? sample : 0.f;
        const float l = fabsf(in[0][i]), r = fabsf(in[1][i]);
        if(l > peak_l) peak_l = l;
        if(r > peak_r) peak_r = r;
        if(fabsf(out[0][i]) > peak_ol) peak_ol = fabsf(out[0][i]);
        if(fabsf(out[1][i]) > peak_or) peak_or = fabsf(out[1][i]);
    }
    if(amplitude <= 0.f)
        tone_phase = 0.f;  // next tone starts at a zero crossing
    RaisePeak(peak_in[0], PeakUnits(peak_l));
    RaisePeak(peak_in[1], PeakUnits(peak_r));
    RaisePeak(peak_out[0], PeakUnits(peak_ol));
    RaisePeak(peak_out[1], PeakUnits(peak_or));

    // Snapshot capture of the inputs (SCOPE command).
    uint8_t state = scope_state.load(std::memory_order_acquire);
    if(state == kScopeArmed) {
        size_t start = 0;
        bool found = !scope_trigger.load(std::memory_order_relaxed)
                     || now_ms - scope_armed_ms >= diag::kScopeTriggerTimeoutMs;
        if(!found) {
            float prev = scope_prev_sample;
            for(size_t i = 0; i < size; ++i) {  // rising zero crossing on the left input
                if(prev < 0.f && in[0][i] >= 0.f) {
                    start = i;
                    found = true;
                    break;
                }
                prev = in[0][i];
            }
        }
        if(found) {
            scope_fill = 0;
            state = kScopeCapturing;
            scope_state.store(state, std::memory_order_relaxed);
            const uint16_t frames = scope_frames.load(std::memory_order_relaxed);
            for(size_t i = start; i < size && scope_fill < frames; ++i, ++scope_fill) {
                scope_buffer[scope_fill * 2] = ToInt16(in[0][i]);
                scope_buffer[scope_fill * 2 + 1] = ToInt16(in[1][i]);
            }
            if(scope_fill >= frames)
                scope_state.store(kScopeReady, std::memory_order_release);
        }
    } else if(state == kScopeCapturing) {
        const uint16_t frames = scope_frames.load(std::memory_order_relaxed);
        for(size_t i = 0; i < size && scope_fill < frames; ++i, ++scope_fill) {
            scope_buffer[scope_fill * 2] = ToInt16(in[0][i]);
            scope_buffer[scope_fill * 2 + 1] = ToInt16(in[1][i]);
        }
        if(scope_fill >= frames)
            scope_state.store(kScopeReady, std::memory_order_release);
    }
    scope_prev_sample = size ? in[0][size - 1] : scope_prev_sample;
}

// ---- USB CDC transmit -------------------------------------------------------------
// libDaisy's CDC_Transmit_HS() dereferences the class data without a NULL check, and
// a USB reset handled in the OTG_HS interrupt frees that data (pClassData = NULL).
// The readiness check and the start of the transfer therefore run with the USB
// interrupt (priority 1) masked via BASEPRI; the audio interrupt (priority 0) still runs.
constexpr uint32_t kUsbIrqPriority = 1;

class CdcSink final : public diag::UsbSink {
  public:
    diag::SendResult Send(const uint8_t* data, size_t len) override {
        const uint32_t saved = __get_BASEPRI();
        __set_BASEPRI(kUsbIrqPriority << (8u - __NVIC_PRIO_BITS));
        __DSB();
        __ISB();
        diag::SendResult result;
        if(hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED || hUsbDeviceHS.pClassData == nullptr)
            result = diag::SendResult::NotReady;
        else if(usb.TransmitExternal(const_cast<uint8_t*>(data), len) == UsbHandle::Result::OK)
            result = diag::SendResult::Ok;
        else
            result = diag::SendResult::Busy;
        __set_BASEPRI(saved);
        return result;
    }
};
CdcSink cdc_sink;

// ---- microSD: read-only, time-bounded block driver ------------------------------
// Instead of libDaisy's SD_Driver this firmware links its own FatFs disk driver:
//  * its write entry only returns RES_WRPRT and it always reports STA_PROTECT, so
//    FatFs refuses any write access (FR_WRITE_PROTECTED); no SD write or erase
//    command is linked at all (checked by `make check`);
//  * each read waits at most kSdOpTimeoutMs and a whole test at most kSdBudgetMs,
//    unlike libDaisy's SD_read() with two 30 s waits per operation;
//  * it is re-linked before every test, which resets FatFs' "initialised" flag, so a
//    swapped card or a failed first attempt is initialised again.
// With BOOT_SRAM, .bss lives in DTCM, which SDMMC1's IDMA cannot reach; the FATFS
// object (including its sector window) therefore lives in SDRAM.
constexpr uint32_t kSdOpTimeoutMs = 1000;
constexpr uint32_t kSdBudgetMs = 5000;
constexpr uint32_t kSdBlockSize = 512;
uint32_t sd_deadline = 0;

bool SdBudgetLeft() {
    return static_cast<int32_t>(sd_deadline - HAL_GetTick()) > 0;
}

bool SdWaitTransferState(uint32_t start) {
    while(BSP_SD_GetCardState() != SD_TRANSFER_OK)
        if(HAL_GetTick() - start > kSdOpTimeoutMs || !SdBudgetLeft())
            return false;
    return true;
}

extern "C" DSTATUS RoStatus(BYTE) {
    // After a reset abort (AbortReaderSd) the controller is unpowered; a CMD13 would
    // then only run into the HAL's software timeout. FatFs asks for the status again
    // while closing the directory, so answer from the handle state instead.
    if(hsd1.State != HAL_SD_STATE_READY)
        return STA_NOINIT | STA_PROTECT;
    return static_cast<DSTATUS>(
        (BSP_SD_GetCardState() == SD_TRANSFER_OK ? 0 : STA_NOINIT) | STA_PROTECT);
}

extern "C" DSTATUS RoInitialize(BYTE lun) {
    if(BSP_SD_Init() != MSD_OK)
        return STA_NOINIT | STA_PROTECT;
    return RoStatus(lun);
}

extern "C" DRESULT RoRead(BYTE, BYTE* buff, DWORD sector, UINT count) {
    if(!SdBudgetLeft())
        return RES_ERROR;
    const uint32_t aligned = reinterpret_cast<uint32_t>(buff) & ~0x1Fu;
    const int32_t span = static_cast<int32_t>(count * kSdBlockSize
                                              + (reinterpret_cast<uint32_t>(buff) - aligned));
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(aligned), span);
    const uint32_t start = HAL_GetTick();
    if(!SdWaitTransferState(start))
        return RES_ERROR;
    if(BSP_SD_ReadBlocks_DMA(reinterpret_cast<uint32_t*>(buff), sector, count) != MSD_OK)
        return RES_ERROR;
    while(hsd1.State == HAL_SD_STATE_BUSY) {  // set back to READY by the SDMMC1 IRQ
        if(HAL_GetTick() - start > kSdOpTimeoutMs || !SdBudgetLeft()) {
            diag::AbortReaderSd(hsd1);
            return RES_ERROR;
        }
    }
    if(hsd1.ErrorCode != HAL_SD_ERROR_NONE || !SdWaitTransferState(start))
        return RES_ERROR;
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(aligned), span);
    return RES_OK;
}

// Never reaches the card: refuses every write. FatFs does not call it because the
// volume reports STA_PROTECT; it exists so the driver table has no NULL entry.
extern "C" DRESULT RoWrite(BYTE, const BYTE*, DWORD, UINT) {
    return RES_WRPRT;
}

extern "C" DRESULT RoIoctl(BYTE, BYTE cmd, void*) {
    return cmd == CTRL_SYNC ? RES_OK : RES_PARERR;
}

const Diskio_drvTypeDef kReadOnlySd = {
    RoInitialize,
    RoStatus,
    RoRead,
#if _USE_WRITE == 1
    RoWrite,
#endif
#if _USE_IOCTL == 1
    RoIoctl,
#endif
};

SdmmcHandler sdmmc;
bool sdmmc_configured = false;
char sd_path[4] = {};
DSY_SDRAM_BSS alignas(32) FATFS sd_fs;
DSY_SDRAM_BSS alignas(32) DIR sd_dir;
FILINFO sd_info;

const char* FsTypeName(BYTE type) {
    switch(type) {
        case FS_FAT12: return "FAT12";
        case FS_FAT16: return "FAT16";
        case FS_FAT32: return "FAT32";
#if _FS_EXFAT
        case FS_EXFAT: return "EXFAT";
#endif
        default: return "?";
    }
}

diag::SdResult RunSdTest(bool card_present) {
    diag::SdResult result;
    if(!card_present)
        return result;  // SdStatus::None: no access attempted
    if(!sdmmc_configured) {
        SdmmcHandler::Config cfg;
        cfg.Defaults();
        cfg.speed = SdmmcHandler::Speed::STANDARD;  // conservative for diagnostics
        sdmmc.Init(cfg);
        sdmmc_configured = true;
    } else if(hsd1.State != HAL_SD_STATE_RESET) {
        HAL_SD_DeInit(&hsd1);  // power the card interface down; Init runs again below
    }
    FATFS_LinkDriver(&kReadOnlySd, sd_path);  // fresh link = FatFs initialises again
    sd_deadline = HAL_GetTick() + kSdBudgetMs;

    FRESULT res = f_mount(&sd_fs, sd_path, 1);  // mount, list root, unmount
    if(res == FR_OK) {
        result.fs_type = FsTypeName(sd_fs.fs_type);
        const uint64_t sectors = static_cast<uint64_t>(sd_fs.n_fatent - 2) * sd_fs.csize;
        result.size_mb = static_cast<uint32_t>(sectors * kSdBlockSize / (1024u * 1024u));
        res = f_opendir(&sd_dir, sd_path);
        if(res == FR_OK) {
            while(result.entries < 1000) {
                res = f_readdir(&sd_dir, &sd_info);
                if(res != FR_OK || sd_info.fname[0] == 0)
                    break;
                ++result.entries;
            }
            f_closedir(&sd_dir);
        }
    }
    f_mount(nullptr, sd_path, 0);
    FATFS_UnLinkDriver(sd_path);
    result.status = res == FR_OK ? diag::SdStatus::Mounted : diag::SdStatus::Error;
    result.fresult = static_cast<int>(res);
    return result;
}

#ifdef DIAG_CARD_READER
bool CardPresent() {
    const uint32_t saved = __get_PRIMASK();
    __disable_irq();
    const bool present = callback_inputs.sd_card;
    __set_PRIMASK(saved);
    return present;
}
// IDMA drains/fills the SDMMC FIFO while audio/USB interrupts run. PIO can overrun
// that FIFO when preempted. This buffer is reachable by SDMMC1 IDMA (DTCM is not),
// owns complete cache lines, and has no constructor touching uninitialized SDRAM.
DSY_SDRAM_BSS alignas(32) uint8_t reader_dma_buffer[diag::ReaderDma::kBytes];
class ReaderBackend final : public diag::ReaderDmaBackend {
  public:
    bool Available() override { return CardPresent() && !ReaderUsbInterrupted(); }
    uint32_t Now() override { return HAL_GetTick(); }
    bool StartRead(uint8_t* buffer, uint32_t lba) override {
        return HAL_SD_ReadBlocks_DMA(&hsd1, buffer, lba, 1) == HAL_OK;
    }
    bool StartWrite(uint8_t* buffer, uint32_t lba) override {
        return HAL_SD_WriteBlocks_DMA(&hsd1, buffer, lba, 1) == HAL_OK;
    }
    bool Busy() override { return hsd1.State == HAL_SD_STATE_BUSY; }
    uint32_t Error() override { return hsd1.ErrorCode; }
    bool CardReady() override { return HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER; }
    void Clean(uint8_t* buffer) override { SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(buffer), 512); }
    void Invalidate(uint8_t* buffer) override { SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(buffer), 512); }
    void Abort() override { diag::AbortReaderSd(hsd1); }
};
ReaderBackend reader_backend;
diag::ReaderDma reader_dma(reader_backend, reader_dma_buffer);
class ReaderCard final : public diag::CardDevice {
  public:
    bool Open(uint32_t& sectors) override {
        reader_dma.ResetStatus();
        if(hsd1.State != HAL_SD_STATE_RESET) HAL_SD_DeInit(&hsd1);
        SdmmcHandler::Config cfg; cfg.Defaults();
        cfg.speed = SdmmcHandler::Speed::STANDARD;
        sdmmc.Init(cfg); sdmmc_configured = true;
        if(BSP_SD_Init() != MSD_OK || ReaderUsbInterrupted()) return false;
        NVIC_SetPriority(SDMMC1_IRQn, 2);  // audio=0, USB=1; main-loop DMA wait allows all IRQs
        HAL_SD_CardInfoTypeDef info{};
        if(HAL_SD_GetCardInfo(&hsd1, &info) != HAL_OK || info.LogBlockSize != 512)
            return false;
        sectors = info.LogBlockNbr;
        return sectors != 0;
    }
    bool Read(uint32_t lba, uint8_t* block) override {
        return reader_dma.Read(lba, block);
    }
    bool Write(uint32_t lba, const uint8_t* block) override {
        return reader_dma.Write(lba, block);
    }
    bool Sync() override {
        return reader_dma.Sync();
    }
    void Close() override {
        reader_backend.Abort();
        if(hsd1.Instance == SDMMC1) HAL_SD_DeInit(&hsd1);
    }
};
ReaderCard reader_card;
diag::CardReader card_reader(reader_card);
#endif

// ---- Board implementation ---------------------------------------------------------
class DevkitBoard final : public diag::Board {
  public:
    diag::Inputs ReadInputs() override {
        diag::Inputs in;
        __disable_irq();
        in = callback_inputs;
        __enable_irq();
        for(size_t i = 0; i < diag::kNumPots; ++i) {
            in.pot_norm[i] = diag::NormalizePot(hw.pot[i].Value());
            in.pot_raw[i] = hw.seed.adc.GetMux(0, static_cast<uint8_t>(i));
        }
        // CV IN 1/2 are ADC channels 1/2 (channel 0 is the pot mux), see desktop_devkit.cpp.
        for(size_t i = 0; i < diag::kNumCv; ++i) {
            in.cv_norm[i] = diag::NormalizeCv(hw.cv[i].Value());
            in.cv_raw[i] = hw.seed.adc.Get(static_cast<uint8_t>(i + 1));
        }
        return in;
    }

    void ApplyCvOut(uint16_t code1, uint16_t code2) override {
        hw.SetDacOut(DacHandle::Channel::ONE, code1);  // polling DAC: short register write
        hw.SetDacOut(DacHandle::Channel::TWO, code2);
    }

    uint32_t ControlTicks() override { return control_ticks.load(std::memory_order_relaxed); }

    void ApplyLeds(uint16_t leds, uint8_t brightness_percent) override {
        const float on = brightness_percent / 100.f;
        for(size_t i = 0; i < diag::kNumLeds; ++i)
            hw.SetLed(static_cast<int>(i), (leds >> i) & 1u ? on : 0.f);
        hw.UpdateLeds();  // DMA driver may wait for the previous transfer: main loop only
    }

    diag::SdResult RunSdTest(bool card_present) override { return ::RunSdTest(card_present); }
    // DDP BOOT, after the ACK: no SD access is running (the test blocks the main loop).
    void EnterBootloader() override { bootloader::Enter(hw); }

    bool AudioSupported() override { return true; }
    void ApplyTone(const diag::ToneSettings& tone, uint32_t keep_until_ms) override {
        tone_freq_hz.store(tone.freq_hz, std::memory_order_relaxed);
        tone_channels.store(static_cast<uint8_t>(tone.channels), std::memory_order_relaxed);
        tone_amplitude.store(tone.mode == diag::ToneMode::Sine ? tone.amplitude : 0,
                             std::memory_order_relaxed);
        tone_until_ms.store(keep_until_ms, std::memory_order_release);
    }
    diag::AudioLevels ReadAudioLevels() override {
        diag::AudioLevels levels;
        for(size_t i = 0; i < diag::kNumAudio; ++i) {
            levels.in[i] = peak_in[i].exchange(0, std::memory_order_relaxed);
            levels.out[i] = peak_out[i].exchange(0, std::memory_order_relaxed);
        }
        return levels;
    }
    bool StartScope(uint16_t frames, bool trigger) override {
        const uint8_t state = scope_state.load(std::memory_order_acquire);
        if(state == kScopeArmed || state == kScopeCapturing)
            return false;
        scope_frames.store(frames, std::memory_order_relaxed);
        scope_trigger.store(trigger, std::memory_order_relaxed);
        scope_armed_ms = System::GetNow();
        scope_state.store(kScopeArmed, std::memory_order_release);
        return true;
    }
    const int16_t* ScopeData(uint16_t& frames) override {
        if(scope_state.load(std::memory_order_acquire) != kScopeReady) {
            frames = 0;
            return nullptr;
        }
        frames = scope_frames.load(std::memory_order_relaxed);
        return scope_buffer;  // stays valid until the next StartScope
    }
#ifdef DIAG_CARD_READER
    bool SdTestAllowed() override { return card_reader.Local(); }
    void ReaderCommand(uint32_t command, ddp::Writer& out) override {
        bool ok = true;
        if(command & diag::kRequestReaderShare)
            ok = ReaderUsbIdle() && card_reader.Share(CardPresent());
        if(command & diag::kRequestReaderReturn)
            ok = ReaderUsbIdle() && card_reader.Return();
        if(!ok) out.Str("ERR c=READER e=STATE");
        else out.Str("READER state=").Str(diag::ReaderStateName(card_reader.State()))
            .Str(" sectors=").U32(card_reader.Sectors()).Str(" busy=").U32(!ReaderUsbIdle())
            .Str(" a=").Str(command & diag::kRequestReaderShare ? "SHARE" :
                            command & diag::kRequestReaderReturn ? "RETURN" : "STATUS");
        if(ok) {
            const auto& io = reader_dma.Status();
            out.Str(" io=").Str(io.operation).Str(" fault=").U32(static_cast<uint8_t>(io.fault))
                .Str(" he=").U32(io.hal_error).Str(" lba=").U32(io.lba)
                .Str(" ms=").U32(io.elapsed_ms);
        }
    }
#endif
};
DevkitBoard board;

const char* BootloaderName(System::BootInfo::Version version) {
    switch(version) {
        case System::BootInfo::Version::LT_v6_0: return "LT_v6_0";
        case System::BootInfo::Version::NONE: return "NONE";
        case System::BootInfo::Version::v6_0: return "v6_0";
        case System::BootInfo::Version::v6_1: return "v6_1";
        default: return "unknown";
    }
}

diag::App* app = nullptr;

void UsbReceive(uint8_t* buf, uint32_t* len) {
    // USB interrupt: copy only, never parse or transmit here.
    if(app && buf && len)
        app->OnUsbReceive(buf, *len);
}
} // namespace

int main() {
    hw.Init();
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(48);  // 1 kHz control rate, matches AnalogControl setup

    static diag::App diagnostic(board, cdc_sink,
                                BootloaderName(System::GetBootloaderVersion()),
                                System::GetNow());
    app = &diagnostic;

#ifdef DIAG_CARD_READER
    if(!InitReaderUsb(card_reader)) while(true) { hw.seed.SetLed(true); }
#else
    usb.Init(UsbHandle::FS_EXTERNAL);
#endif
    // libDaisy configures USB and audio DMA with priority 0; let audio preempt USB.
    NVIC_SetPriority(OTG_HS_IRQn, kUsbIrqPriority);
    NVIC_SetPriority(OTG_HS_EP1_IN_IRQn, kUsbIrqPriority);
    NVIC_SetPriority(OTG_HS_EP1_OUT_IRQn, kUsbIrqPriority);
    usb.SetReceiveCallback(UsbReceive, UsbHandle::FS_EXTERNAL);

    hw.seed.StartAudio(AudioCallback);
    System::Delay(100);  // let the mux ADC and debouncers settle

    while(true) {
        const uint32_t now = System::GetNow();
#ifdef DIAG_CARD_READER
        PollReaderUsb();
        card_reader.Observe(CardPresent());
#endif
        diagnostic.Step(now);
        // Seed USR LED: slow blink = waiting for host, fast blink = host connected.
        hw.seed.SetLed(((now / (diagnostic.HostActive(now) ? 125u : 500u)) & 1u) != 0);
    }
}
