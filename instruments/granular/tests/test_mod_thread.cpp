// ThreadSanitizer: the three hand-overs of the Nebulae Mod on Daisy build, each with the
// firmware's producer and consumer on their own thread. Every value read must be complete
// and untorn; dropped live events and frames are allowed (the tap reports them).
#include "mod_bridge.h"
#include "modulation.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

static int Tap() {
    static neb::modbridge::MidiTap tap;
    std::atomic<bool> done{false};
    std::thread usb([&] {
        for(unsigned n = 0; n < 100000; ++n) {
            const uint8_t v = uint8_t(n & 0x7F);
            const uint8_t bytes[] = {0x90, v, v, 0xF0, 0x7D, 0x4D, 0x44, v, v, v, v, 0xF7};
            for(uint8_t b : bytes) tap.Byte(b);
        }
        done.store(true);
    });
    unsigned events = 0, frames = 0;
    neb::modbridge::LiveEvent e;
    neb::modbridge::SysExFrame f;
    for(;;) {
        const bool finished = done.load();
        bool any = false;
        while(tap.Pop(e)) {
            any = true;
            if(e.status != 0x90 || e.data0 != e.data1) { std::puts("FAIL torn live event"); return 1; }
            ++events;
        }
        while(tap.Pop(f)) {
            any = true;
            if(f.size != 7) { std::puts("FAIL torn frame size"); return 1; }
            for(unsigned i = 4; i < 7; ++i) if(f.body[i] != f.body[3]) { std::puts("FAIL torn frame"); return 1; }
            ++frames;
        }
        if(finished && !any) break;
    }
    usb.join();
    if(!events || !frames) { std::puts("FAIL nothing taken"); return 1; }
    std::printf("tap: %u live events, %u frames\n", events, frames);
    return 0;
}

// Audio reads while the timer publishes, and the other way round. A frame holds one
// counter in all eight columns, so a torn frame shows up as two different values.
static int Frames() {
    static mod::TripleBuffer<neb::columns::Frame> offsets, bases;
    std::atomic<bool> done{false};
    std::thread timer([&] {
        for(unsigned n = 1; n <= 500000; ++n) {
            neb::columns::Frame& f = offsets.Back();
            for(float& v : f.value) v = float(n & 0xFFFF);
            offsets.Publish();
            if(bases.Fetch()) {
                const float first = bases.Front().value[0];
                for(float v : bases.Front().value) if(v != first) { std::puts("FAIL torn bases"); std::abort(); }
            }
        }
        done.store(true);
    });
    unsigned taken = 0, n = 0;
    while(!done.load()) {
        if(offsets.Fetch()) {
            ++taken;
            const float first = offsets.Front().value[0];
            for(float v : offsets.Front().value) if(v != first) { std::puts("FAIL torn offsets"); return 1; }
        }
        neb::columns::Frame& f = bases.Back();
        ++n;
        for(float& v : f.value) v = float(n & 0xFFFF);
        bases.Publish();
    }
    timer.join();
    if(!taken) { std::puts("FAIL nothing taken"); return 1; }
    std::printf("frames: %u offset frames taken\n", taken);
    return 0;
}

int main() {
    if(Tap() || Frames()) return 1;
    std::puts("PASS Mod on Daisy hand-overs under threads");
    return 0;
}
