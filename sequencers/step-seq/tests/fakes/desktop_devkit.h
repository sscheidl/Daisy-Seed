#pragma once
// Host stand-in for the libDaisy Desktop Dev Kit API used by the Step Seq firmware
// (src/main.cpp). Keys use libDaisy's pinned DigitalControl debouncer; everything else
// is scripted by the test and records what the firmware writes.
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
#define DSY_SYSTEM_H
#define DSY_SDRAM_BSS
#define STEP_SEQ_HOST_TEST 1
namespace daisy {
struct System {
    inline static uint32_t now = 0;
    static uint32_t GetNow() { return now; }
    static void Delay(uint32_t ms) { now += ms; }
    enum BootloaderMode { STM = 0, DAISY, DAISY_SKIP_TIMEOUT, DAISY_INFINITE_TIMEOUT };
    inline static unsigned bootloader_resets = 0;
    inline static BootloaderMode bootloader_mode = STM;
    static void ResetToBootloader(BootloaderMode mode) { ++bootloader_resets; bootloader_mode = mode; }
};
struct Random {
    static void Init() {}
    static uint32_t GetValue() { return 0x1234ABCDu; }
};
} // namespace daisy
#include "hid/digital_control.h"
struct FakeCycles {
    uint32_t value = 0, step = 0;
    operator uint32_t() { return value += step; }
    FakeCycles& operator=(uint32_t v) { value = v; return *this; }
};
struct FakeCounter { FakeCycles CYCCNT; uint32_t CTRL = 0; };
struct FakeCoreDebug { uint32_t DEMCR = 0; };
inline FakeCounter fakeDwt;
inline FakeCoreDebug fakeCoreDebug;
inline FakeCounter* DWT = &fakeDwt;
inline FakeCoreDebug* CoreDebug = &fakeCoreDebug;
inline constexpr uint32_t DWT_CTRL_CYCCNTENA_Msk = 1;
inline constexpr uint32_t CoreDebug_DEMCR_TRCENA_Msk = 1;
inline uint32_t SystemCoreClock = 480000000;
// DAC registers as in CMSIS DAC_TypeDef (only the ones the adapter writes) and a log.
struct DAC_TypeDef {
    uint32_t SWTRIGR = 0, DHR12R1 = 0, DHR12R2 = 0;
};
inline DAC_TypeDef fakeDac;
inline DAC_TypeDef* const DAC1 = &fakeDac;
inline uint32_t HAL_GetUIDw0() { return 0x00410032u; }
inline uint32_t HAL_GetUIDw1() { return 0x3138510Bu; }
inline uint32_t HAL_GetUIDw2() { return 0x32313533u; }
namespace daisy {
struct Switch3 {
    enum { POS_CENTER = 0, POS_UP = 1, POS_DOWN = 2 };
};
struct AudioHandle {
    using InputBuffer = const float* const*;
    using OutputBuffer = float**;
};
struct SaiHandle {
    struct Config { enum class SampleRate { SAI_48KHZ }; };
};
struct DacHandle {
    enum class Channel { ONE, TWO, BOTH };
};
enum MidiMessageType { NoteOff, NoteOn, PolyphonicKeyPressure, ControlChange, ProgramChange,
                       ChannelPressure, PitchBend, SystemCommon, SystemRealTime, ChannelMode, MessageLast };
enum SystemCommonType { SystemExclusive, MTCQuarterFrame, SongPositionPointer, SongSelect };
enum SystemRealTimeType { TimingClock, SRTUndefined0, Start, Continue, Stop };
enum ChannelModeType { AllSoundOff, ResetAllControllers, LocalControl, AllNotesOff };
struct NoteOnEvent { int channel; uint8_t note, velocity; };
struct MidiEvent {
    MidiMessageType type = NoteOn;
    int channel = 0;
    uint8_t data[2] = {0, 0};
    uint8_t sysex_data[128] = {};
    uint8_t sysex_message_len = 0;
    SystemCommonType sc_type = SystemExclusive;
    SystemRealTimeType srt_type = TimingClock;
    ChannelModeType cm_type = AllSoundOff;
    NoteOnEvent AsNoteOn() { return {channel, data[0], data[1]}; }
};
struct MidiUsbTransport {
    struct Config {
        enum Periph { INTERNAL, EXTERNAL, HOST };
        Periph periph = INTERNAL;
    };
};
// Scripted events in, every sent message recorded.
struct FakeMidi {
    std::deque<MidiEvent> incoming;
    std::vector<std::vector<uint8_t>> sent;
    bool receiving = false;
    void StartReceive() { receiving = true; }
    void Listen() {}
    bool HasEvents() const { return !incoming.empty(); }
    MidiEvent PopEvent() {
        MidiEvent event = incoming.front();
        incoming.pop_front();
        return event;
    }
    void SendMessage(uint8_t* bytes, std::size_t size) { sent.emplace_back(bytes, bytes + size); }
};
struct MidiUsbHandler : FakeMidi {
    struct Config { MidiUsbTransport::Config transport_config; };
    MidiUsbTransport::Config::Periph periph = MidiUsbTransport::Config::INTERNAL;
    void Init(Config config) { periph = config.transport_config.periph; }
};
struct MidiUartHandler : FakeMidi {};
namespace desktop_devkit {
struct Hardware {
    struct Seed {
        void SetAudioSampleRate(SaiHandle::Config::SampleRate) {}
        void SetAudioBlockSize(std::size_t) {}
        float AudioCallbackRate() const { return 1000.f; }
        template <typename Callback> void StartAudio(Callback) {}
    } seed;
    struct Analog {
        float value = 0;
        float Value() const { return value; }
        void SetSampleRate(float) {}
    };
    struct Gate {
        bool high = false;
        bool State() const { return high; }
    };
    std::array<Analog, 8> pot{};
    std::array<Analog, 2> cv{};
    std::array<Gate, 2> gate_in{};
    std::array<float, 16> led{};
    unsigned ledUpdates = 0;
    std::array<DigitalControl, 16> button{};
    std::array<bool, 16> pressed{};
    struct Toggle3 { int position = Switch3::POS_UP; int Read() const { return position; } } tog_sw18;
    struct Toggle2 { bool pressed = false; bool Pressed() const { return pressed; } } tog_sw17;
    struct Detect { bool pressed = false; bool Pressed() const { return pressed; } } right_detect;
    MidiUartHandler midi;
    std::vector<std::pair<int, uint16_t>> dac_init; // SetDacOut calls (start-up only)
    void Init() { for (auto& b : button) b.Init(true); }
    void UpdateAllControls() {
        for (std::size_t i = 0; i < button.size(); ++i)
            button[i].Debounce(!pressed[i]);
    }
    void SetDacOut(DacHandle::Channel channel, uint16_t code) { dac_init.emplace_back(int(channel), code); }
    void SetLed(int index, float brightness) { led[std::size_t(index)] = brightness; }
    void UpdateLeds() { ++ledUpdates; }
};
} // namespace desktop_devkit
} // namespace daisy
