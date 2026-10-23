// Execute the production callback with fake GPIO/ADC, but the real libDaisy
// DigitalControl and real DSP. Firmware main is compiled, never run on the host.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; } // The USB state the firmware reads.
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
void Tick(unsigned count = 1) {
    float left[toy_piano::kBlockSize], right[toy_piano::kBlockSize];
    float* outputs[] = {left, right};
    for(unsigned i = 0; i < count; ++i) {
        AudioCallback(nullptr, outputs, toy_piano::kBlockSize);
        ++daisy::System::now;
        for(std::size_t sample = 0; sample < toy_piano::kBlockSize; ++sample)
            assert(std::isfinite(left[sample]) && left[sample] == right[sample]);
    }
}
}

namespace {
using Message = std::vector<uint8_t>;
daisy::MidiEvent Event(daisy::MidiMessageType type, uint8_t note, uint8_t velocity) {
    daisy::MidiEvent event;
    event.type = type;
    event.data[0] = note;
    event.data[1] = velocity;
    return event;
}
void TestUsbMidi() {
    // Only the key changes below may produce MIDI: start from silence.
    hw.pressed.fill(false);
    Tick(8);
    ServiceUsbMidi();
    usb_midi.sent.clear();

    // Host has not configured the device: key events are discarded, nothing is sent.
    hUsbDeviceHS.dev_state = USBD_STATE_ADDRESSED;
    hw.pressed[2] = true;
    Tick(10);
    ServiceUsbMidi();
    assert(usb_midi.sent.empty());
    hw.pressed[2] = false;
    Tick(8);
    ServiceUsbMidi();
    assert(usb_midi.sent.empty());

    // Configured: note-on with velocity 100 in key order in one packet, note-off on release.
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    hw.pressed[0] = hw.pressed[15] = true;
    Tick(10);
    ServiceUsbMidi();
    assert((usb_midi.sent == std::vector<Message>{{0x90, 60, 100, 0x90, 84, 100}}));
    hw.pressed[0] = hw.pressed[15] = false;
    Tick(8);
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 2);
    assert((usb_midi.sent[1] == Message{0x80, 60, 0, 0x80, 84, 0}));

    // A contact dropout while held sends neither a second note-on nor a note-off.
    usb_midi.sent.clear();
    hw.pressed[5] = true;
    Tick(20);
    ServiceUsbMidi();
    assert((usb_midi.sent == std::vector<Message>{{0x90, 69, 100}}));
    hw.pressed[5] = false;
    Tick(3);
    hw.pressed[5] = true;
    Tick(20);
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 1);
    hw.pressed[5] = false;
    Tick(8);
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 2 && usb_midi.sent[1] == (Message{0x80, 69, 0}));

    // A chord goes out as one transfer; an idle service call sends nothing.
    usb_midi.sent.clear();
    hw.pressed[1] = hw.pressed[2] = hw.pressed[3] = true;
    Tick(10);
    ServiceUsbMidi();
    ServiceUsbMidi();
    assert((usb_midi.sent == std::vector<Message>{{0x90, 62, 100, 0x90, 64, 100, 0x90, 65, 100}}));
    hw.pressed.fill(false);
    Tick(8);
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 2 && usb_midi.sent[1].size() == 9);

    // The main loop may be late: nothing is lost or left hanging. Presses and
    // releases that happen entirely between two service calls are never reported;
    // what is down at the next call is, and its release follows.
    usb_midi.sent.clear();
    for(unsigned round = 0; round < 80; ++round) {
        hw.pressed[round % toy_piano::kNumKeys] = true;
        Tick(10);
        hw.pressed[round % toy_piano::kNumKeys] = false;
        Tick(8);
    }
    hw.pressed[3] = true;
    Tick(10);
    ServiceUsbMidi();
    assert((usb_midi.sent == std::vector<Message>{{0x90, 65, 100}}));
    hw.pressed[3] = false;
    Tick(8);
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 2 && (usb_midi.sent[1] == Message{0x80, 65, 0}));

    // More changes than fit in one packet: the rest follows on the next calls.
    usb_midi.sent.clear();
    hw.pressed.fill(true);
    Tick(10);
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 1 && usb_midi.sent[0].size() == 3 * toy_piano::kMaxKeyMessagesPerPacket);
    assert(usb_midi.sent[0][1] == 60 && usb_midi.sent[0][22] == 72);
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 2 && usb_midi.sent[1][1] == 72 && usb_midi.sent[1][22] == 84);
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 2);
    hw.pressed.fill(false);
    Tick(8);
    ServiceUsbMidi();
    ServiceUsbMidi();
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 4);
    for(const Message& packet : {usb_midi.sent[2], usb_midi.sent[3]})
        for(std::size_t i = 0; i < packet.size(); i += 3)
            assert(packet[i] == 0x80 && packet[i + 2] == 0);

    // The pool starts from silence: let the key voices ring out first.
    usb_midi.sent.clear();
    Tick(3300);
    for(std::size_t i = 0; i < toy_piano::kNumMidiVoices; ++i)
        assert(piano.MidiVoiceNote(i) == -1);

    // Receive: only note-on with velocity > 0 plays; any channel; everything else is ignored.
    usb_midi.sent.clear();
    daisy::MidiEvent other_channel = Event(daisy::NoteOn, 100, 127);
    other_channel.channel = 9;
    usb_midi.incoming = {Event(daisy::NoteOn, 61, 100), other_channel,
                         Event(daisy::NoteOff, 61, 0), Event(daisy::NoteOn, 62, 0),
                         Event(daisy::ControlChange, 64, 127)};
    ServiceUsbMidi();
    assert(usb_midi.incoming.empty());
    Tick();
    assert(piano.MidiVoiceNote(0) == 61 && piano.MidiVoiceNote(1) == 100);
    for(std::size_t i = 2; i < toy_piano::kNumMidiVoices; ++i)
        assert(piano.MidiVoiceNote(i) == -1);
    assert(piano.MidiEnvelope(0).Active() && piano.MidiEnvelope(1).Level() > 0);
    assert(usb_midi.sent.empty()); // Received notes are never echoed back.
    Tick(3300);
    assert(piano.MidiVoiceNote(0) == -1 && piano.MidiVoiceNote(1) == -1);

    // A burst larger than the voice pool and the per-block limit: bounded per block, in order.
    for(uint8_t i = 0; i < 20; ++i)
        usb_midi.incoming.push_back(Event(daisy::NoteOn, 40 + i, 100));
    ServiceUsbMidi();
    Tick();
    for(std::size_t i = 0; i < toy_piano::kNumMidiVoices; ++i)
        assert(piano.MidiVoiceNote(i) == 40 + static_cast<int>(i));
    Tick();
    for(std::size_t i = 0; i < 4; ++i) // The four oldest voices are taken, oldest first.
        assert(piano.MidiVoiceNote(i) == 56 + static_cast<int>(i));
    // More than the queue holds in one millisecond: dropped without harm.
    for(unsigned i = 0; i < 100; ++i)
        usb_midi.incoming.push_back(Event(daisy::NoteOn, 30 + i % 40, 90));
    ServiceUsbMidi();
    Tick(8);
    Tick(3300);
    for(std::size_t i = 0; i < toy_piano::kNumMidiVoices; ++i)
        assert(piano.MidiVoiceNote(i) == -1);
    hUsbDeviceHS.dev_state = 0;
}
}

int main() {
    hw.Init();
    piano.Init();
    InitModulation(48000.0f); // v0.3: empty matrix, the v0.2 behavior below is unchanged
    hw.pot[1].value = 1.0f; // Long decay exposes unintended retriggers.

    // Held at audio startup: exactly the same seven-sample press qualification.
    hw.pressed[0] = true;
    Tick(6);
    assert(led_mask.load() == 0 && !piano.Envelope(0).Active());
    Tick();
    assert(led_mask.load() == 1 && piano.Envelope(0).Active());
    Tick(100);

    for(std::size_t key = 0; key < toy_piano::kNumKeys; ++key) {
        hw.pressed.fill(false);
        Tick(8);
        assert(led_mask.load() == 0);
        hw.pressed[key] = true;
        Tick(100);
        const uint32_t bit = 1u << key;
        assert(led_mask.load() == bit);
        // A dropout shorter than the release qualification must not retrigger,
        // even though DigitalControl reports a second RisingEdge on recovery.
        for(unsigned gap = 1; gap <= 6; ++gap) {
            float previous = piano.Envelope(key).Level();
            hw.pressed[key] = false;
            for(unsigned ms = 0; ms < gap + 10; ++ms) {
                if(ms == gap)
                    hw.pressed[key] = true;
                Tick();
                assert(led_mask.load() == bit);
                const float level = piano.Envelope(key).Level();
                assert(level <= previous);
                previous = level;
            }
        }
        // A qualified release allows the next real press to retrigger.
        hw.pressed[key] = false;
        Tick(7);
        assert(led_mask.load() == 0);
        const float before = piano.Envelope(key).Level();
        hw.pressed[key] = true;
        Tick(9);
        assert(led_mask.load() == bit && piano.Envelope(key).Level() > before);
    }

    hw.pressed.fill(false);
    Tick(8);
    hw.pressed.fill(true);
    Tick(10);
    assert(led_mask.load() == 0xffff);
    for(std::size_t key = 0; key < toy_piano::kNumKeys; ++key)
        assert(piano.Envelope(key).Active());
    Tick(3300);
    assert(led_mask.load() == 0xffff); // Silence after AD, LEDs still held.
    for(std::size_t key = 0; key < toy_piano::kNumKeys; ++key)
        assert(!piano.Envelope(key).Active());
    hw.pressed.fill(false);
    Tick(7);
    assert(led_mask.load() == 0);
    TestUsbMidi();
    assert(hw.led_transfers == 0); // No LED I2C transfer in audio callback.
    std::cout << "PASS: production callback + pinned debounce, 16 keys, 1-6 ms dropouts, release/repress, startup, hold, stereo, USB MIDI in/out\n";
}
