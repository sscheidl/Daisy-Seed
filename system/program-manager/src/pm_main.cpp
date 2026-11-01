// Daisy Program Manager - hardware glue for the Seed3 Desktop Dev Kit.
// Runs in place from QSPI behind the unchanged Daisy bootloader (BOOT_QSPI, own
// linker). Everything with logic lives in the portable pm_* and diag_* modules and is
// host-tested; this file connects them to libDaisy, the shared read-only SD driver
// (common/sd), the data cache and the bootloader's BootInfo JUMP order.
//
// Profiles (Makefile): PM_PROFILE_PROBE = LED/CDC/service without SD; PM_PROFILE_MANAGER
// = complete version 1. Hardware status: experimental, NICHT GEMESSEN (docs/HARDWARE_TEST.md).
#include "devkit_board.h"
#include "bootloader_daisy.h"
#include "per/sdmmc.h"
#include "pm_manager.h"
#include "pm_protocol.h"
#include "version.h"
#include <cstring>
#ifdef PM_PROFILE_MANAGER
#include "ro_sd.h"
#include "reader_abort.h"
#endif

using namespace daisy;

extern SD_HandleTypeDef hsd1;  // libDaisy src/per/sdmmc.cpp

#ifndef PM_BUILD_REV
#define PM_BUILD_REV "nogit"
#endif

namespace {
desktop_devkit::Hardware hw;
UsbHandle usb;
diag::devkit::AudioDiagnostics audio;
diag::devkit::CdcSink cdc_sink(usb);

// Embedded version marker, found by tools/check_qspi_image.py in the BIN.
__attribute__((used)) const char kVersionMarker[] = PROGRAM_MANAGER_MARKER;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    audio.Process(hw, in, out, size, System::GetNow());
}

// ---- Image target: the 480 KiB AXI SRAM region the loader fills -------------------
class SramTarget final : public pm::LoadTarget {
  public:
    uint8_t* Base() override { return reinterpret_cast<uint8_t*>(pm::kTargetBase); }
    uint32_t Capacity() override { return pm::kTargetSize; }
    void Clean(uint32_t len) override {
        // The image was written by the CPU through the data cache; the bootloader
        // jumps after a reset that drops the cache. Write every dirty line back and
        // order the writes before the BootInfo order is committed (CONTRACT.md 7.4).
        SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(pm::kTargetBase), static_cast<int32_t>(len));
        __DSB();
        __ISB();
    }
};
SramTarget sram_target;

// ---- Boot commit: the bootloader's existing JUMP order ----------------------------
class DaisyBootCommit final : public pm::BootCommit {
  public:
    void Quiesce() override {
        // Nothing may write memory or run a transfer after this point: audio/control
        // callback off, LEDs dark (last transfer waited for), card interface down.
        hw.seed.StopAudio();
        hw.ClearLeds();
        hw.UpdateLeds();
        hw.UpdateLeds();  // waits until the previous transfer has finished
        hw.SetDacOut(DacHandle::Channel::ONE, 0);  // DAC code 0; electrical level NICHT GEMESSEN
        hw.SetDacOut(DacHandle::Channel::TWO, 0);
#ifdef PM_PROFILE_MANAGER
        ro_sd_unmount();
        if(hsd1.State != HAL_SD_STATE_RESET)
            HAL_SD_DeInit(&hsd1);
#endif
    }
    void Commit(uint32_t address) override {
        // Same order as the bootloader itself and System::ResetToBootloader(): target
        // first, status last, then an immediate software reset. The bootloader's
        // startup_process() consumes JUMP and jumps to `address` before any init.
        // Backup SRAM access was enabled by the bootloader (InitBackupSram).
        __disable_irq();
        boot_info.data = address;
        __DSB();
        boot_info.status = System::BootInfo::Type::JUMP;
        __DSB();
        RCC->CIER = 0x00000000;
        HAL_NVIC_SystemReset();
        while(true) {}
    }
};
DaisyBootCommit boot_commit;

#ifdef PM_PROFILE_MANAGER
// ---- Card: shared read-only FatFs R0.16 driver (common/sd) ------------------------
// The bounce buffer is the only SDMMC1 IDMA target: AXI SRAM above the image target
// (linker section .pm_axi_bounce), 32-byte aligned, whole cache lines. FatFs objects
// and the catalog text live in DTCM like any other .bss (CPU copies only).
constexpr uint32_t kBounceBytes = 4096;  // 8 sectors per transfer
__attribute__((section(".pm_axi_bounce"), aligned(32))) uint8_t sd_bounce[kBounceBytes];
SdmmcHandler sdmmc;
bool sdmmc_configured = false;
FATFS sd_fs;
FIL sd_file;
DIR sd_dir;
FILINFO sd_info;

int SdCardPresent() { return audio.CardPresent() ? 1 : 0; }
void SdAbort() { diag::AbortReaderSd(hsd1); }  // RCC-reset abort, hardware-tested in the reader

class CardStorage final : public pm::Storage {
  public:
    void Init() {
        RoSdConfig c{};
        c.bounce = sd_bounce;
        c.bounce_bytes = kBounceBytes;
        c.transfer_timeout_ms = 500;   // per transfer, including the card's busy state
        c.budget_ms = 0;               // the loader has its own overall budget
        c.card_present = SdCardPresent;
        c.service = nullptr;
        c.abort = SdAbort;
        ro_sd_init(&c);
    }

    bool Mount(int& fresult) {
        if(mounted_) {
            fresult = FR_OK;
            return true;
        }
        if(!sdmmc_configured) {
            SdmmcHandler::Config cfg;
            cfg.Defaults();
            cfg.speed = SdmmcHandler::Speed::STANDARD;  // conservative, like the diagnostics
            sdmmc.Init(cfg);
            sdmmc_configured = true;
        }
        const FRESULT r = ro_sd_mount(&sd_fs);
        fresult = r;
        mounted_ = r == FR_OK;
        return mounted_;
    }

    void Unmount() override {
        if(open_) {
            f_close(&sd_file);
            open_ = false;
        }
        if(mounted_) {
            ro_sd_unmount();
            mounted_ = false;
        }
    }

    CatalogResult ReadCatalog(char* buf, uint32_t cap, uint32_t& len, int& detail) override {
        len = 0;
        if(!audio.CardPresent())
            return CatalogResult::NoCard;
        if(!Mount(detail))
            return CatalogResult::MountFailed;
        FRESULT r = f_open(&sd_file, "0:/DAISY/catalog.json", FA_READ);
        detail = r;
        if(r == FR_NO_FILE || r == FR_NO_PATH)
            return CatalogResult::Missing;
        if(r != FR_OK)
            return CatalogResult::ReadError;
        const FSIZE_t size = f_size(&sd_file);
        if(size > cap) {
            f_close(&sd_file);
            return CatalogResult::TooLarge;
        }
        UINT got = 0;
        r = f_read(&sd_file, buf, static_cast<UINT>(size), &got);
        f_close(&sd_file);
        detail = r;
        if(r != FR_OK || got != size)
            return CatalogResult::ReadError;
        len = got;
        return CatalogResult::Ok;
    }

    bool RootHasBinFile() override {
        int fresult;
        if(!Mount(fresult) || f_opendir(&sd_dir, "0:/") != FR_OK)
            return false;
        bool found = false;
        for(int i = 0; i < 512 && !found; ++i) {
            if(f_readdir(&sd_dir, &sd_info) != FR_OK || sd_info.fname[0] == 0)
                break;
            if(sd_info.fattrib & AM_DIR)
                continue;  // the bootloader skips directories as well
            found = NameHasBin(sd_info.fname);
        }
        f_closedir(&sd_dir);
        return found;
    }

    // pm::ImageSource
    bool Open(const char* path, uint32_t& file_size) override {
        int fresult;
        if(open_ || !Mount(fresult))
            return false;
        char full[8 + pm::kPathMax + 1];
        std::strcpy(full, "0:/DAISY/");
        std::strncat(full, path, pm::kPathMax);
        if(f_open(&sd_file, full, FA_READ) != FR_OK)
            return false;
        open_ = true;
        file_size = static_cast<uint32_t>(f_size(&sd_file));
        return true;
    }
    int32_t Read(uint8_t* dst, uint32_t len) override {
        if(!open_)
            return -1;
        UINT got = 0;
        if(f_read(&sd_file, dst, len, &got) != FR_OK)
            return -1;
        return static_cast<int32_t>(got);
    }
    void Close() override {
        if(open_) {
            f_close(&sd_file);
            open_ = false;
        }
    }
    bool CardPresent() override { return audio.CardPresent(); }

    // Diagnostics SDTEST through the same driver: mount, count root entries, report.
    diag::SdResult RunSdTest(bool card_present) {
        diag::SdResult result;
        if(!card_present)
            return result;
        Unmount();
        int fresult;
        if(!Mount(fresult)) {
            result.status = diag::SdStatus::Error;
            result.fresult = fresult;
            return result;
        }
        result.fs_type = FsTypeName(sd_fs.fs_type);
        const uint64_t sectors = static_cast<uint64_t>(sd_fs.n_fatent - 2) * sd_fs.csize;
        result.size_mb = static_cast<uint32_t>(sectors * 512u / (1024u * 1024u));
        FRESULT r = f_opendir(&sd_dir, "0:/");
        if(r == FR_OK) {
            while(result.entries < 1000) {
                r = f_readdir(&sd_dir, &sd_info);
                if(r != FR_OK || sd_info.fname[0] == 0)
                    break;
                ++result.entries;
            }
            f_closedir(&sd_dir);
        }
        result.status = r == FR_OK ? diag::SdStatus::Mounted : diag::SdStatus::Error;
        result.fresult = r;
        return result;
    }

  private:
    static bool NameHasBin(const char* name) {
        // ".bin" in any case anywhere in the name, as the bootloader's strstr(".bin"/".BIN")
        // plus mixed case; conservative by design.
        char lower[FF_MAX_LFN + 1];
        size_t n = 0;
        for(; name[n] && n < FF_MAX_LFN; ++n)
            lower[n] = static_cast<char>(name[n] >= 'A' && name[n] <= 'Z' ? name[n] + 32 : name[n]);
        lower[n] = 0;
        return std::strstr(lower, ".bin") != nullptr;
    }
    static const char* FsTypeName(BYTE type) {
        switch(type) {
            case FS_FAT12: return "FAT12";
            case FS_FAT16: return "FAT16";
            case FS_FAT32: return "FAT32";
            case FS_EXFAT: return "EXFAT";
            default: return "?";
        }
    }
    bool mounted_ = false;
    bool open_ = false;
};
CardStorage storage;
constexpr bool kSdSupported = true;
#else
// ---- Probe profile: no card access at all -----------------------------------------
class NoStorage final : public pm::Storage {
  public:
    void Init() {}
    CatalogResult ReadCatalog(char*, uint32_t, uint32_t& len, int& detail) override {
        len = 0;
        detail = 0;
        return CatalogResult::NoCard;
    }
    bool RootHasBinFile() override { return false; }
    void Unmount() override {}
    bool Open(const char*, uint32_t&) override { return false; }
    int32_t Read(uint8_t*, uint32_t) override { return -1; }
    void Close() override {}
    bool CardPresent() override { return audio.CardPresent(); }
    diag::SdResult RunSdTest(bool) { return diag::SdResult(); }  // SdStatus::None
};
NoStorage storage;
constexpr bool kSdSupported = false;
#endif

pm::Manager manager(storage, sram_target, boot_commit, kSdSupported);

// ---- diag::Board: the kit through the shared adapter, gated by the manager mode ----
class ManagerBoard final : public diag::devkit::DevkitBoardBase {
  public:
    ManagerBoard() : DevkitBoardBase(hw, audio) {}
    // LEDs: the diagnostics layer drives them only in the Diagnostics mode; the
    // manager's own display (slots, loading, errors) is applied in the main loop.
    void ApplyLeds(uint16_t leds, uint8_t brightness_percent) override {
        if(manager.mode() == pm::Mode::Diagnostics)
            DevkitBoardBase::ApplyLeds(leds, brightness_percent);
    }
    // CV OUT and the tone only in Diagnostics (and they are already session-bound).
    void ApplyCvOut(uint16_t code1, uint16_t code2) override {
        if(manager.mode() == pm::Mode::Diagnostics)
            DevkitBoardBase::ApplyCvOut(code1, code2);
    }
    void ApplyTone(const diag::ToneSettings& tone, uint32_t keep_until_ms) override {
        if(manager.mode() == pm::Mode::Diagnostics)
            DevkitBoardBase::ApplyTone(tone, keep_until_ms);
        else
            audio.MuteTone();
    }
    diag::SdResult RunSdTest(bool card_present) override { return storage.RunSdTest(card_present); }
    // DDP BOOT or S9+S16: service (DFU) through the common bootloader entry.
    void EnterBootloader() override {
        manager.RequestService();
        bootloader::Enter(hw);
    }
};
ManagerBoard board;

diag::App* app = nullptr;

void UsbReceive(uint8_t* buf, uint32_t* len) {
    // USB interrupt: copy only, never parse or transmit here.
    if(app && buf && len)
        app->OnUsbReceive(buf, *len);
}

// Manager LED display: dim slots, bright selection/progress; rate limited like the
// diagnostics (the PCA9685 driver may wait for the previous DMA transfer).
void ApplyManagerLeds(const pm::LedFrame& frame) {
    for(size_t i = 0; i < diag::kNumLeds; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        hw.SetLed(static_cast<int>(i), (frame.bright & bit) ? 1.f : (frame.dim & bit) ? 0.25f : 0.f);
    }
    hw.UpdateLeds();
}
} // namespace

int main() {
    hw.Init();  // DaisySeed::Init(true): 480 MHz, caches, SDRAM; qspi.Init is skipped (XIP)
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(48);  // 1 kHz control rate, matches AnalogControl setup
    storage.Init();

    static const diag::Identity identity = {
#ifdef PM_PROFILE_MANAGER
        "DaisyProgramManager", PROGRAM_MANAGER_VERSION, "BOOT_QSPI", ",pm,pmsd",
#else
        "DaisyProgramManagerProbe", PROGRAM_MANAGER_VERSION, "BOOT_QSPI", ",pm",
#endif
    };
    pm::ProtocolConfig protocol_config;
    protocol_config.app = identity.app;
    protocol_config.fw = PROGRAM_MANAGER_VERSION;
    protocol_config.profile = kSdSupported ? "manager" : "probe";
    protocol_config.build = PM_BUILD_REV;
    static pm::Protocol protocol(manager, protocol_config);

    static diag::App diagnostic(board, cdc_sink,
                                diag::devkit::BootloaderName(System::GetBootloaderVersion()),
                                System::GetNow(), &identity);
    diagnostic.SetHook(&protocol);
    app = &diagnostic;

    usb.Init(UsbHandle::FS_EXTERNAL);
    diag::devkit::SetUsbPriority();  // audio (0) preempts USB (1)
    usb.SetReceiveCallback(UsbReceive, UsbHandle::FS_EXTERNAL);

    hw.seed.StartAudio(AudioCallback);
    System::Delay(100);  // let the mux ADC and debouncers settle

    pm::Mode last_mode = pm::Mode::Select;
    pm::LedFrame last_leds;
    bool leds_valid = false;
    uint32_t last_led_ms = 0;

    while(true) {
        const uint32_t now = System::GetNow();
        manager.Step(now, audio.HeldKeys(), audio.CardPresent());
        diagnostic.Step(now);

        // Leaving the Diagnostics mode: its outputs must not stay on.
        const pm::Mode mode = manager.mode();
        if(mode != last_mode) {
            if(last_mode == pm::Mode::Diagnostics) {
                hw.SetDacOut(DacHandle::Channel::ONE, 0);
                hw.SetDacOut(DacHandle::Channel::TWO, 0);
                audio.MuteTone();
            }
            last_mode = mode;
            leds_valid = false;
        }
        const pm::LedFrame leds = manager.Leds(now);
        if(!leds.owner_diag && mode != pm::Mode::Service
           && (!leds_valid || leds.dim != last_leds.dim || leds.bright != last_leds.bright)
           && now - last_led_ms >= diag::App::kLedMinIntervalMs) {
            ApplyManagerLeds(leds);
            last_leds = leds;
            leds_valid = true;
            last_led_ms = now;
        }
        // Seed USR LED: slow blink = waiting for host, fast blink = host connected.
        hw.seed.SetLed(((now / (diagnostic.HostActive(now) ? 125u : 500u)) & 1u) != 0);
    }
}
