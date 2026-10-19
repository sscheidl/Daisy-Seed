#include "desktop_devkit.h"
#include "physical_modeling.h"
#include "midi_control.h"
#include "spsc_queue.h"
#include "version.h"
#include <atomic>
#include <new>
namespace {
using namespace physical_modeling;
const char kFirmwareId[] = "PHYSICAL-MODELING-VERSION=" PHYSICAL_MODELING_VERSION;
daisy::desktop_devkit::Hardware hw;
daisy::MidiUsbHandler midi;
// SDRAM is initialized by hw.Init(). Construct DSP there only afterwards.
alignas(Synth) unsigned char DSY_SDRAM_BSS synth_storage[sizeof(Synth)];
Synth* synth = nullptr;
SpscQueue<MidiMessage, 256> midi_in;
std::atomic<uint32_t> led_mask{0};
ModelSwitch model_switch;
uint32_t held = 0;
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(daisy::desktop_devkit::kNumButtons == kNotes.size());

void InitializeSynth() {
    // .sdram_bss is NOLOAD and is not cleared by the BOOT_SRAM startup.
    // Upstream DSP constructors assume zero-backed static storage.
    std::fill_n(synth_storage, sizeof(synth_storage), static_cast<unsigned char>(0));
    synth = new(synth_storage) Synth{};
    synth->Init();
}

void AudioCallback(daisy::AudioHandle::InputBuffer,
                   daisy::AudioHandle::OutputBuffer out, size_t size) {
    hw.UpdateAllControls();
    for(unsigned i = 0; i < 8; ++i) synth->parameters.Pot(i, hw.pot[i].Value());
    unsigned model;
    if(model_switch.Process(hw.tog_sw17.Pressed(), hw.tog_sw18.Read(), model))
        synth->parameters.SelectModel(model);
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
        for(unsigned i = 0; i < 32 && midi_in.Pop(message); ++i) HandleMidi(*synth, message);
    }
    led_mask.store(held, std::memory_order_relaxed);
    synth->Process(out[0], out[1], size);
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
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(kBlockSize);
    for(unsigned i = 0; i < 100; ++i) {
        for(auto& pot : hw.pot) pot.Process();
        daisy::System::Delay(1);
    }
    daisy::MidiUsbHandler::Config config;
    config.transport_config.periph = daisy::MidiUsbTransport::Config::EXTERNAL;
    midi.Init(config); midi.StartReceive();
    hw.seed.StartAudio(AudioCallback);
    while(true) { ServiceMidi(); ServiceLeds(); daisy::System::Delay(1); }
}
