// Exercise production Controls() with libDaisy's pinned DigitalControl debouncer.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <iostream>
static void Tick(unsigned count) {
    for (unsigned n = 0; n < count; ++n) {
        Controls();
        ++daisy::System::now; // 1 ms fake clock; real controls run at 1.5 kHz.
    }
}
static void Tap(unsigned key) {
    hw.pressed[key] = true;
    Tick(15);
    hw.pressed[key] = false;
    Tick(10);
}
int main() {
    hw.Init();
    controlInterval = 30;
    Tick(8);
    const ambient::Mode expected[5] = {ambient::Mode::Cloud, ambient::Mode::Halo,
                                       ambient::Mode::Scatter, ambient::Mode::Abyss,
                                       ambient::Mode::Bloom};
    for (unsigned i = 0; i < 5; ++i) {
        Tap(i);
        assert(parameters.mode == expected[i]);
        assert((uiState.load() & 7u) == i);
        RenderLeds(uiState.load());
        for (unsigned led = 0; led < 14; ++led)
            assert(hw.led[led] == (led == i ? .5f : 0.f));
    }
    Tap(0);
    for (unsigned i = 5; i < 14; ++i) {
        Tap(i);
        assert(parameters.mode == ambient::Mode::Cloud);
    }
    hw.tog_sw18.position = Switch3::POS_UP;
    Tick(20);
    assert(parameters.width == 1.f);
    hw.tog_sw18.position = Switch3::POS_DOWN;
    Tick(20);
    assert(parameters.width == 0.f);
    hw.tog_sw17.pressed = true;
    Tick(1);
    assert(legacyMono);
    hw.tog_sw17.pressed = false;
    Tick(1);
    assert(!legacyMono);

    Tap(15); // Dream/Cloud latches Sustain.
    assert(sustain && (uiState.load() & 8u));
    RenderLeds(uiState.load());
    assert(hw.led[15] == .5f && hw.led[14] == 0.f);
    hw.pressed[15] = true;
    Tick(15);
    for (unsigned dropout = 1; dropout <= 6; ++dropout) {
        hw.pressed[15] = false;
        Tick(dropout);
        hw.pressed[15] = true;
        Tick(15);
        assert(!sustain); // One real second press, no repeated latch toggles.
    }
    hw.pressed[15] = false;
    Tick(10);
    Tap(1);
    hw.pressed[15] = true;
    Tick(15);
    assert(sustain && parameters.sustain);
    hw.pressed[15] = false;
    Tick(10);
    assert(!sustain && !parameters.sustain);

    Tap(14); // Active -> bypass.
    assert(bypassed && (uiState.load() & 16u));
    RenderLeds(uiState.load());
    assert(hw.led[14] == .5f && hw.led[15] == 0.f);
    Tap(14); // Short press from bypass -> latched active.
    assert(!bypassed && !(uiState.load() & 16u));
    Tap(14); // Back to bypass.
    assert(bypassed);
    hw.pressed[14] = true;
    Tick(270); // Press from bypass temporarily enables the effect.
    assert(!bypassed);
    hw.pressed[14] = false;
    Tick(10);
    assert(bypassed && (uiState.load() & 16u));
    RenderLeds(uiState.load());
    assert(hw.led[14] == .5f && hw.ledUpdates > 0);
    // Bootloader entry: S13+S14 (reserved) alone for 3 s; no effect parameter changes.
    using daisy::System;
    const uint32_t ui = uiState.load();
    hw.pressed[12] = hw.pressed[13] = true;
    Tick(2990);
    assert(!bootloader::Service(bootRequest, hw) && System::bootloader_resets == 0);
    Tick(20);
    assert(bootloader::Service(bootRequest, hw) && System::bootloader_resets == 1);
    assert(System::bootloader_mode == System::DAISY_INFINITE_TIMEOUT && uiState.load() == ui);
    for (unsigned i = 0; i < 16; ++i)
        assert(hw.led[i] == 1.f);
    hw.pressed[12] = hw.pressed[13] = false;
    Tick(10);
    std::cout << "PASS: five modes, reserved buttons, switches, LED state, sustain, "
                 "bypass gestures, dropout suppression and bootloader keys\n";
}
