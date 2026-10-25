// SPDX-License-Identifier: MIT
// Run the production AudioCallback and Controls() with libDaisy's pinned
// DigitalControl debouncer and a fake kit (4 s buffers, see the Makefile).
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include "memory_volume.h"
#include <cmath>
#include <cstdio>

// The card behind main.cpp's storage code: in memory, always mounted.
static MemoryVolume memoryCard;
namespace looper {
bool InitCard(bool (*present)()) { return present != nullptr; }
Volume& CardVolume() { return memoryCard; }
}  // namespace looper

static std::vector<uint8_t> Pcm16Stereo(uint32_t frames, int16_t value) {
    std::vector<uint8_t> b;
    auto put = [&](uint32_t v, int n) {
        for (int i = 0; i < n; ++i)
            b.push_back(uint8_t(v >> (8 * i)));
    };
    auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
    tag("RIFF");
    put(36 + frames * 4, 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(1, 2);
    put(2, 2);
    put(48000, 4);
    put(48000 * 4, 4);
    put(4, 2);
    put(16, 2);
    tag("data");
    put(frames * 4, 4);
    for (uint32_t i = 0; i < frames * 2; ++i)
        put(uint16_t(value), 2);
    return b;
}

static int failures = 0;
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

constexpr size_t kBlock = 48;
static uint64_t t = 0;
static float peak = 0.f, loopEnergy = 0.f;
static bool allFinite = true;

static void Blocks(unsigned count, float amplitude = 0.f) {
    float il[kBlock], ir[kBlock], ol[kBlock], orr[kBlock];
    const float* in[2] = {il, ir};
    float* out[2] = {ol, orr};
    for (unsigned b = 0; b < count; ++b) {
        for (size_t i = 0; i < kBlock; ++i, ++t) {
            il[i] = amplitude * float(std::sin(2.0 * 3.14159265358979 * 220.0 * double(t) / 48000.0));
            ir[i] = -1.f;  // ignored: no plug in the right input
        }
        AudioCallback(in, out, kBlock);
        ServiceStorage(daisy::System::now);  // the main loop between callbacks
        for (size_t i = 0; i < kBlock; ++i) {
            allFinite &= std::isfinite(ol[i]) && std::isfinite(orr[i]);
            peak = std::max({peak, std::fabs(ol[i]), std::fabs(orr[i])});
            loopEnergy += std::fabs(ol[i] - il[i]);
        }
        ++System::now;  // 48 samples = 1 ms
    }
}
static void Press(unsigned key, float amplitude = 0.f) {
    hw.pressed[key] = true;
    Blocks(15, amplitude);
    hw.pressed[key] = false;
    Blocks(15, amplitude);
}

int main() {
    hw.Init();
    auto* a = reinterpret_cast<looper::Frame*>(storageA);
    auto* b = reinterpret_cast<looper::Frame*>(storageB);
    std::uninitialized_fill_n(a, LOOPER_BUFFER_FRAMES, looper::Frame{});
    std::uninitialized_fill_n(b, LOOPER_BUFFER_FRAMES, looper::Frame{});
    CHECK(engine.Init(48000.f, a, b, LOOPER_BUFFER_FRAMES));
    memoryCard.files["0:/looper/loops/one.wav"] = Pcm16Stereo(24000, 8192);  // 0.5 s at 0.25
    cardPresent.store(hw.sd_detect.Pressed());
    InitStorage();
    CHECK(storageReady && engine.GetState() == looper::State::Empty);  // nothing saved yet
    hw.pot[knobs::Varispeed].value = .4f;  // unity
    hw.pot[knobs::Inertia].value = 0.f;
    hw.pot[knobs::Slope].value = .5f;
    hw.pot[knobs::Size].value = 1.f;  // whole loop
    Blocks(20);
    CHECK(engine.VarispeedTarget() == 0.f && engine.GetState() == looper::State::Empty);

    // Record a first take of about one second, then stop.
    Press(keys::Record, .5f);
    CHECK(engine.GetState() == looper::State::Recording);
    Blocks(1000, .5f);
    Press(keys::Record, .5f);
    CHECK(engine.GetState() == looper::State::Looping);
    CHECK(engine.Length() > 46000 && engine.Length() < 50000);
    CHECK((uiState.load() & ui::HasLoop) && (uiState.load() & ui::CanUndo));
    loopEnergy = 0.f;
    Blocks(500);
    CHECK(loopEnergy > 100.f);  // silent input, the loop plays

    // Play/pause.
    Press(keys::Play);
    CHECK(!engine.Playing() && !(uiState.load() & ui::Playing));
    Press(keys::Play);
    CHECK(engine.Playing());

    // Knobs: varispeed -24..+36 semitones, inertia 0..1.
    hw.pot[knobs::Varispeed].value = 1.f;
    Blocks(2);
    CHECK(engine.VarispeedTarget() == 36.f);
    hw.pot[knobs::Varispeed].value = 0.f;
    Blocks(2);
    CHECK(engine.VarispeedTarget() == -24.f);
    hw.pot[knobs::Varispeed].value = .5f;
    Blocks(2);
    CHECK(engine.VarispeedTarget() == 6.f);
    hw.pot[knobs::Varispeed].value = .4f;
    hw.pot[knobs::Inertia].value = .7f;
    Blocks(2);
    CHECK(engine.VarispeedTarget() == 0.f && engine.Inertia() == .7f);
    hw.pot[knobs::Inertia].value = 0.f;

    // Reverse toggles.
    Press(keys::Reverse);
    Blocks(20);
    CHECK(engine.Reversed() && (uiState.load() & ui::Reversed));
    Press(keys::Reverse);
    Blocks(20);
    CHECK(!engine.Reversed());

    // Overdub, then undo and redo with the same key.
    Press(keys::Record, .3f);
    CHECK(engine.Overdubbing());
    Blocks(300, .3f);
    Press(keys::Record);
    Blocks(50);
    CHECK(!engine.Overdubbing() && engine.CanUndo());
    Press(keys::Undo);
    CHECK(engine.CanRedo() && !engine.CanUndo() && (uiState.load() & ui::CanRedo));
    Press(keys::Undo);
    CHECK(engine.CanUndo() && !engine.CanRedo());

    // Erase is undoable.
    Press(keys::Erase);
    CHECK(engine.GetState() == looper::State::Empty && !(uiState.load() & ui::HasLoop));
    Press(keys::Undo);
    CHECK(engine.GetState() == looper::State::Looping);

    // Mode keys: S6 cycles, S9..S12 select directly.
    auto mode = [] { return (uiState.load() >> ui::kModeShift) & 3u; };
    Press(keys::LoopMode);
    CHECK(engine.GetLoopMode() == looper::LoopMode::Replace && mode() == 1);
    Press(keys::kFirstMode + 3);
    CHECK(engine.GetLoopMode() == looper::LoopMode::Resample && mode() == 3);
    Press(keys::LoopMode);
    CHECK(engine.GetLoopMode() == looper::LoopMode::SoundOnSound && mode() == 0);
    // Frippertronics: decay pot as in the manual, undo key locked.
    Press(keys::kFirstMode + 2);
    CHECK(engine.GetLoopMode() == looper::LoopMode::Frippertronics && mode() == 2);
    CHECK(!engine.CanUndo() && !(uiState.load() & (ui::CanUndo | ui::CanRedo)));
    const uint32_t before = engine.Length();
    Press(keys::Undo);
    Blocks(50);
    CHECK(!engine.OperationPending() && engine.Length() == before && !engine.CanRedo());
    hw.pot[knobs::Decay].value = 1.f;
    Blocks(200);
    CHECK(engine.FrippertronicsDecay() == 1.f && engine.FrippertronicsRetention() < 1e-3f);
    hw.pot[knobs::Decay].value = 0.f;
    Blocks(200);
    CHECK(std::fabs(engine.FrippertronicsRetention() - looper::kMaxRetention) < 1e-3f);
    Press(keys::kFirstMode);
    CHECK(engine.CanRedo());  // the redo of the erase above is visible again
    // Slice, Skip, Start and Size pots.
    hw.pot[knobs::Slice].value = 1.f;
    hw.pot[knobs::Skip].value = .5f;
    Blocks(2);
    CHECK(engine.SliceDivisor() == 16);
    hw.pot[knobs::Slice].value = 0.f;
    hw.pot[knobs::Skip].value = 0.f;
    hw.pot[knobs::Start].value = .5f;
    hw.pot[knobs::Size].value = .5f;
    Blocks(20);
    CHECK(engine.WindowStart() == engine.Length() / 2 && engine.WindowSize() < engine.Length());
    hw.pot[knobs::Start].value = 0.f;
    hw.pot[knobs::Size].value = 1.f;
    Blocks(20);
    CHECK(engine.WindowSize() == engine.Length());
    // Resample with the record key: the loop is replaced and undoable.
    Press(keys::kFirstMode + 3);
    Press(keys::Record);
    CHECK(engine.Resampling() && (uiState.load() & ui::Recording));
    Blocks(500);
    Press(keys::Record);
    Blocks(100);
    CHECK(!engine.Resampling() && engine.Length() > 500 * 48 && engine.Length() < 560 * 48);
    Press(keys::Undo);
    CHECK(engine.Length() == before);
    Press(keys::kFirstMode + 1);
    Press(keys::Seam);
    CHECK(engine.GetSeamMode() == looper::SeamMode::Crossfade && (uiState.load() & ui::Crossfade));

    // A held key with contact dropouts does not retrigger.
    const bool reversed = engine.Reversed();
    hw.pressed[keys::Reverse] = true;
    Blocks(15);
    for (unsigned dropout = 1; dropout <= 6; ++dropout) {
        hw.pressed[keys::Reverse] = false;
        Blocks(dropout);
        hw.pressed[keys::Reverse] = true;
        Blocks(15);
    }
    hw.pressed[keys::Reverse] = false;
    Blocks(20);
    CHECK(engine.Reversed() != reversed);

    // Recording until the 4 s buffer is full stops automatically.
    Press(keys::Erase);
    Blocks(50);
    Press(keys::Record, .9f);
    Blocks(4100, .9f);
    CHECK(engine.GetState() == looper::State::Looping && engine.Length() == LOOPER_BUFFER_FRAMES);
    Blocks(200, .9f);
    CHECK(allFinite && peak <= 1.f);

    // microSD: the full loop is saved after 2 s idle, S8 imports loops/one.wav,
    // and a power-up recalls the last saved loop.
    Blocks(4000);
    CHECK(memoryCard.files.count("0:/looper/loop.wav") && store.Saves() > 0);
    uint32_t shown = ~0u;
    ShowLeds(daisy::System::now, shown);
    CHECK(store.GetStatus(daisy::System::now) == looper::LoopStore::Status::Idle);
    Press(keys::Import);
    Blocks(100);
    CHECK(engine.Length() == 24000 && engine.StoredFrame(12000).left == .25f);
    CHECK(std::string(store.LastImportName()) == "one.wav" && engine.CanUndo());
    Blocks(3000);
    const uint32_t saved = store.Saves();
    CHECK(saved >= 2);
    CHECK(engine.Init(48000.f, a, b, LOOPER_BUFFER_FRAMES) && engine.GetState() == looper::State::Empty);
    InitStorage();  // power-up
    CHECK(engine.GetState() == looper::State::Looping && engine.Length() == 24000);
    CHECK(engine.StoredFrame(12000).right == .25f && !engine.CanUndo());

    // Bootloader entry: S13+S14 (no looper function) for 3 s, never during card I/O.
    hw.pressed[12] = hw.pressed[13] = true;
    Blocks(2990);
    ServiceBootloader();
    CHECK(System::bootloader_resets == 0);
    Blocks(20);
    ServiceBootloader();
    CHECK(System::bootloader_resets == 1 && System::bootloader_mode == System::DAISY_INFINITE_TIMEOUT);
    hw.pressed[12] = hw.pressed[13] = false;
    Blocks(15);
    hw.pressed[keys::Import] = true;
    for (unsigned i = 0; i < 20 && !store.Active(); ++i)
        Blocks(1);
    CHECK(store.Active());
    bootRequest.Set(bootloader::Request::kKeys);
    unsigned waited = 0;
    for (; waited < 1000 && store.Active(); ++waited) {
        ServiceBootloader();
        Blocks(1);
    }
    CHECK(System::bootloader_resets == 1 && waited > 0 && !store.Active());
    ServiceBootloader();
    CHECK(System::bootloader_resets == 2);
    hw.pressed[keys::Import] = false;
    Blocks(15);

    if (failures) {
        std::printf("FAILED: %d production callback check(s)\n", failures);
        return 1;
    }
    std::printf("PASS production callback: keys, knobs, LEDs state, auto-stop, bounded output, bootloader entry after card I/O\n");
    return 0;
}
