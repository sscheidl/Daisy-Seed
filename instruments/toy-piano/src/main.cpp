#include "desktop_devkit.h"
#include "spsc_queue.h"
#include "toy_piano.h"
#include "usbd_core.h"
#include "version.h"
#include <atomic>

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
static_assert(std::atomic<uint32_t>::is_always_lock_free, "LED mailbox must be lock-free");
static_assert(daisy::desktop_devkit::kNumButtons == toy_piano::kNumKeys);
static_assert(daisy::desktop_devkit::kNumLeds == toy_piano::kNumKeys);

void AudioCallback(daisy::AudioHandle::InputBuffer,
                   daisy::AudioHandle::OutputBuffer out, size_t size) {
    hw.UpdateAllControls(); // Shift registers + official debouncing + ADC values.
    const float attack = attack_pot.Process(hw.pot[0].Value());
    const float decay = decay_pot.Process(hw.pot[1].Value());
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
        if(event.type == daisy::NoteOn) {
            const auto note = event.AsNoteOn();
            if(note.velocity > 0)
                midi_in.Push({note.note, note.velocity});
        }
    }
    // Without an enumerated host the transmit path has no endpoint to use.
    const bool connected = hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED;
    SendKeyChanges(led_mask.load(std::memory_order_relaxed), connected);
}
}

int main() {
    asm volatile("" : : "r"(kFirmwareId)); // Keep the version string in the image.
    hw.Init();
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(toy_piano::kBlockSize);
    piano.Init(hw.seed.AudioSampleRate());
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
