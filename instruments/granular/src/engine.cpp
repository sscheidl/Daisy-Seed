#include "engine.h"
#include <cstring>
namespace neb {
void Engine::Init(int16_t* a,int16_t* b,uint32_t cap,int16_t* file,FileBridge& bridge) {
    bank_[0]=a; bank_[1]=b; cap_=cap; file_=file; bridge_=&bridge;
    // Everything in the object is initialized by its constructor AFTER SDRAM init.
    for(unsigned c=0;c<2;++c) {vocoder_[c].Init(kRate);limiter_[c].Init(kRate);}
    BuildWindows(bank_windows_);
    for(unsigned i=0;i<8;++i) smoothed_[i]=ui.params.primary[i];
}
float Engine::Random() {random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return float(random_&0xffffff)/16777216.f;}
void Engine::ResetDsp() {
    for(unsigned c=0;c<2;++c) {vocoder_[c].Reset();limiter_[c].Init(kRate); for(auto& g:grains_[c]) g.active=false;}
    grain_clock_[0]=grain_clock_[1]=1; Transition();
}
void Engine::Transition() {fade_from_[0]=tail_[0];fade_from_[1]=tail_[1];transition_=0;}
void Engine::BeginRecord() {
    if(finalizing || !cap_) return;
    recording=true; overdub_=live_length_!=0; write_=0; copy_=0;
    write_start_=overdub_ && live ? last_read_frame_%live_length_ : 0;
    // Inactive bank is only written; all DSP reads the immutable active bank.
}
void Engine::EndRecord() {
    if(!recording) return;
    recording=false;
    if(overdub_) {copy_=write_;finalizing=true;}
    else if(write_) {live_length_=write_;PublishLive();}
}
void Engine::PublishLive(bool resume) {
    active_bank_^=1; recording=false; finalizing=false; live=true;
    if(!resume) position_=0;
    last_read_frame_=0;
    ResetDsp();
    if(!circular && overdub_) {ui.params.CC(Speed,.5f);ui.params.CC(Pitch,.5f);}
    if(resume) BeginRecord();
}
void Engine::Act(Action a) {
    switch(a) {
    case Action::Record: if(recording) EndRecord();else BeginRecord();break;
    case Action::FileNext: {
        if(!occupied) {requested_slot=1;break;}
        unsigned start=active_slot_?active_slot_:(file_order==1?1:0), step=file_order==1?15:1;
        if(file_order==2) start=unsigned(Random()*16);
        for(unsigned j=1;j<=16;++j) {unsigned s=(start+step*j+15)%16; if(occupied&(1u<<s)) {requested_slot=s+1;break;}}
        break;
    }
    case Action::Source: {bool next=!live;EndRecord();live=next;position_=0;ResetDsp();break;}
    case Action::Reset: position_=0;++resets;reset_light_=1920;Transition();break;
    case Action::Freeze: frozen=!frozen;break;
    case Action::Clear: recording=finalizing=false;live_length_=write_=0;position_=0;ResetDsp();break;
    case Action::Circular: circular=!circular;break;
    case Action::Order: file_order=(file_order+1)%3;break;
    case Action::GrainTrigger: trigger_mode=!trigger_mode;break;
    case Action::ResetAlt: ui.params.ResetAlt();break;
    }
}
void Engine::Key(unsigned id,unsigned note,bool on) {
    if(id>=16) return;
    if(on) {key_mask_|=uint16_t(1u<<id);notes.Set(16,note,100);position_=0;Transition();}
    else {
        key_mask_ &= uint16_t(~(1u<<id));
        for(unsigned i=0;i<16;++i) if((key_mask_&(1u<<i)) && kNotes[i]==note) return;
        notes.Off(16,note);
    }
}
void Engine::MidiEvent(const Midi& e) {
    if(e.channel>15) return;
    switch(e.type) {
    case 0x90: if(e.b) {notes.Set(e.channel,e.a,e.b);position_=0;Transition();}else notes.Off(e.channel,e.a);break;
    case 0x80: notes.Off(e.channel,e.a);break;
    case 0xe0: bend_[e.channel]=(float((unsigned(e.b)<<7)|e.a)-8192)/8192;break;
    case 0xb0:
        if(e.a>=20 && e.a<=27) ui.params.CC(e.a-20,e.b/127.f);
        if(e.a==120 || e.a==123) notes.Clear(e.channel);
        if(e.a==121) bend_[e.channel]=0;
        break;
    case 0xff: Disconnect();break;
    }
}
void Engine::Disconnect() {for(unsigned c=0;c<16;++c) {notes.Clear(c);bend_[c]=0;}}
uint16_t Engine::Status() const {
    if(ui.secondary) return 256 | (file_order<<9) | (circular?1:0) | 4 | (trigger_mode?16:0);
    return (recording||finalizing?1:0) | (live?4:0) | (frozen?16:0) | (reset_light_?8:0);
}
void Engine::RenderGrains(const PcmView& src,const float* left,const float* right,
                          unsigned n,float density,float pitch,float input_gain) {
    std::fill(gran_[0],gran_[0]+n,0);std::fill(gran_[1],gran_[1]+n,0);
    for(unsigned s=0;s<n;++s) {
        for(unsigned c=0;c<2;++c) {
            grain_clock_[c]+=density/kRate;
            bool launch=grain_clock_[c]>=1 || (s==0 && pending_trigger_);
            if(launch) {
                grain_clock_[c]=std::fmod(grain_clock_[c],1.0);
                Grain* g=nullptr;
                for(auto& v:grains_[c]) if(!v.active) {g=&v;break;}
                // Bounded overload: drop a grain rather than cutting an existing tail.
                if(g && Random()>=alt_[Window]*.99f) {
                    const float overlap=std::clamp(smoothed_[Overlap]*smoothed_[Overlap]+(2*Random()-1)*alt_[Overlap]*alt_[Overlap],0.f,1.f);
                    float ms=density>0 ? overlap*(density<4?4:6)*1000/density+1 : overlap*overlap*1000+.5f;
                    ms=std::min(ms,8000.f);
                    g->active=true;g->envelope=0;g->increment=1000/(kRate*ms);
                    g->position=positions_[s]+(2*Random()-1)*alt_[Start]*alt_[Start]*src.frames;
                    g->position=std::fmod(g->position,double(src.frames));
                    if(g->position<0) g->position+=src.frames;
                    g->step=pitch*std::pow(2.f,2*(2*Random()-1)*alt_[Pitch]);
                    g->pan=c*(1-alt_[Size])+Random()*alt_[Size];
                    g->delay=uint32_t(Random()*alt_[Density]*alt_[Density]*10*kRate);
                }
            }
            for(auto& g:grains_[c]) {
                if(!g.active) continue;
                if(g.delay) {--g.delay;continue;}
                const float ep=g.envelope*WIN_LEN;const unsigned i=unsigned(ep);
                if(i>=WIN_LEN) {g.active=false;continue;}
                const float env=window_[i]+(ep-i)*(window_[i+1]-window_[i]);
                const float x=src.Read(g.position,c)*GRAIN_AMP*env;
                gran_[0][s]+=x*std::sqrt(1-g.pan);gran_[1][s]+=x*std::sqrt(g.pan);
                const float fm=(!live && smoothed_[Density]>.001f)?alt_[Blend]*(c?right[s]:left[s])*input_gain:0;
                g.position+=g.step*(1+std::clamp(fm,-4.f,4.f));
                if(g.position<0 || g.position>=src.frames) {
                    g.position=std::fmod(g.position,double(src.frames));
                    if(g.position<0) g.position+=src.frames;
                }
                g.envelope+=g.increment;
            }
        }
    }
    pending_trigger_=false;
    limiter_[0].Process(gran_[0],int(n));limiter_[1].Process(gran_[1],int(n));
}
void Engine::Process(const float* left,const float* right,float* ol,float* or_,unsigned n) {
    if(!n || n>kBlock) return;
    const bool gate=bridge_->gate.load(std::memory_order_acquire);
    if(gate && recording && !live) EndRecord();
    const uint32_t gen=bridge_->generation.load(std::memory_order_acquire);
    if(gen!=generation_) {
        const auto& f=bridge_->published;
        file_view_={file_,f.frames,f.rate,f.channels};generation_=gen;active_slot_=bridge_->slot;
        file_detached_=false;
        if(!live) {position_=0;ResetDsp();}
    }
    if(!gate) bridge_->ack.store(false,std::memory_order_release);
    const float smooth=1-std::exp(-float(n)/(kRate*.02f));
    for(unsigned i=0;i<8;++i) {
        if(!(recording && !overdub_ && !circular && (i==Start||i==Size||i==Speed)))
            smoothed_[i]+=(i==Window?1-std::exp(-float(n)/(kRate*.1f)):smooth)*(ui.params.primary[i]-smoothed_[i]);
        alt_[i]+=smooth*(ui.params.secondary[i]-alt_[i]);
    }
    const PcmView src=live?PcmView{bank_[active_bank_],live_length_,kRate,2}:file_view_;
    const int note=notes.Current();
    // Pitch bend follows the most recently sounding MIDI channel (see control.cpp).
    const float bend=notes.Bend(bend_);
    const float musical=note<0?1.f:std::pow(2.f,(float(note)-60+2*bend)/12);
    pitch_=std::clamp(LawPitch(KnobPitch(smoothed_[Pitch]))*musical,.015625f,32.f);
    speed_=LawSpeed(KnobSpeed(smoothed_[Speed]),frozen);
    const double start=std::min(double(smoothed_[Start])*src.frames,double(src.frames?src.frames-1:0));
    const double length=std::max(1.0,double(smoothed_[Size]*smoothed_[Size])*(src.frames-start));
    for(unsigned s=0;s<n;++s) {
        if(!src.frames) {position_=0;positions_[s]=0;continue;}
        if(position_<0 || position_>=length) {
            position_=std::fmod(position_,length);if(position_<0) position_+=length;
            ++resets;reset_light_=1920;
        }
        positions_[s]=start+position_;
        position_+=speed_*double(src.rate)/kRate;
    }
    last_read_frame_=uint32_t(positions_[n-1]);
    WinMix(bank_windows_,smoothed_[Window],window_);
    float density=trigger_mode && smoothed_[Density]<.05f?0:LawDensity(smoothed_[Density]);
    const float input_gain=std::pow(10.f,alt_[Speed]); // 0..20 dB, unity default
    float bv=0,bd=0,bg=0;
    if(live) LawBlendLive(smoothed_[Blend],&bv,&bd,&bg);
    else {bv=std::sqrt(1-smoothed_[Blend]);bg=std::sqrt(smoothed_[Blend]);}
    const bool read_ok=src.frames && (live || !file_detached_);
    if(read_ok) {
        for(unsigned c=0;c<2;++c)
            vocoder_[c].Process(positions_,src,c,pitch_*src.rate/kRate,live?1.f:.8f,voc_[c],int(n));
        RenderGrains(src,left,right,n,density,pitch_*src.rate/kRate,input_gain);
    } else {
        std::fill(voc_[0],voc_[0]+n,0);std::fill(voc_[1],voc_[1]+n,0);
        std::fill(gran_[0],gran_[0]+n,0);std::fill(gran_[1],gran_[1]+n,0);
    }
    const float musical_target=notes.engaged?(note<0?0.f:notes.Velocity()):1.f;
    for(unsigned s=0;s<n;++s) {
        file_gain_=std::clamp(file_gain_+(gate?-1.f:1.f)/256,0.f,1.f);
        musical_gain_+=.004f*(musical_target-musical_gain_);
        transition_=std::min(1.f,transition_+1.f/256);
        float l=voc_[0][s]*bv+gran_[0][s]*bg+left[s]*input_gain*bd;
        float r=voc_[1][s]*bv+gran_[1][s]*bg+right[s]*input_gain*bd;
        if(recording && write_<(overdub_?live_length_:cap_)) {
            const uint32_t i=overdub_?(write_start_+write_++)%live_length_:write_++;
            float rl=live?left[s]*input_gain:l, rr=live?right[s]*input_gain:r;
            if(live && (overdub_ || circular)) {
                const float feed=circular?1-bd:bd;
                rl=voc_[0][s]*bv+gran_[0][s]*bg+left[s]*input_gain*feed;
                rr=voc_[1][s]*bv+gran_[1][s]*bg+right[s]*input_gain*feed;
            }
            bank_[active_bank_^1][2*i]=Encode(rl);bank_[active_bank_^1][2*i+1]=Encode(rr);
        }
        const float gain=musical_gain_*(live?1.f:file_gain_);
        auto limit=[](float x) {
            if(!std::isfinite(x)) return 0.f;
            const float a=std::abs(x);
            return a<=.9f?x:std::copysign(.9f+.05f*std::tanh((a-.9f)/.05f),x);
        };
        ol[s]=tail_[0]=transition_*limit(l*gain)+(1-transition_)*fade_from_[0];
        or_[s]=tail_[1]=transition_*limit(r*gain)+(1-transition_)*fade_from_[1];
    }
    if(recording && write_>=(overdub_?live_length_:cap_)) {
        if(!overdub_) live_length_=write_;
        PublishLive(circular);
    }
    if(finalizing) {
        const uint32_t count=std::min(uint32_t(8192),live_length_-copy_);
        const uint32_t start=(write_start_+copy_)%live_length_;
        const uint32_t first=std::min(count,live_length_-start);
        std::memcpy(bank_[active_bank_^1]+2*start,bank_[active_bank_]+2*start,size_t(first)*4);
        if(count>first) std::memcpy(bank_[active_bank_^1],bank_[active_bank_],size_t(count-first)*4);
        copy_+=count;if(copy_==live_length_) PublishLive();
    }
    if(gate && file_gain_==0 && !bridge_->ack.load(std::memory_order_relaxed)) {
        file_detached_=true;
        if(!live) ResetDsp();
        bridge_->ack.store(true,std::memory_order_release);
    }
    reset_light_=reset_light_>n?reset_light_-n:0;
}
}
