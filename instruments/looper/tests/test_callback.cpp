// SPDX-License-Identifier: MIT
// Run the production AudioCallback and Controls() with libDaisy's pinned
// DigitalControl debouncer and a fake kit (4 s buffers, see the Makefile).
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include <cmath>
#include <cstdio>

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
static bool finite = true;

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
        for (size_t i = 0; i < kBlock; ++i) {
            finite &= std::isfinite(ol[i]) && std::isfinite(orr[i]);
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
    hw.pot[knobs::Varispeed].value = .4f;  // unity
    hw.pot[knobs::Inertia].value = 0.f;
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

    // Mode keys.
    Press(keys::LoopMode);
    CHECK(engine.GetLoopMode() == looper::LoopMode::Replace && (uiState.load() & ui::Replace));
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
    CHECK(finite && peak <= 1.f);

    if (failures) {
        std::printf("FAILED: %d production callback check(s)\n", failures);
        return 1;
    }
    std::printf("PASS production callback: keys, knobs, LEDs state, auto-stop, bounded output\n");
    return 0;
}
