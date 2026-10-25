// Host tests for the shared bootloader entry: key gesture, SysEx detection and
// the main-loop service, against a fake daisy::System.
#include "bootloader_daisy.h"
#include <cassert>
#include <cstdio>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;

bool Feed(bootloader::SysExMatcher& matcher, const Bytes& bytes) {
    bool matched = false;
    for(uint8_t b : bytes)
        matched = matcher.Byte(b) || matched;
    return matched;
}

void TestFormat() {
    // The documented frames, byte for byte (README.md, PROTOCOL.md of the editors).
    assert((Bytes(std::begin(bootloader::kRequest), std::end(bootloader::kRequest)) ==
            Bytes{0xF0, 0x7D, 0x42, 0x4C, 0x01, 0x01, 0x42, 0x4F, 0x4F, 0x54, 0xF7}));
    assert((Bytes(std::begin(bootloader::kAck), std::end(bootloader::kAck)) ==
            Bytes{0xF0, 0x7D, 0x42, 0x4C, 0x01, 0x41, 0xF7}));
    // The request body is the frame without F0/F7, as libDaisy stores it.
    assert(sizeof(bootloader::kRequestBody) + 2 == sizeof(bootloader::kRequest));
    for(std::size_t i = 0; i < sizeof(bootloader::kRequestBody); ++i)
        assert(bootloader::kRequestBody[i] == bootloader::kRequest[i + 1]);
    for(uint8_t b : bootloader::kRequestBody)
        assert(b < 0x80);
    for(std::size_t i = 1; i + 1 < sizeof(bootloader::kAck); ++i)
        assert(bootloader::kAck[i] < 0x80);
}

void TestRequestBody() {
    const uint8_t* body = bootloader::kRequestBody;
    const std::size_t size = sizeof(bootloader::kRequestBody);
    assert(bootloader::IsRequestBody(body, size));
    assert(!bootloader::IsRequestBody(body, size - 1)); // truncated
    uint8_t longer[sizeof(bootloader::kRequestBody) + 1] = {};
    for(std::size_t i = 0; i < size; ++i)
        longer[i] = body[i];
    assert(!bootloader::IsRequestBody(longer, sizeof(longer)));
    // Every single changed byte is rejected, including the reply bit and the version.
    for(std::size_t i = 0; i < size; ++i) {
        uint8_t copy[sizeof(bootloader::kRequestBody)];
        for(std::size_t j = 0; j < size; ++j)
            copy[j] = body[j];
        copy[i] ^= 0x01;
        assert(!bootloader::IsRequestBody(copy, size));
        copy[i] = uint8_t(body[i] ^ 0x40);
        assert(!bootloader::IsRequestBody(copy, size));
    }
    // A Mod on Daisy IDENTIFY frame body (7D 4D 44 ...) is not a request.
    const uint8_t identify[] = {0x7D, 0x4D, 0x44, 0x01, 0x00, 0x01, 0x00, 0x00, 0x7F};
    assert(!bootloader::IsRequestBody(identify, sizeof(identify)));
    // The firmware's own reply, echoed back by a DAW, is not a request either.
    assert(!bootloader::IsRequestBody(bootloader::kAck + 1, sizeof(bootloader::kAck) - 2));
}

void TestMatcher() {
    const Bytes request(std::begin(bootloader::kRequest), std::end(bootloader::kRequest));
    bootloader::SysExMatcher m;
    assert(Feed(m, request));
    assert(!Feed(m, {0x90, 60, 100, 0x80, 60, 0})); // notes afterwards: nothing
    assert(Feed(m, request));                        // repeatable

    // Real-time bytes inside the SysEx are ignored (clock from a DAW).
    Bytes clocked;
    for(uint8_t b : request) {
        clocked.push_back(b);
        clocked.push_back(0xF8);
    }
    assert(Feed(m, clocked));

    // Notes before, running status around it.
    Bytes surrounded{0x90, 60, 100, 62, 100};
    surrounded.insert(surrounded.end(), request.begin(), request.end());
    surrounded.insert(surrounded.end(), {0x80, 60, 0});
    assert(Feed(m, surrounded));

    // Truncated, longer, corrupted, interrupted by a status byte: never.
    Bytes truncated(request.begin(), request.end() - 2);
    truncated.push_back(0xF7);
    assert(!Feed(m, truncated));
    Bytes longer(request.begin(), request.end() - 1);
    longer.insert(longer.end(), {0x00, 0xF7});
    assert(!Feed(m, longer));
    Bytes corrupt = request;
    corrupt[6] = 'b';
    assert(!Feed(m, corrupt));
    Bytes interrupted(request.begin(), request.begin() + 5);
    interrupted.insert(interrupted.end(), {0x90, 0x7D, 0x42, 0x4C, 0x01, 0x01, 'B', 'O', 'O', 'T', 0xF7});
    assert(!Feed(m, interrupted));
    // F7 without F0, the reply, and a Mod on Daisy frame.
    assert(!Feed(m, Bytes(request.begin() + 1, request.end())));
    assert(!Feed(m, Bytes(std::begin(bootloader::kAck), std::end(bootloader::kAck))));
    assert(!Feed(m, {0xF0, 0x7D, 0x4D, 0x44, 0x01, 0x00, 0x01, 0x00, 0x00, 0x7F, 0xF7}));
    // A new F0 restarts the match: an aborted frame followed by a complete one.
    Bytes restart(request.begin(), request.begin() + 4);
    restart.insert(restart.end(), request.begin(), request.end());
    assert(Feed(m, restart));
}

void TestKeys() {
    constexpr uint32_t s9 = 1u << 8, s16 = 1u << 15, s1 = 1u, s13 = 1u << 12;
    constexpr uint32_t pair = s9 | s16;
    assert(bootloader::kKeyMask == pair && bootloader::kHoldMs == 5000);
    bootloader::KeyCombo combo;
    uint32_t t = 1000;
    // Held alone: nothing before 5 s, exactly once at 5 s.
    assert(!combo.Process(pair, t));
    assert(combo.Holding());
    for(uint32_t ms = 1; ms < bootloader::kHoldMs; ++ms)
        assert(!combo.Process(pair, t + ms));
    assert(combo.Process(pair, t + bootloader::kHoldMs));
    assert(!combo.Holding());
    for(uint32_t ms = 1; ms < 10000; ms += 7)
        assert(!combo.Process(pair, t + bootloader::kHoldMs + ms)); // still held: once
    // Released and pressed again: a new full hold is needed.
    t = 20000;
    assert(!combo.Process(0, t));
    assert(!combo.Process(pair, t + 1));
    assert(!combo.Process(pair, t + 1 + bootloader::kHoldMs - 1));
    assert(combo.Process(pair, t + 1 + bootloader::kHoldMs));

    // One key alone, or the pair plus any other key, never fires.
    for(uint32_t other : {s9, s16, pair | s1, pair | s13, 0xFFFFu}) {
        bootloader::KeyCombo c;
        for(uint32_t ms = 0; ms <= 2 * bootloader::kHoldMs; ++ms)
            assert(!c.Process(other, 50000 + ms));
    }
    // A third key during the hold restarts the timer; so does a short release.
    bootloader::KeyCombo c;
    t = 70000;
    assert(!c.Process(pair, t));
    assert(!c.Process(pair | s1, t + 4000));
    assert(!c.Process(pair, t + 4001));
    assert(!c.Process(pair, t + 4001 + bootloader::kHoldMs - 1));
    assert(c.Process(pair, t + 4001 + bootloader::kHoldMs));
    bootloader::KeyCombo d;
    assert(!d.Process(pair, t));
    assert(!d.Process(s16, t + 4500)); // S9 bounced off for one tick
    assert(!d.Process(pair, t + 4501));
    assert(!d.Process(pair, t + 5500));
    assert(d.Process(pair, t + 4501 + bootloader::kHoldMs));

    // The millisecond counter may wrap during the hold.
    bootloader::KeyCombo w;
    const uint32_t start = 0xFFFFFFFFu - 1000;
    assert(!w.Process(pair, start));
    assert(!w.Process(pair, start + 4999)); // wrapped
    assert(w.Process(pair, start + 5000));
}

void TestService() {
    using daisy::System;
    daisy::desktop_devkit::Hardware hw;
    bootloader::Request request;
    std::vector<Bytes> sent;
    auto send = [&](uint8_t* bytes, std::size_t size) { sent.emplace_back(bytes, bytes + size); };

    assert(!request.Pending());
    assert(!bootloader::Service(request, hw, send));
    assert(System::bootloader_resets == 0 && sent.empty() && hw.led_updates == 0);

    // Keys: no reply, all LEDs on, reset into the bootloader without timeout.
    request.Set(bootloader::Request::kKeys);
    assert(request.Pending());
    const uint32_t before = System::now;
    assert(bootloader::Service(request, hw, send));
    assert(sent.empty());
    assert(System::bootloader_resets == 1);
    assert(System::bootloader_mode == System::DAISY_INFINITE_TIMEOUT);
    for(float led : hw.led)
        assert(led == 1.0f);
    assert(hw.led_updates == 2);
    assert(System::now - before >= bootloader::kSettleMs);
    assert(!request.Pending());

    // SysEx: the reply goes out before the reset.
    request.Set(bootloader::Request::kSysEx);
    assert(bootloader::Service(request, hw, [&](uint8_t* bytes, std::size_t size) {
        assert(System::bootloader_resets == 1);
        send(bytes, size);
    }));
    assert((sent == std::vector<Bytes>{Bytes(std::begin(bootloader::kAck), std::end(bootloader::kAck))}));
    assert(System::bootloader_resets == 2);

    // Both at once: one reply, one reset. Keys-only overload never replies.
    request.Set(bootloader::Request::kKeys);
    request.Set(bootloader::Request::kSysEx);
    assert(bootloader::Service(request, hw, send));
    assert(sent.size() == 2 && System::bootloader_resets == 3);
    request.Set(bootloader::Request::kSysEx);
    assert(bootloader::Service(request, hw));
    assert(sent.size() == 2 && System::bootloader_resets == 4);
    assert(!bootloader::Service(request, hw));
}
}

int main() {
    TestFormat();
    TestRequestBody();
    TestMatcher();
    TestKeys();
    TestService();
    std::puts("PASS: bootloader entry format, SysEx body/stream matching, S9+S16 5 s gesture, service/reply/reset");
}
