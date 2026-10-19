#include "hid/midi_parser.h"
#include "midi_control.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct Target {
    physical_modeling::ParameterManager parameters;
    unsigned notes=0;
    void NoteOn(unsigned note,unsigned velocity) { assert(note<128 && velocity>0 && velocity<128); ++notes; }
};
int main() {
    daisy::MidiParser parser; parser.Init(); Target target;
    auto bytes=[&](std::initializer_list<uint8_t> data) {
        for(uint8_t byte:data) {
            daisy::MidiEvent e;
            if(!parser.Parse(byte,&e)) continue;
            uint8_t status;
            if(e.type==daisy::NoteOn) status=0x90;
            else if(e.type==daisy::ControlChange) status=0xb0;
            else if(e.type==daisy::PitchBend) status=0xe0;
            else continue;
            physical_modeling::HandleMidi(target,{status,e.data[0],e.data[1]});
        }
    };
    bytes({0x90,60}); assert(target.notes==0);
    bytes({0xf8,127}); assert(target.notes==1);
    bytes({64,100,67,0}); assert(target.notes==2); // Running status and zero velocity.
    bytes({0x90,60,0xb0,21,127}); assert(target.notes==2 && target.parameters.Target(1)==1);
    bytes({0xf0,1,2,3,0xf7,0x90,72,127}); assert(target.notes==3);
    bytes({0xf1,1,0x90,60,127,0x90,64,127}); assert(target.notes==5); // System subtype must not leak into notes.
    bytes({0xe0,0}); bytes({64});
    for(unsigned i=0;i<500;++i) target.parameters.Advance();
    assert(target.parameters.PitchOffset()==0);
    puts("PASS libDaisy parser compatibility fix: incomplete messages, realtime interleave, running status, interrupted notes, SysEx recovery");
}
