// MIT. Main/callback hand-over under two real threads (run with -fsanitize=thread on
// Linux, make test-thread): SPSC queue order, triple-buffer snapshots that are never torn,
// and the note feed with Note-Off reconciliation while the producer keeps running.
#include "step_seq/engine.h"
#include "step_seq/midi_out.h"
#include "step_seq/spsc.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace step_seq;

struct Snap {
    uint32_t a = 0, b = 0, c = 0;
};

int main() {
    int failures = 0;
    // SPSC: every value arrives once and in order.
    SpscQueue<uint32_t, 64> q;
    std::thread producer([&] {
        for(uint32_t i = 1; i <= 200000;)
            if(q.Push(i))
                ++i;
    });
    uint32_t expected = 1;
    while(expected <= 200000) {
        uint32_t v;
        if(q.Pop(v)) {
            if(v != expected)
                ++failures;
            ++expected;
        }
    }
    producer.join();
    // Triple buffer: a reader only ever sees complete snapshots.
    TripleBuffer<Snap> tb;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for(uint32_t i = 1; i < 300000; ++i) {
            Snap& s = tb.Back();
            s.a = i;
            s.b = i * 3u;
            s.c = i ^ 0x5A5Au;
            tb.Publish();
        }
        done = true;
    });
    uint32_t last = 0;
    while(!done) {
        if(tb.Update()) {
            const Snap& s = tb.Front();
            if(s.b != s.a * 3u || s.c != (s.a ^ 0x5A5Au) || s.a < last)
                ++failures;
            last = s.a;
        }
    }
    writer.join();
    // Note feed: the callback thread runs the engine, main sends; at the end nothing hangs.
    NoteFeed feed;
    std::vector<int> sent_on(128, 0);
    struct Sink {
        void operator()(uint8_t*, int) const {}
    };
    MidiPort<Sink> usb(Sink{}, 1 << 30), din(Sink{}, 24);
    std::atomic<bool> stop{false};
    std::thread callback([&] {
        Engine engine;
        engine.Init();
        engine.settings.tempo = 300;
        for(auto& s : engine.pattern)
            s.gate_percent = 100;
        TickInput in;
        in.run_switch = true;
        TickOutput out;
        for(int i = 0; i < 30000; ++i) {
            if(i == 29000)
                in.run_switch = false; // stop with panic
            engine.Tick(in, out);
            feed.Feed(out);
        }
        stop = true;
    });
    while(!stop)
        ServiceMidi(feed, 1, true, usb, din);
    callback.join();
    for(int i = 0; i < 10; ++i)
        ServiceMidi(feed, 1, true, usb, din);
    if(usb.AnySent(1) || din.AnySent(1))
        ++failures;
    if(failures) {
        std::printf("FAIL: %d thread checks\n", failures);
        return 1;
    }
    std::printf("Step Seq thread tests passed (SPSC order, untorn snapshots, note feed without hanging notes).\n");
    return 0;
}
