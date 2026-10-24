// TEST_PLAN_PHASE2.md § 2.5: main loop and audio IRQ as two threads (run under TSan).
#include "common.h"
#include <atomic>
#include <cstdio>
#include <thread>

using namespace test;

namespace {
struct Checked {
    uint32_t serial = 0;
    uint32_t data[30]{};
    uint32_t sum = 0;
};

void TestTripleBuffer() {
    TripleBuffer<Checked> box;
    std::atomic<bool> done{false};
    constexpr uint32_t kCount = 200000;
    std::thread reader([&] {
        uint32_t last = 0;
        while(!done.load(std::memory_order_acquire) || last < kCount) {
            if(box.Fetch()) {
                const Checked& c = box.Front();
                uint32_t sum = 0;
                for(uint32_t v : c.data)
                    sum += v;
                assert(sum == c.sum);          // never a mixed value
                assert(c.serial >= last);      // never older than before
                last = c.serial;
            }
        }
    });
    for(uint32_t i = 1; i <= kCount; ++i) {
        Checked& c = box.Back();
        c.serial = i;
        c.sum = 0;
        for(uint32_t j = 0; j < 30; ++j) {
            c.data[j] = i * 31 + j;
            c.sum += c.data[j];
        }
        box.Publish();
    }
    done.store(true, std::memory_order_release);
    reader.join();
}

void TestModuleThreads() {
    Device d;
#if MOD_ON_DAISY_EXTENSIONS
    d.module.EnableExtensions({true, true}); // min/max windows cross the threads via an epoch
#endif
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        VoicePressure voice;
        float sink = 0;
        while(!stop.load(std::memory_order_relaxed)) {
            d.module.engine.SetHardware(0.3f, -0.2f, true, false);
            d.module.engine.ReportBase(1, 0.4f);
            d.module.engine.Tick(48);
            for(std::size_t t = 0; t < kTargets; ++t)
                sink += d.module.engine.ApplyVoice(t, 0.5f, voice.Process(d.module.engine, 60));
        }
        assert(sink == sink);
    });
    for(int i = 0; i < 20000; ++i) {
        d.Midi(0x90, uint8_t(60 + i % 12), 100);
        d.Midi(0xA0, uint8_t(60 + i % 12), uint8_t(i & 127));
        d.Midi(0xB0, 1, uint8_t(i & 127));
        d.Midi(0xD0, uint8_t(i & 127));
        d.Midi(0xF8, 0);
        if(i % 500 == 0)
            d.Midi(0xFA, 0);
        d.Midi(0x80, uint8_t(60 + (i + 6) % 12), 0);
        const uint8_t cell[4] = {uint8_t(i % 8), uint8_t(i % 6), uint8_t(i % 101), 2};
        d.Request(proto::SetCell, std::vector<uint8_t>(cell, cell + 4));
        if(i % 100 == 0)
            d.Request(proto::Monitor, Bytes({25}));
        if(i % 64 == 0)
            d.Request(proto::ClearMatrix, {});
#if MOD_ON_DAISY_EXTENSIONS
        if(i % 50 == 0)
            d.Request(proto::SetSource, Bytes({0, ParamFm, unsigned(i % 201) & 127, unsigned(i % 201) >> 7}));
#endif
    }
    stop.store(true, std::memory_order_relaxed);
    audio.join();
}
}

int main() {
    TestTripleBuffer();
    TestModuleThreads();
    std::printf("Mod on Daisy thread tests passed.\n");
}
