#pragma once
#include "parameter_manager.h"
namespace physical_modeling {
struct MidiMessage { uint8_t status = 0, data1 = 0, data2 = 0; };
// USB libDaisy parser owns framing/running status. Validate its messages again.
template<class Synth> void HandleMidi(Synth& synth, MidiMessage message) {
    if(message.status < 0x80 || message.status >= 0xf0 || message.data1 > 127 || message.data2 > 127) return;
    switch(message.status & 0xf0) {
        case 0x90:
            if(message.data2) synth.NoteOn(message.data1, message.data2);
            break; // Velocity zero and Note Off deliberately leave resonators ringing.
        case 0xb0:
            if(message.data1 >= 20 && message.data1 <= 33)
                synth.parameters.Set(message.data1 - 20, float(message.data2) / 127.0f);
            break;
        case 0xe0:
            synth.parameters.SetBend(unsigned(message.data1) | (unsigned(message.data2) << 7));
            break;
        default: break; // Note Off, aftertouch, program changes: no musical assignment.
    }
}
}
