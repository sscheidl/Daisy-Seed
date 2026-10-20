#include "control.h"
#include <cassert>
#include <thread>
int main() {
    neb::Queue<unsigned,64> q;
    constexpr unsigned n=1000000;
    std::thread producer([&]{for(unsigned i=0;i<n;++i) while(!q.Push(i)) std::this_thread::yield();});
    for(unsigned i=0;i<n;++i) {unsigned got;while(!q.Pop(got)) std::this_thread::yield();assert(got==i);}
    producer.join();
}
