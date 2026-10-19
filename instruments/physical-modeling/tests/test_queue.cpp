#include "spsc_queue.h"
#include "desktop_devkit.h"
#include "cpu_telemetry.h"
#include <thread>
#include <cassert>
#include <cstdio>
int main() {
    physical_modeling::SpscQueue<unsigned,256> queue;
    std::thread producer([&] { for(unsigned i=0;i<100000;++i) while(!queue.Push(i)) std::this_thread::yield(); });
    for(unsigned i=0;i<100000;++i) { unsigned item; while(!queue.Pop(item)) std::this_thread::yield(); assert(item==i); }
    producer.join(); puts("PASS concurrent SPSC order, wrap and backpressure");
    physical_modeling::SpscQueue<physical_modeling::CpuSnapshot,4> snapshots;
    std::thread meter([&] {
        for(unsigned i=0;i<100000;++i) {
            physical_modeling::CpuSnapshot sample{.25f,.75f,i,i/3,uint8_t(i%6)};
            while(!snapshots.Push(sample)) std::this_thread::yield();
        }
    });
    for(unsigned i=0;i<100000;++i) {
        physical_modeling::CpuSnapshot sample;
        while(!snapshots.Pop(sample)) std::this_thread::yield();
        assert(sample.average==.25f && sample.maximum==.75f && sample.blocks==i && sample.overruns==i/3 && sample.model==i%6);
    }
    meter.join(); puts("PASS concurrent coherent CPU snapshots and backpressure");
}
