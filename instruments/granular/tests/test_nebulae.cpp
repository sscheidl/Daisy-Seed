#include "engine.h"
#include "heap_bounds.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <random>
#include <chrono>
using namespace neb;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)) {std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}} while(0)
static void Near(double a,double b,double e=1e-5) {CHECK(std::abs(a-b)<=e);}
static void P16(std::vector<uint8_t>& b,uint16_t v) {b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
static void P32(std::vector<uint8_t>& b,uint32_t v) {P16(b,uint16_t(v));P16(b,uint16_t(v>>16));}
static void Put32(std::vector<uint8_t>& b,unsigned p,uint32_t v) {for(unsigned j=0;j<4;++j) b[p+j]=uint8_t(v>>(8*j));}
static void Chunk(std::vector<uint8_t>& b,const char* id,const std::vector<uint8_t>& data) {
    b.insert(b.end(),id,id+4);P32(b,uint32_t(data.size()));b.insert(b.end(),data.begin(),data.end());if(data.size()&1) b.push_back(0);
}
static std::vector<uint8_t> Wav(unsigned rate,unsigned ch,unsigned frames=32,bool reversed=false) {
    std::vector<uint8_t> b={'R','I','F','F',0,0,0,0,'W','A','V','E'},fmt,data;
    P16(fmt,1);P16(fmt,uint16_t(ch));P32(fmt,rate);P32(fmt,rate*ch*2);P16(fmt,uint16_t(ch*2));P16(fmt,16);
    for(unsigned i=0;i<frames*ch;++i) P16(data,uint16_t(i*777));
    Chunk(b,"JUNK",{1,2,3});
    if(reversed) {Chunk(b,"data",data);Chunk(b,"fmt ",fmt);}else {Chunk(b,"fmt ",fmt);Chunk(b,"data",data);}
    Put32(b,4,uint32_t(b.size()-8));return b;
}
struct MemoryFile : File {
    std::vector<uint8_t> bytes;uint32_t pretend=0,fail_after=UINT32_MAX;
    FileError opening=FileError::None;
    FileError Open(unsigned s) override {CHECK(s>=1 && s<=16);return opening;}
    uint32_t Size() const override {return pretend?pretend:uint32_t(bytes.size());}
    bool Read(uint32_t p,void* dst,uint32_t n) override {
        if(uint64_t(p)+n>bytes.size() || uint64_t(p)+n>fail_after) return false;
        std::memcpy(dst,bytes.data()+p,n);return true;
    }
    void Close() override {}
};
static WavParser Parse(MemoryFile& f,size_t cap=1024*1024) {
    WavParser p;p.Begin(f.Size(),cap);unsigned iterations=0;while(!p.Step(f)) CHECK(++iterations<=4098);return p;
}
static void TestWav(size_t actual_capacity=41695360) {
    uintptr_t next=0;
    CHECK(neb_heap_adjust(1000,2000,1000,1000,&next) && next==2000);
    CHECK(!neb_heap_adjust(1000,2000,2000,1,&next));
    CHECK(neb_heap_adjust(1000,2000,2000,-1000,&next) && next==1000);
    CHECK(!neb_heap_adjust(1000,2000,1000,-1,&next));
    CHECK(!neb_heap_adjust(1000,2000,1000,PTRDIFF_MIN,&next));
    CHECK(!neb_heap_adjust(UINTPTR_MAX-100,UINTPTR_MAX,UINTPTR_MAX,1,&next));
    CHECK(!neb_heap_adjust(2000,1000,1000,0,&next));
    MemoryFile f;
    for(unsigned sr:{44100u,48000u}) for(unsigned ch:{1u,2u}) for(bool rev:{false,true}) {
        f.bytes=Wav(sr,ch,32,rev);auto p=Parse(f);CHECK(p.error==FileError::None);
        CHECK(p.info.frames==32 && p.info.rate==sr && p.info.channels==ch);
        CHECK(Parse(f,32*ch*2).error==FileError::None);
        CHECK(Parse(f,32*ch*2-1).error==FileError::TooLarge);
        auto good=f.bytes;
        for(unsigned cut=0;cut<good.size();++cut) {f.bytes.assign(good.begin(),good.begin()+cut);CHECK(Parse(f).error!=FileError::None);}
    }
    auto good=Wav(48000,2);
    for(unsigned field:{32u,34u,36u,40u,44u,46u}) { // fmt fields after odd JUNK
        f.bytes=good;f.bytes[field]=0xff;CHECK(Parse(f).error==FileError::Format);
    }
    for(unsigned sr:{44000u,22050u,96000u}) {f.bytes=Wav(sr,2);CHECK(Parse(f).error==FileError::Format);}
    for(unsigned ch:{0u,3u}) {f.bytes=Wav(48000,ch);CHECK(Parse(f).error==FileError::Format);}
    f.bytes=good;f.bytes[32]=0xfe;f.bytes[33]=0xff;CHECK(Parse(f).error==FileError::Format); // extensible
    f.bytes=good;Chunk(f.bytes,"data",{0,0});Put32(f.bytes,4,uint32_t(f.bytes.size()-8));CHECK(Parse(f).error==FileError::Format);
    f.bytes=good;Chunk(f.bytes,"fmt ",std::vector<uint8_t>(16));Put32(f.bytes,4,uint32_t(f.bytes.size()-8));CHECK(Parse(f).error==FileError::Format);
    f.bytes=good;Put32(f.bytes,16,UINT32_MAX);CHECK(Parse(f).error==FileError::Format);
    f.bytes=good;f.bytes.push_back(0);CHECK(Parse(f).error==FileError::Format);
    f.bytes=good;f.fail_after=25;CHECK(Parse(f).error==FileError::Io);f.fail_after=UINT32_MAX;
    // Large virtual data extents: validate all four real RAM format limits without allocating them.
    for(unsigned sr:{44100u,48000u}) for(unsigned ch:{1u,2u}) {
        const size_t cap=actual_capacity;
        const uint32_t frames=uint32_t(cap/(ch*2));
        f.bytes=Wav(sr,ch,1);const unsigned data=48;
        CHECK(!std::memcmp(f.bytes.data()+data,"data",4));
        Put32(f.bytes,data+4,frames*ch*2);f.pretend=data+8+frames*ch*2;
        Put32(f.bytes,4,f.pretend-8);auto p=Parse(f,cap);CHECK(p.error==FileError::None && p.info.frames==frames);
        Put32(f.bytes,data+4,(frames+1)*ch*2);f.pretend+=ch*2;Put32(f.bytes,4,f.pretend-8);
        CHECK(Parse(f,cap).error==FileError::TooLarge);f.pretend=0;
    }
    // Bounded adversarial headers, including overflow and malformed chunk graphs.
    std::mt19937 rng(1234);
    for(unsigned i=0;i<10000;++i) {
        f.bytes=good;for(unsigned j=0;j<3;++j) f.bytes[rng()%f.bytes.size()]=uint8_t(rng());
        auto p=Parse(f);if(p.error==FileError::None) CHECK(p.info.bytes<=1024*1024);
    }
    std::puts("PASS WAV: format matrix, exact capacities, truncation, duplicate/padded/reordered chunks, 10000 corruptions");
}
struct UiSink : Sink {
    std::vector<Action> actions;std::vector<unsigned> slots,banks;unsigned on=0,off=0;
    void Act(Action a) override {actions.push_back(a);}
    void Select(unsigned s) override {slots.push_back(s);}
    void SelectBank(unsigned b) override {banks.push_back(b);}
    void Key(unsigned,unsigned,bool pressed) override {if(pressed)++on;else ++off;}
};
static void TestUi() {
    Ui ui;UiSink s;ControlFrame f;for(unsigned i=0;i<8;++i) f.pot[i]=float(i)/7;
    ui.Process(f,s);CHECK(ui.mode==1);
    for(unsigned i=0;i<16;++i) {f.keys=uint16_t(1u<<i);ui.Process(f,s);f.keys=0;ui.Process(f,s);CHECK(s.slots.back()==i+1);}
    f.toggle=2;f.ms=1;ui.Process(f,s);f.ms=20;ui.Process(f,s);CHECK(ui.mode==1);
    f.ms=21;ui.Process(f,s);CHECK(ui.mode==2);
    f.keys=0xffff;ui.Process(f,s);CHECK(s.on==16 && ui.keyboard==0xffff);
    f.toggle=1;f.ms=30;ui.Process(f,s);f.ms=50;ui.Process(f,s);CHECK(s.off==16 && ui.mode==0 && ui.keyboard==0);
    CHECK(s.actions.empty());f.keys=0;ui.Process(f,s);
    f.keys=4;f.ms=60;ui.Process(f,s);f.keys=0;f.ms=160;ui.Process(f,s);CHECK(s.actions.back()==Action::Source);
    f.keys=4;f.ms=200;ui.Process(f,s);f.ms=501;ui.Process(f,s);CHECK(ui.secondary);
    for(unsigned i=0;i<8;++i) {f.pot[i]=0;ui.Process(f,s);f.pot[i]=1;ui.Process(f,s);Near(ui.params.secondary[i],1);}
    f.keys=5;ui.Process(f,s);CHECK(s.actions.back()==Action::Circular);
    f.keys=6;ui.Process(f,s);CHECK(s.actions.back()==Action::Order);
    f.keys=12;ui.Process(f,s);CHECK(s.actions.back()==Action::ResetAlt);
    f.keys=20;ui.Process(f,s);CHECK(s.actions.back()==Action::GrainTrigger);
    const size_t count=s.actions.size();f.keys=0;ui.Process(f,s);CHECK(s.actions.size()==count);
    f.keys=2;f.ms=600;ui.Process(f,s);f.keys=10;f.ms=1199;ui.Process(f,s);CHECK(s.actions.back()==Action::Reset);
    f.keys=2;f.ms=1200;ui.Process(f,s);f.keys=10;f.ms=1201;ui.Process(f,s);CHECK(s.actions.back()==Action::Clear);
    f.keys=0;ui.Process(f,s);CHECK(s.actions.back()==Action::Clear);
    for(auto pair:{std::pair<unsigned,Action>{1,Action::Record},{8,Action::Reset},{16,Action::Freeze}}) {
        f.keys=uint16_t(pair.first);ui.Process(f,s);CHECK(s.actions.back()==pair.second);f.keys=0;ui.Process(f,s);
    }
    Parameters p;float raw[8]={.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f};p.Update(raw,false);
    for(unsigned i=0;i<8;++i) {p.CC(i,.8f);p.Update(raw,false);Near(p.primary[i],.8f);raw[i]=.9f;p.Update(raw,false);Near(p.primary[i],.9f);}
    p.Update(raw,true);p.Update(raw,false);Near(p.primary[0],.9f);
    for(unsigned slot=1;slot<=16;++slot) CHECK(SlotLeds(1,0,slot,0,false,FileError::None,0,0)==1u<<(slot-1));
    for(unsigned e=1;e<=5;++e) {
        unsigned pulses=0;bool last=false;
        for(unsigned ms=0;ms<2000;++ms) {bool now=SlotLeds(1,0,0,3,false,FileError(e),ms,0)!=0;if(now&&!last)++pulses;last=now;}
        CHECK(pulses==e);
    }
    CHECK(SlotLeds(2,0xa571,4,5,true,FileError::Io,30,0)==0xa571);
    std::puts("PASS UI: all 16 slots/keys, three switch positions, SOURCE tap/hold, eight catch pots/CCs, five actions, secondary chords, LEDs");
}
struct Rig {
    std::vector<int16_t> a,b,pcm;FileBridge bridge;std::unique_ptr<Engine> e;
    std::array<float,kBlock> l{},r{},ol{},or_{};
    Rig(unsigned cap=8192,unsigned frames=48000,unsigned rate=48000):a(cap*2),b(cap*2),pcm(frames*2),e(new Engine) {
        for(unsigned i=0;i<frames;++i) {pcm[2*i]=Encode(.2f*std::sin(6.283185307*440*i/rate));pcm[2*i+1]=Encode(.2f*std::sin(6.283185307*660*i/rate));}
        e->Init(a.data(),b.data(),cap,pcm.data(),bridge);
        bridge.published={0,uint32_t(pcm.size()*2),frames,rate,2};bridge.generation.store(1);
    }
    void Tick() {e->Process(l.data(),r.data(),ol.data(),or_.data(),kBlock);
        for(unsigned i=0;i<kBlock;++i) {CHECK(std::isfinite(ol[i]) && std::abs(ol[i])<=.95f);CHECK(std::isfinite(or_[i]) && std::abs(or_[i])<=.95f);}}
};
static double Frequency(const std::vector<float>& signal) {
    std::vector<double> crosses;
    for(unsigned i=1;i<signal.size();++i) if(signal[i-1]<0 && signal[i]>=0) crosses.push_back(i-1-double(signal[i-1])/(signal[i]-signal[i-1]));
    CHECK(crosses.size()>20);return (crosses.size()-1)*48000/(crosses.back()-crosses.front());
}
static void TestDsp() {
    Near(LawSpeed(KnobSpeed(.5f),false),1);Near(LawPitch(KnobPitch(.5f)),1);
    Near(LawSpeed(0,false),-4);Near(LawPitch(0),.125);Near(LawPitch(1),4);
    double prev=0;
    const double density_golden[]={.12467167764365818,4.387300825272935,11.071755409325538,27.940588695853425,983.2527335864003};
    for(unsigned i=0;i<5;++i) Near(LawDensity(i*.25f),density_golden[i],density_golden[i]*2e-6);
    for(unsigned i=0;i<=1000;++i) {const float d=LawDensity(i/1000.f);CHECK(d>prev);prev=d;}
    float v,d,g;LawBlendLive(0,&v,&d,&g);Near(v,1);Near(d,0);Near(g,0);
    LawBlendLive(.5f,&v,&d,&g);Near(v,0);Near(d,1);Near(g,0);
    LawBlendLive(1,&v,&d,&g);Near(v,0);Near(d,0);Near(g,1);
    float windows[W_BANK][WIN_LEN+1],mixed[WIN_LEN+1];BuildWindows(windows);
    for(unsigned k=0;k<=600;++k) {WinMix(windows,k/600.f,mixed);for(float x:mixed) CHECK(std::isfinite(x) && x>=-1e-5 && x<=1.0001f);}
    WinMix(windows,2.f/6,mixed);Near(mixed[32],1);Near(mixed[WIN_LEN],0); // actual RampDown
    auto fft=std::make_unique<RFFT>();fft->Init();float signal[FFT_N],original[FFT_N];
    for(unsigned i=0;i<FFT_N;++i) original[i]=signal[i]=float(std::sin(.1*i)+.2*std::cos(.2*i));
    fft->Forward(signal);fft->Inverse(signal);for(unsigned i=0;i<FFT_N;++i) Near(signal[i],original[i],1e-5);
    for(unsigned sr:{44100u,48000u}) for(float speed:{.1f,.5f,.8f}) for(float pitch:{.25f,.5f,.75f}) {
        Rig r(8192,sr,sr);r.e->ui.params.primary[Speed]=speed;r.e->ui.params.primary[Pitch]=pitch;
        std::vector<float> left,right;
        for(unsigned block=0;block<180;++block) {r.Tick();if(block>=90) {left.insert(left.end(),r.ol.begin(),r.ol.end());right.insert(right.end(),r.or_.begin(),r.or_.end());}}
        const double ratio=LawPitch(KnobPitch(pitch));
        Near(Frequency(left),440*ratio,2);Near(Frequency(right),660*ratio,2);
    }
    Rig r;r.Tick();r.e->Act(Action::Freeze);r.Tick();const double pos=r.e->Position();r.Tick();Near(r.e->Position(),pos);
    r.e->Act(Action::Reset);Near(r.e->Position(),0);r.e->Act(Action::Freeze);
    r.e->ui.params.primary[Speed]=0;for(unsigned i=0;i<20;++i) r.Tick();CHECK(r.e->EffectiveSpeed()<0);
    r.e->live=true;r.e->ui.params.primary[Blend]=.5f;r.l.fill(.25f);r.r.fill(-.125f);
    for(unsigned i=0;i<80;++i) r.Tick();
    CHECK(r.ol.back()>0 && r.or_.back()<0); // live without file/record
    r.e->Act(Action::Record);for(unsigned i=0;i<4;++i) r.Tick();r.e->Act(Action::Record);
    CHECK(r.e->LiveLength()==4*kBlock && !r.e->recording);
    r.e->Act(Action::Record);CHECK(r.e->recording);for(unsigned i=0;i<4;++i) r.Tick();CHECK(!r.e->recording && r.e->LiveLength()==4*kBlock);
    r.e->Act(Action::Record);r.Tick();r.e->Act(Action::Record);CHECK(r.e->finalizing);r.Tick();CHECK(!r.e->finalizing);
    r.e->Act(Action::Clear);CHECK(r.e->LiveLength()==0 && !r.e->recording);
    r.e->Act(Action::Circular);r.e->Act(Action::Record);for(unsigned i=0;i<20;++i) r.Tick();CHECK(r.e->recording);r.e->Act(Action::Record);
    for(unsigned i=0;i<30;++i) r.Tick();
    CHECK(!r.e->recording && !r.e->finalizing);
    for(unsigned i=0;i<1000;++i) {for(unsigned p=0;p<8;++p) {r.e->ui.params.primary[p]=float((i*13+p*37)%101)/100;r.e->ui.params.secondary[p]=float((i+p)%101)/100;}r.Tick();}
    std::puts("PASS DSP: FFT roundtrip, seven windows/morphs, BLEND, 18 stereo tone/rate/pitch/speed cases, freeze/reset/reverse, live/record/overdub/circular and stress");
}
static void TestMidi() {
    Rig r;r.e->MidiEvent({0x90,0,60,100});CHECK(r.e->notes.Current()==60);
    r.e->MidiEvent({0x90,1,64,100});r.e->MidiEvent({0x80,0,60,0});CHECK(r.e->notes.Current()==64);
    r.e->MidiEvent({0x90,1,64,0});CHECK(r.e->notes.Current()==-1);
    r.e->Key(7,72,true);r.e->Key(8,72,true);r.e->Key(7,72,false);CHECK(r.e->notes.Current()==72);r.e->Key(8,72,false);CHECK(r.e->notes.Current()==-1);
    r.e->Key(0,60,true);r.e->MidiEvent({0x90,0,60,100});r.e->Key(0,60,false);CHECK(r.e->notes.Current()==60);
    r.e->MidiEvent({0xe0,0,127,127});for(unsigned i=0;i<10;++i) r.Tick();Near(r.e->EffectivePitch(),std::pow(2.f,2.f*8191/8192/12),.001);
    for(unsigned p=0;p<8;++p) {r.e->MidiEvent({0xb0,0,uint8_t(20+p),31});Near(r.e->ui.params.primary[p],31/127.f);}
    r.e->MidiEvent({0xb0,0,123,0});CHECK(r.e->notes.Current()==-1);
    r.e->MidiEvent({0x90,0,67,100});r.e->Disconnect();CHECK(r.e->notes.Current()==-1);
    std::puts("PASS MIDI: velocity-zero, last-note/channel/keyboard collisions, duplicate physical C5, pitch bend, eight CCs, all-notes-off/disconnect");
}
// Audibility regressions: note-state assertions alone missed the sticky mute.
static double SettledRms(Rig& r) {
    double sum=0;
    for(unsigned block=0;block<200;++block) {
        r.Tick();
        if(block>=100) for(float x:r.ol) sum+=double(x)*x;
    }
    return std::sqrt(sum/(100*kBlock));
}
static void TestFreePlayback() {
    for(unsigned mode=0;mode<3;++mode) {
        Rig r;r.e->ui.mode=mode;
        const double baseline=SettledRms(r);CHECK(baseline>.01);
        r.e->Key(0,60,true);r.Tick();r.e->Key(0,60,false);
        CHECK(SettledRms(r)>baseline*.5);
        // Note Off, velocity-zero, all-sound/all-notes-off, reset, disconnect.
        for(unsigned release=0;release<6;++release) {
            r.e->MidiEvent({0x90,3,60,100});r.Tick();
            switch(release) {
            case 0:r.e->MidiEvent({0x80,3,60,0});break;
            case 1:r.e->MidiEvent({0x90,3,60,0});break;
            case 2:r.e->MidiEvent({0xb0,3,120,0});break;
            case 3:r.e->MidiEvent({0xb0,3,123,0});break;
            case 4:r.e->MidiEvent({0xff,0,0,0});break;
            case 5:r.e->Disconnect();break;
            }
            CHECK(r.e->notes.Current()==-1);CHECK(SettledRms(r)>baseline*.5);
        }
        r.e->Key(0,60,true);r.Tick();r.e->notes.AllOff();
        CHECK(SettledRms(r)>baseline*.5);
    }
    for(unsigned toggle:{0u,1u}) {
        Rig r;ControlFrame f;std::copy(std::begin(r.e->ui.params.primary),std::end(r.e->ui.params.primary),f.pot);
        f.toggle=2;r.e->ui.Process(f,*r.e);f.ms=21;r.e->ui.Process(f,*r.e);
        CHECK(r.e->ui.mode==2);const double baseline=SettledRms(r);
        f.keys=1;r.e->ui.Process(f,*r.e);r.Tick();CHECK(r.e->notes.Current()==60);
        f.toggle=uint8_t(toggle);f.ms=30;r.e->ui.Process(f,*r.e);f.ms=51;r.e->ui.Process(f,*r.e);
        CHECK(r.e->ui.mode==(toggle==0?1u:0u));CHECK(r.e->notes.Current()==-1);
        CHECK(SettledRms(r)>baseline*.5);
    }
    std::puts("PASS audio regressions: free playback after keyboard/MIDI release, panic/reset/disconnect and both keyboard mode exits");
}
static void TestRecordSource() {
    Rig r(32768);r.e->live=true;r.l.fill(.2f);
    r.e->Act(Action::Record);for(unsigned i=0;i<80;++i)r.Tick();r.e->Act(Action::Record);
    const uint32_t length=r.e->LiveLength();CHECK(length==80*kBlock && r.e->live);
    r.e->Act(Action::Record);r.Tick();r.e->Act(Action::Source);
    CHECK(!r.e->live && r.e->finalizing);
    for(unsigned i=0;i<4;++i)r.Tick();
    CHECK(!r.e->live && !r.e->recording && !r.e->finalizing && r.e->LiveLength()==length);
    // Overdub completion also preserves an explicit return to Live.
    r.e->Act(Action::Source);r.e->Act(Action::Record);r.Tick();
    r.e->Act(Action::Source);r.e->Act(Action::Source);
    for(unsigned i=0;i<4;++i)r.Tick();
    CHECK(r.e->live && r.e->LiveLength()==length);
    // Every normal recording locks START/SIZE/SPEED, including overdubs.
    r.e->Act(Action::Record);r.Tick();const float speed=r.e->EffectiveSpeed();
    r.e->ui.params.primary[Speed]=0;for(unsigned i=0;i<10;++i)r.Tick();
    Near(r.e->EffectiveSpeed(),speed);r.e->Act(Action::Record);
    for(unsigned i=0;i<4;++i)r.Tick();
    std::puts("PASS recording regressions: SOURCE survives multi-block overdub publication; normal overdub speed lock");
}
static void TestLoader() {
    MemoryFile f;f.bytes=Wav(48000,2,4096);std::vector<int16_t> pcm(8192,1234);FileBridge b;Loader load;load.Init(f,pcm.data(),pcm.size()*2,b);
    CHECK(load.Request(3));for(unsigned i=0;i<10 && load.state==Loader::State::Validate;++i) load.Service();
    CHECK(load.state==Loader::State::WaitMute && b.gate.load());load.Service();CHECK(pcm[0]==1234);
    b.ack.store(true);load.Service();for(unsigned i=0;i<10 && load.state!=Loader::State::Publish;++i) load.Service();
    CHECK(load.active==3 && b.published.frames==4096 && pcm[1]==777);
    CHECK(!load.Request(4));b.ack.store(false);load.Service();CHECK(load.state==Loader::State::Idle);
    auto previous=pcm;f.bytes[0]='X';CHECK(load.Request(4));for(unsigned i=0;i<10 && load.state!=Loader::State::Idle;++i) load.Service();
    CHECK(load.error==FileError::Format && load.active==3 && pcm==previous && !b.gate.load());
    f.bytes=Wav(44100,2,4096);f.fail_after=5000;CHECK(load.Request(5));
    for(unsigned i=0;i<10 && load.state!=Loader::State::WaitMute;++i) load.Service();
    b.ack.store(true);
    for(unsigned i=0;i<10 && load.state!=Loader::State::Publish;++i) load.Service();
    CHECK(load.error==FileError::Io && load.active==0 && b.published.frames==0 && !b.gate.load());
    b.ack.store(false);load.Service();f.opening=FileError::NoCard;CHECK(!load.Request(6));CHECK(load.error==FileError::NoCard);
    // Production DSP fade/ack, metadata publication and immediate second load handshake.
    Rig r;MemoryFile mf;mf.bytes=Wav(48000,1,2048);Loader ll;ll.Init(mf,r.pcm.data(),r.pcm.size()*2,r.bridge);
    CHECK(ll.Request(1));for(unsigned i=0;i<40 && ll.state!=Loader::State::Idle;++i) {ll.Service();r.Tick();}
    CHECK(ll.active==1 && !r.bridge.ack.load());CHECK(ll.Request(2));
    for(unsigned i=0;i<40 && ll.state!=Loader::State::Idle;++i) {ll.Service();r.Tick();}
    CHECK(ll.active==2);
    std::puts("PASS loader: preflight retention, bounded load, atomic fade/ack/publish, destructive I/O failure, no-card, back-to-back loads");
}

// ---------------- v0.2.0: SW17 banks, panel LEDs, FILE per bank, F6, F3 ----------------
static void TestSw17() {
    Ui ui;UiSink s;ControlFrame f;
    ui.Process(f,s);CHECK(ui.mode==1 && !ui.bank_view);
    f.keys=1;f.ms=1;ui.Process(f,s);f.keys=0;f.ms=2;ui.Process(f,s);CHECK(s.slots.back()==1 && s.banks.empty());
    // SW17 to the bank position, stable for 20 ms; a key held across the change stays muted.
    f.keys=4;f.ms=10;ui.Process(f,s);const size_t slots=s.slots.size();
    f.toggle17=kSw17BankWhenPressed;f.ms=11;ui.Process(f,s);f.ms=30;ui.Process(f,s);CHECK(!ui.bank_view);
    f.ms=31;ui.Process(f,s);CHECK(ui.bank_view);
    f.keys=4|8;f.ms=40;ui.Process(f,s);CHECK(s.banks.size()==1 && s.banks.back()==4);  // only the new key
    f.keys=0;f.ms=41;ui.Process(f,s);f.keys=4;f.ms=42;ui.Process(f,s);CHECK(s.banks.back()==3);
    CHECK(s.slots.size()==slots);  // no slot selection while in bank view
    for(unsigned i=0;i<16;++i) {f.keys=uint16_t(1u<<i);f.ms+=2;ui.Process(f,s);f.keys=0;f.ms+=2;ui.Process(f,s);CHECK(s.banks.back()==i+1);}
    // A bounce shorter than 20 ms is ignored.
    f.toggle17=!kSw17BankWhenPressed;f.ms+=1;ui.Process(f,s);f.toggle17=kSw17BankWhenPressed;f.ms+=10;ui.Process(f,s);
    f.ms+=30;ui.Process(f,s);CHECK(ui.bank_view);
    // NEBULAE and KEYBOARD ignore SW17.
    f.toggle=1;f.ms+=1;ui.Process(f,s);f.ms+=25;ui.Process(f,s);CHECK(ui.mode==0 && !ui.bank_view);
    f.toggle=2;f.ms+=1;ui.Process(f,s);f.ms+=25;ui.Process(f,s);CHECK(ui.mode==2 && !ui.bank_view);
    const size_t banks=s.banks.size();f.keys=1;f.ms+=1;ui.Process(f,s);CHECK(s.on==1 && s.banks.size()==banks);
    f.keys=0;f.toggle=0;f.ms+=1;ui.Process(f,s);f.ms+=25;ui.Process(f,s);CHECK(ui.mode==1 && ui.bank_view);
    std::puts("PASS SW17: slot/bank view only in SAMPLE, 20 ms debounce, held keys suppressed, 16 bank keys");
}
static void TestPanelLeds() {
    LedState s;s.mode=1;s.bank_view=true;s.bank=3;s.bank_mask=0x00ff;
    LedFrame f=PanelLeds(s);CHECK(f.on==0x4 && f.dim==0xfb);
    s.bank_mask=0x0003;s.bank=2;f=PanelLeds(s);CHECK(f.on==2 && f.dim==1);  // missing banks off
    s.scanning_bank=2;unsigned lit=0;for(s.ms=0;s.ms<1000;++s.ms) {f=PanelLeds(s);lit+=f.on==2;CHECK((f.dim&2)==0);}
    CHECK(lit==500);  // 5 Hz flash
    s.scanning_bank=0;s.bank_failed=true;unsigned pulses=0;bool last=false;
    for(s.ms=0;s.ms<2000;++s.ms) {const bool on=PanelLeds(s).on&2;if(on&&!last)++pulses;last=on;}
    CHECK(pulses==4);
    s=LedState{};s.mode=1;s.active=5;f=PanelLeds(s);CHECK(f.on==0x10 && f.dim==0);
    s.active=0;f=PanelLeds(s);CHECK(f.on==0);  // playing sample from another bank: no slot LED
    s.mode=0;s.status=1|4;s.overrun=true;f=PanelLeds(s);CHECK(f.on==(1|4|kOverrunLed));
    s.overrun=false;CHECK(PanelLeds(s).on==(1|4));
    s.mode=2;s.keys=0x8001;s.overrun=true;s.bank_mask=0xffff;f=PanelLeds(s);CHECK(f.on==0x8001 && f.dim==0);
    std::puts("PASS panel LEDs: bank view bright/dim/off, scan flash, code 4, slot only in its bank, D16 overrun latch in NEBULAE only");
}
static void TestFileBanks() {
    Rig r;r.e->occupied=0b1011;r.e->selected_bank=2;
    r.bridge.slot=2;r.bridge.bank=1;r.bridge.generation.fetch_add(1);r.Tick();CHECK(r.e->PlayingBank()==1);
    r.e->Act(Action::FileNext);CHECK(r.e->requested_slot==1);  // other bank: first slot of the order
    r.e->file_order=1;r.e->Act(Action::FileNext);CHECK(r.e->requested_slot==4);  // backward: last
    r.bridge.bank=2;r.bridge.generation.fetch_add(1);r.Tick();
    r.e->file_order=0;r.e->Act(Action::FileNext);CHECK(r.e->requested_slot==4);  // same bank: after 2
    r.e->file_order=1;r.e->Act(Action::FileNext);CHECK(r.e->requested_slot==1);
    r.e->SelectBank(7);CHECK(r.e->requested_bank==7 && r.e->requested_slot==1);  // a bank loads nothing
    std::puts("PASS FILE per bank: forward/backward start in the selected bank, bank select without load");
}
static void TestTriggerClock() {
    {   // Trigger in the very first block: period 0, cap 0, the 0.5 ms floor (original: 0 -> 0.5).
        Rig z;z.e->ui.params.primary[Density]=0;z.e->ui.params.primary[Overlap]=1;
        z.e->Init(z.a.data(),z.b.data(),8192,z.pcm.data(),z.bridge);z.e->trigger_mode=true;
        z.e->TriggerGrain();z.Tick();Near(z.e->LastGrainMs(),.5f,1e-6);
    }
    Rig r;r.e->trigger_mode=true;r.e->ui.params.primary[Density]=0;r.e->ui.params.primary[Overlap]=1;
    for(unsigned i=0;i<100;++i) r.Tick();
    // First edge: the period counts from start (original ktickssincelastclock init 0), but
    // the portk-smoothed value starts at 0, so the first grains stay short.
    r.e->TriggerGrain();r.Tick();
    CHECK(r.e->LastGrainMs()<30 && r.e->LastGrainMs()<=6*r.e->SmoothedClockSeconds()*1000*1.0001f+.5f);
    // Clock every 19 blocks (101.3 ms): after settling, size <= 6 x period.
    const float period=19.f*kBlock/kRate;
    for(unsigned b=0;b<19*80;++b) {if(b%19==0) r.e->TriggerGrain();r.Tick();}
    Near(r.e->SmoothedClockSeconds(),period,period*.01);
    r.e->TriggerGrain();r.Tick();
    CHECK(r.e->LastGrainMs()<=6*r.e->SmoothedClockSeconds()*1000*1.0001f && r.e->LastGrainMs()>590);
    // Faster clock shrinks grains; a very slow clock leaves the overlap law (1000.5 ms) in force.
    for(unsigned b=0;b<2*400;++b) {if(b%2==0) r.e->TriggerGrain();r.Tick();}
    r.e->TriggerGrain();r.Tick();CHECK(r.e->LastGrainMs()<70 && r.e->LastGrainMs()>=.5f);
    for(unsigned b=0;b<3;++b) {for(unsigned i=0;i<1900;++i) r.Tick();r.e->TriggerGrain();r.Tick();}  // ~10 s period
    for(unsigned i=0;i<800;++i) r.Tick();
    r.e->TriggerGrain();r.Tick();Near(r.e->LastGrainMs(),1000.5f,.5f);
    // 2 minutes without a clock: the counter saturates at 60 s, everything stays finite.
    for(unsigned i=0;i<22600;++i) r.Tick();
    r.e->TriggerGrain();r.Tick();CHECK(std::isfinite(r.e->SmoothedClockSeconds()) && r.e->SmoothedClockSeconds()<=60.f);
    // Internal density (> 0) is never clock-limited.
    Rig d;d.e->ui.params.primary[Density]=.3f;d.e->ui.params.primary[Overlap]=1;
    for(unsigned i=0;i<200;++i) d.Tick();
    CHECK(d.e->LastGrainMs()>100);
    std::puts("PASS F6 trigger clock: 0.5 ms floor, 6x smoothed period cap, fast/slow/saturated clocks, internal density unaffected");
}
static void TestPackedFft() {
    auto fft=std::make_unique<RFFT>();fft->Init();
    std::mt19937 rng(99);std::uniform_real_distribution<float> u(-1,1);
    float a[FFT_N+2],b[FFT_N+2],ra[FFT_N+2],rb[FFT_N+2];
    for(unsigned trial=0;trial<20;++trial) {
        for(unsigned i=0;i<FFT_N;++i) {ra[i]=a[i]=u(rng)*(trial%3==0?1e-3f:1.f);rb[i]=b[i]=trial%5==0?0:u(rng);}
        fft->Forward(ra);fft->Forward(rb);fft->ForwardPair(a,b);
        float peak=0,err=0;
        for(unsigned i=0;i<FFT_N;++i) {peak=std::max({peak,std::abs(ra[i]),std::abs(rb[i])});err=std::max({err,std::abs(a[i]-ra[i]),std::abs(b[i]-rb[i])});}
        CHECK(err<=peak*2e-5f+1e-7f);
    }
    // Mincer A/B on a stereo source: identical hop timing (no L/R offset), outputs equal
    // within float rounding, channels stay isolated.
    const unsigned frames=96000;std::vector<int16_t> pcm(frames*2);
    for(unsigned i=0;i<frames;++i) {pcm[2*i]=Encode(.3f*std::sin(6.283185307f*220*i/48000));pcm[2*i+1]=Encode(i%4800<64?.5f:0.f);}
    const PcmView view{pcm.data(),frames,48000,2};
    auto packed=std::make_unique<Mincer>(),plain=std::make_unique<Mincer>();
    packed->Init(48000);plain->Init(48000);plain->SetPackedForward(false);
    double positions[kBlock];float pa[kBlock],pb[kBlock];double pos=0,diff=0,energy=0,error=0;
    for(unsigned c=0;c<2;++c) {
        packed->Reset();plain->Reset();pos=0;
        for(unsigned block=0;block<300;++block) {
            for(unsigned s=0;s<kBlock;++s) {positions[s]=pos;pos+=.7;}
            packed->Process(positions,view,c,1.3f,1.f,pa,kBlock);plain->Process(positions,view,c,1.3f,1.f,pb,kBlock);
            for(unsigned s=0;s<kBlock;++s) {diff=std::max(diff,double(std::abs(pa[s]-pb[s])));energy+=double(pb[s])*pb[s];error+=double(pa[s]-pb[s])*(pa[s]-pb[s]);CHECK(std::isfinite(pa[s]));}
        }
    }
    const double ratio_db=10*std::log10(error/energy);
    CHECK(energy>1 && diff<2e-4 && ratio_db<-80);
    std::printf("PASS F3 packed forward FFT: 20 random pairs, Mincer A/B max diff %.2e, error/signal %.1f dB, same hop timing per channel\n",diff,ratio_db);
}
int main(int argc,char** argv) {
    if(argc==3 && std::strcmp(argv[1],"--wav-capacity")==0) {
        const size_t capacity=size_t(std::strtoull(argv[2],nullptr,10));
        CHECK(capacity>1024 && capacity<=kSdramBytes);TestWav(capacity);return 0;
    }
    const auto start=std::chrono::steady_clock::now();
    TestWav();TestUi();TestDsp();TestMidi();TestFreePlayback();TestRecordSource();TestLoader();
    TestSw17();TestPanelLeds();TestFileBanks();TestTriggerClock();TestPackedFft();
    std::printf("PASS %u assertions in %.2f s (HOST timing, not Cortex-M7 CPU evidence)\n",checks,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
}
