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
    uint8_t b[40]{};
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
        const uint32_t frame=info.channels*(info.bits/8u);
        if(!fmt_ || !data_ || !info.bytes || info.bytes%frame) return fail(FileError::Format);
        info.frames=info.bytes/frame;
        // The buffer always holds PCM16, whatever the file stores.
        if(uint64_t(info.frames)*info.channels*2>capacity_) return fail(FileError::TooLarge);
        done_=true; return true;
    }
    if(end_-pos_<8 || ++chunks_>4096) return fail(FileError::Format);
    if(!file.Read(pos_,b,8)) return fail(FileError::Io);
    const uint32_t n=U32(b+4);
    const uint64_t next=uint64_t(pos_)+8+n+(n&1);
    if(next>end_) return fail(FileError::Format);
    if(!std::memcmp(b,"fmt ",4)) {
        // 16: plain PCM. 18: PCM with cbSize 0. 40: EXTENSIBLE, or plain PCM with the
        // empty extension some DAWs write (cbSize 0, the 22 bytes after it unused).
        if(fmt_ || (n!=16 && n!=18 && n!=40)) return fail(FileError::Format);
        if(!file.Read(pos_+8,b,n)) return fail(FileError::Io);
        const unsigned tag=U16(b), ch=U16(b+2), rate=U32(b+4), bits=U16(b+14);
        if(tag==0xFFFE) {
            // KSDATAFORMAT_SUBTYPE_PCM; valid bits may be fewer than the container.
            static const uint8_t kPcm[16]={1,0,0,0,0,0,0x10,0,0x80,0,0,0xAA,0,0x38,0x9B,0x71};
            const unsigned valid=U16(b+18);
            if(n!=40 || U16(b+16)!=22 || std::memcmp(b+24,kPcm,16) || !valid || valid>bits)
                return fail(FileError::Format);
        } else if(tag!=1 || (n>16 && U16(b+16)!=0)) {
            return fail(FileError::Format);
        }
        if((ch!=1 && ch!=2) || (rate!=44100 && rate!=48000) || (bits!=16 && bits!=24)
           || U16(b+12)!=ch*(bits/8) || U32(b+8)!=rate*ch*(bits/8)) return fail(FileError::Format);
        info.channels=uint16_t(ch); info.rate=rate; info.bits=uint16_t(bits); fmt_=true;
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
        active=0; bridge_->published={}; bridge_->slot=0; bridge_->bank=0;
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
        // 4092 bytes for 24-bit: whole samples of mono and stereo in every chunk.
        const bool wide=parser_.info.bits==24;
        const uint32_t chunk=wide ? 4092u : uint32_t(sizeof bounce_);
        const uint32_t n=std::min(chunk,parser_.info.bytes-read_);
        if(!file_->Read(parser_.info.offset+read_,bounce_,n)) { Fail(FileError::Io,true); break; }
        // Decode LE explicitly: portable host oracle and Cortex-M7 have identical PCM.
        if(wide) for(uint32_t i=0;i<n;i+=3) pcm_[(read_+i)/3]=Pcm24To16(bounce_+i);
        else for(uint32_t i=0;i<n;i+=2) pcm_[(read_+i)/2]=static_cast<int16_t>(U16(bounce_+i));
        read_+=n;
        if(read_==parser_.info.bytes) {
            const unsigned bank=file_->Bank();
            file_->Close(); active=target; bridge_->published=parser_.info; bridge_->slot=active; bridge_->bank=bank;
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
LedFrame PanelLeds(const LedState& s) {
    LedFrame f;
    auto pulse=[&](unsigned code) {return s.ms%2000<code*240 && s.ms%240<100;};
    if(s.mode==2) {f.on=s.keys;return f;}
    if(s.mode==0) {
        f.on=SlotLeds(0,s.keys,s.active,s.target,s.loading,s.error,s.ms,s.status);
        if(s.overrun) f.on|=kOverrunLed;
        return f;
    }
    if(!s.bank_view) {
        f.on=SlotLeds(1,s.keys,s.active,s.target,s.loading,s.error,s.ms,s.status);
        return f;
    }
    // Bank view: active bank bright, other existing banks dimmed, missing banks off;
    // the bank whose list is being read flashes at 5 Hz, an unreadable one pulses 4.
    const uint16_t bit=s.bank>=1 && s.bank<=16 ? uint16_t(1u<<(s.bank-1)) : 0;
    f.dim=s.bank_mask & uint16_t(~bit);
    if(s.scanning_bank==s.bank && bit) {if(s.ms%200<100) f.on|=bit;}
    else if(s.bank_failed && bit) {if(pulse(4)) f.on|=bit;}
    else f.on|=bit;
    f.dim&=uint16_t(~f.on);
    return f;
}
}
