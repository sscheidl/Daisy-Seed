#include "control.h"
#include <cstring>
namespace neb {
bool MidiParser::Byte(uint8_t byte,Midi& out) {
    if(byte>=0xf8) {if(byte==0xff) {out={0xff,0,0,0};return true;}return false;}
    if(byte&0x80) {
        used_=0;
        if(byte==0xf0) {sysex_=true;status_=0;return false;}
        sysex_=false;
        status_=byte<0xf0?byte:0;return false;
    }
    if(sysex_ || !status_) return false;
    const uint8_t type=status_&0xf0;
    if(type==0xc0 || type==0xd0) return false; // single data byte, ignored
    if(used_==0) {first_=byte;used_=1;return false;}
    used_=0;
    if(type!=0x80 && type!=0x90 && type!=0xb0 && type!=0xe0) return false;
    out={type,uint8_t(status_&15),first_,byte};return true;
}
void Parameters::Update(const float* raw,bool alt) {
    auto pot=[](float v) {v=Unit(v);return v<.003f?0.f:v>.997f?1.f:v;};
    if(!ready_) {
        for(unsigned i=0;i<8;++i) { primary[i]=last_[i]=pot(raw[i]); caught_[0][i]=true; }
        ready_=true;
    }
    if(page_!=alt) {
        for(unsigned i=0;i<8;++i) caught_[unsigned(alt)][i]=false;
        page_=alt;
    }
    for(unsigned i=0;i<8;++i) {
        const float r=pot(raw[i]); float& value=alt?secondary[i]:primary[i];
        bool& c=caught_[unsigned(alt)][i];
        if(!c && (std::abs(r-value)<.012f || (last_[i]-value)*(r-value)<=0)) c=true;
        if(c) value=r;
        last_[i]=r;
    }
}
void Parameters::CC(unsigned i,float value) { if(i<8) {primary[i]=Unit(value); caught_[0][i]=false;} }
void Parameters::ResetAlt() { for(unsigned i=0;i<8;++i) {secondary[i]=0; caught_[1][i]=false;} }
void Ui::Process(const ControlFrame& f,Sink& sink) {
    const uint8_t sw=f.toggle<=2?f.toggle:0;
    if(!initialized_) { stable_=candidate_=sw; initialized_=true; mode=sw==1?0:sw==2?2:1; }
    if(sw!=candidate_) {candidate_=sw;switch_at_=f.ms;}
    if(candidate_!=stable_ && f.ms-switch_at_>=20) {
        for(unsigned i=0;i<16;++i) if(keyboard&(1u<<i)) sink.Key(i,kNotes[i],false);
        keyboard=0; stable_=candidate_; mode=stable_==1?0:stable_==2?2:1;
        held_=f.keys; suppressed_=f.keys; secondary=false; source_used_=file_used_=true;
    }
    suppressed_ &= f.keys;
    const uint16_t keys=f.keys & ~suppressed_, rise=keys & ~held_, fall=held_ & ~keys;
    held_=keys;
    if(mode==2) {
        keyboard=keys;
        for(unsigned i=0;i<16;++i) {
            if(rise&(1u<<i)) sink.Key(i,kNotes[i],true);
            if(fall&(1u<<i)) sink.Key(i,kNotes[i],false);
        }
    } else if(mode==1) {
        for(unsigned i=0;i<16;++i) if(rise&(1u<<i)) sink.Select(i+1);
    } else {
        if(rise&4) {source_at_=f.ms;source_used_=false;}
        secondary=(keys&4) && f.ms-source_at_>=300;
        if(secondary) source_used_=true;
        // SOURCE+button is a secondary chord even before the hold threshold.
        if((keys&4) && (rise&27)) {secondary=true; source_used_=true;}
        if(rise&2) {file_at_=f.ms;file_used_=false;}
        if((keys&2) && (rise&8) && f.ms-file_at_>=600 && !(keys&4)) {
            sink.Act(Action::Clear); file_used_=true;
        } else if(rise&8) sink.Act(secondary?Action::ResetAlt:Action::Reset);
        if(rise&1) sink.Act(secondary?Action::Circular:Action::Record);
        if((rise&2) && secondary) {sink.Act(Action::Order);file_used_=true;}
        if((keys&2) && (rise&16) && !secondary) file_used_=true; // Save is intentionally unsupported/read-only.
        else if(rise&16) sink.Act(secondary?Action::GrainTrigger:Action::Freeze);
        if((fall&2) && !file_used_) sink.Act(Action::FileNext);
        if((fall&4) && !source_used_) sink.Act(Action::Source);
    }
    params.Update(f.pot,secondary && mode==0);
}
void Notes::Set(unsigned s,unsigned n,unsigned v) {
    if(s>16 || n>127) return;
    if(!v) {Off(s,n);return;}
    // Counter wrapping is exceedingly rare; renormalize by clearing safely.
    if(++serial_==0) {AllOff();++serial_;}
    order_[s][n]=serial_; velocity_[s][n]=v; engaged=true;
}
void Notes::Off(unsigned s,unsigned n) {if(s<=16 && n<128) order_[s][n]=0;}
void Notes::Clear(unsigned s) {if(s<=16) std::memset(order_[s],0,sizeof order_[s]);}
void Notes::AllOff() {std::memset(order_,0,sizeof order_);}
int Notes::Current() const {
    uint32_t newest=0; int note=-1;
    for(unsigned s=0;s<17;++s) for(unsigned n=0;n<128;++n)
        if(order_[s][n]>newest) {newest=order_[s][n];note=int(n);}
    return note;
}
float Notes::Velocity() const {
    uint32_t newest=0; unsigned v=0;
    for(unsigned s=0;s<17;++s) for(unsigned n=0;n<128;++n)
        if(order_[s][n]>newest) {newest=order_[s][n];v=velocity_[s][n];}
    return float(v)/127;
}
float Notes::Bend(const float* bends) const {
    uint32_t newest=0; float bend=0;
    for(unsigned s=0;s<17;++s) for(unsigned n=0;n<128;++n)
        if(order_[s][n]>newest) {newest=order_[s][n];bend=s<16?bends[s]:0;}
    return bend;
}
}
