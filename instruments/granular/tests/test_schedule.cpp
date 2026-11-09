// Vocoder scheduling candidates (engine.h Schedule) against the common grid, on the
// Engine's vocoder layer before BLEND and the mix:
//   kScheduleCompensated must be the common grid's output delayed by exactly one block,
//   bit for bit, on both channels, within an epoch between resets, for stereo and mono
//   sources; kScheduleStagger must leave the left channel untouched and change the right.
#include "engine.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
using namespace neb;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)) {std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}} while(0)

struct Rig {
    std::vector<int16_t> a,b;std::vector<int16_t>& pcm;FileBridge bridge;std::unique_ptr<Engine> e;
    std::array<float,kBlock> l{},r{},ol{},or_{};
    Rig(std::vector<int16_t>& source,uint32_t frames,uint32_t rate,uint16_t channels,unsigned schedule)
        : a(8192*2),b(8192*2),pcm(source),e(new Engine) {
        e->Init(a.data(),b.data(),8192,pcm.data(),bridge);
        e->SetSchedule(schedule);
        bridge.published={0,uint32_t(pcm.size()*2),frames,rate,channels};bridge.generation.store(1);
        e->ui.params.primary[Speed]=.55f;e->ui.params.primary[Pitch]=.62f;e->ui.params.primary[Size]=.9f;
        e->ui.params.primary[Blend]=0;e->ui.params.primary[Density]=.3f;
    }
    void Tick() {
        for(unsigned i=0;i<kBlock;++i) {l[i]=.1f*std::sin(.05f*i);r[i]=-.1f*std::sin(.07f*i);}
        e->Process(l.data(),r.data(),ol.data(),or_.data(),kBlock);
        for(unsigned i=0;i<kBlock;++i) CHECK(std::isfinite(ol[i]) && std::isfinite(or_[i]));
    }
    std::array<float,kBlock> Voc(unsigned c) const {std::array<float,kBlock> v;std::memcpy(v.data(),e->VocoderOutput(c),sizeof v);return v;}
};
static bool Same(const std::array<float,kBlock>& x,const std::array<float,kBlock>& y) {return std::memcmp(x.data(),y.data(),sizeof x)==0;}
static bool Silent(const std::array<float,kBlock>& x) {for(float v:x) if(v!=0) return false;return true;}

static void Compare(uint16_t channels) {
    const uint32_t frames=96000,rate=channels==1?44100:48000;
    std::vector<int16_t> pcm(size_t(frames)*channels);
    for(uint32_t i=0;i<frames;++i) for(unsigned c=0;c<channels;++c) {
        const double t=i/double(rate);
        pcm[size_t(i)*channels+c]=Encode(float(.3*std::sin(6.2831853*(c?330:220)*t)+(i%9600<48?(c?-.4:.4):0.)));
    }
    Rig common(pcm,frames,rate,channels,kScheduleCommon),stagger(pcm,frames,rate,channels,kScheduleStagger),
        pipeline(pcm,frames,rate,channels,kScheduleCompensated);
    std::array<float,kBlock> prev[2]{};unsigned delayed=0,right_differs=0,primed=0;
    auto run=[&](unsigned blocks,bool after_reset) {
        for(unsigned k=0;k<blocks;++k) {
            common.Tick();stagger.Tick();pipeline.Tick();
            for(unsigned c=0;c<2;++c) {
                const auto now=common.Voc(c),piped=pipeline.Voc(c);
                if(k==0 && after_reset) {CHECK(Silent(piped));++primed;}            // priming block
                else {CHECK(Same(piped,prev[c]));++delayed;}
                prev[c]=now;
            }
            CHECK(Same(stagger.Voc(0),common.Voc(0)));                               // left untouched
            if(!Same(stagger.Voc(1),common.Voc(1))) ++right_differs;
            if(channels==1) CHECK(Same(pipeline.Voc(0),pipeline.Voc(1)) && Same(stagger.Voc(0),stagger.Voc(1)));
        }
    };
    run(300,true);
    // A source change resets the DSP in every rig; the relation resumes after one block.
    for(Rig* rig:{&common,&stagger,&pipeline}) rig->bridge.generation.store(2);
    run(200,true);
    // Freeze and a pitch change inside an epoch: still an exact one-block delay.
    for(Rig* rig:{&common,&stagger,&pipeline}) {rig->e->Act(Action::Freeze);rig->e->ui.params.primary[Pitch]=.8f;}
    run(200,false);
    // The overload protection resets the vocoders as well.
    for(Rig* rig:{&common,&stagger,&pipeline}) rig->e->SkipVocoder(2);
    common.Tick();stagger.Tick();pipeline.Tick();common.Tick();stagger.Tick();pipeline.Tick();
    for(unsigned c=0;c<2;++c) prev[c]=common.Voc(c);
    run(100,true);
    CHECK(delayed>1500 && primed==6);
    if(channels==2) CHECK(right_differs>500); else CHECK(right_differs==0);
    std::printf("PASS schedule, %s source: compensated stagger == common grid delayed one block in %u channel blocks (bit for bit, through source change, freeze, pitch and protection resets); plain stagger leaves the left channel untouched%s\n",
                channels==1?"mono":"stereo",delayed,channels==2?" and changes the right":"");
}

int main() {
    Compare(2);Compare(1);
    std::printf("PASS vocoder scheduling: %u checks\n",checks);
}
