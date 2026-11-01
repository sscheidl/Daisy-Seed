#pragma once
// Main-loop half of the return to the program manager: darkens the panel LEDs and
// resets with DAISY_SKIP_TIMEOUT, so the bootloader starts the QSPI application (the
// manager) after its 100 ms window. Uses daisy::System from libDaisy; host tests
// provide a fake with ResetToBootloader().
//
// The application finishes its own work BEFORE calling Enter(): stop or fade audio,
// send pending MIDI note-offs, bring CV outputs to their rest code, complete running
// card writes. Enter() does none of that; it must be called from the main loop only.
#include "return_entry.h"
#include "desktop_devkit.h"

namespace manager_return {

// Time for the last LED transfer and the SysEx reply to leave before the reset.
constexpr uint32_t kSettleMs = 20;

template <class Hardware>
void Enter(Hardware& hw) {
    for(int i = 0; i < 16; ++i)
        hw.SetLed(i, 0.0f);
    hw.UpdateLeds();
    hw.UpdateLeds(); // waits until the previous transfer has finished
    daisy::System::Delay(kSettleMs);
    daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_SKIP_TIMEOUT);
}

// Acts on a pending request: sends the SysEx reply if a host asked, then Enter().
// Returns true if a request was handled (on hardware the call does not return then).
// Call it only when the application is ready to stop (see above).
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

template <class Hardware>
bool Service(Request& request, Hardware& hw) {
    return Service(request, hw, [](uint8_t*, std::size_t) {});
}

} // namespace manager_return
