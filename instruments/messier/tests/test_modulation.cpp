// Separate Mod on Daisy build of Nebulae (NEBULAE_MOD): the production main.cpp answers the
// editor over J7, the matrix moves the primaries through the existing laws, and everything
// the regular build guarantees still holds around it. The last part shares the card with
// Windows and comes back on a real FAT32 or exFAT image.
// Usage: test_modulation <card.img> <expected.txt>
#include "production_harness.h"
#include "mod_on_daisy/protocol.h"
#include <algorithm>
#include <map>
#include <new>
#include <sstream>
#include <string>

namespace {
namespace proto=mod::proto;
uint8_t seq=0;
int16_t file_buffer[2*20000];
constexpr uint32_t kLiveCapacity=48000;

struct Reply {uint8_t command=0;std::vector<uint8_t> payload;};
bool ParseMd(const std::vector<uint8_t>& message,proto::Frame& f) {
    if(message.size()<11 || message[0]!=0xf0 || message.back()!=0xf7) return false;
    if(proto::ParseHeader(message.data()+1,message.size()-2,f)!=proto::Parse::Ok) return false;
    return proto::Verify(message.data()+1,message.size()-2,f)==0;
}
// One editor request through the real USB byte path, waiting for its reply.
Reply Md(uint8_t command,std::vector<uint8_t> payload,uint8_t device=0) {
    uint8_t frame[proto::kMaxFrame];
    seq=uint8_t((seq+1)&127);
    const size_t size=proto::BuildFrame(frame,device,command,seq,payload.data(),payload.size());
    CHECK(size>0);
    usb.sent.clear();
    ReceiveUsb(frame,size,nullptr);
    for(unsigned i=0;i<20;++i) {
        Advance(1);
        for(const auto& message:usb.sent) {
            proto::Frame f;
            if(ParseMd(message,f) && f.seq==seq && f.command!=proto::MonitorFrame)
                return {f.command,std::vector<uint8_t>(f.payload,f.payload+f.length)};
        }
    }
    CHECK(!"no reply from the matrix");
    return {};
}
void Ack(uint8_t command,std::vector<uint8_t> payload) {CHECK(Md(command,std::move(payload)).command==(command|proto::kReplyBit));}
void SetCell(mod::Source source,unsigned target,int depth) {
    Ack(proto::SetCell,{uint8_t(source),uint8_t(target),uint8_t(std::abs(depth)),uint8_t((depth<0?1:0)|2)});
    Advance(3);
}
void SetSource(unsigned source,unsigned param,unsigned value) {
    Ack(proto::SetSource,{uint8_t(source),uint8_t(param),uint8_t(value&127),uint8_t(value>>7)});
}
void ClearMatrix() {Ack(proto::ClearMatrix,{});Advance(5);}
void Wheel(uint8_t value) {Send({0xb0,1,value});Advance(60);}
void Settled() {Advance(700);}  // 20 ms and 100 ms smoothers, far past 5 time constants

std::array<float,mod::kTargets> MonitorBases() {
    Ack(proto::Monitor,{20});
    usb.sent.clear();
    Advance(200);
    const std::vector<uint8_t>* last=nullptr;
    proto::Frame f;
    for(const auto& message:usb.sent) if(ParseMd(message,f) && f.command==proto::MonitorFrame) last=&message;
    CHECK(last!=nullptr);
    CHECK(ParseMd(*last,f) && f.length>=52);  // 36 bytes plus the eight base values
    proto::Reader r(f.payload,f.length);
    for(unsigned i=0;i<18;++i) r.U14();
    std::array<float,mod::kTargets> bases{};
    for(float& v:bases) v=proto::DecodeSigned(r.U14());
    Ack(proto::Monitor,{0});
    return bases;
}

void Discovery() {
    const Reply id=Md(proto::Identify,{},proto::kBroadcast);
    CHECK(id.command==(proto::Identify|proto::kReplyBit));
    proto::Reader r(id.payload.data(),id.payload.size());
    for(unsigned i=0;i<5;++i) r.U7();
    char key[mod::kMaxKeyLength+1]={},version[13]={};
    r.Str(key,mod::kMaxKeyLength);r.Str(version,12);
    CHECK(std::string(key)=="nebulae" && std::string(version)==NEBULAE_VERSION);
    CHECK(r.U7()==mod::kMaxActive);
    CHECK(r.U14()==1000);  // the tick rate; DSP sees it per block, see MODULATION.md
    r.U7();
    CHECK(r.U32()==modulation.controller.Schema());
    const unsigned caps=r.U7();
    CHECK((caps&0x10) && (caps&0x20) && (caps&0x40) && r.AtEnd());  // bases, ranges, LFO FM; no modes
    const char* names[]={"Start","Speed","Size","Density","Pitch","Overlap","Blend","Window"};
    const char* spans[]={"100%","+-50%","100%","100%","+-50%","100%","100%","100%"};
    for(unsigned t=0;t<mod::kTargets;++t) {
        const Reply target=Md(proto::GetTarget,{uint8_t(t)});
        proto::Reader tr(target.payload.data(),target.payload.size());
        CHECK(tr.U7()==t && tr.U7()==1);  // present, global, not latched
        tr.U14();
        char name[13]={},span[9]={};
        tr.Str(name,12);tr.Str(span,8);
        CHECK(std::string(name)==names[t] && std::string(span)==spans[t]);
    }
    const Reply allowed=Md(proto::GetAllowed,{});
    proto::Reader ar(allowed.payload.data(),allowed.payload.size());
    CHECK(ar.Mask64()==~uint64_t(0));
    CHECK(Md(proto::GetMode,{}).command==proto::kNak);  // no modes reported
}

// A column follows base + offset through the smoother, clamped, and comes back exactly.
void ActiveRoute() {
    hw.pot[neb::Start].value=.25f;Settled();
    CHECK(engine->ui.params.primary[neb::Start]==.25f);
    CHECK(std::abs(engine->Smoothed(neb::Start)-.25f)<1e-5f);
    SetCell(mod::ModWheel,neb::Start,50);
    Wheel(127);Settled();
    CHECK(engine->ui.params.primary[neb::Start]==.25f);  // the register stays the base
    CHECK(std::abs(engine->Smoothed(neb::Start)-.75f)<1e-3f);
    // Clamped at both ends, whatever the depth.
    SetCell(mod::ModWheel,neb::Start,100);Settled();
    CHECK(engine->Smoothed(neb::Start)<=1.f && engine->Smoothed(neb::Start)>.999f);
    SetCell(mod::ModWheel,neb::Start,-100);Settled();
    CHECK(engine->Smoothed(neb::Start)>=0.f && engine->Smoothed(neb::Start)<.001f);
    // Several connections on one column add up and stay bounded.
    SetCell(mod::ModWheel,neb::Start,100);
    SetCell(mod::ChannelAt,neb::Start,100);
    Send({0xd0,127});Settled();
    CHECK(modulation.engine.Offset(neb::Start)>1.9f);
    CHECK(engine->Smoothed(neb::Start)<=1.f);
    Send({0xd0,0});
    // An audible route: the LFO moves Pitch through its law at the block rate.
    ClearMatrix();Wheel(0);
    SetSource(0,0,12000);SetCell(mod::Lfo1,neb::Pitch,100);
    float low=1e9f,high=0;
    for(unsigned i=0;i<400;++i) {Advance(5);low=std::min(low,engine->EffectivePitch());high=std::max(high,engine->EffectivePitch());}
    CHECK(low<.5f && high>1.5f);
    CHECK(engine->ui.params.primary[neb::Pitch]==.5f);
    // Clearing returns every column to its base, exactly.
    ClearMatrix();Advance(5);
    for(float v:mod_offsets.Front().value) CHECK(v==0.f);
    Settled();Settled();
    for(unsigned t=0;t<mod::kTargets;++t) CHECK(std::abs(engine->Smoothed(t)-engine->ui.params.primary[t])<1e-5f);
    CHECK(engine->EffectivePitch()==1.f && engine->EffectiveSpeed()==1.f);
    hw.pot[neb::Start].value=0;Settled();
}

// Timer interrupts merge behind the audio callback; the LFO must not slow down with them.
void TickRateUnderAudioLoad() {
    SetSource(0,1,mod::SawUp);SetSource(0,0,9278);  // about 1 Hz
    SetCell(mod::Lfo1,neb::Pitch,10);
    for(unsigned step:{1u,5u,6u}) {
        std::vector<uint32_t> wraps;
        float last=modulation.engine.Source(mod::Lfo1);
        for(unsigned elapsed=0;elapsed<5000;elapsed+=step) {
            daisy::System::now+=step;
            PollControls(nullptr);
            if(elapsed%5<step) {Block();Main();}
            const float v=modulation.engine.Source(mod::Lfo1);
            if(v<last-1.f) wraps.push_back(daisy::System::now);
            last=v;
        }
        CHECK(wraps.size()>=4);
        const float period=float(wraps.back()-wraps.front())/float(wraps.size()-1);
        CHECK(std::abs(period-1000.f)<20.f);
    }
    CHECK(mod_clock.Dropped()==0);
    ClearMatrix();SetSource(0,1,mod::Sine);Settled();
}

void Monitor() {
    hw.pot[neb::Density].value=.6f;hw.pot[neb::Overlap].value=.3f;Settled();
    Send({0xb0,26,64});Advance(10);  // CC26 takes Blend over
    SetCell(mod::ModWheel,neb::Density,100);SetCell(mod::ModWheel,neb::Blend,100);
    Wheel(127);Settled();
    const auto bases=MonitorBases();
    // Unmodulated, although both modulated columns sit at their upper end.
    for(unsigned t=0;t<mod::kTargets;++t) CHECK(std::abs(bases[t]-engine->ui.params.primary[t])<1e-3f);
    CHECK(std::abs(bases[neb::Density]-.6f)<1e-3f && std::abs(bases[neb::Blend]-64.f/127.f)<1e-3f);
    CHECK(engine->Smoothed(neb::Density)>.99f);
    ClearMatrix();Wheel(0);
    hw.pot[neb::Blend].value=64.f/127.f;Advance(20);
    hw.pot[neb::Density].value=.4f;hw.pot[neb::Overlap].value=.5f;
    hw.pot[neb::Blend].value=0;Advance(20);Settled();
}

// Pickup compares the pot with the base, not with what the matrix made of it.
void Pickup() {
    hw.pot[neb::Size].value=.9f;Advance(20);
    Send({0xb0,22,102});Advance(10);
    const float base=engine->ui.params.primary[neb::Size];
    CHECK(std::abs(base-102.f/127.f)<1e-6f);
    SetCell(mod::ModWheel,neb::Size,-20);Wheel(127);Settled();
    // The pot passes the modulated value (base - 0.2) without crossing the base: not caught.
    hw.pot[neb::Size].value=.95f;Advance(20);
    CHECK(engine->ui.params.primary[neb::Size]==base);
    hw.pot[neb::Size].value=.81f;Advance(20);  // within 1.2 % of the base
    CHECK(engine->ui.params.primary[neb::Size]==.81f);
    hw.pot[neb::Size].value=1;Advance(20);
    ClearMatrix();Wheel(0);Settled();
}

// The centre detent and snap bands live in the laws behind the offset.
void CentreBands() {
    CHECK(engine->ui.params.primary[neb::Speed]==.5f && engine->ui.params.primary[neb::Pitch]==.5f);
    SetCell(mod::ModWheel,neb::Speed,1);Wheel(64);Settled();
    const float offset=modulation.engine.Offset(neb::Speed);
    CHECK(offset>.002f && offset<.003f);
    CHECK(engine->Smoothed(neb::Speed)>.502f);
    CHECK(engine->EffectiveSpeed()==1.f);  // inside the snap band of LawSpeed
    SetCell(mod::ModWheel,neb::Speed,100);Wheel(127);Settled();
    CHECK(std::abs(engine->EffectiveSpeed()-4.f)<.01f);  // depth 100 reaches the end of the knob
    SetCell(mod::ModWheel,neb::Speed,-100);Settled();
    CHECK(std::abs(engine->EffectiveSpeed()+4.f)<.01f);
    ClearMatrix();Wheel(0);Settled();CHECK(engine->EffectiveSpeed()==1.f);
}

// Notes transpose on top of the modulated knob; priority and release are the instrument's.
void NotePriority() {
    SetCell(mod::ModWheel,neb::Pitch,20);Wheel(127);Settled();
    const float knob=neb::LawPitch(neb::KnobPitch(engine->Smoothed(neb::Pitch)));
    CHECK(knob>1.1f && engine->EffectivePitch()==knob);
    Send({0x90,72,100});Advance(10);CHECK(engine->notes.Current()==72);
    Send({0x90,84,100});Advance(10);CHECK(engine->notes.Current()==84);
    Settled();
    CHECK(std::abs(engine->EffectivePitch()-knob*4.f)<1e-3f);  // last note wins, two octaves up
    Send({0x80,84,0});Advance(10);CHECK(engine->notes.Current()==72);
    Advance(10);CHECK(std::abs(engine->EffectivePitch()-knob*2.f)<1e-3f);
    Send({0x80,72,0});Advance(10);CHECK(engine->notes.Current()==-1);
    Advance(10);CHECK(engine->EffectivePitch()==knob);
    ClearMatrix();Wheel(0);Settled();
}

// START, SIZE and SPEED hold during a normal capture, modulated or not.
void RecordHolds() {
    hw.tog_sw18.value=1;Advance(30);CHECK(engine->ui.mode==0);
    SetSource(0,0,12000);
    for(unsigned t:{neb::Start,neb::Size,neb::Speed,neb::Pitch}) SetCell(mod::Lfo1,t,30);
    Advance(100);
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(15);
    CHECK(engine->recording && !engine->circular);
    const float held[3]={engine->Smoothed(neb::Start),engine->Smoothed(neb::Size),engine->Smoothed(neb::Speed)};
    float low=1e9f,high=-1e9f;
    for(unsigned i=0;i<60;++i) {
        Advance(5);
        CHECK(engine->recording);
        CHECK(engine->Smoothed(neb::Start)==held[0] && engine->Smoothed(neb::Size)==held[1]
              && engine->Smoothed(neb::Speed)==held[2]);
        low=std::min(low,engine->Smoothed(neb::Pitch));high=std::max(high,engine->Smoothed(neb::Pitch));
    }
    CHECK(high-low>.05f);  // Pitch has no record hold
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(15);
    CHECK(!engine->recording && engine->LiveLength()>0);
    low=1e9f;high=-1e9f;
    for(unsigned i=0;i<100;++i) {Advance(5);low=std::min(low,engine->Smoothed(neb::Start));high=std::max(high,engine->Smoothed(neb::Start));}
    CHECK(high-low>.05f);  // and the hold ends with the capture
    // Circular recording does not hold them.
    engine->circular=true;
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(15);
    CHECK(engine->recording);
    low=1e9f;high=-1e9f;
    for(unsigned i=0;i<60;++i) {Advance(5);low=std::min(low,engine->Smoothed(neb::Speed));high=std::max(high,engine->Smoothed(neb::Speed));}
    CHECK(high-low>.05f);
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(15);
    engine->circular=false;engine->Act(neb::Action::Clear);
    ClearMatrix();SetSource(0,0,6000);Settled();
    hw.tog_sw18.value=0;Advance(30);
}

// The gate 1 grain trigger keeps its edges while the matrix reads the same input.
void Gates() {
    SetSource(0,2,mod::GateClock);
    SetCell(mod::Lfo1,neb::Overlap,10);
    engine->trigger_mode=true;
    const float before=engine->SmoothedClockSeconds();
    for(unsigned i=0;i<6;++i) {
        hw.gate_in[0].pulse=true;hw.gate_in[0].high=true;Advance(20);
        CHECK(!hw.gate_in[0].pulse);
        hw.gate_in[0].high=false;Advance(180);
    }
    CHECK(modulation.engine.GateFollower(0).HasReference());
    CHECK(engine->SmoothedClockSeconds()!=before);
    engine->trigger_mode=false;
    ClearMatrix();SetSource(0,2,mod::Free);
    // CV reaches the matrix from the same timer.
    SetCell(mod::Cv1,neb::Density,100);
    hw.cv[0].value=.5f;Advance(30);
    CHECK(std::abs(modulation.engine.Offset(neb::Density)-.5f)<.05f);
    hw.cv[0].value=0;ClearMatrix();Settled();
}

// One byte stream, four independent consumers.
void SharedUsbPath() {
    usb.sent.clear();
    const unsigned resets=daisy::System::bootloader_resets;
    uint8_t identify[proto::kMaxFrame];
    seq=uint8_t((seq+1)&127);
    const size_t size=proto::BuildFrame(identify,proto::kBroadcast,proto::Identify,seq,nullptr,0);
    std::vector<uint8_t> stream={0xf0,0x7d,0x4e,0x42,0x01,0x01,0xf7};            // NB STATUS
    stream.insert(stream.end(),identify,identify+3);stream.push_back(0xf8);      // MD, clock inside
    stream.insert(stream.end(),identify+3,identify+size);
    const uint8_t rest[]={0xf0,0x7d,0x42,0x4c,0x01,0x01,0xf8,'B','O','O','T',0xf7,  // BL
                          0xf0,0x41,1,2,3,0xf7,                                      // foreign SysEx
                          0x90,67,100,0xb0,1,99,0xb0,21,100};                         // note, CC1, CC21
    stream.insert(stream.end(),rest,rest+sizeof rest);
    ReceiveUsb(stream.data(),stream.size(),nullptr);
    ServiceUsb();Block();Advance(6);
    bool nb=false,md=false,bl=false;
    const std::vector<uint8_t> ack(std::begin(bootloader::kAck),std::end(bootloader::kAck));
    for(const auto& message:usb.sent) {
        proto::Frame f;
        nb|=message.size()==26 && message[2]==0x4e && message[5]==0x41;
        md|=ParseMd(message,f) && f.command==(proto::Identify|proto::kReplyBit) && f.seq==seq;
        bl|=message==ack;
    }
    CHECK(nb && md && bl);
    CHECK(daisy::System::bootloader_resets==resets+1);
    CHECK(engine->notes.Current()==67);
    CHECK(modulation.live.ModWheelRaw()==99);
    CHECK(std::abs(engine->ui.params.primary[neb::Speed]-100.f/127.f)<1e-6f);
    CHECK(modulation.controller.RejectedFrames()==0);
    Send({0x80,67,0,0xb0,1,0});
    hw.pot[neb::Speed].value=100.f/127.f;Advance(20);hw.pot[neb::Speed].value=.5f;Advance(20);Settled();
}

void Disconnect() {
    Send({0x90,60,100});Advance(5);
    CHECK(modulation.live.NoteOns()>0);
    SetCell(mod::PolyAt,neb::Pitch,50);
    Send({0xa0,60,127});Advance(30);
    CHECK(modulation.live.LastPressureRaw()==127);
    hUsbDeviceHS.dev_state=0;ServiceUsb();Block();
    CHECK(modulation.live.LastPressureRaw()==0 && engine->notes.Current()==-1);
    hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;ServiceUsb();Block();
    ClearMatrix();Settled();
}

// What the restart in PREPARE_SHARE leaves behind: RAM state is gone, the intent survives.
void Restart(const neb::BootIntent& intent) {
    perf=Perf{};
    fade_out.store(false);faded.store(false);master_gain=1;
    occupied.store(0);shown_bank.store(0);callback_overruns=0;
    banks.Reset();
    bridge.bank=bridge.slot=0;bridge.published={};
    BuildEngine(file_buffer,sizeof file_buffer,kLiveCapacity);
    hw.seed.audio_stopped=false;
    modulation.~Module();new(&modulation) mod::Module;
    new(&mod_offsets) mod::TripleBuffer<neb::columns::Frame>;
    new(&mod_bases) mod::TripleBuffer<neb::columns::Frame>;
    InitSd();
    ArmRestore(intent);
    InitModulation();
}

uint32_t Fnv(const int16_t* pcm,size_t n) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<n;++i) for(int b=0;b<2;++b) {h^=(uint16_t(pcm[i])>>(8*b))&0xFF;h*=16777619u;}
    return h;
}

void ShareAndReturn(const char* expected_path) {
    std::ifstream ex(expected_path);
    std::map<std::string,std::vector<std::string>> expected;
    for(std::string line;std::getline(ex,line);) {
        std::vector<std::string> f;std::stringstream ss(line);
        for(std::string part;std::getline(ss,part,'|');) f.push_back(part);
        expected[f[0]]=f;
    }
    const auto kick=expected.at("Neue Bank/2 Kick.WAV");
    const auto frames=uint32_t(std::stoul(kick[4]));
    // Bank 3, slot 1: a mono file, so a stale stereo sample cannot pass.
    hw.tog_sw17.state=neb::kSw17BankWhenPressed;Advance(30);
    hw.pressed[2]=true;Advance(15);hw.pressed[2]=false;Settle();
    hw.tog_sw17.state=!neb::kSw17BankWhenPressed;Advance(30);
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Settle();
    CHECK(loader.active==1 && bridge.bank==3 && bridge.published.channels==1 && bridge.published.frames==frames);
    CHECK(Fnv(file_buffer,frames)==uint32_t(std::stoul(kick[5],nullptr,16)));
    // Editor: a route, then "Save file" reads the matrix back.
    SetCell(mod::Lfo2,neb::Window,-35);
    SetCell(mod::ModWheel,neb::Blend,60);
    const uint32_t saved_crc=modulation.controller.CurrentCrc();
    const Reply saved=Md(proto::GetMatrix,{});
    CHECK(saved.command==(proto::GetMatrix|proto::kReplyBit));
    // Busy and confirmation with the matrix running, monitor on as with an open editor.
    Ack(proto::Monitor,{25});
    engine->live=true;
    hw.tog_sw18.value=1;Advance(30);
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(15);CHECK(engine->recording);
    Send({0xf0,0x7d,0x4e,0x42,0x01,0x02,0x01,1,2,3,4,0xf7});Advance(2);
    auto r=LastReply();CHECK(r[5]==0x42 && r[6]==neb::sysex::kBusy && !perf.restarting);
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(15);CHECK(!engine->recording);
    Send({0xf0,0x7d,0x4e,0x42,0x01,0x02,0x00,1,2,3,4,0xf7});Advance(2);
    r=LastReply();CHECK(r[6]==neb::sysex::kConfirmRequired && !perf.restarting);
    hw.tog_sw18.value=2;Advance(30);hw.pressed[4]=true;Advance(15);CHECK(sent_notes[neb::kNotes[4]]);
    usb.sent.clear();
    const unsigned resets=daisy::System::bootloader_resets;
    Send({0xf0,0x7d,0x4e,0x42,0x01,0x02,0x01,9,9,9,9,0xf7});Main();
    CHECK(perf.restarting && hw.seed.audio_stopped && !perf.mounted);
    CHECK(daisy::System::bootloader_resets==resets+1 && daisy::System::bootloader_mode==daisy::System::DAISY_SKIP_TIMEOUT);
    // The ACK leads, and nothing of the matrix sits between it and the host's note offs.
    bool accepted=false;
    for(const auto& message:usb.sent) {
        proto::Frame f;
        if(!accepted) {accepted=message.size()>6 && message[2]==0x4e && message[5]==0x42 && message[6]==neb::sysex::kAccepted;continue;}
        CHECK(!ParseMd(message,f));
    }
    CHECK(accepted);
    const neb::BootIntent intent=neb::TakeBootIntent(intent_words);
    CHECK(intent.personality==neb::Personality::SdManagement && intent.bank==3 && intent.slot==1);
    // SD management ran and returned: performance intent with the same bank and slot.
    // The key goes up first: PollControls keeps a static key mask that a real restart clears.
    hw.pressed[4]=false;hw.tog_sw18.value=0;
    for(unsigned i=0;i<30;++i) {++daisy::System::now;PollControls(nullptr);}
    Block();
    neb::BootIntent back=intent;back.personality=neb::Personality::Performance;
    Restart(back);
    Advance(30);Settle();
    CHECK(perf.mounted && banks.BankCount()==3 && shown_bank.load()==3);
    CHECK(loader.active==1 && bridge.bank==3 && loader.error==neb::FileError::None);
    CHECK(bridge.published.channels==1 && bridge.published.frames==frames && bridge.published.rate==48000);
    CHECK(Fnv(file_buffer,frames)==uint32_t(std::stoul(kick[5],nullptr,16)));
    CHECK(engine->LiveLength()==0 && !engine->live);
    // The matrix is volatile: empty again, and no offset reaches the audio.
    CHECK(!modulation.engine.Active() && modulation.controller.CurrentCrc()!=saved_crc);
    for(float v:mod_offsets.Front().value) CHECK(v==0.f);
    // Editor: reconnect, then the saved patch goes back unchanged.
    CHECK(Md(proto::Identify,{},proto::kBroadcast).command==(proto::Identify|proto::kReplyBit));
    const std::vector<uint8_t> matrix(saved.payload.begin(),saved.payload.end()-5);  // without its CRC
    CHECK(Md(proto::SetMatrix,matrix).command==(proto::SetMatrix|proto::kReplyBit));
    Advance(5);
    CHECK(modulation.controller.CurrentCrc()==saved_crc && modulation.engine.Active());
    Wheel(127);Settled();
    CHECK(modulation.engine.Offset(neb::Blend)>.59f);
    CHECK(engine->Smoothed(neb::Blend)>.59f);
    Wheel(0);ClearMatrix();
}
}

int main(int argc,char** argv) {
    CHECK(argc==3);
    LoadCard(argv[1]);
    Boot(file_buffer,sizeof file_buffer,kLiveCapacity);
    Settle();
    CHECK(perf.mounted && banks.BankCount()==3 && loader.active==1);
    Discovery();
    ActiveRoute();
    TickRateUnderAudioLoad();
    Monitor();
    Pickup();
    CentreBands();
    NotePriority();
    RecordHolds();
    Gates();
    SharedUsbPath();
    Disconnect();
    ShareAndReturn(argv[2]);
    CHECK(aborts==0);
    std::printf("PASS Mod on Daisy production build: %u checks; discovery, routes, clamping, tick rate, monitor, "
                "pickup, snap bands, notes, record holds, gates, NB/MD/BL on one USB path, share and return\n",checks);
}
