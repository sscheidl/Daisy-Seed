#include "spsc_queue.h"
#include <thread>
#include <cassert>
#include <cstdio>
int main() {
    physical_modeling::SpscQueue<unsigned,256> queue;
    std::thread producer([&] { for(unsigned i=0;i<100000;++i) while(!queue.Push(i)) std::this_thread::yield(); });
    for(unsigned i=0;i<100000;++i) { unsigned item; while(!queue.Pop(item)) std::this_thread::yield(); assert(item==i); }
    producer.join(); puts("PASS concurrent SPSC order, wrap and backpressure");
}
