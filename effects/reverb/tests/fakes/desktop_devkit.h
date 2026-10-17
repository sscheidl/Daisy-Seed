#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#define DSY_SYSTEM_H
#define DSY_SDRAM_BSS
namespace daisy {
struct System {
    inline static uint32_t now = 0;
    static uint32_t GetNow() { return now; }
    static void Delay(uint32_t ms) { now += ms; }
};
}
#include "hid/digital_control.h"
struct FakeCounter { uint32_t CYCCNT = 0, CTRL = 0; };
struct FakeCoreDebug { uint32_t DEMCR = 0; };
inline FakeCounter fakeDwt;
inline FakeCoreDebug fakeCoreDebug;
inline FakeCounter* DWT = &fakeDwt;
inline FakeCoreDebug* CoreDebug = &fakeCoreDebug;
inline constexpr uint32_t DWT_CTRL_CYCCNTENA_Msk = 1;
inline constexpr uint32_t CoreDebug_DEMCR_TRCENA_Msk = 1;
inline uint32_t SystemCoreClock = 480000000;
namespace daisy {
struct AudioHandle {
    using InputBuffer = const float* const*;
    using OutputBuffer = float**;
};
struct SaiHandle {
    struct Config { enum class SampleRate { SAI_48KHZ }; };
};
namespace desktop_devkit {
struct Hardware {
    struct Seed {
        void SetAudioSampleRate(SaiHandle::Config::SampleRate) {}
        void SetAudioBlockSize(std::size_t) {}
        float AudioCallbackRate() const { return 1500.f; }
        float AudioSampleRate() const { return 48000.f; }
        template <typename Callback> void StartAudio(Callback) {}
    } seed;
    struct Pot {
        float value = 0;
        float Value() const { return value; }
        void SetSampleRate(float) {}
    };
    std::array<Pot, 8> pot{};
    std::array<DigitalControl, 16> button{};
    std::array<bool, 16> pressed{};
    struct RightDetect { bool pressed = false; bool Pressed() const { return pressed; } } right_detect;
    void Init() { for (auto& b : button) b.Init(true); }
    void UpdateAllControls() {
        for (std::size_t i = 0; i < button.size(); ++i)
            button[i].Debounce(!pressed[i]);
    }
    void SetLed(int, float) {}
    void UpdateLeds() {}
};
}
}
