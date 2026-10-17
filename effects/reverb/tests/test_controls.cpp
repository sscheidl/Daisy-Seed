// Run production Controls() with libDaisy's pinned DigitalControl debouncer.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <iostream>
static void Tick(unsigned count) {
    for (unsigned n = 0; n < count; ++n) {
        Controls();
        ++daisy::System::now;
    }
}
int main() {
    hw.Init();
    controlInterval = 30;
    Tick(8);
    hw.pressed[5] = true;
    Tick(15);
    assert(held);
    for (unsigned dropout = 1; dropout <= 6; ++dropout) {
        hw.pressed[5] = false;
        Tick(dropout);
        hw.pressed[5] = true;
        Tick(15);
        assert(held);
    }
    hw.pressed[5] = false;
    Tick(9);
    hw.pressed[5] = true;
    Tick(15);
    assert(!held);
    hw.pressed[5] = false;
    Tick(9);
    hw.pressed[6] = true;
    Tick(15);
    assert(bypassed);
    hw.pressed[6] = false;
    Tick(3);
    hw.pressed[6] = true;
    Tick(15);
    assert(bypassed);
    std::cout << "PASS: production controls ignore 1–6 ms held-button dropouts\n";
}
