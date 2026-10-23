// Bit-exact sound of the production callback for a fixed performance (keys, pots,
// model switch, USB-MIDI notes, CCs and pitch bend). The hash was recorded with the
// unchanged v0.1.0 firmware (GCC 13.3, glibc 2.39, as on the CI runner). The
// separate Mod on Daisy build (MOD=1) must reproduce it with an empty matrix.
#define main firmware_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace physical_modeling;
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }

namespace {
constexpr uint64_t kV010Hash = 0x37DE0BF9D93B81CEull; // recorded with v0.1.0, see header comment

double energy = 0;

uint64_t Perform() {
    uint64_t hash = 1469598103934665603ull;
    float left[48], right[48];
    float* output[] = {left, right};
    for(unsigned block = 0; block < 4000; ++block) {
        for(unsigned i = 0; i < 8; ++i)
            hw.pot[i].value = 0.2f + 0.08f * float(i) + (block > 2000 ? 0.1f : 0.0f);
        hw.tog_sw17.value = block >= 2600;
        hw.tog_sw18.value = block < 1300 ? 1 : (block < 2600 ? 0 : 2);
        for(unsigned key = 0; key < 16; ++key) {
            const unsigned start = 30 + 150 * key;
            hw.pressed[key] = block >= start && block < start + 40;
        }
        if(block % 170 == 20) {
            midi.incoming.push_back({daisy::NoteOn, 0, {uint8_t(40 + (block / 170) * 7 % 48), uint8_t(40 + block % 80)}});
            midi.incoming.push_back({daisy::ControlChange, 0, {uint8_t(20 + (block / 170) % 8), uint8_t(block % 128)}});
            midi.incoming.push_back({daisy::ControlChange, 0, {1, uint8_t(block % 128)}}); // ignored in v0.1.0
        }
        if(block % 170 == 90)
            midi.incoming.push_back({daisy::PitchBend, 0, {uint8_t(block % 128), uint8_t(32 + block % 64)}});
        ServiceMidi();
        AudioCallback(nullptr, output, 48);
        daisy::System::now++;
        for(unsigned i = 0; i < 48; ++i) {
            for(float v : {left[i], right[i]}) {
                energy += double(v) * double(v);
                uint32_t bits;
                std::memcpy(&bits, &v, sizeof(bits));
                for(int b = 0; b < 4; ++b) {
                    hash ^= (bits >> (8 * b)) & 0xFF;
                    hash *= 1099511628211ull;
                }
            }
        }
    }
    return hash;
}
}

int main() {
    hw.Init();
    cpu_meter.Init(48000, 48);
    InitializeSynth();
#if PHYSICAL_MODELING_MOD
    InitModulation(48000.0f); // empty matrix: must not change a single sample
#endif
    const uint64_t hash = Perform();
    std::printf("golden performance hash: %016" PRIX64 ", RMS %.4f\n", hash, std::sqrt(energy / (4000.0 * 96)));
    assert(energy > 10.0); // the performance actually sounds
    if(kV010Hash)
        assert(hash == kV010Hash);
    std::printf("PASS: output bit-identical to the v0.1.0 recording\n");
}
