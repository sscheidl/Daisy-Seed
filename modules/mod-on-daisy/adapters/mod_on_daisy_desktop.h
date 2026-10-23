#pragma once
// The only libDaisy-dependent part of Mod on Daisy: Seed3 Desktop CV/gate
// inputs, libDaisy MIDI events and the USB-MIDI transmit path.
#include "desktop_devkit.h" // brings daisy_seed.h, MidiEvent and MidiUsbHandler
#include "usbd_core.h"
#include "mod_on_daisy/module.h"

extern "C" USBD_HandleTypeDef hUsbDeviceHS; // external USB port (J7)

namespace mod::adapter {
// Audio IRQ, after hw.UpdateAllControls() (which processes the CV inputs).
inline void ReadHardware(Engine& engine, daisy::desktop_devkit::Hardware& hw) {
    engine.SetHardware(hw.cv[0].Value(), hw.cv[1].Value(), hw.gate_in[0].State(), hw.gate_in[1].State());
}

// libDaisy MidiEvent -> MidiMessage. Returns false for types Mod on Daisy ignores.
// The SysEx pointer refers into `event`, which must outlive the call to HandleMidi.
inline bool ToMessage(const daisy::MidiEvent& event, MidiMessage& m) {
    const uint8_t channel = uint8_t(event.channel & 15);
    m = MidiMessage{};
    switch(event.type) {
        case daisy::NoteOff: m.status = uint8_t(0x80 | channel); break;
        case daisy::NoteOn: m.status = uint8_t(0x90 | channel); break;
        case daisy::PolyphonicKeyPressure: m.status = uint8_t(0xA0 | channel); break;
        case daisy::ControlChange: m.status = uint8_t(0xB0 | channel); break;
        case daisy::ChannelPressure: m.status = uint8_t(0xD0 | channel); break;
        case daisy::ChannelMode: // CC120..127 are parsed separately by libDaisy
            m.status = uint8_t(0xB0 | channel);
            m.data0 = uint8_t(120 + event.cm_type);
            m.data1 = event.data[1];
            return true;
        case daisy::SystemRealTime:
            if(event.srt_type == daisy::TimingClock)
                m.status = 0xF8;
            else if(event.srt_type == daisy::Start)
                m.status = 0xFA;
            else
                return false;
            return true;
        case daisy::SystemCommon:
            if(event.sc_type != daisy::SystemExclusive)
                return false;
            m.status = 0xF0;
            m.sysex = event.sysex_data;
            m.sysex_size = event.sysex_message_len;
            return true;
        default: return false;
    }
    m.data0 = event.data[0];
    m.data1 = event.data[1];
    return true;
}

// Main loop only: SendMessage may block for up to 3 x 100 us.
class UsbMidiSender : public MidiSender {
  public:
    explicit UsbMidiSender(daisy::MidiUsbHandler& midi) : midi_(midi) {}
    bool Send(const uint8_t* bytes, std::size_t size) override {
        if(hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED)
            return false; // libDaisy does not check for an enumerated host itself
        midi_.SendMessage(const_cast<uint8_t*>(bytes), size);
        return true;
    }
  private:
    daisy::MidiUsbHandler& midi_;
};
}
