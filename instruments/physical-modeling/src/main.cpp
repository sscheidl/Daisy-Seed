#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "physical_modeling.h"
#include "midi_control.h"
#include "spsc_queue.h"
#include "version.h"
#include "cpu_telemetry.h"
#include "usbd_core.h"
#include <atomic>
#include <new>
#if PHYSICAL_MODELING_COMPOSED
#include "composed_version.h"
#endif
#if PHYSICAL_MODELING_MOD
#include "modulation.h"
#include "mod_on_daisy_desktop.h"
#include <cmath>
#endif
extern "C" USBD_HandleTypeDef hUsbDeviceHS;
namespace {
using namespace physical_modeling;
#if PHYSICAL_MODELING_COMPOSED && PHYSICAL_MODELING_AMBIENT
const char kFirmwareId[] = "PHYSICAL-MODELING-FX-AMBIENT-VERSION=" PHYSICAL_MODELING_COMPOSED_VERSION;
#elif PHYSICAL_MODELING_COMPOSED
const char kFirmwareId[] = "PHYSICAL-MODELING-FX-TORUS-VERSION=" PHYSICAL_MODELING_COMPOSED_VERSION;
#elif PHYSICAL_MODELING_MOD
const char kFirmwareId[] = "PHYSICAL-MODELING-MOD-VERSION=" PHYSICAL_MODELING_VERSION;
#else
const char kFirmwareId[] = "PHYSICAL-MODELING-VERSION=" PHYSICAL_MODELING_VERSION;
#endif
daisy::desktop_devkit::Hardware hw;
daisy::MidiUsbHandler midi;
// SDRAM is initialized by hw.Init(). Construct DSP there only afterwards.
alignas(Synth) unsigned char DSY_SDRAM_BSS synth_storage[sizeof(Synth)];
Synth* synth = nullptr;
SpscQueue<MidiMessage, 256> midi_in;
std::atomic<uint32_t> led_mask{0};
ModelSwitch model_switch;
CpuTelemetry cpu_meter;
std::atomic<uint32_t> cpu_request{0};
SpscQueue<CpuSnapshot, 4> cpu_snapshots;
bool cpu_reply_pending = false; // Main loop only; at most one outstanding request.
uint32_t held = 0;
// Software entry into the Daisy bootloader: S9+S16 for 5 s, or SysEx over J7.
bootloader::KeyCombo boot_keys; // audio callback only
bootloader::Request boot_request;
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(daisy::desktop_devkit::kNumButtons == kNotes.size());
#if PHYSICAL_MODELING_MOD
// Mod on Daisy (separate build, docs/MODULATION.md): grid, replies, Poly AT per voice.
mod::Module modulation;
const mod::InstrumentInfo kModInfo = MakeModInfo();
mod::adapter::UsbMidiSender mod_sender(midi);
mod::VoicePressure voice_pressure[kVoices];
uint8_t model_origin = mod::kModeUnknown; // audio IRQ only: switches or CC28 (mod4)

void InitModulation(float sample_rate) {
    modulation.Init(kModInfo, sample_rate, kBlockSize);
    // extended monitor, LFO cross modulation (mod3), active model (mod4)
    modulation.EnableExtensions({true, true, true});
}

// Audio IRQ: global offsets into the parameter smoothing, Poly AT into the voices.
void Modulate(size_t size) {
    mod::Engine& engine = modulation.engine;
    mod::adapter::ReadHardware(engine, hw);
    engine.Tick(size);
    for(unsigned t = 0; t < 8; ++t) {
        if(t == kLevelColumn)
            continue;
        synth->parameters.SetModOffset(t, engine.Offset(t));
        engine.ReportBase(t, synth->parameters.Target(t)); // pot/CC position, for the editor
    }
    synth->parameters.SetModLevel(LevelGain(engine.Apply(kLevelColumn, kLevelBase)));
    engine.ReportBase(kLevelColumn, kLevelBase);
    std::array<float, 5> gain{};
    bool voices = false;
    for(unsigned t = 0; t < 5; ++t) {
        gain[t] = engine.VoiceGain(t);
        voices = voices || gain[t] != 0.0f;
    }
    std::array<float, kVoices> pressure{};
    for(unsigned v = 0; v < kVoices; ++v) {
        const int note = int(std::lround(synth->VoiceNote(v)));
        pressure[v] = voice_pressure[v].Process(engine, note);
    }
    synth->SetVoiceModulation(voices, gain, pressure); // false: the shared upstream patch
}
#endif

void InitializeSynth() {
    // .sdram_bss is NOLOAD and is not cleared by the BOOT_SRAM startup.
    // Upstream DSP constructors assume zero-backed static storage.
    std::fill_n(synth_storage, sizeof(synth_storage), static_cast<unsigned char>(0));
    synth = new(synth_storage) Synth{};
    synth->Init();
}

void AudioCallback(daisy::AudioHandle::InputBuffer,
                   daisy::AudioHandle::OutputBuffer out, size_t size) {
    const uint32_t request = cpu_request.exchange(0, std::memory_order_relaxed);
    cpu_meter.Begin(request == 2);
    hw.UpdateAllControls();
#if PHYSICAL_MODELING_MOD
    Modulate(size);
#endif
    for(unsigned i = 0; i < 8; ++i) synth->parameters.Pot(i, hw.pot[i].Value());
    unsigned model;
    if(model_switch.Process(hw.tog_sw17.Pressed(), hw.tog_sw18.Read(), model)) {
        synth->parameters.SelectModel(model);
#if PHYSICAL_MODELING_MOD
        model_origin = mod::kModeFromDevice;
#endif
    }
    for(unsigned i = 0; i < kNotes.size(); ++i) {
        const uint32_t bit = 1u << i;
        if(hw.button[i].RisingEdge() && !(held & bit)) {
            held |= bit;
            synth->NoteOn(kNotes[i], 100);
        }
        if(hw.button[i].FallingEdge()) held &= ~bit;
    }
    // Leave queued MIDI intact when the DSP FIFO cannot accept an entire batch.
    if(synth->PendingNotes() <= 192) {
        MidiMessage message;
        for(unsigned i = 0; i < 32 && midi_in.Pop(message); ++i) {
#if PHYSICAL_MODELING_MOD
            if((message.status & 0xf0) == 0xb0 && message.data1 == 28) model_origin = mod::kModeFromMidi;
#endif
            HandleMidi(*synth, message);
        }
    }
#if PHYSICAL_MODELING_MOD
    modulation.engine.ReportMode(uint8_t(synth->ModelIndex()), model_origin); // the model that sounds
#endif
    led_mask.store(held, std::memory_order_relaxed);
    if(boot_keys.Process(held, daisy::System::GetNow()))
        boot_request.Set(bootloader::Request::kKeys);
    synth->Process(out[0], out[1], size);
    cpu_meter.End();
    if(request) cpu_snapshots.Push(cpu_meter.Snapshot(synth->ModelIndex()));
}
void ServiceMidi() {
    // Retry an event after backpressure; never discard a popped Note On.
    static bool pending = false;
    static MidiMessage message;
    midi.Listen();
    for(unsigned i = 0; i < 64; ++i) {
        if(pending) {
            if(!midi_in.Push(message)) return;
            pending = false;
        }
        if(!midi.HasEvents()) return;
        const auto event = midi.PopEvent();
        if(bootloader::IsRequest(event)) {
            boot_request.Set(bootloader::Request::kSysEx);
            continue;
        }
        if(event.type == daisy::ControlChange && CpuRequest(event.channel, event.data[0], event.data[1])) {
            if(!cpu_reply_pending) {
                cpu_reply_pending = true;
                cpu_request.store(event.data[1], std::memory_order_relaxed);
            }
            continue;
        }
#if PHYSICAL_MODELING_MOD
        mod::MidiMessage mod_message; // Mod on Daisy: SysEx, CC1, aftertouch; notes are observed
        if(mod::adapter::ToMessage(event, mod_message) && modulation.controller.HandleMidi(mod_message))
            continue;
#endif
        uint8_t status;
        if(event.type == daisy::NoteOn) status = 0x90;
        else if(event.type == daisy::NoteOff) status = 0x80;
        else if(event.type == daisy::ControlChange) status = 0xb0;
        else if(event.type == daisy::PitchBend) status = 0xe0;
        else continue;
        message = {uint8_t(status | (event.channel & 15)), event.data[0], event.data[1]};
        pending = true;
    }
}
void ServiceCpuTelemetry() {
    CpuSnapshot snapshot;
    if(!cpu_snapshots.Pop(snapshot)) return;
    cpu_reply_pending = false;
    if(hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED) return;
    auto packet = EncodeCpuSnapshot(snapshot);
    midi.SendMessage(packet.data(), packet.size()); // Main loop only, on request.
}
// Main loop only: replies over USB if a host asked, then resets into the bootloader.
void ServiceBootloader() {
    bootloader::Service(boot_request, hw, [](uint8_t* bytes, size_t size) {
        if(hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED)
            midi.SendMessage(bytes, size);
    });
}
void ServiceLeds() {
    static uint32_t previous = 0, last = 0;
    const uint32_t now = daisy::System::GetNow();
    if(now - last < 10) return;
    last = now;
    const uint32_t mask = led_mask.load(std::memory_order_relaxed);
    if(previous == mask) return;
    for(unsigned i = 0; i < 16; ++i) hw.SetLed(i, mask & (1u << i) ? 1.0f : 0.0f);
    hw.UpdateLeds();
    previous = mask;
}
}
int main() {
    asm volatile("" : : "r"(kFirmwareId));
    hw.Init();
    InitializeSynth();
#if PHYSICAL_MODELING_MOD
    InitModulation(48000.0f);
#endif
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(kBlockSize);
    cpu_meter.Init(hw.seed.AudioSampleRate(), kBlockSize);
    for(unsigned i = 0; i < 100; ++i) {
        for(auto& pot : hw.pot) pot.Process();
        daisy::System::Delay(1);
    }
    daisy::MidiUsbHandler::Config config;
    config.transport_config.periph = daisy::MidiUsbTransport::Config::EXTERNAL;
    midi.Init(config); midi.StartReceive();
    hw.seed.StartAudio(AudioCallback);
    while(true) {
        ServiceMidi(); ServiceCpuTelemetry(); ServiceBootloader(); ServiceLeds();
#if PHYSICAL_MODELING_MOD
        modulation.controller.Service(daisy::System::GetNow(), mod_sender);
#endif
        daisy::System::Delay(1);
    }
}
