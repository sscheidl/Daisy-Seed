// Golden performance of the production callback. The regular and the MOD build run the
// same script and must print the same FNV-1a hash of every output sample: file playback,
// Live capture, overdub, circular, every pot and its secondary, CC20-27, notes, bend,
// freeze, gate grains, plus everything only the matrix reads (CC1, aftertouch, clock,
// CV, gates, the editor's read-only SysEx). Usage: test_golden <card.img> [--route]
// --route (MOD build): one active connection, the negative control. The hash must differ,
// so a broken hook cannot pass by never reaching the audio.
#include "production_harness.h"
#if NEBULAE_MOD
#include "mod_on_daisy/protocol.h"
#endif

namespace {
uint64_t hash_value=14695981039346656037ull;
uint32_t noise=0x1234567u;
uint64_t frame_count=0;
void Input() {
    for(unsigned i=0;i<neb::kBlock;++i,++frame_count) {
        noise=noise*1664525u+1013904223u;
        const float tone=.3f*std::sin(float(frame_count)*.0314f);
        input_l[i]=tone+float(int32_t(noise)>>8)*(.05f/8388608.f);
        input_r[i]=.5f*tone-float(int32_t(noise)>>9)*(.05f/8388608.f);
    }
}
void Hash(const float* x) {
    for(unsigned i=0;i<neb::kBlock;++i) {
        uint32_t bits;std::memcpy(&bits,&x[i],sizeof bits);
        for(unsigned b=0;b<4;++b) {hash_value^=(bits>>(8*b))&0xff;hash_value*=1099511628211ull;}
    }
}
// NEBULAE_GOLDEN_DUMP=<file>: also write every output block (left, then right, float32)
// for tests/compare_audio.cpp.
std::FILE* dump=nullptr;
void Output() {
    Hash(output_l.data());Hash(output_r.data());
    if(dump) {
        CHECK(std::fwrite(output_l.data(),sizeof(float),neb::kBlock,dump)==neb::kBlock);
        CHECK(std::fwrite(output_r.data(),sizeof(float),neb::kBlock,dump)==neb::kBlock);
    }
}
void Press(unsigned key) {hw.pressed[key]=true;Advance(15);hw.pressed[key]=false;Advance(15);}
void Sweep(unsigned pot,float from,float to) {
    for(unsigned i=0;i<=20;++i) {hw.pot[pot].value=from+(to-from)*float(i)/20.f;Advance(6);}
}
void SendBytes(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> copy(bytes);ReceiveUsb(copy.data(),copy.size(),nullptr);ServiceUsb();
}
// The same bytes reach the regular build, which must ignore them.
void MatrixOnlyTraffic() {
    SendBytes({0xb0,1,100,0xd0,90,0xa0,60,70});
    for(unsigned i=0;i<48;++i) {SendBytes({0xf8});Advance(1);}
    SendBytes({0xfa,0xf8,0xb0,1,0,0xd0,0,0xa0,60,0});
    hw.cv[0].value=.7f;hw.cv[1].value=-.4f;hw.gate_in[1].high=true;Advance(40);
    hw.gate_in[1].high=false;hw.cv[0].value=0;hw.cv[1].value=0;Advance(40);
    SendBytes({0xf0,0x7d,0x4d,0x44,1,0x7f,1,1,0,0x7e,0xf7});                     // IDENTIFY
    SendBytes({0xf0,0x7d,0x4d,0x44,1,0,0x30,2,1,25,0x37,0xf7});                  // MONITOR 25 Hz
    Advance(120);
}
void Performance() {
    Settle();
    Sweep(neb::Start,0,.7f);Sweep(neb::Size,1,.3f);Sweep(neb::Density,.4f,.9f);Sweep(neb::Overlap,.5f,.1f);
    MatrixOnlyTraffic();
    hw.tog_sw18.value=1;Advance(30);CHECK(engine->ui.mode==0);
    for(unsigned pot=0;pot<8;++pot) Sweep(pot,pot%2?.8f:.2f,pot%2?.3f:.6f);
    // Secondaries: hold SOURCE, sweep, release, then pickup back on the primaries.
    hw.pressed[2]=true;Advance(400);
    for(unsigned pot=0;pot<8;++pot) Sweep(pot,.3f,.5f);
    hw.pressed[2]=false;Advance(30);
    for(unsigned pot=0;pot<8;++pot) Sweep(pot,.5f,.45f);
    // MIDI: CC takeover of every pot, note priority, bend, all notes off.
    for(uint8_t cc=20;cc<28;++cc) {Send({0xb0,cc,uint8_t(10+cc*3)});Advance(10);}
    Send({0x90,67,100});Advance(40);Send({0x90,72,60});Advance(40);Send({0xe0,0,90});Advance(40);
    Send({0x80,72,0});Advance(40);Send({0xb0,121,0});Send({0x80,67,0});Advance(40);
    // First Live capture, overdub, circular, freeze, reset, source and trigger mode.
    Press(0);Advance(300);Press(0);Advance(100);CHECK(engine->live && engine->LiveLength()>0);
    MatrixOnlyTraffic();
    Press(0);Advance(200);Press(0);Advance(300);
    hw.pressed[2]=true;Advance(20);hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(15);hw.pressed[2]=false;Advance(30);
    CHECK(engine->circular);
    Press(0);Advance(300);Press(0);Advance(100);
    Press(4);Advance(120);Press(4);Press(3);Advance(60);
    Press(2);Advance(200);Press(2);Advance(100);
    hw.pressed[2]=true;Advance(20);hw.pressed[4]=true;Advance(15);hw.pressed[4]=false;Advance(15);hw.pressed[2]=false;Advance(30);
    CHECK(engine->trigger_mode);
    Send({0xb0,23,0});Advance(30);
    for(unsigned i=0;i<12;++i) {hw.gate_in[0].pulse=true;hw.gate_in[0].high=true;Advance(25);hw.gate_in[0].high=false;Advance(25);}
    // SAMPLE and KEYBOARD modes, then the next card sample.
    hw.tog_sw18.value=2;Advance(30);CHECK(engine->ui.mode==2);
    for(unsigned key=0;key<16;key+=5) Press(key);
    hw.tog_sw18.value=0;Advance(30);
    hw.pressed[2]=true;Advance(15);hw.pressed[2]=false;Settle();
    Advance(300);
}
}

int main(int argc,char** argv) {
    CHECK(argc>=2);
    LoadCard(argv[1]);
    static int16_t file_buffer[2*20000];
    Boot(file_buffer,sizeof file_buffer,48000);
    if(const char* path=std::getenv("NEBULAE_GOLDEN_DUMP")) {dump=std::fopen(path,"wb");CHECK(dump);}
    block_input=Input;block_output=Output;
#if NEBULAE_MOD
    if(argc==3) {
        CHECK(std::strcmp(argv[2],"--route")==0);
        uint8_t frame[mod::proto::kMaxFrame];
        const uint8_t cell[]={mod::Lfo1,neb::Pitch,20,2};
        const size_t size=mod::proto::BuildFrame(frame,0,mod::proto::SetCell,1,cell,sizeof cell);
        ReceiveUsb(frame,size,nullptr);Advance(5);
        CHECK(modulation.engine.Active());
    }
#else
    CHECK(argc==2);
#endif
    Performance();
    if(dump) std::fclose(dump);
    std::printf("golden %016llx\n",static_cast<unsigned long long>(hash_value));
}
