#include "physical_modeling.h"
#include "midi_control.h"
#include "stmlib/utils/random.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
#include <new>
using namespace physical_modeling;
static Synth synth;
static float left[48], right[48];
static double Render(unsigned blocks) {
    double energy = 0;
    for(unsigned b = 0; b < blocks; ++b) {
        synth.Process(left, right, 48);
        for(unsigned i = 0; i < 48; ++i) {
            assert(std::isfinite(left[i]) && std::isfinite(right[i]));
            assert(std::fabs(left[i]) < 0.8f && std::fabs(right[i]) < 0.8f);
            energy += double(left[i])*left[i] + double(right[i])*right[i];
        }
    }
    return energy;
}
static void Reset() {
    synth.~Synth(); new(&synth) Synth{}; synth.Init();
    stmlib::Random::Seed(0x12345678);
}
static void Parameters() {
    assert(Transpose(0)==-24 && Transpose(.5f)==0 && Transpose(1)==24);
    assert(Bend(0)==-2 && Bend(8192)==0 && Bend(16383)==2);
    for(unsigned x=0; x<16384; ++x) assert(Bend(x)>=-2 && Bend(x)<=2);
    assert(Mix(.3f,-.2f,0)==.3f && std::fabs(Mix(.3f,-.2f,1)+.2f)<1e-6f);
    assert(Output(.5f,1)==.4f && Output(.5f,0)==0);
    for(float x : {0.0f,1.0f,-1.0f,1e30f,-1e30f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        assert(std::isfinite(Output(x,1)) && std::fabs(Output(x,1))<.8f);
    ParameterManager p;
    float value = 0; PotFilter pot;
    assert(pot.Process(.5f,value));
    for(unsigned i=0;i<200;++i) assert(!pot.Process(.5001f,value));
    for(unsigned i=0;i<500;++i) pot.Process(1,value);
    assert(value==1);
    for(unsigned i=0;i<500;++i) pot.Process(0,value);
    assert(value==0);
    p.Pot(2,.5f); p.Set(Brightness,1);
    for(unsigned i=0;i<500;++i) p.Pot(2,.5001f);
    assert(p.Target(Brightness)==1); // Stationary knob cannot overwrite MIDI.
    for(unsigned i=0;i<500;++i) p.Pot(2,.1f);
    assert(p.Target(Brightness)<.11f);
    ModelSwitch sw;
    unsigned model = 99;
    for(unsigned bank=0;bank<2;++bank) for(int pos : {1,0,2}) {
        for(unsigned i=0;i<19;++i) sw.Process(bank,pos,model);
        assert(sw.Process(bank,pos,model));
        assert(model==SwitchModel(bank,pos));
        assert(!sw.Process(bank,pos,model));
    }
    for(unsigned i=0;i<100;++i) assert(!sw.Process(i&1, i&1 ? 1:0, model));
    puts("PASS parameter bounds, smoothing, jitter, ownership, switch debounce, output safety");
}
static void Events() {
    Reset();
    for(auto note:kNotes) assert(synth.NoteOn(note,127));
    Render(1); assert(synth.TriggerCount()==16 && synth.PendingNotes()==0);
    assert(synth.VoiceNote(0)==79 && synth.VoiceNote(3)==84);
    for(unsigned i=0;i<16;++i) synth.NoteOn(72,100);
    Render(1); assert(synth.TriggerCount()==32);
    for(unsigned i=0;i<4;++i) assert(synth.VoiceNote(i)==72);
    const auto count=synth.TriggerCount();
    HandleMidi(synth,{0x90,60,0}); HandleMidi(synth,{0x80,60,127});
    HandleMidi(synth,{0x90,255,127}); HandleMidi(synth,{0x90,60,255});
    Render(1); assert(synth.TriggerCount()==count);
    for(unsigned note=0;note<128;++note) HandleMidi(synth,{0x9f,uint8_t(note),127});
    Render(8); assert(synth.TriggerCount()==count+128);
    for(unsigned cc=20;cc<=33;++cc) {
        HandleMidi(synth,{0xb0,uint8_t(cc),0});
        assert(synth.parameters.Target(cc-20)==(cc==29 ? 1.0f:0.0f));
        HandleMidi(synth,{0xb0,uint8_t(cc),127}); assert(synth.parameters.Target(cc-20)==1);
    }
    synth.parameters.Set(Frequency,.5f);
    for(auto raw : {0u,8192u,16383u}) {
        HandleMidi(synth,{0xe0,uint8_t(raw&127),uint8_t(raw>>7)}); Render(100);
        assert(std::fabs(synth.parameters.PitchOffset()-Bend(raw))<1e-4f);
    }
    Reset();
    for(unsigned i=0;i<256;++i) assert(synth.NoteOn(i%128,127));
    assert(!synth.NoteOn(60,127) && synth.OverflowCount()==1);
    Render(16); assert(synth.TriggerCount()==256);
    puts("PASS 16 simultaneous keys, repeated/unison notes, 0..127 MIDI, velocity-zero, CC20..33, bend, FIFO bounds");
}
static void Models() {
    std::array<double,6> energies{};
    for(unsigned model=0;model<6;++model) {
        Reset(); synth.parameters.SelectModel(model); Render(25);
        assert(synth.ModelIndex()==model);
        synth.parameters.Set(Wet,0); Render(25);
        assert(Render(10)<1e-10); // FM must also be silent before any strike.
        for(unsigned note : {60u,64u,67u,72u}) synth.NoteOn(note,127);
        energies[model]=Render(250);
        assert(energies[model]>1e-5);
        const double tail=Render(1500); assert(tail>0);
        synth.parameters.Set(Wet,1); synth.parameters.Set(Decay,1);
        synth.parameters.Set(Brightness,1); synth.parameters.Set(Damping,1);
        synth.parameters.Set(Diffusion,1); synth.parameters.Set(InputGain,1);
        Render(100);
        for(unsigned n=0;n<128;++n) synth.NoteOn(n,127);
        const double wet_energy=Render(3000); printf("wet model %u = %.9f\n",model,wet_energy); assert(wet_energy>1e-6);
        // Extreme but valid modulation, and switching with sounding voices.
        for(unsigned step=0;step<100;++step) {
            synth.parameters.Set(Structure,step&1); synth.parameters.Set(Position,(step>>1)&1);
            synth.parameters.Set(Frequency,(step>>2)&1); synth.parameters.SetBend(step&1 ? 0:16383);
            synth.parameters.SelectModel(step%6); synth.NoteOn(step%128,127); Render(5);
        }
        printf("PASS actual DSP model %u, dry energy %.6f, wet tails, extremes and transitions\n", model+1,energies[model]);
    }
    for(unsigned a=0;a<6;++a) for(unsigned b=a+1;b<6;++b)
        assert(std::fabs(energies[a]-energies[b])>1e-7);
    // Excitation is applied before resonance, including FM's internal envelope.
    for(unsigned model=0;model<6;++model) {
        Reset(); synth.parameters.SelectModel(model); synth.parameters.Set(Excitation,0);
        Render(100); synth.NoteOn(69,127); assert(Render(100)<1e-10);
    }
    puts("PASS distinct DSP paths, zero excitation, maximum parameters, finite protected stereo output");
}
static void Reverb() {
    uint16_t buffer[kReverbWords]{}; torus::Reverb reverb;
    reverb.Init(buffer); reverb.set_time(.98f); reverb.set_amount(1);
    reverb.set_diffusion(.625f); reverb.set_input_gain(.2f); reverb.set_lp(.7f);
    double early=0, late=0;
    for(unsigned block=0;block<30000;++block) {
        std::fill_n(left,48,0); std::fill_n(right,48,0);
        if(block==0) left[0]=1;
        reverb.Process(left,right,48);
        for(unsigned i=0;i<48;++i) {
            assert(std::isfinite(left[i]) && std::isfinite(right[i]));
            if(block<2000) early+=double(left[i])*left[i]+double(right[i])*right[i];
            if(block>28000) late+=double(left[i])*left[i]+double(right[i])*right[i];
        }
        if(block==0) assert(std::fabs(left[0])<0.99f && std::fabs(right[0])<0.99f); // Allpasses have feedthrough, but no unity dry copy.
    }
    assert(early>0 && late<early);
    puts("PASS Torus wet-only impulse response, stable .98 feedback and 30 second decay");
}
static double Peak(unsigned note, unsigned bend) {
    Reset(); synth.parameters.SelectModel(0); synth.parameters.Set(Structure,.25f);
    synth.parameters.Set(Wet,0); synth.parameters.Set(Damping,.7f);
    synth.parameters.SetBend(bend); Render(100); synth.NoteOn(note,127); Render(50);
    std::vector<float> samples;
    for(unsigned b=0;b<100;++b) {
        Render(1); for(unsigned i=0;i<48;++i) samples.push_back(left[i]+right[i]);
    }
    const double expected=440*std::pow(2.0,(double(note)-69+Bend(bend))/12);
    double peak=0,best=-1;
    for(double hz=expected*.95;hz<expected*1.05;hz+=1) {
        double re=0,im=0;
        for(unsigned i=0;i<samples.size();++i) {
            const double phase=6.283185307179586*hz*i/48000;
            const double window=.5-.5*std::cos(6.283185307179586*i/(samples.size()-1));
            re+=samples[i]*window*std::cos(phase); im+=samples[i]*window*std::sin(phase);
        }
        const double power=re*re+im*im;
        if(power>best) { best=power; peak=hz; }
    }
    printf("pitch expected %.2f actual %.2f power %.9f\n",expected,peak,best); assert(best>1e-6 && std::fabs(peak-expected)<4);
    return peak;
}
static void PitchAndVelocity() {
    const double a=Peak(69,8192),octave=Peak(81,8192),bend=Peak(69,16383);
    assert(std::fabs(octave/a-2)<.02 && bend>a);
    for(unsigned model=0;model<6;++model) {
        double energies[2];
        for(unsigned pass=0;pass<2;++pass) {
            Reset(); synth.parameters.SelectModel(model); synth.parameters.Set(Wet,0); Render(50);
            synth.NoteOn(69,pass ? 127:32); energies[pass]=Render(100);
        }
        assert(energies[0]>0 && energies[0]<energies[1]);
    }
    printf("PASS audio spectral pitch A4 %.2f Hz, octave %.2f Hz, bend %.2f Hz; velocity in all models\n",a,octave,bend);
}
static void RawDsp() {
    static torus::Part part;
    torus::PerformanceState state{}; state.internal_exciter=true;
    float input[12]{};
    for(unsigned model=0;model<6;++model) {
        part.Init(); part.set_model(static_cast<torus::ResonatorModel>(model));
        for(unsigned block=0;block<2048;++block) {
            torus::Patch patch{};
            patch.structure=(block>>7)&1; patch.brightness=(block>>8)&1;
            patch.damping=(block>>9)&1; patch.position=(block>>10)&1;
            state.chord=std::min(10,int(patch.structure*11)); state.fm=block&1 ? -26:26;
            if(block%8==0) for(unsigned i=0;i<4;++i) part.Trigger((block/8+i)%128,1);
            part.Process(state,patch,input,left,right,12);
            for(unsigned i=0;i<12;++i) assert(std::isfinite(left[i]) && std::isfinite(right[i]));
        }
    }
    puts("PASS raw six-model DSP finite before final output protection, all note/parameter extremes");
}
int main() { setbuf(stdout,nullptr); Parameters(); Events(); Models(); Reverb(); PitchAndVelocity(); RawDsp(); printf("PASS Synth static bytes: %zu; reverb reservation: %zu\n",sizeof(Synth),kReverbWords*sizeof(uint16_t)); }



