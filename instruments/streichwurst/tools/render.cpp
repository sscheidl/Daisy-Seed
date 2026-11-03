// SPDX-License-Identifier: MIT
// Own offline test signals of the composed product as 16-bit stereo WAV files (for
// listening on the PC; not original renders, no statement about the original sound).
//   render <directory>
#include "product.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace streichwurst;
using namespace streichwurst::product;

namespace {
InstrumentStorage keys;
streichwurst_fx::FxStorage fx_storage;
Product<> product;

void WriteWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if(!f)
        return;
    const uint32_t frames = uint32_t(l.size()), data = frames * 4, rate = 48000;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + data);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(rate);
    u32(rate * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(data);
    for(uint32_t i = 0; i < frames; ++i)
        for(float v : {l[i], r[i]}) {
            const float c = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
            u16(uint16_t(int16_t(std::lround(c * 32767.f))));
        }
    std::fclose(f);
}

void Render(const std::string& dir, const char* name, const State& s,
            const std::vector<std::pair<std::size_t, Event>>& events, std::size_t seconds) {
    product.Prepare(48000.f, 48, keys, fx_storage);
    product.Update(s, nullptr);
    product.Reset();
    const std::size_t blocks = seconds * 1000;
    std::vector<float> l(blocks * 48), r(blocks * 48);
    std::size_t next = 0;
    for(std::size_t b = 0; b < blocks; ++b) {
        std::vector<Event> ev;
        while(next < events.size() && events[next].first == b)
            ev.push_back(events[next++].second);
        product.Render(ev.data(), ev.size(), l.data() + b * 48, r.data() + b * 48, 48);
    }
    WriteWav(dir + "/" + name, l, r);
    std::printf("%s/%s\n", dir.c_str(), name);
}
} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::vector<std::pair<std::size_t, Event>> chord = {
        {0, NoteOn(0, 48, 100)}, {0, NoteOn(0, 55, 100)}, {0, NoteOn(0, 60, 100)}, {0, NoteOn(0, 64, 100)},
        {3000, NoteOff(0, 48)}, {3000, NoteOff(0, 55)}, {3000, NoteOff(0, 60)}, {3000, NoteOff(0, 64)}};
    for(int mode = 0; mode < 3; ++mode) {
        State s;
        s.fx.ensemble_mode = mode;
        const std::string name = "strings-mode" + std::to_string(mode) + ".wav";
        Render(dir, name.c_str(), s, chord, 5);
    }
    State dry;
    dry.fx.ensemble_on = false;
    dry.fx.reverb_wet = 0.f;
    Render(dir, "strings-dry.wav", dry, chord, 5);
    State sweep;
    sweep.fx.animate_amount = 0.6f;
    sweep.fx.phaser_amount = 0.5f;
    sweep.inst.string_octave = OctaveBoth;
    Render(dir, "strings-animate-phaser.wav", sweep, chord, 5);
    State solo;
    solo.inst.split_mode = SplitSplit;
    solo.inst.split_key = 60;
    solo.inst.solo_tremolo_depth = 0.3f;
    Render(dir, "split-strings-solo.wav", solo,
           {{0, NoteOn(0, 48, 100)}, {0, NoteOn(0, 55, 100)}, {500, NoteOn(0, 72, 110)}, {1500, NoteOff(0, 72)},
            {1600, NoteOn(0, 76, 110)}, {2600, NoteOff(0, 76)}, {3000, NoteOff(0, 48)}, {3000, NoteOff(0, 55)}},
           5);
    return 0;
}
