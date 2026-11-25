#include "engine.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>
struct InputFile : neb::File {
    std::vector<uint8_t> bytes;
    InputFile():bytes(44+8192) {
        const uint8_t header[]={ 'R','I','F','F',0x24,0x20,0,0,'W','A','V','E',
            'f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,
            'd','a','t','a',0,0x20,0,0 };
        std::memcpy(bytes.data(),header,44);
        for(unsigned i=44;i<bytes.size();++i) bytes[i]=uint8_t(i);
    }
    neb::FileError Open(unsigned) override {return neb::FileError::None;}
    uint32_t Size() const override {return uint32_t(bytes.size());}
    bool Read(uint32_t p,void* d,uint32_t n) override {assert(uint64_t(p)+n<=bytes.size());std::memcpy(d,bytes.data()+p,n);return true;}
    void Close() override {}
};
int main() {
    std::vector<int16_t> pcm(4096),a(8192),b(8192);
    neb::FileBridge bridge;neb::Engine engine;engine.Init(a.data(),b.data(),4096,pcm.data(),bridge);
    InputFile file;neb::Loader loader;loader.Init(file,pcm.data(),pcm.size()*2,bridge);
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        float in[neb::kBlock]{},left[neb::kBlock],right[neb::kBlock];
        while(!stop.load(std::memory_order_acquire)) {
            engine.Process(in,in,left,right,neb::kBlock);
            for(unsigned i=0;i<neb::kBlock;++i) assert(std::isfinite(left[i]) && std::isfinite(right[i]));
            std::this_thread::yield();
        }
    });
    for(unsigned i=0;i<128;++i) {
        assert(loader.Request(i%16+1));
        while(loader.state!=neb::Loader::State::Idle) {loader.Service();std::this_thread::yield();}
        assert(loader.active==i%16+1 && loader.error==neb::FileError::None);
    }
    stop.store(true,std::memory_order_release);audio.join();
}
