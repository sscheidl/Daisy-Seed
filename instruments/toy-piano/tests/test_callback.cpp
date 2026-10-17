// Execute the production callback with fake GPIO/ADC, but the real libDaisy
// DigitalControl and real DSP. Firmware main is compiled, never run on the host.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cmath>
#include <iostream>

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

int main() {
    hw.Init();
    piano.Init();
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
    assert(hw.led_transfers == 0); // No LED I2C transfer in audio callback.
    std::cout << "PASS: production callback + pinned debounce, 16 keys, 1-6 ms dropouts, release/repress, startup, hold, stereo\n";
}
