// SPDX-License-Identifier: MIT
// Bit-exact sound of the production callback for a fixed performance: a first take,
// overdubs, reverse, all four loop modes, every pot, MIDI CC knobs and keys, and the
// messages only Mod on Daisy consumes (CC1, aftertouch, clock, notes, CV and gates).
// The Makefile builds this file twice, as the regular image and as the MOD build
// (LOOPER_MOD) with an empty matrix, and requires the same FNV-1a hash from both.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include "memory_volume.h"
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

// main.cpp's storage hooks; the performance runs without a card.
static MemoryVolume memoryCard;
namespace looper {
bool InitCard(bool (*)()) { return false; }
Volume& CardVolume() { return memoryCard; }
}  // namespace looper
#include "sd_management_fakes.h"

namespace {
constexpr size_t kBlock = 48;
constexpr unsigned kBlocks = 9000;

void Event(daisy::MidiMessageType type, uint8_t a, uint8_t b) {
    daisy::MidiEvent e;
    e.type = type;
    e.channel = int(a % 16);
    e.data[0] = a;
    e.data[1] = b;
    usbMidi.incoming.push_back(e);
}
void RealTime(daisy::SystemRealTimeType type) {
    daisy::MidiEvent e;
    e.type = daisy::SystemRealTime;
    e.srt_type = type;
    usbMidi.incoming.push_back(e);
}

// A key held for 15 blocks from `start` (debounced by the real DigitalControl).
bool Held(unsigned block, unsigned start) { return block >= start && block < start + 15; }

double energy = 0;

uint64_t Perform() {
    uint64_t hash = 1469598103934665603ull;
    float il[kBlock], ir[kBlock], ol[kBlock], orr[kBlock];
    const float* in[2] = {il, ir};
    float* out[2] = {ol, orr};
    uint64_t t = 0;
    for (unsigned block = 0; block < kBlocks; ++block) {
        // Pots: unity speed, whole loop; later every pot moves.
        hw.pot[knobs::Varispeed].value = block < 2500 ? .4f : (block < 3500 ? .5f : .35f);
        hw.pot[knobs::Inertia].value = block < 3000 ? 0.f : .4f;
        hw.pot[knobs::Slope].value = block < 5000 ? .5f : .8f;
        hw.pot[knobs::Start].value = block < 4000 ? 0.f : .25f;
        hw.pot[knobs::Size].value = block < 4000 ? 1.f : .6f;
        hw.pot[knobs::Slice].value = block < 4500 ? 0.f : .7f;
        hw.pot[knobs::Skip].value = block < 4500 ? 0.f : .6f;
        hw.pot[knobs::Decay].value = .3f;
        // Inputs Mod on Daisy reads in the MOD build; they must not change the sound.
        hw.cv[0].value = .5f + .4f * float(std::sin(double(block) * .01));
        hw.cv[1].value = float(block % 100) / 100.f;
        hw.gate_in[0].high = (block / 50) % 2;
        hw.gate_in[1].high = (block / 80) % 2;
        // Keys: first take 20..1020, overdub, reverse, undo, modes, seam.
        hw.pressed[keys::Record] = Held(block, 20) || Held(block, 1020) || Held(block, 1800) ||
                                   Held(block, 2200) || Held(block, 6200) || Held(block, 6700);
        hw.pressed[keys::Reverse] = Held(block, 2600) || Held(block, 3200);
        hw.pressed[keys::Undo] = Held(block, 2400);
        hw.pressed[keys::Seam] = Held(block, 3800);
        hw.pressed[keys::kFirstMode + 2] = Held(block, 5200);  // Frippertronics
        hw.pressed[keys::kFirstMode + 1] = Held(block, 6000);  // Replace
        hw.pressed[keys::kFirstMode + 3] = Held(block, 6500);  // Resample
        hw.pressed[keys::kFirstMode] = Held(block, 7500);      // Sound on Sound
        hw.pressed[keys::Play] = Held(block, 8000) || Held(block, 8300);
        // MIDI over J7: the looper's CCs and what only the matrix consumes.
        if (block % 97 == 13) {
            Event(daisy::ControlChange, 1, uint8_t(block % 128));                 // mod wheel
            Event(daisy::ChannelPressure, uint8_t(block % 128), 0);               // aftertouch
            Event(daisy::PolyphonicKeyPressure, 60, uint8_t(block % 128));        // poly AT
            Event(daisy::NoteOn, uint8_t(48 + block % 24), 100);                  // observed only
            Event(daisy::ControlChange, 7, 100);                                  // nobody's
            RealTime(daisy::TimingClock);
        }
        if (block % 97 == 50)
            Event(daisy::NoteOff, uint8_t(48 + (block - 37) % 24), 0);
        if (block == 3000)
            RealTime(daisy::Start);
        if (block == 5600)
            Event(daisy::ControlChange, 20, 70);  // CC20: +6 semitones, takes the knob over
        if (block == 5700)
            Event(daisy::ControlChange, 26, 90);  // CC26: Skip
        if (block == 7000 || block == 7020) // CC105: reverse, press and release
            Event(daisy::ControlChange, 105, block == 7000 ? 127 : 0);
        if (block == 7800 || block == 7820)  // CC104: undo
            Event(daisy::ControlChange, 104, block == 7800 ? 127 : 0);
        ServiceUsbMidi();
        for (size_t i = 0; i < kBlock; ++i, ++t) {
            const double phase = 2.0 * 3.14159265358979 * double(t) / 48000.0;
            il[i] = float(.4 * std::sin(220.0 * phase) + .1 * std::sin(1375.0 * phase));
            ir[i] = 0.f;  // no plug in the right input: the left feeds both
        }
        if (block >= 6200 && block < 6700)
            for (float& v : il)
                v *= .5f;
        AudioCallback(in, out, kBlock);
        ++daisy::System::now;
        for (size_t i = 0; i < kBlock; ++i) {
            for (float v : {ol[i], orr[i]}) {
                energy += double(v - il[i]) * double(v - il[i]);
                uint32_t bits;
                std::memcpy(&bits, &v, sizeof(bits));
                for (int b = 0; b < 4; ++b) {
                    hash ^= (bits >> (8 * b)) & 0xFF;
                    hash *= 1099511628211ull;
                }
            }
        }
    }
    return hash;
}
}  // namespace

int main() {
    hw.Init();
    auto* a = reinterpret_cast<looper::Frame*>(storageA);
    auto* b = reinterpret_cast<looper::Frame*>(storageB);
    std::uninitialized_fill_n(a, LOOPER_BUFFER_FRAMES, looper::Frame{});
    std::uninitialized_fill_n(b, LOOPER_BUFFER_FRAMES, looper::Frame{});
    if (!engine.Init(48000.f, a, b, LOOPER_BUFFER_FRAMES))
        return 1;
#if LOOPER_MOD
    InitModulation(48000.f);  // empty matrix: must not change a single sample
#endif
    const uint64_t hash = Perform();
    // Everything that depends on the build goes to stderr; stdout must match exactly.
#if LOOPER_MOD
    std::fprintf(stderr, "MOD build, empty matrix\n");
#else
    std::fprintf(stderr, "regular build\n");
#endif
    if (energy < 10.0 || engine.Length() == 0 || !usbMidi.sent.empty()) {
        std::printf("FAILED: the performance must play a loop (loop energy %.1f) and send nothing\n",
                    energy);
        return 1;
    }
    std::printf("golden performance hash: %016" PRIX64 ", loop RMS %.4f\n", hash,
                std::sqrt(energy / (double(kBlocks) * kBlock * 2)));
    return 0;
}
