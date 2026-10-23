// Bit-exact sound of the production callback for a fixed performance of keys,
// pots and USB MIDI. The reference hash was recorded with the unchanged v0.2
// firmware (glibc 2.39, GCC 13.3, as on the CI runner). From v0.3 on, the same
// performance with an empty modulation matrix must give the same hash.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint64_t kV02Hash = 0x52580874FA24361Full; // recorded with v0.2, see header comment

daisy::MidiEvent Event(daisy::MidiMessageType type, uint8_t a, uint8_t b) {
    daisy::MidiEvent event;
    event.type = type;
    event.data[0] = a;
    event.data[1] = b;
    return event;
}

uint64_t Perform() {
    uint64_t hash = 1469598103934665603ull; // FNV-1a over the output sample bits
    float left[toy_piano::kBlockSize], right[toy_piano::kBlockSize];
    float* outputs[] = {left, right};
    for(unsigned block = 0; block < 9000; ++block) {
        hw.pot[0].value = block < 4000 ? 0.15f : 0.55f; // attack
        hw.pot[1].value = block < 6000 ? 0.62f : 0.35f; // decay
        for(std::size_t key = 0; key < toy_piano::kNumKeys; ++key) {
            const unsigned start = 100 + 230 * unsigned(key);
            hw.pressed[key] = block >= start && block < start + 70;
        }
        if(block % 280 == 50) {
            const uint8_t note = uint8_t(36 + (block / 280) * 5 % 60);
            usb_midi.incoming.push_back(Event(daisy::NoteOn, note, uint8_t(30 + block % 97)));
            usb_midi.incoming.push_back(Event(daisy::ControlChange, 1, uint8_t(block % 128)));
        }
        if(block % 280 == 200)
            usb_midi.incoming.push_back(Event(daisy::NoteOff, uint8_t(36 + (block / 280) * 5 % 60), 0));
        ServiceUsbMidi();
        AudioCallback(nullptr, outputs, toy_piano::kBlockSize);
        ++daisy::System::now;
        for(std::size_t i = 0; i < toy_piano::kBlockSize; ++i) {
            assert(left[i] == right[i]);
            uint32_t bits;
            std::memcpy(&bits, &left[i], sizeof(bits));
            for(int b = 0; b < 4; ++b) {
                hash ^= (bits >> (8 * b)) & 0xFF;
                hash *= 1099511628211ull;
            }
        }
    }
    return hash;
}
}

int main() {
    hw.Init();
    piano.Init();
#ifdef TOY_PIANO_MODULATION
    InitModulation(48000.0f);
#endif
    const uint64_t hash = Perform();
    std::printf("golden performance hash: %016" PRIX64 "\n", hash);
    if(kV02Hash)
        assert(hash == kV02Hash);
    std::printf("PASS: output with an empty matrix is bit-identical to v0.2\n");
}
