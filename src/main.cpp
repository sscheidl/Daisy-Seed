#include "desktop_devkit.h"
#include "spsc_queue.h"
#include "toy_piano.h"
#include "usbd_core.h"
#include <atomic>

// libDaisy's USB device state for the external port (J7 on the carrier).
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace {
daisy::desktop_devkit::Hardware hw;
toy_piano::ToyPiano piano;
toy_piano::PotFilter attack_pot, decay_pot;
std::atomic<uint32_t> led_mask{0};
daisy::MidiUsbHandler usb_midi;
struct NoteIn { uint8_t note, velocity; };
struct KeyOut { uint8_t key; bool pressed; };
// Main loop -> audio callback: notes received over USB.
toy_piano::SpscQueue<NoteIn, toy_piano::kMidiQueueSize> midi_in;
// Audio callback -> main loop: key edges to send over USB. Tx() can block briefly.
toy_piano::SpscQueue<KeyOut, toy_piano::kMidiQueueSize> midi_out;
static_assert(std::atomic<uint32_t>::is_always_lock_free, "LED mailbox must be lock-free");
static_assert(daisy::desktop_devkit::kNumButtons == toy_piano::kNumKeys);
static_assert(daisy::desktop_devkit::kNumLeds == toy_piano::kNumKeys);

void AudioCallback(daisy::AudioHandle::InputBuffer,
                   daisy::AudioHandle::OutputBuffer out, size_t size) {
    hw.UpdateAllControls(); // Shift registers + official debouncing + ADC filters.
    const float attack = attack_pot.Process(hw.pot[0].Value());
    const float decay = decay_pot.Process(hw.pot[1].Value());
    // Only convert times on a new note, then latch them for that one shot.
    static uint32_t held = 0;
    uint32_t triggers = 0;
    for(size_t key = 0; key < toy_piano::kNumKeys; ++key) {
        const uint32_t bit = 1u << key;
        if(hw.button[key].RisingEdge()) {
            triggers |= bit;
            held |= bit;
            midi_out.Push({static_cast<uint8_t>(key), true});
        }
        if(hw.button[key].FallingEdge()) {
            held &= ~bit; // No release event is sent to the envelope.
            midi_out.Push({static_cast<uint8_t>(key), false});
        }
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
    for(size_t i = 0; i < size; ++i) {
        const float sample = piano.Process();
        out[0][i] = sample;
        out[1][i] = sample;
    }
}

// Main loop only: USB parsing happens in the USB interrupt, but sending blocks.
void ServiceUsbMidi() {
    usb_midi.Listen(); // Restarts reception after an overflow.
    while(usb_midi.HasEvents()) {
        auto event = usb_midi.PopEvent();
        if(event.type == daisy::NoteOn) {
            const auto note = event.AsNoteOn();
            if(note.velocity > 0)
                midi_in.Push({note.note, note.velocity});
        }
    }
    // Without an enumerated host the transmit path has no endpoint to use.
    const bool connected = hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED;
    KeyOut key_event;
    while(midi_out.Pop(key_event)) {
        if(!connected)
            continue;
        uint8_t message[3] = {
            static_cast<uint8_t>((key_event.pressed ? 0x90 : 0x80) | toy_piano::kMidiOutChannel),
            toy_piano::kMidiNotes[key_event.key],
            key_event.pressed ? toy_piano::kMidiOutVelocity : uint8_t{0}};
        usb_midi.SendMessage(message, 3);
    }
}
}

int main() {
    hw.Init();
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(toy_piano::kBlockSize);
    piano.Init(hw.seed.AudioSampleRate());
    // Let the mux ADC and existing AnalogControl smoothing settle before use.
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
