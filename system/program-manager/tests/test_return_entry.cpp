// Return-to-manager triggers: key gesture timing, SysEx matching, distinctness from
// the bootloader (service) entry, request flags.
#include "check.h"
#include "return_entry.h"
#include <vector>

using namespace manager_return;

int main() {
    {   // Key gesture: exactly S1+S16 for 3 s fires once; release re-arms.
        KeyCombo combo;
        uint32_t t = 100;
        for(; t < 100 + kHoldMs; t += 10)
            CHECK(!combo.Process(kKeyMask, t));
        CHECK(combo.Holding());
        CHECK(combo.Process(kKeyMask, t));         // fires at 3000 ms
        CHECK(!combo.Process(kKeyMask, t + 1000)); // not again while held
        CHECK(!combo.Holding());
        CHECK(!combo.Process(0, t + 1001));
        CHECK(!combo.Process(kKeyMask, t + 1002)); // new hold starts
        CHECK(combo.Holding());
        CHECK(combo.Process(kKeyMask, t + 1002 + kHoldMs));
    }
    {   // A third key, a missing key or the service pair resets the timer.
        KeyCombo combo;
        CHECK(!combo.Process(kKeyMask, 0));
        CHECK(!combo.Process(kKeyMask | (1u << 5), 2900));
        CHECK(!combo.Process(kKeyMask, 2901));   // timer restarted
        CHECK(!combo.Process(kKeyMask, 5000));
        CHECK(combo.Process(kKeyMask, 5901));
        KeyCombo other;
        CHECK(!other.Process(1u << 0, 0));
        CHECK(!other.Process(1u << 0, 10000));   // one key never fires
        CHECK(!other.Process(bootloader::kKeyMask, 0));
        CHECK(!other.Process(bootloader::kKeyMask, 10000));  // S9+S16 is the service gesture
    }
    {   // Timer wrap-around of the millisecond clock.
        KeyCombo combo;
        CHECK(!combo.Process(kKeyMask, 0xFFFFFF00u));
        CHECK(!combo.Process(kKeyMask, 0xFFFFFFFFu));
        CHECK(combo.Process(kKeyMask, 0xFFFFFF00u + kHoldMs));  // wrapped
    }
    {   // SysEx body: exact match only, distinct from the bootloader request.
        CHECK(IsRequestBody(kRequestBody, sizeof(kRequestBody)));
        CHECK(!IsRequestBody(bootloader::kRequestBody, sizeof(bootloader::kRequestBody)));
        CHECK(!bootloader::IsRequestBody(kRequestBody, sizeof(kRequestBody)));
        uint8_t shorter[8];
        std::memcpy(shorter, kRequestBody, 8);
        CHECK(!IsRequestBody(shorter, 8));
        uint8_t longer[10];
        std::memcpy(longer, kRequestBody, 9);
        longer[9] = 0;
        CHECK(!IsRequestBody(longer, 10));
        uint8_t other[9];
        std::memcpy(other, kRequestBody, 9);
        other[4] = 0x02;  // another command
        CHECK(!IsRequestBody(other, 9));
        CHECK_EQ(kRequest[1], 0x7D);
        CHECK_EQ(kRequest[2], 'P');
        CHECK_EQ(kRequest[3], 'M');
        CHECK_EQ(kAck[5], kCommandOpen | kReplyBit);
        CHECK_EQ(sizeof(kRequest), 11u);
        CHECK_EQ(sizeof(kAck), 7u);
    }
    {   // Raw stream matcher: complete request, real-time bytes inside, interruptions.
        SysExMatcher m;
        bool fired = false;
        for(uint8_t b : kRequest)
            fired = m.Byte(b) || fired;
        CHECK(fired);
        fired = false;
        std::vector<uint8_t> with_clock(kRequest, kRequest + sizeof(kRequest));
        with_clock.insert(with_clock.begin() + 4, 0xF8);  // MIDI clock inside
        with_clock.insert(with_clock.begin() + 9, 0xFE);  // active sensing
        for(uint8_t b : with_clock)
            fired = m.Byte(b) || fired;
        CHECK(fired);
        fired = false;
        for(uint8_t b : bootloader::kRequest)  // the BL request does not fire PM
            fired = m.Byte(b) || fired;
        CHECK(!fired);
        for(uint8_t b : {0xF0, 0x7D, 0x50, 0x4D, 0x01, 0x90, 0x40, 0x7F})  // note-on interrupts
            fired = m.Byte(static_cast<uint8_t>(b)) || fired;
        CHECK(!fired);
        for(uint8_t b : kRequest)  // a clean request afterwards still works
            fired = m.Byte(b) || fired;
        CHECK(fired);
        // Truncated then restarted.
        SysExMatcher n;
        for(size_t i = 0; i < 6; ++i)
            CHECK(!n.Byte(kRequest[i]));
        CHECK(!n.Byte(0xF7));  // early end: wrong length
        fired = false;
        for(uint8_t b : kRequest)
            fired = n.Byte(b) || fired;
        CHECK(fired);
    }
    {   // Request flags are the shared lock-free type.
        Request r;
        CHECK(!r.Pending());
        r.Set(Request::kKeys);
        r.Set(Request::kSysEx);
        CHECK(r.Pending());
        CHECK_EQ(r.Take(), Request::kKeys | Request::kSysEx);
        CHECK_EQ(r.Take(), Request::kNone);
    }
    return check::Finish("test_return_entry");
}
