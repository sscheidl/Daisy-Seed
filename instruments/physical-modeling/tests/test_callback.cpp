#define main firmware_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cstdio>
using namespace physical_modeling;
int main() {
    hw.Init();
    std::fill_n(synth_storage, sizeof(synth_storage), static_cast<unsigned char>(0xff));
    InitializeSynth(); // Exercise actual startup from uninitialized NOLOAD memory.
    for(auto& pot:hw.pot) pot.value=.5f;
    float left[48],right[48]; float* output[]={left,right};
    auto tick=[&](unsigned count) {
        for(unsigned n=0;n<count;++n) {
            AudioCallback(nullptr,output,48); daisy::System::now++;
            for(float v:left) assert(std::isfinite(v) && std::fabs(v)<.8f);
        }
    };
    tick(25);
    for(auto& key:hw.pressed) key=true;
    tick(8);
    assert(led_mask.load()==65535 && synth->TriggerCount()==16);
    assert(hw.led_transfers==0); ServiceLeds(); assert(hw.led_transfers==1);
    daisy::System::now+=10; ServiceLeds(); assert(hw.led_transfers==1);
    for(auto& key:hw.pressed) key=false;
    tick(8); assert(led_mask.load()==0 && synth->TriggerCount()==16);
    ServiceLeds();
    for(unsigned bank=0;bank<2;++bank) for(int pos:{1,0,2}) {
        hw.tog_sw17.value=bank; hw.tog_sw18.value=pos;
        tick(40); assert(synth->ModelIndex()==SwitchModel(bank,pos));
    }
    midi.incoming.push_back({daisy::NoteOn,0,{69,127}});
    midi.incoming.push_back({daisy::PitchBend,0,{127,127}});
    midi.incoming.push_back({daisy::ControlChange,0,{21,127}});
    ServiceMidi(); tick(100);
    assert(synth->TriggerCount()==17);
    assert(synth->parameters.Target(Structure)==1);
    assert(std::fabs(synth->parameters.PitchOffset()-2)<1e-3);
    for(unsigned i=0;i<300;++i) midi.incoming.push_back({daisy::NoteOn,0,{uint8_t(i%128),100}});
    // Local + USB strikes, with a deliberately full producer queue.
    for(auto& key:hw.pressed) key=true;
    for(unsigned i=0;i<100;++i) { ServiceMidi(); tick(1); }
    assert(synth->TriggerCount()==333 && synth->OverflowCount()==0);
    assert(midi.sent.empty());
    const auto count=synth->TriggerCount();
    midi.incoming.push_back({daisy::NoteOn,0,{60,0}});
    ServiceMidi(); tick(2); assert(synth->TriggerCount()==count);
    puts("PASS production callback: all keys/LEDs, switches, USB CC/bend, 300-event backpressure, no MIDI echo");
}
