#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Replace only the platform clock used by the real, pinned DigitalControl.
// The STM32 System header is not host-buildable; no debounce logic is copied.
#define DSY_SYSTEM_H
namespace daisy {
struct System {
    inline static uint32_t now = 0;
    static uint32_t GetNow() { return now; }
    static void Delay(uint32_t ms) { now += ms; }
};
}
#include "hid/digital_control.h"

namespace daisy {
struct AudioHandle {
    using InputBuffer = const float* const*;
    using OutputBuffer = float**;
};
struct SaiHandle {
    struct Config { enum class SampleRate { SAI_48KHZ }; };
};
namespace desktop_devkit {
constexpr std::size_t kNumButtons = 16, kNumLeds = 16;
struct Hardware {
    struct Seed {
        void SetAudioSampleRate(SaiHandle::Config::SampleRate) {}
        void SetAudioBlockSize(std::size_t) {}
        float AudioSampleRate() const { return 48000.0f; }
        template <typename Callback> void StartAudio(Callback) {}
    } seed;
    struct Pot {
        float value = 0;
        float Value() const { return value; }
        void Process() {}
    };
    std::array<Pot, 8> pot{};
    std::array<DigitalControl, kNumButtons> button{};
    std::array<bool, kNumButtons> pressed{};
    unsigned led_transfers = 0;
    void Init() {
        for(auto& b : button)
            b.Init(true);
    }
    void UpdateAllControls() {
        for(std::size_t i = 0; i < kNumButtons; ++i)
            button[i].Debounce(!pressed[i]); // Active-low switch, as on the kit.
    }
    void SetLed(int, float) {}
    void UpdateLeds() { ++led_transfers; }
};
}
}
