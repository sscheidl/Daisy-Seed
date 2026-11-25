// The production vocoder path against the reference it replaced in 0.2.2
// (reference_dsp.cpp, "MINCER (production)"): the CMSIS-DSP FFT backend against the
// plain radix-2 RFFT, PcmReader bit for bit against PcmView::Read, and Mincer against
// ReferenceMincer over sources, pitches and scan modes.
//
// What the A/B can and cannot show: the algorithm amplifies last-bit rounding. Phase
// locking normalises sums of three neighbouring bins that can nearly cancel, and the
// result feeds back through prev_. The reference therefore differs from ITSELF by up to
// 0 dB error/signal when nothing but its FFT rounding changes (the 0.2.0 F3 change).
// Sample identity is not a possible criterion, and no number here proves inaudibility:
// listening is the owner's test.
#include "fft.h"
#include "reference_dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <vector>
using namespace neb;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)) {std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}} while(0)

static void TestFft() {
    auto ref=std::make_unique<RFFT>();ref->Init();fft::Init();fft::Init();
    std::mt19937 rng(7);std::uniform_real_distribution<float> u(-1,1);
    std::vector<std::vector<float>> inputs;
    for(unsigned trial=0;trial<20;++trial) {
        std::vector<float> x(FFT_N);const float scale=trial%3==0?1e-3f:trial%7==0?1e-6f:1.f;
        for(float& v:x) v=u(rng)*scale;
        inputs.push_back(x);
    }
    for(unsigned at:{0u,7u,2047u}) {std::vector<float> x(FFT_N,0.f);x[at]=1;inputs.push_back(x);}
    inputs.push_back(std::vector<float>(FFT_N,.25f));                                  // DC
    {std::vector<float> x(FFT_N);for(unsigned i=0;i<FFT_N;++i) x[i]=i%2?-.5f:.5f;inputs.push_back(x);}  // Nyquist
    for(unsigned bin:{1u,5u,511u,1023u}) {
        std::vector<float> x(FFT_N);
        for(unsigned i=0;i<FFT_N;++i) x[i]=float(std::cos(6.283185307179586*bin*i/FFT_N+.3));
        inputs.push_back(x);
    }
    inputs.push_back(std::vector<float>(FFT_N,0.f));
    double worst_forward=0,worst_inverse=0;
    for(const auto& x:inputs) {
        float peak=0;for(float v:x) peak=std::max(peak,std::abs(v));
        std::vector<float> expected(x),time(x),bins(FFT_N),back(FFT_N);
        ref->Forward(expected.data());                    // Csound packing, in place
        fft::Forward(time.data(),bins.data());
        float bin_peak=0,err=0;
        for(unsigned i=0;i<FFT_N;++i) {bin_peak=std::max(bin_peak,std::abs(expected[i]));err=std::max(err,std::abs(bins[i]-expected[i]));}
        CHECK(err<=bin_peak*2e-5f+1e-9f);
        if(bin_peak>0) worst_forward=std::max(worst_forward,double(err/bin_peak));
        // The inverse reads its spectrum only and returns the input (same scaling as RFFT).
        std::vector<float> spectrum(expected);
        fft::Inverse(spectrum.data(),back.data());
        CHECK(std::memcmp(spectrum.data(),expected.data(),sizeof(float)*FFT_N)==0);
        err=0;for(unsigned i=0;i<FFT_N;++i) err=std::max(err,std::abs(back[i]-x[i]));
        CHECK(err<=peak*2e-5f+1e-9f);
        if(peak>0) worst_inverse=std::max(worst_inverse,double(err/peak));
        for(float v:back) CHECK(std::isfinite(v));
    }
    std::printf("PASS FFT backend: %zu inputs against the radix-2 reference, worst relative error forward %.1e, round trip %.1e\n",
                inputs.size(),worst_forward,worst_inverse);
}

static void TestReader() {
    std::mt19937 rng(11);std::uniform_int_distribution<int> sample(-32768,32767);
    unsigned compared=0,fmods=0;
    // WrapFmod against std::fmod, bit for bit, over the ranges Read can see.
    for(double n:{1.0,2.0,3.0,7.0,100.0,513.0,12345.0,2097152.0,2147483647.0}) {
        std::vector<double> xs={0.0,-0.0,n,-n,2*n,-2*n,n-1e-9,n+1e-9,-n+1e-9,-n-1e-9,.5,-.5,1e-300,-1e-300,
            1125899906842623.0,-1125899906842623.0,1e15,-1e15,1e300,-1e300,double(INFINITY),-double(INFINITY),double(NAN)};
        for(double scale:{10.0,1e4,1e9,1e12,1e15}) {
            std::uniform_real_distribution<double> u(-scale,scale);
            for(unsigned i=0;i<3000;++i) xs.push_back(u(rng));
        }
        for(unsigned k=1;k<2000;++k) {xs.push_back(k*n);xs.push_back(-k*n);xs.push_back(std::nextafter(k*n,0.0));xs.push_back(std::nextafter(k*n,1e300));}
        for(double x:xs) {
            const double a=std::fmod(x,n),b=WrapFmod(x,n);
            if(std::isnan(a)) CHECK(std::isnan(b)); else CHECK(std::memcmp(&a,&b,sizeof a)==0);
            ++fmods;
        }
    }
    std::printf("PASS WrapFmod: %u values bit-identical to std::fmod\n",fmods);
    for(unsigned frames:{0u,1u,2u,3u,7u,512u,4801u}) for(unsigned channels:{1u,2u}) for(bool null_data:{false,true}) {
        std::vector<int16_t> pcm(size_t(frames)*channels+2);
        for(auto& v:pcm) v=int16_t(sample(rng));
        const PcmView view{null_data?nullptr:pcm.data(),frames,48000,uint16_t(channels)};
        std::vector<double> positions={0.0,-0.0,.25,1.0,1.5,double(frames),double(frames)-1,double(frames)-1.0000001,
            double(frames)-.5,double(frames)-2,double(frames)+.75,-1e-9,-1.0,-.5,-double(frames)-.25,1e12,-1e12,
            double(NAN),double(INFINITY),-double(INFINITY),4294967296.0,4294967295.5};
        std::uniform_real_distribution<double> u(-3.0*frames-10,3.0*frames+10);
        for(unsigned i=0;i<4000;++i) positions.push_back(u(rng));
        for(unsigned c=0;c<2;++c) {
            const PcmReader read(view,c);
            for(double p:positions) {
                const float a=view.Read(p,c),b=read(p);
                CHECK(std::memcmp(&a,&b,sizeof a)==0);++compared;
            }
        }
    }
    std::printf("PASS PcmReader: %u reads bit-identical to PcmView::Read (empty, one-frame, wrap, negative, NaN, mono/stereo)\n",compared);
}

struct Source {
    const char* name;std::vector<int16_t> pcm;uint32_t frames,rate;uint16_t channels;
    PcmView View() const {return {pcm.data(),frames,rate,channels};}
};
static Source Make(const char* name,uint32_t frames,uint32_t rate,uint16_t channels,
                   const std::function<float(uint32_t,unsigned)>& f) {
    Source s{name,std::vector<int16_t>(size_t(frames)*channels),frames,rate,channels};
    for(uint32_t i=0;i<frames;++i) for(unsigned c=0;c<channels;++c) s.pcm[size_t(i)*channels+c]=Encode(f(i,c));
    return s;
}
struct Scan {const char* name;double speed;bool freeze_half;unsigned retrigger;double loop;};

// Long-term band energies of one signal: independent of the vocoder's phase state.
constexpr unsigned kBandEdges[]={1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,1024};
constexpr unsigned kBands=sizeof kBandEdges/sizeof kBandEdges[0]-1;
struct Spectrum {
    std::vector<float> frame=std::vector<float>(FFT_N);unsigned used=0;double band[kBands]={};
    void Push(float x,RFFT& fft,const float* window) {
        frame[used]=x*window[used];
        if(++used<FFT_N) return;
        used=0;fft.Forward(frame.data());
        for(unsigned k=0;k<kBands;++k) for(unsigned bin=kBandEdges[k];bin<kBandEdges[k+1];++bin)
            band[k]+=double(frame[2*bin])*frame[2*bin]+double(frame[2*bin+1])*frame[2*bin+1];
    }
};
// Largest band energy difference in dB over the bands within 60 dB of the total.
static double BandDb(const Spectrum& candidate,const Spectrum& reference) {
    double total=0,worst=0;for(double v:reference.band) total+=v;
    for(unsigned k=0;k<kBands;++k) if(reference.band[k]>total*1e-6 && candidate.band[k]>0)
        worst=std::max(worst,std::abs(10*std::log10(candidate.band[k]/reference.band[k])));
    return worst;
}
// Sample error of a candidate against the reference.
struct Pair {
    double max_diff=0,error=0,energy=0,window_error=0,window_energy=0,worst_window_db=-999;
    void Add(double candidate,double reference) {
        const double d=candidate-reference;
        max_diff=std::max(max_diff,std::abs(d));error+=d*d;energy+=reference*reference;
        window_error+=d*d;window_energy+=reference*reference;
    }
    void CloseWindow(unsigned samples) {
        // Windows below -80 dBFS RMS are silence: a ratio there says nothing.
        if(window_energy/samples>1e-8) worst_window_db=std::max(worst_window_db,10*std::log10(window_error/window_energy+1e-30));
        window_error=window_energy=0;
    }
    double RatioDb() const {return energy>0?10*std::log10(error/energy+1e-30):-999;}
};
struct Result {Pair fresh,baseline;double fresh_band_db=0,baseline_band_db=0;};

// Both channels through the production vocoder, the reference and the reference with its
// two forward FFTs unpacked, on the same position stream (wrapped into a loop, as
// Engine::Process does). "fresh" is production against reference; "baseline" is the
// reference against itself with nothing but FFT rounding changed.
static Result Run(const Source& source,float pitch,const Scan& scan,unsigned blocks,bool sweep_pitch=false) {
    const PcmView view=source.View();
    auto work=std::make_unique<MincerWork>();work->Init();
    auto analysis=std::make_unique<RFFT>();analysis->Init();
    std::unique_ptr<Mincer> fresh[2]={std::make_unique<Mincer>(),std::make_unique<Mincer>()};
    std::unique_ptr<ReferenceMincer> ref[2]={std::make_unique<ReferenceMincer>(),std::make_unique<ReferenceMincer>()};
    std::unique_ptr<ReferenceMincer> plain[2]={std::make_unique<ReferenceMincer>(),std::make_unique<ReferenceMincer>()};
    for(unsigned c=0;c<2;++c) {fresh[c]->Init(48000,*work);ref[c]->Init(48000);plain[c]->Init(48000);plain[c]->SetPackedForward(false);}
    const double loop=scan.loop>0?scan.loop:double(source.frames?source.frames:1);
    double pos=0;Result r;unsigned in_window=0;
    auto spectrum=std::make_unique<Spectrum[]>(6);  // fresh, reference, plain x two channels
    double positions[kBlock];float a[2][kBlock],b[2][kBlock],d[2][kBlock];
    for(unsigned block=0;block<blocks;++block) {
        if(scan.retrigger && block && block%scan.retrigger==0) pos=0;
        const bool frozen=scan.freeze_half && block>=blocks/2;
        for(unsigned s=0;s<kBlock;++s) {
            if(pos<0 || pos>=loop) {pos=std::fmod(pos,loop);if(pos<0) pos+=loop;}
            positions[s]=pos;
            if(!frozen) pos+=scan.speed*double(source.rate)/kRate;
        }
        const float p=(sweep_pitch?pitch*std::pow(2.f,2*std::sin(block*.01f)):pitch)*source.rate/kRate;
        for(unsigned c=0;c<2;++c) {
            fresh[c]->Process(positions,view,c,p,.8f,a[c],kBlock);
            ref[c]->Process(positions,view,c,p,.8f,b[c],kBlock);
            plain[c]->Process(positions,view,c,p,.8f,d[c],kBlock);
        }
        for(unsigned s=0;s<kBlock;++s) {
            for(unsigned c=0;c<2;++c) {
                CHECK(std::isfinite(a[c][s]) && std::isfinite(b[c][s]) && std::isfinite(d[c][s]));
                r.fresh.Add(a[c][s],b[c][s]);r.baseline.Add(d[c][s],b[c][s]);
                spectrum[c].Push(a[c][s],*analysis,work->window);
                spectrum[2+c].Push(b[c][s],*analysis,work->window);
                spectrum[4+c].Push(d[c][s],*analysis,work->window);
            }
            if(++in_window==4096) {in_window=0;r.fresh.CloseWindow(2*4096);r.baseline.CloseWindow(2*4096);}
        }
    }
    for(unsigned c=0;c<2;++c) {
        r.fresh_band_db=std::max(r.fresh_band_db,BandDb(spectrum[c],spectrum[2+c]));
        r.baseline_band_db=std::max(r.baseline_band_db,BandDb(spectrum[4+c],spectrum[2+c]));
    }
    return r;
}

static void TestVocoderAb() {
    std::mt19937 rng(3);std::uniform_real_distribution<float> u(-1,1);
    const double tau=6.283185307179586;
    std::vector<float> noise(2*65536);for(float& v:noise) v=.5f*u(rng);
    auto music=[&](uint32_t i,unsigned) {
        const double t=i/48000.0;const double hit=std::fmod(t,.37);
        return float(.2*std::sin(tau*110*t)+.15*std::sin(tau*277.2*t+1)+.1*std::sin(tau*1661*t)+.4*std::exp(-hit*60)*std::sin(tau*900*hit));
    };
    std::vector<Source> sources;
    sources.push_back(Make("tone L, clicks R",96000,48000,2,[&](uint32_t i,unsigned c) {return c?(i%4800<64?.5f:0.f):float(.3*std::sin(tau*220*i/48000));}));
    sources.push_back(Make("independent noise",65536,48000,2,[&](uint32_t i,unsigned c) {return noise[2*i+c];}));
    sources.push_back(Make("dual mono music",120000,48000,2,music));
    sources.push_back(Make("anticorrelated music",120000,48000,2,[&](uint32_t i,unsigned c) {return c?-music(i,0):music(i,0);}));
    sources.push_back(Make("mono 44.1 kHz music",110250,44100,1,[&](uint32_t i,unsigned) {return music(uint32_t(i*48000.0/44100),0);}));
    sources.push_back(Make("one-LSB noise",48000,48000,2,[&](uint32_t i,unsigned c) {return noise[2*(i%65536)+c]>0?1/32768.f:-1/32768.f;}));
    sources.push_back(Make("single impulse",48000,48000,2,[](uint32_t i,unsigned c) {return i==(c?24000u:1000u)?.9f:0.f;}));
    sources.push_back(Make("silence",48000,48000,2,[](uint32_t,unsigned) {return 0.f;}));
    sources.push_back(Make("one frame",1,48000,2,[](uint32_t,unsigned c) {return c?-.25f:.5f;}));
    sources.push_back(Make("100 frames",100,48000,2,[&](uint32_t i,unsigned c) {return float(.4*std::sin(tau*(c+1)*i/100));}));
    sources.push_back(Make("513 frames",513,48000,2,[&](uint32_t i,unsigned c) {return float(.4*std::sin(tau*(3+c)*i/513));}));
    sources.push_back(Make("2047 frames mono",2047,48000,1,[&](uint32_t i,unsigned) {return float(.4*std::sin(tau*9*i/2047));}));
    const Scan forward{"forward 0.7",.7,false,0,0},unity{"forward 1.0",1,false,0,0},reverse{"reverse",-1,false,0,0};
    const Scan freeze{"freeze at half",.9,true,0,0},retrigger{"retrigger",1,false,37,0},fast{"speed 4",4,false,0,0};
    const Scan short_loop{"300-frame loop",1,false,0,300};
    struct Case {unsigned source;float pitch;Scan scan;unsigned blocks;bool sweep;};
    std::vector<Case> cases={
        {0,1.3f,forward,1200,false},{0,1,unity,1200,false},{1,1,unity,1200,false},{1,.5f,reverse,1200,false},
        {2,1,unity,1200,false},{2,1,freeze,1200,false},{2,1,retrigger,1200,false},{2,2,fast,1200,false},
        {2,.015625f,forward,1200,false},{2,32,forward,1200,false},{2,1,short_loop,1200,false},{2,1,unity,1200,true},
        {3,1,unity,1200,false},{3,.75f,reverse,1200,false},{4,1,unity,1200,false},{4,1.5f,forward,1200,false},
        {5,1,unity,1200,false},{6,1,unity,1200,false},{7,1,unity,400,false},{8,1,unity,400,false},
        {9,1,unity,1200,false},{9,3,reverse,1200,false},{10,1,unity,1200,false},{11,.5f,forward,1200,false},
        {2,1,unity,33750,false},   // three minutes
    };
    // Gates:
    // (1) where the reference agrees with itself (baseline below -80 dB) the replacement
    //     must agree with it too; that is where a real defect would show;
    // (2) elsewhere the replacement may not be further from the reference than the
    //     reference is from itself, plus 6 dB;
    // (3) long-term band energies stay within 0.5 dB of the reference, or of the
    //     baseline's own spread plus 0.5 dB.
    bool within_gates=true;unsigned tight=0;double worst_tight=-999,worst_band=0,worst_excess=-999;
    for(const Case& c:cases) {
        const Result r=Run(sources[c.source],c.pitch,c.scan,c.blocks,c.sweep);
        const double fresh_db=r.fresh.RatioDb(),base_db=r.baseline.RatioDb();
        std::printf("  %-21s pitch %-8g %-15s %6u blocks: new/ref %7.1f dB (max %.1e, window %6.1f dB, bands %.2f dB) | ref/ref %7.1f dB (max %.1e, bands %.2f dB)\n",
                    sources[c.source].name,double(c.pitch),c.sweep?"pitch sweep":c.scan.name,c.blocks,
                    fresh_db,r.fresh.max_diff,r.fresh.worst_window_db,r.fresh_band_db,base_db,r.baseline.max_diff,r.baseline_band_db);
        if(std::strcmp(sources[c.source].name,"silence")==0) {CHECK(r.fresh.max_diff==0 && r.fresh.energy==0);continue;}
        if(base_db<-80) {++tight;worst_tight=std::max(worst_tight,fresh_db);within_gates&=fresh_db<-80 && r.fresh.max_diff<2e-4;}
        else {worst_excess=std::max(worst_excess,fresh_db-base_db);within_gates&=fresh_db<=base_db+6;}
        worst_band=std::max(worst_band,r.fresh_band_db);
        within_gates&=r.fresh_band_db<=std::max(.5,r.baseline_band_db+.5);
    }
    CHECK(within_gates && tight>=10);
    std::printf("PASS vocoder A/B: %zu scenarios; %u well-conditioned ones agree to %.1f dB or better; elsewhere at most %+.1f dB against the reference's own rounding spread; band energies within %.2f dB\n",
                cases.size(),tight,worst_tight,worst_excess,worst_band);
}

// Premises of Engine::Process's mono shortcut: on a mono source channel 1 computes bit
// for bit what channel 0 computes, and a copied state continues bit for bit.
static void TestMonoShortcut() {
    const double tau=6.283185307179586;
    const Source mono=Make("mono",30000,44100,1,[&](uint32_t i,unsigned) {return float(.4*std::sin(tau*330*i/44100)+.2*std::sin(tau*1234.5*i/44100));});
    const PcmView view=mono.View();
    auto work=std::make_unique<MincerWork>();work->Init();
    auto left=std::make_unique<Mincer>(),right=std::make_unique<Mincer>(),copy=std::make_unique<Mincer>();
    left->Init(48000,*work);right->Init(48000,*work);copy->Init(48000,*work);
    double positions[kBlock];float a[kBlock],b[kBlock],c[kBlock];double pos=0;unsigned identical=0;
    for(unsigned block=0;block<600;++block) {
        for(unsigned s=0;s<kBlock;++s) {positions[s]=pos;pos+=1.3*44100/48000;if(pos>=mono.frames) pos-=mono.frames;}
        const float pitch=(block<300?.75f:2.5f)*44100/48000.f;
        left->Process(positions,view,0,pitch,.8f,a,kBlock);
        right->Process(positions,view,1,pitch,.8f,b,kBlock);
        CHECK(std::memcmp(a,b,sizeof a)==0);identical+=kBlock;
        if(block==200) copy->CopyStateFrom(*left);
        if(block>200) {copy->Process(positions,view,0,pitch,.8f,c,kBlock);CHECK(std::memcmp(a,c,sizeof a)==0);}
    }
    std::printf("PASS mono shortcut premises: %u samples of channel 1 bit-identical to channel 0 on a mono source; a copied state continues bit-identically\n",identical);
}

int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    TestFft();TestReader();TestMonoShortcut();TestVocoderAb();
    std::printf("PASS vocoder replacement: %u checks\n",checks);
}
