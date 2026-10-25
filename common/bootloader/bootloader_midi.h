#pragma once
// For apps that receive USB-MIDI through libDaisy's MidiUsbHandler events.
#include "bootloader_entry.h"
#include "desktop_devkit.h" // brings MidiEvent

namespace bootloader {
// True if a parsed event is the bootloader request SysEx.
inline bool IsRequest(const daisy::MidiEvent& event) {
    return event.type == daisy::SystemCommon && event.sc_type == daisy::SystemExclusive &&
           IsRequestBody(event.sysex_data, event.sysex_message_len);
}
}
