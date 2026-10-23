#include "desktop_devkit.h"
#include "modulation.h"
#include "mod_on_daisy_desktop.h"
#include "spsc_queue.h"
#include "toy_piano.h"
#include "usbd_core.h"
#include "version.h"
#include <atomic>
#define TOY_PIANO_MODULATION 1 // tests: call InitModulation() before the callback

// libDaisy's USB device state for the external port (J7 on the carrier).
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace {
// Identifies a flashed image: `strings ToyPiano-v*.bin | grep TOYPIANO`.
// scripts/check-firmware.py compares it with src/version.h.
const char kFirmwareId[] = "TOYPIANO-FW-VERSION=" TOY_PIANO_VERSION;
daisy::desktop_devkit::Hardware hw;
toy_piano::ToyPiano piano;
toy_piano::PotFilter attack_pot, decay_pot;
std::atomic<uint32_t> led_mask{0};
daisy::MidiUsbHandler usb_midi;
struct NoteIn { uint8_t note, velocity; };
// Main loop -> audio callback: notes received over USB.
toy_piano::SpscQueue<NoteIn, toy_piano::kMidiQueueSize> midi_in;
// Keys the host has been told are down. Main loop only.
uint32_t sent_mask = 0;
// Mod on Daisy: matrix state, its USB reply path and Poly AT smoothing per voice.
mod::Module modulation;
const mod::InstrumentInfo kModInfo = toy_piano::MakeModInfo();
mod::adapter::UsbMidiSender mod_sender(usb_midi);
mod::VoicePressure voice_pressure[toy_piano::kNumVoices];
bool modulating = false; // audio callback only
static_assert(std::atomic<uint32_t>::is_always_lock_free, "LED mailbox must be lock-free");
static_assert(daisy::desktop_devkit::kNumButtons == toy_piano::kNumKeys);
static_assert(daisy::desktop_devkit::kNumLeds == toy_piano::kNumKeys);

void InitModulation(float sample_rate) {
    modulation.Init(kModInfo, sample_rate, toy_piano::kBlockSize);
}

// Per-voice pitch, level and harmonic from the matrix. Key voices have no Poly AT.
void ModulateVoices(size_t size) {
    const mod::Engine& engine = modulation.engine;
    if(!engine.Active()) {
        if(modulating)
            piano.ClearModulation(size);
        modulating = false;
        return; // no active connection: the voices render exactly as in v0.2
    }
    modulating = true;
    for(size_t v = 0; v < toy_piano::kNumVoices; ++v) {
        const int note = v < toy_piano::kNumKeys ? -1 : piano.MidiVoiceNote(v - toy_piano::kNumKeys);
        const float p = voice_pressure[v].Process(engine, note);
        const float pitch = engine.ApplyVoice(toy_piano::ModPitch, toy_piano::kPitchBase, p);
        const float level = engine.ApplyVoice(toy_piano::ModLevel, toy_piano::kLevelBase, p);
        const float harmonic = engine.ApplyVoice(toy_piano::ModHarmonic, toy_piano::kHarmonicBase, p);
        piano.Modulate(v, toy_piano::PitchRatio(pitch), toy_piano::LevelGain(level),
                       toy_piano::HarmonicWeight(harmonic), size);
    }
}

void AudioCallback(daisy::AudioHandle::InputBuffer,
                   daisy::AudioHandle::OutputBuffer out, size_t size) {
    hw.UpdateAllControls(); // Shift registers + official debouncing + ADC values.
    mod::adapter::ReadHardware(modulation.engine, hw);
    modulation.engine.Tick(size);
    // Attack and decay are latched at the strike; the matrix shifts the pot value.
    const float attack = modulation.engine.Apply(toy_piano::ModAttack, attack_pot.Process(hw.pot[0].Value()));
    const float decay = modulation.engine.Apply(toy_piano::ModDecay, decay_pot.Process(hw.pot[1].Value()));
    // Only convert times on a new note, then latch them for that one shot.
    static uint32_t held = 0;
    uint32_t triggers = 0;
    for(size_t key = 0; key < toy_piano::kNumKeys; ++key) {
        const uint32_t bit = 1u << key;
        // DigitalControl may report another rise after a brief contact dropout.
        if(hw.button[key].RisingEdge() && !(held & bit)) {
            triggers |= bit;
            held |= bit;
        }
        if(hw.button[key].FallingEdge())
            held &= ~bit; // No release event is sent to the envelope.
    }
    // Note-offs are ignored on purpose: every note is a one-shot AD envelope.
    NoteIn notes[toy_piano::kMaxMidiPerBlock];
    size_t num_notes = 0;
    while(num_notes < toy_piano::kMaxMidiPerBlock && midi_in.Pop(notes[num_notes]))
        ++num_notes;
    if(triggers || num_notes) {
        const float attack_seconds = toy_piano::LogTime(attack,
            toy_piano::kAttackMin, toy_piano::kAttackMax);
        const float decay_seconds = toy_piano::LogTime(decay,
            toy_piano::kDecayMin, toy_piano::kDecayMax);
        for(size_t key = 0; key < toy_piano::kNumKeys; ++key)
            if(triggers & (1u << key))
                piano.Trigger(key, attack_seconds, decay_seconds);
        for(size_t i = 0; i < num_notes; ++i)
            piano.NoteOn(notes[i].note, notes[i].velocity, attack_seconds, decay_seconds);
    }
    led_mask.store(held, std::memory_order_relaxed);
    ModulateVoices(size);
    for(size_t i = 0; i < size; ++i) {
        const float sample = piano.Process();
        out[0][i] = sample;
        out[1][i] = sample;
    }
}

// Sends the key changes since the last call as one USB transfer. It compares the
// debounced key state with what the host was told, so a note-off cannot get lost
// in a queue. Changes beyond the packet limit follow on the next call.
void SendKeyChanges(uint32_t mask, bool connected) {
    uint32_t changed = mask ^ sent_mask;
    if(!changed)
        return;
    if(!connected) { // Nobody listens: keys stay local.
        sent_mask = mask;
        return;
    }
    uint8_t message[3 * toy_piano::kMaxKeyMessagesPerPacket];
    size_t size = 0;
    for(size_t key = 0; key < toy_piano::kNumKeys && size < sizeof(message); ++key) {
        const uint32_t bit = 1u << key;
        if(!(changed & bit))
            continue;
        const bool pressed = (mask & bit) != 0;
        message[size++] = static_cast<uint8_t>((pressed ? 0x90 : 0x80) | toy_piano::kMidiOutChannel);
        message[size++] = toy_piano::kMidiNotes[key];
        message[size++] = pressed ? toy_piano::kMidiOutVelocity : uint8_t{0};
        sent_mask ^= bit;
    }
    usb_midi.SendMessage(message, size);
}

// Main loop only: USB parsing happens in the USB interrupt, but sending blocks.
void ServiceUsbMidi() {
    usb_midi.Listen(); // Restarts reception after an overflow.
    while(usb_midi.HasEvents()) {
        auto event = usb_midi.PopEvent();
        mod::MidiMessage message; // Mod on Daisy sees every event first (SysEx, CC1, AT)
        if(mod::adapter::ToMessage(event, message) && modulation.controller.HandleMidi(message))
            continue;
        if(event.type == daisy::NoteOn) {
            const auto note = event.AsNoteOn();
            if(note.velocity > 0)
                midi_in.Push({note.note, note.velocity});
        }
    }
    // Without an enumerated host the transmit path has no endpoint to use.
    const bool connected = hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED;
    SendKeyChanges(led_mask.load(std::memory_order_relaxed), connected);
    modulation.controller.Service(daisy::System::GetNow(), mod_sender); // at most one reply per ms
}
}

int main() {
    asm volatile("" : : "r"(kFirmwareId)); // Keep the version string in the image.
    hw.Init();
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(toy_piano::kBlockSize);
    piano.Init(hw.seed.AudioSampleRate());
    InitModulation(hw.seed.AudioSampleRate());
    // Let the mux ADC settle and populate AnalogControl values before use.
    for(unsigned i = 0; i < 100; ++i) {
        for(auto& pot : hw.pot)
            pot.Process();
        daisy::System::Delay(1);
    }
    daisy::MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = daisy::MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_config);
    usb_midi.StartReceive();
    hw.seed.StartAudio(AudioCallback);
    uint32_t last_led_update = daisy::System::GetNow();
    uint32_t previous_mask = 0;
    while(true) {
        ServiceUsbMidi();
        const uint32_t now = daisy::System::GetNow();
        if(now - last_led_update >= toy_piano::kLedIntervalMs) {
            last_led_update = now;
            const uint32_t mask = led_mask.load(std::memory_order_relaxed);
            if(mask != previous_mask) {
                for(size_t key = 0; key < toy_piano::kNumKeys; ++key)
                    hw.SetLed(key, (mask & (1u << key)) ? 1.0f : 0.0f);
                hw.UpdateLeds(); // I2C DMA/buffer swap stays outside audio IRQ.
                previous_mask = mask;
            }
        }
        daisy::System::Delay(1);
    }
}
