#define main firmware_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cstdio>
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
int main() {
    using namespace torus::profile;
    Init(480000000); Reset();
    ticks = 0xfffffff0u;
    { Scope timing(Voices); ticks = 0x20; }
    auto s = Get(Voices);
    assert(s.calls == 1 && s.total == 48 && s.maximum == 48);
    counters[Voices].total = UINT64_MAX - 1;
    counters[Voices].calls = UINT32_MAX;
    { Scope timing(Voices); ticks += 10; }
    assert(Get(Voices).total == UINT64_MAX && Get(Voices).calls == UINT32_MAX);
    auto packet = Encode(Get(Voices));
    assert(packet[0] == 0xbf && packet[1] == 70 && packet[2] == 1);
    uint64_t total = 0;
    for(unsigned i = 0; i < 10; ++i) total |= uint64_t(packet[3*(7+i)+2]) << (7*i);
    assert(total == UINT64_MAX && packet.back() == 64);
    assert(Request(15,119,16) && Request(15,119,22));
    assert(!Request(0,119,16) && !Request(15,119,23));
    Reset();
    hw.Init(); InitializeSynth(); cpu_meter.Init(48000,48);
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    float l[48], r[48]; float* out[] = {l,r};
    auto request = [&](unsigned value) {
        midi.incoming.push_back({daisy::ControlChange,15,{119,uint8_t(value)}});
        ServiceMidi();
        AudioCallback(nullptr,out,48);
    };
    render_hash = 0xfedcba9876543210ull;
    request(20);
    assert(midi.sent.empty()); // IRQ only publishes a coherent snapshot.
    ServiceCpuTelemetry();
    assert(midi.sent.size() == 1 && midi.sent.back().size() == 87 && !profile_pending);
    request(22); ServiceCpuTelemetry();
    assert(blocks == 1 && Get(Voices).calls == 4);
    hUsbDeviceHS.dev_state = 0;
    request(16); ServiceCpuTelemetry();
    assert(midi.sent.size() == 2 && !profile_pending);
    puts("PASS profiling wrap/saturation, framing, request handling, IRQ isolation and offline guard");
}
