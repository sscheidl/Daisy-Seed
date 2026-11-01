// ThreadSanitizer: the control SysEx mailbox between the USB interrupt (producer
// thread) and the main loop (consumer). Every taken body must be complete and
// untorn; dropped requests are allowed (the host retries).
#include "control_sysex.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
int main() {
    neb::sysex::Collector c;
    std::atomic<bool> done{false};
    std::thread usb([&] {
        for(unsigned n = 0; n < 200000; ++n) {
            const uint8_t v = uint8_t(n & 0x7F);
            const uint8_t frame[] = {0xF0, 0x7D, 0x4E, 0x42, 0x01, 0x02, v, v, v, v, v, 0xF7};
            for(uint8_t b : frame) c.Byte(b);
        }
        done.store(true);
    });
    unsigned taken = 0;
    uint8_t body[neb::sysex::kMaxBody];
    size_t size = 0;
    while(!done.load() || c.Take(body, size)) {
        if(!c.Take(body, size)) continue;
        if(size != 7 || body[0] != 1 || body[1] != 2) { std::puts("FAIL torn header"); return 1; }
        for(size_t i = 3; i < 7; ++i) if(body[i] != body[2]) { std::puts("FAIL torn payload"); return 1; }
        ++taken;
    }
    usb.join();
    if(!taken) { std::puts("FAIL nothing taken"); return 1; }
    std::printf("PASS control SysEx mailbox under threads: %u complete requests taken\n", taken);
}
