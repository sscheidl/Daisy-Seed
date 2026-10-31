#include "reader_dma.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while(0)
class Backend final : public diag::ReaderDmaBackend {
  public:
    uint32_t tick = 0, ready_at = 30, busy_until = 10, available_until = 100000;
    uint32_t error = 0, error_at_completion = 0;
    bool start_ok = true, initialized = true, usb_reset = false;
    uint32_t status_error = 0, usb_reset_at = 100000;
    bool force_available = false;
    unsigned starts = 0, aborts = 0;
    std::string order;
    uint8_t* dma_buffer = nullptr;
    std::array<uint8_t,512> written{};
    bool Available() override { return (force_available || tick < available_until) && !(usb_reset && tick >= usb_reset_at); }
    uint32_t Now() override { return tick++; }
    bool StartRead(uint8_t* p, uint32_t lba) override {
        if(!initialized) return false;
        CHECK(lba == 7); order += 'R'; dma_buffer = p; ++starts; return start_ok;
    }
    bool StartWrite(uint8_t* p, uint32_t lba) override {
        if(!initialized) return false;
        CHECK(lba == 7); order += 'W'; dma_buffer = p; ++starts;
        std::memcpy(written.data(),p,512); return start_ok;
    }
    bool Busy() override {
        if(tick < busy_until) return true;
        error = error_at_completion;
        if(order.find('R') != std::string::npos) std::memset(dma_buffer,0x5a,512);
        order += 'D'; return false;
    }
    uint32_t Error() override { return error; }
    bool CardReady() override {
        order += 'P'; error |= status_error; return tick >= ready_at;
    }
    void Clean(uint8_t* p) override { CHECK((reinterpret_cast<uintptr_t>(p)&31)==0); order += 'C'; }
    void Invalidate(uint8_t* p) override { CHECK((reinterpret_cast<uintptr_t>(p)&31)==0); order += 'I'; }
    void Abort() override { ++aborts; order += 'A'; initialized = false; }
};
void ReadWrite() {
    Backend b; alignas(32) uint8_t buffer[512]{};
    std::array<uint8_t,512> block{};
    diag::ReaderDma io(b,buffer);
    CHECK(io.Read(7,block.data()));
    CHECK(b.order.substr(0,3)=="CIR"); CHECK(b.order.back()=='I');
    CHECK(b.tick>=b.ready_at && b.aborts==0);
    for(auto value:block) CHECK(value==0x5a);
    Backend w; diag::ReaderDma writer(w,buffer);
    CHECK(writer.Write(7,block.data())); CHECK(w.order.substr(0,2)=="CW");
    CHECK(w.order.back()=='P'); CHECK(w.tick>=w.ready_at);
    CHECK(w.written==block && w.aborts==0);
}
void Errors() {
    for(unsigned mode=0;mode<7;++mode) {
        Backend b; alignas(32) uint8_t buffer[544]{};
        uint8_t destination[512]; std::memset(destination,0x33,512);
        diag::ReaderDma io(b, mode==6 ? buffer+1 : buffer);
        if(mode==0) b.start_ok=false;
        if(mode==1) b.busy_until=100000;
        if(mode==2) b.error_at_completion=0x20;
        if(mode==3) b.ready_at=100000;
        if(mode==4) b.available_until=4;
        if(mode==5) b.available_until=15;
        CHECK(!io.Read(7,destination)); CHECK(b.aborts==1);
        CHECK(b.tick<2100);  // no unbounded wait, including failed programming
        for(uint8_t value:destination) CHECK(value==0x33); // never publish partial DMA bytes
        if(mode==1) CHECK(io.Status().fault==diag::ReaderIoFault::DmaTimeout);
        if(mode==2) CHECK(io.Status().fault==diag::ReaderIoFault::Dma && io.Status().hal_error==0x20);
        if(mode==3) CHECK(io.Status().fault==diag::ReaderIoFault::ProgramTimeout);
        if(mode==4 || mode==5) CHECK(io.Status().fault==diag::ReaderIoFault::Unavailable);
        if(mode==6) CHECK(io.Status().fault==diag::ReaderIoFault::Alignment && b.starts==0);
        CHECK(b.error==io.Status().hal_error); // recovery preserves the operation error
    }
    {
        Backend b; alignas(32) uint8_t buffer[512]{};
        uint8_t source[512]{}; b.ready_at=100000;
        diag::ReaderDma io(b,buffer); CHECK(!io.Write(7,source));
        CHECK(io.Status().fault==diag::ReaderIoFault::ProgramTimeout);
    }
    {
        Backend b; alignas(32) uint8_t buffer[512]{};
        b.tick=0xfffffffe; b.busy_until=100000; b.force_available=true;
        diag::ReaderDma io(b,buffer); uint8_t block[512]{};
        CHECK(!io.Read(7,block)); // timeout remains bounded across the uint32 tick wrap
        CHECK(io.Status().fault==diag::ReaderIoFault::DmaTimeout);
        CHECK(io.Status().elapsed_ms>=1000 && io.Status().elapsed_ms<1100);
    }
}
void WriteAndSyncFailures() {
    // Write start, DMA timeout/error, removal during DMA/programming, USB reset,
    // programming timeout, and CMD13 errors (including an apparently READY card).
    for(unsigned mode=0;mode<8;++mode) {
        Backend b; alignas(32) uint8_t buffer[512]{}; uint8_t source[512]{};
        if(mode==0) b.start_ok=false;
        if(mode==1) b.busy_until=100000;
        if(mode==2) b.error_at_completion=0x20;
        if(mode==3) b.available_until=4;
        if(mode==4) b.available_until=15;
        if(mode==5) { b.usb_reset=true; b.usb_reset_at=4; }
        if(mode==6) b.ready_at=100000;
        if(mode==7) { b.status_error=0x40; b.ready_at=0; }
        diag::ReaderDma io(b,buffer);
        CHECK(!io.Write(7,source)); CHECK(b.aborts==1 && !b.initialized);
        CHECK(b.tick<2100);
        if(mode==7) CHECK(io.Status().fault==diag::ReaderIoFault::CardStatus && io.Status().hal_error==0x40);
        const auto starts=b.starts;
        CHECK(!io.Write(7,source)); CHECK(b.starts==starts); // RESET/HAL_BUSY cannot masquerade as success
    }
    for(unsigned mode=0;mode<5;++mode) {
        Backend b; alignas(32) uint8_t buffer[512]{};
        if(mode==1) b.ready_at=100000;
        if(mode==2) b.available_until=2;
        if(mode==3) b.status_error=0x80;
        if(mode==4) b.error=0x20;
        diag::ReaderDma io(b,buffer);
        CHECK(io.Sync()==(mode==0)); CHECK(b.starts==0);
        CHECK(b.aborts==(mode==0?0u:1u)); CHECK(b.tick<2100);
        if(mode==3 || mode==4) CHECK(io.Status().fault==diag::ReaderIoFault::CardStatus);
        CHECK(std::strcmp(io.Status().operation,"SYNC")==0);
    }
}
int main() {
    ReadWrite(); Errors(); WriteAndSyncFailures();
    std::printf("reader DMA: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
