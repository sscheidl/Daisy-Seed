#pragma once
// For apps that receive USB-MIDI through libDaisy's MidiUsbHandler events.
#include "return_entry.h"
#include "desktop_devkit.h" // brings MidiEvent

namespace manager_return {
// True if a parsed event is the "return to the program manager" SysEx request.
inline bool IsRequest(const daisy::MidiEvent& event) {
    return event.type == daisy::SystemCommon && event.sc_type == daisy::SystemExclusive &&
           IsRequestBody(event.sysex_data, event.sysex_message_len);
}
}
