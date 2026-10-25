#pragma once
#include <array>
#include <cstdint>

// Host stand-ins for the parts of libDaisy that bootloader_daisy.h uses.
namespace daisy {
struct System {
    enum BootloaderMode { STM = 0, DAISY, DAISY_SKIP_TIMEOUT, DAISY_INFINITE_TIMEOUT };
    inline static uint32_t now = 0;
    inline static unsigned bootloader_resets = 0;
    inline static BootloaderMode bootloader_mode = STM;
    static uint32_t GetNow() { return now; }
    static void Delay(uint32_t ms) { now += ms; }
    static void ResetToBootloader(BootloaderMode mode) {
        ++bootloader_resets;
        bootloader_mode = mode;
    }
};
namespace desktop_devkit {
struct Hardware {
    std::array<float, 16> led{};
    unsigned led_updates = 0;
    void SetLed(int index, float brightness) { led[std::size_t(index)] = brightness; }
    void UpdateLeds() { ++led_updates; }
};
}
}
