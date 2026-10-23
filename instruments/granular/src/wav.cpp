#include "wav.h"
#include <cstring>

namespace neb {
static uint16_t U16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1])<<8; }
static uint32_t U32(const uint8_t* p) { return uint32_t(U16(p)) | uint32_t(U16(p+2))<<16; }
void WavParser::Begin(uint32_t bytes, size_t capacity) {
    *this = WavParser{}; size_=bytes; capacity_=capacity;
}
bool WavParser::Step(File& file) {
    if(done_) return true;
    auto fail = [&](FileError e) { error=e; done_=true; return true; };
    uint8_t b[24]{};
    if(pos_==0) {
        if(size_<12) return fail(FileError::Format);
        if(!file.Read(0,b,12)) return fail(FileError::Io);
        if(std::memcmp(b,"RIFF",4) || std::memcmp(b+8,"WAVE",4)) return fail(FileError::Format);
        // Exact RIFF extent, no concatenated files or truncated/trailing ambiguity.
        const uint64_t end=uint64_t(U32(b+4))+8;
        if(end != size_ || end<12) return fail(FileError::Format);
        end_=uint32_t(end); pos_=12; return false;
    }
    if(pos_==end_) {
        if(!fmt_ || !data_ || !info.bytes || info.bytes%(info.channels*2)) return fail(FileError::Format);
        info.frames=info.bytes/(info.channels*2);
        if(info.bytes>capacity_) return fail(FileError::TooLarge);
        done_=true; return true;
    }
    if(end_-pos_<8 || ++chunks_>4096) return fail(FileError::Format);
    if(!file.Read(pos_,b,8)) return fail(FileError::Io);
    const uint32_t n=U32(b+4);
    const uint64_t next=uint64_t(pos_)+8+n+(n&1);
    if(next>end_) return fail(FileError::Format);
    if(!std::memcmp(b,"fmt ",4)) {
        if(fmt_ || (n!=16 && n!=18)) return fail(FileError::Format);
        if(!file.Read(pos_+8,b,n)) return fail(FileError::Io);
        const unsigned ch=U16(b+2), rate=U32(b+4);
        if(U16(b)!=1 || (ch!=1 && ch!=2) || (rate!=44100 && rate!=48000)
           || U16(b+14)!=16 || U16(b+12)!=ch*2 || U32(b+8)!=rate*ch*2
           || (n==18 && U16(b+16)!=0)) return fail(FileError::Format);
        info.channels=ch; info.rate=rate; fmt_=true;
    } else if(!std::memcmp(b,"data",4)) {
        if(data_) return fail(FileError::Format);
        info.offset=pos_+8; info.bytes=n; data_=true;
    }
    pos_=uint32_t(next); return false;
}
void Loader::Init(File& file,int16_t* pcm,size_t cap,FileBridge& bridge) {
    file_=&file; pcm_=pcm; capacity_=cap; bridge_=&bridge;
    state=State::Idle; error=FileError::None; active=target=0;
}
bool Loader::Request(unsigned slot) {
    if(state!=State::Idle || slot<1 || slot>16) return false;
    target=slot; error=file_->Open(slot);
    if(error!=FileError::None) return false;
    parser_.Begin(file_->Size(),capacity_); state=State::Validate; return true;
}
void Loader::Fail(FileError e,bool destructive) {
    error=e; file_->Close();
    if(destructive) {
        active=0; bridge_->published={}; bridge_->slot=0;
        bridge_->generation.fetch_add(1,std::memory_order_release);
        bridge_->gate.store(false,std::memory_order_release);
    }
    state=destructive ? State::Publish : State::Idle;
}
void Loader::Service() {
    switch(state) {
    case State::Idle: break;
    case State::Publish:
        if(!bridge_->ack.load(std::memory_order_acquire)) state=State::Idle;
        break;
    case State::Validate:
        if(parser_.Step(*file_)) {
            if(parser_.error!=FileError::None) { Fail(parser_.error,false); break; }
            bridge_->gate.store(true,std::memory_order_release); state=State::WaitMute;
        } break;
    case State::WaitMute:
        if(bridge_->ack.load(std::memory_order_acquire)) { read_=0; state=State::Read; }
        break;
    case State::Read: {
        const uint32_t n=std::min(uint32_t(sizeof bounce_),parser_.info.bytes-read_);
        if(!file_->Read(parser_.info.offset+read_,bounce_,n)) { Fail(FileError::Io,true); break; }
        // Decode LE explicitly: portable host oracle and Cortex-M7 have identical PCM.
        for(uint32_t i=0;i<n;i+=2) pcm_[(read_+i)/2]=static_cast<int16_t>(U16(bounce_+i));
        read_+=n;
        if(read_==parser_.info.bytes) {
            file_->Close(); active=target; bridge_->published=parser_.info; bridge_->slot=active;
            bridge_->generation.fetch_add(1,std::memory_order_release);
            bridge_->gate.store(false,std::memory_order_release); state=State::Publish;
        } break;
    }
    }
}
uint16_t SlotLeds(unsigned mode,uint16_t keys,unsigned active,unsigned target,
                  bool loading,FileError error,uint32_t ms,uint16_t status) {
    if(mode==2) return keys;
    if(mode==0) {
        if(status&256) {
            const unsigned order=(status>>9)&3;
            if(ms%1200<(order+1)*240 && ms%240<100) status|=2;
        } else if(loading || error!=FileError::None) {
            const bool on=loading?(ms%200<100):(ms%2000<unsigned(error)*240 && ms%240<100);
            if(on) status|=2;
        }
        return status & 31;
    }
    uint16_t mask=active ? uint16_t(1u<<(active-1)) : 0;
    if(target && (loading || error!=FileError::None)) {
        const uint16_t bit=uint16_t(1u<<(target-1)); mask &= ~bit;
        bool on=loading ? (ms%200<100) : (ms%2000 < unsigned(error)*240 && ms%240<100);
        if(on) mask |= bit;
    }
    return mask;
}
}
