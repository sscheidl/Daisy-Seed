// Final-mix A/B of two golden performances (NEBULAE_GOLDEN_DUMP of test_golden): the
// production engine against the same engine around the 0.2.1 vocoder
// (-DNEBULAE_REFERENCE_VOCODER=1). Usage: compare_audio <candidate.f32> <reference.f32>
//
// The vocoder amplifies last-bit rounding (tests/test_vocoder.cpp), so the sample error
// is reported, not gated. Gated are measures that do not depend on the vocoder's phase
// state: long-term band energies, the level of 85 ms windows, the peak, finiteness.
#include "reference_dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
using namespace neb;
#define CHECK(x) do {if(!(x)) {std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}} while(0)

static std::vector<float> Load(const char* path) {
    std::ifstream in(path,std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
    CHECK(!bytes.empty() && bytes.size()%(2*sizeof(float)*kBlock)==0);
    std::vector<float> samples(bytes.size()/sizeof(float));
    std::memcpy(samples.data(),bytes.data(),bytes.size());
    return samples;
}
constexpr unsigned kBandEdges[]={1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,1024};
constexpr unsigned kBands=sizeof kBandEdges/sizeof kBandEdges[0]-1;

int main(int argc,char** argv) {
    CHECK(argc==3);
    const std::vector<float> a=Load(argv[1]),b=Load(argv[2]);
    CHECK(a.size()==b.size());
    // Dump layout: per block, kBlock left samples then kBlock right samples.
    const size_t blocks=a.size()/(2*kBlock);
    auto fft=std::make_unique<RFFT>();fft->Init();
    float window[FFT_N];for(unsigned i=0;i<FFT_N;++i) window[i]=float(.5-.5*std::cos(6.283185307179586*i/FFT_N));
    double worst_band=0,worst_level=0,error=0,energy=0,max_diff=0,peak_a=0,peak_b=0,total_a=0,total_b=0;
    unsigned level_windows=0;
    for(unsigned channel=0;channel<2;++channel) {
        std::vector<float> xa(blocks*kBlock),xb(blocks*kBlock);
        for(size_t block=0;block<blocks;++block) for(unsigned s=0;s<kBlock;++s) {
            xa[block*kBlock+s]=a[(2*block+channel)*kBlock+s];xb[block*kBlock+s]=b[(2*block+channel)*kBlock+s];
        }
        double band_a[kBands]={},band_b[kBands]={};
        for(size_t start=0;start+FFT_N<=xa.size();start+=FFT_N/2) {
            float fa[FFT_N],fb[FFT_N];
            for(unsigned i=0;i<FFT_N;++i) {fa[i]=xa[start+i]*window[i];fb[i]=xb[start+i]*window[i];}
            fft->Forward(fa);fft->Forward(fb);
            for(unsigned k=0;k<kBands;++k) for(unsigned bin=kBandEdges[k];bin<kBandEdges[k+1];++bin) {
                band_a[k]+=double(fa[2*bin])*fa[2*bin]+double(fa[2*bin+1])*fa[2*bin+1];
                band_b[k]+=double(fb[2*bin])*fb[2*bin]+double(fb[2*bin+1])*fb[2*bin+1];
            }
        }
        double total=0;for(double v:band_b) total+=v;
        for(unsigned k=0;k<kBands;++k) if(band_b[k]>total*1e-6 && band_a[k]>0)
            worst_band=std::max(worst_band,std::abs(10*std::log10(band_a[k]/band_b[k])));
        for(size_t start=0;start+4096<=xa.size();start+=4096) {
            double ea=0,eb=0;
            for(unsigned i=0;i<4096;++i) {ea+=double(xa[start+i])*xa[start+i];eb+=double(xb[start+i])*xb[start+i];}
            if(eb/4096>1e-6 && ea>0) {worst_level=std::max(worst_level,std::abs(10*std::log10(ea/eb)));++level_windows;}  // above -60 dBFS RMS
        }
        for(size_t i=0;i<xa.size();++i) {
            CHECK(std::isfinite(xa[i]) && std::isfinite(xb[i]));
            const double d=double(xa[i])-xb[i];
            error+=d*d;energy+=double(xb[i])*xb[i];max_diff=std::max(max_diff,std::abs(d));
            peak_a=std::max(peak_a,double(std::abs(xa[i])));peak_b=std::max(peak_b,double(std::abs(xb[i])));
            total_a+=double(xa[i])*xa[i];total_b+=double(xb[i])*xb[i];
        }
    }
    const double total_db=10*std::log10(total_a/total_b);
    std::printf("final mix, %.1f s stereo: sample error/signal %.1f dB (max diff %.2e; reported, the vocoder is rounding-chaotic); "
                "total energy %+.3f dB, band energies within %.2f dB, level of %u 85 ms windows within %.2f dB, peak %.3f against %.3f\n",
                double(blocks*kBlock)/kRate,10*std::log10(error/energy+1e-30),max_diff,total_db,worst_band,level_windows,worst_level,peak_a,peak_b);
    CHECK(energy>1 && level_windows>100);
    CHECK(std::abs(total_db)<.25 && worst_band<1.0 && worst_level<6.0 && std::abs(peak_a-peak_b)<.1);
    std::puts("PASS final-mix A/B: production engine against the 0.2.1 vocoder in the same engine (phase-independent measures)");
}
