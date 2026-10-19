#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

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
// USB MIDI: the firmware polls events and sends raw 3-byte messages. The fake
// feeds scripted events in and records every message handed to SendMessage().
enum MidiMessageType { NoteOff, NoteOn, ControlChange, PitchBend };
#define DSY_SDRAM_BSS
struct NoteOnEvent { int channel; uint8_t note, velocity; };
struct MidiEvent {
    MidiMessageType type = NoteOn;
    int channel = 0;
    uint8_t data[2] = {0, 0};
    NoteOnEvent AsNoteOn() { return {channel, data[0], data[1]}; }
};
struct MidiUsbTransport {
    struct Config {
        enum Periph { INTERNAL, EXTERNAL, HOST };
        Periph periph = INTERNAL;
    };
};
struct MidiUsbHandler {
    struct Config { MidiUsbTransport::Config transport_config; };
    std::deque<MidiEvent> incoming;
    std::vector<std::vector<uint8_t>> sent;
    MidiUsbTransport::Config::Periph periph = MidiUsbTransport::Config::INTERNAL;
    bool receiving = false;
    void Init(Config config) { periph = config.transport_config.periph; }
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
    struct Toggle { bool value = false; bool Pressed() const { return value; } } tog_sw17;
    struct Switch3 { int value = 0; int Read() const { return value; } } tog_sw18;
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
