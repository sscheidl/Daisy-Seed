#pragma once
// Main-loop half of the bootloader entry: marks the update state on the panel
// LEDs and resets into the Daisy bootloader. Uses daisy::System from libDaisy;
// host tests provide a fake with ResetToBootloader().
#include "bootloader_entry.h"
#include "desktop_devkit.h"

namespace bootloader {

// Time for the last LED transfer and the SysEx reply to leave before the reset.
constexpr uint32_t kSettleMs = 20;

// Main loop only. Lights D1-D16 and resets; the external bootloader then waits for
// DFU on J7 without its two-second timeout. The PCA9685 keeps the LEDs lit across
// the MCU reset until the next application's hw.Init() clears them (expected
// behaviour, see README.md). Returns only where libDaisy refuses: an image that
// does not run under the Daisy bootloader (internal flash).
template <class Hardware>
void Enter(Hardware& hw) {
    for(int i = 0; i < 16; ++i)
        hw.SetLed(i, 1.0f);
    hw.UpdateLeds();
    hw.UpdateLeds(); // waits until the previous transfer has finished
    daisy::System::Delay(kSettleMs);
    daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
}

// Main loop only: acts on a pending request. `send_ack(bytes, size)` transmits the
// SysEx reply and is called only when a host asked over SysEx. Returns true if a
// request was handled (on hardware the call does not return then).
template <class Hardware, class SendAck>
bool Service(Request& request, Hardware& hw, SendAck send_ack) {
    const uint8_t sources = request.Take();
    if(sources == Request::kNone)
        return false;
    if(sources & Request::kSysEx) {
        uint8_t ack[sizeof(kAck)];
        for(std::size_t i = 0; i < sizeof(kAck); ++i)
            ack[i] = kAck[i];
        send_ack(ack, sizeof(ack));
    }
    Enter(hw);
    return true;
}

// Apps without USB-MIDI (keys only).
template <class Hardware>
bool Service(Request& request, Hardware& hw) {
    return Service(request, hw, [](uint8_t*, std::size_t) {});
}

} // namespace bootloader
