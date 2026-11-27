// SPDX-License-Identifier: MIT
// Audrey 3 firmware for the Seed3 Desktop Dev Kit: the Audrey II feedback synth, ported
// faithfully (block size 4, 48 kHz, limiter gain 0.7), with a second pot page and MIDI.
// The callback owns the engine and controls; main owns MIDI, LEDs and the bootloader.
// Hand-over only through an SPSC queue and atomics.
#include "version.h"
const char audrey_3_firmware_version[] = "AUDREY-3-FW-VERSION=" AUDREY_3_FIRMWARE_VERSION;
#include "desktop_devkit.h"
#include "audrey/composition.h"
#include "audrey/control.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "step_seq/controls.h"
#include "step_seq/diagnostics.h"
#include "step_seq/spsc.h"
#include "usbd_core.h"
#include <atomic>
#include <new>
using namespace daisy;
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace fw {
constexpr std::size_t kBlockSize = 4;   // as the Audrey II: 12 kHz parameter rate
constexpr uint32_t kBlocksPerTick = 12; // panel and MIDI at 1 kHz
constexpr uint32_t kLedPeriodMs = 10;
constexpr float kLimiterGain = 0.7f;
constexpr int kLedWarp = 0, kLedGate = 1, kLedOverrun = 15;

struct Command {
    enum class Kind : uint8_t { NoteOn, NoteOff, Cc, AllOff, SoundOff, Mute };
    Kind kind = Kind::Cc;
    uint8_t a = 0, b = 0;
};
} // namespace fw

using Stages = audrey::Stages;
alignas(32) static unsigned char engine_memory[sizeof(audrey::Engine)] __attribute__((section(".audrey_axi")));
static DSY_SDRAM_BSS Stages::Reverb::Storage reverb_memory;
static DSY_SDRAM_BSS Stages::Echo::Storage echo_memory;

static desktop_devkit::Hardware hw;
static audrey::Engine* engine = nullptr;
static Stages::Reverb reverb;
static Stages::Echo echo;
static audrey::Control control;
static daisysp::Limiter limiter[2];
static step_seq::PotFilter pot_filter[8];
static step_seq::Switch3Debounce sw18;
static step_seq::CallbackMeter meter;
static step_seq::SpscQueue<fw::Command, 64> commands;
static bootloader::KeyCombo boot_keys;
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static uint32_t tick_blocks = fw::kBlocksPerTick - 1; // the first block reads the panel
static uint32_t control_ms = 0;
static bool muted = false;
static std::atomic<bool> release_all{false}; // Note-Off lost to a full queue: never a hanging gate
static std::atomic<bool> silence_all{false}; // Sound-Off lost to a full queue: also closes a drone
static std::atomic<uint32_t> mute_ack{0}, led_state{0};

// ----- audio callback ---------------------------------------------------------------------
static void ApplyCommand(const fw::Command& c) {
    using K = fw::Command::Kind;
    switch(c.kind) {
        case K::NoteOn: control.NoteOn(c.a); break;
        case K::NoteOff: control.NoteOff(c.a); break;
        case K::Cc: control.ControlChange(c.a, c.b); break;
        case K::AllOff: control.AllNotesOff(); break;
        case K::SoundOff: control.AllSoundOff(); break;
        case K::Mute:
            muted = true;
            mute_ack.fetch_add(1, std::memory_order_release);
            break;
    }
}

static void Tick() {
    ++control_ms;
    hw.UpdateAllControls();
    fw::Command c;
    while(commands.Pop(c))
        ApplyCommand(c);
    // Consume both overflow flags every tick, so a pending release never lingers into the next
    // one. All Sound Off wins when both are set: it already clears the notes.
    const bool silence = silence_all.exchange(false, std::memory_order_acquire);
    const bool release = release_all.exchange(false, std::memory_order_acquire);
    if(silence)
        control.AllSoundOff(); // closes a drone too; also clears the notes
    else if(release)
        control.AllNotesOff();
    audrey::PanelFrame frame;
    for(int i = 0; i < 8; ++i)
        frame.pots[i] = pot_filter[i].Process(hw.pot[i].Value());
    for(int i = 0; i < 16; ++i)
        if(hw.button[i].Pressed())
            frame.keys |= uint16_t(1u << i);
    if(boot_keys.Process(frame.keys, control_ms)) {
        muted = true; // silence before the bootloader
        boot_request.Set(bootloader::Request::kKeys);
    }
    frame.page_b = hw.tog_sw17.Pressed();
    const int sw = hw.tog_sw18.Read();
    frame.sw18 = sw18.Process(sw == Switch3::POS_UP ? 0 : (sw == Switch3::POS_DOWN ? 2 : 1), control_ms);
    control.Panel(frame);
    engine->SetGate(control.GateTarget());
    uint32_t lit = 0;
    if(control.Bank().Warp())
        lit |= 1u << fw::kLedWarp;
    if(control.GateTarget() > 0.f)
        lit |= 1u << fw::kLedGate;
    if(meter.Overruns())
        lit |= 1u << fw::kLedOverrun;
    led_state.store(lit, std::memory_order_relaxed);
}

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t n) {
    meter.Begin(DWT->CYCCNT);
    if(n != fw::kBlockSize) {
        for(size_t i = 0; i < n; ++i)
            out[0][i] = out[1][i] = 0.f;
        return;
    }
    if(++tick_blocks >= fw::kBlocksPerTick) {
        tick_blocks = 0;
        Tick();
    }
    audrey::EngineSink<Stages> sink(*engine, reverb, echo);
    control.Bank().Process(sink);
    for(size_t i = 0; i < n; ++i)
        engine->Process(in[0][i], out[0][i], out[1][i]);
    limiter[0].ProcessBlock(out[0], n, fw::kLimiterGain);
    limiter[1].ProcessBlock(out[1], n, fw::kLimiterGain);
    if(muted)
        for(size_t i = 0; i < n; ++i)
            out[0][i] = out[1][i] = 0.f;
    meter.End(DWT->CYCCNT);
}

// ----- main loop --------------------------------------------------------------------------
namespace fw {
static void SendUsb(uint8_t* bytes, std::size_t size) {
    if(hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED)
        usb_midi.SendMessage(bytes, size);
}

// A full queue drops Note-Ons and CCs; a lost Note-Off or all-off releases all notes instead.
static void Push(const Command& c) {
    if(commands.Push(c))
        return;
    // A dropped Sound-Off must still close a drone, so it keeps its stronger meaning; a dropped
    // Note-Off or All-Notes-Off only releases the notes.
    if(c.kind == Command::Kind::SoundOff)
        silence_all.store(true, std::memory_order_release);
    else if(c.kind == Command::Kind::NoteOff || c.kind == Command::Kind::AllOff)
        release_all.store(true, std::memory_order_release);
}

static void HandleEvent(MidiEvent e, bool usb) {
    Command c;
    switch(e.type) {
        case NoteOn:
            c.kind = e.data[1] ? Command::Kind::NoteOn : Command::Kind::NoteOff;
            c.a = e.data[0];
            Push(c);
            break;
        case NoteOff:
            c.kind = Command::Kind::NoteOff;
            c.a = e.data[0];
            Push(c);
            break;
        case ControlChange:
            c.kind = Command::Kind::Cc;
            c.a = e.data[0];
            c.b = e.data[1];
            Push(c);
            break;
        case ChannelMode:
            if(e.cm_type == AllNotesOff || e.cm_type == AllSoundOff) {
                c.kind = e.cm_type == AllSoundOff ? Command::Kind::SoundOff : Command::Kind::AllOff;
                Push(c);
            }
            break;
        case SystemCommon:
            if(usb && e.sc_type == SystemExclusive && bootloader::IsRequest(e))
                boot_request.Set(bootloader::Request::kSysEx);
            break;
        default: break;
    }
}

static void ServiceInput() {
    usb_midi.Listen();
    while(usb_midi.HasEvents())
        HandleEvent(usb_midi.PopEvent(), true);
    hw.midi.Listen();
    while(hw.midi.HasEvents())
        HandleEvent(hw.midi.PopEvent(), false);
}

static void ServiceLeds(uint32_t now) {
    static uint32_t last = 0, shown = ~0u;
    if(now - last < kLedPeriodMs)
        return;
    last = now;
    const uint32_t lit = led_state.load(std::memory_order_relaxed);
    if(lit == shown)
        return;
    for(int i = 0; i < 16; ++i)
        hw.SetLed(i, (lit >> i) & 1 ? 0.8f : 0.f);
    hw.UpdateLeds();
    shown = lit;
}

static void ServiceBootloader() {
    if(!boot_request.Pending())
        return;
    const uint32_t before = mute_ack.load(std::memory_order_acquire);
    Command c;
    c.kind = Command::Kind::Mute;
    Push(c);
    const uint32_t start = System::GetNow();
    while(mute_ack.load(std::memory_order_acquire) == before && System::GetNow() - start < 50)
        System::Delay(1);
    bootloader::Service(boot_request, hw, [](uint8_t* bytes, size_t size) { SendUsb(bytes, size); });
}
} // namespace fw

static bool Initialize() {
    hw.Init();
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(fw::kBlockSize);
    const float sample_rate = hw.seed.AudioSampleRate();
    for(auto& p : hw.pot)
        p.SetSampleRate(sample_rate / float(fw::kBlockSize * fw::kBlocksPerTick));
    // SDRAM is up only after hw.Init(): everything in it is constructed here.
    if(!reverb.Prepare({48000, fw::kBlockSize}, reverb_memory) || !echo.Prepare({48000, fw::kBlockSize}, echo_memory))
        return false;
    engine = new(engine_memory) audrey::Engine();
    engine->Init(sample_rate, &reverb, &echo);
    control.Init(sample_rate / float(fw::kBlockSize));
    for(auto& l : limiter)
        l.Init();
    meter.Init((SystemCoreClock / 48000u) * uint32_t(fw::kBlockSize));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_config);
    usb_midi.StartReceive();
    hw.midi.StartReceive();
    return true;
}

int main() {
    asm volatile("" : : "r"(audrey_3_firmware_version)); // Keep the version in the BIN.
    if(!Initialize())
        while(true)
            System::Delay(100);
    hw.seed.StartAudio(AudioCallback);
    while(true) {
        fw::ServiceInput();
        fw::ServiceLeds(System::GetNow());
        fw::ServiceBootloader();
    }
}
