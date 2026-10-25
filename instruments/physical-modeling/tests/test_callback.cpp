#define main firmware_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cstdio>
using namespace physical_modeling;
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
int main() {
    hw.Init();
    std::fill_n(synth_storage, sizeof(synth_storage), static_cast<unsigned char>(0xff));
    cpu_meter.Init(48000,48);
    InitializeSynth(); // Exercise actual startup from uninitialized NOLOAD memory.
    for(auto& pot:hw.pot) pot.value=.5f;
    float left[48],right[48]; float* output[]={left,right};
    auto tick=[&](unsigned count) {
        for(unsigned n=0;n<count;++n) {
            AudioCallback(nullptr,output,48); daisy::System::now++;
            for(float v:left) assert(std::isfinite(v) && std::fabs(v)<.8f);
        }
    };
    tick(25);
    for(auto& key:hw.pressed) key=true;
    tick(8);
    assert(led_mask.load()==65535 && synth->TriggerCount()==16);
    assert(hw.led_transfers==0); ServiceLeds(); assert(hw.led_transfers==1);
    daisy::System::now+=10; ServiceLeds(); assert(hw.led_transfers==1);
    for(auto& key:hw.pressed) key=false;
    tick(8); assert(led_mask.load()==0 && synth->TriggerCount()==16);
    ServiceLeds();
    for(unsigned bank=0;bank<2;++bank) for(int pos:{1,0,2}) {
        hw.tog_sw17.value=bank; hw.tog_sw18.value=pos;
        tick(40); assert(synth->ModelIndex()==SwitchModel(bank,pos));
    }
    midi.incoming.push_back({daisy::NoteOn,0,{69,127}});
    midi.incoming.push_back({daisy::PitchBend,0,{127,127}});
    midi.incoming.push_back({daisy::ControlChange,0,{21,127}});
    ServiceMidi(); tick(100);
    assert(synth->TriggerCount()==17);
    assert(synth->parameters.Target(Structure)==1);
    assert(std::fabs(synth->parameters.PitchOffset()-2)<1e-3);
    for(unsigned i=0;i<300;++i) midi.incoming.push_back({daisy::NoteOn,0,{uint8_t(i%128),100}});
    // Local + USB strikes, with a deliberately full producer queue.
    for(auto& key:hw.pressed) key=true;
    for(unsigned i=0;i<100;++i) { ServiceMidi(); tick(1); }
    assert(synth->TriggerCount()==333 && synth->OverflowCount()==0);
    assert(midi.sent.empty());
    const auto count=synth->TriggerCount();
    midi.incoming.push_back({daisy::NoteOn,0,{60,0}});
    ServiceMidi(); tick(2); assert(synth->TriggerCount()==count);
    // CC28 must persist with both physical switches untouched; a switch change
    // takes over only after its normal debounce window.
    midi.incoming.push_back({daisy::ControlChange,0,{28,0}});
    ServiceMidi(); tick(100); assert(synth->ModelIndex()==0);
    hw.tog_sw17.value=false; hw.tog_sw18.value=0;
    tick(40); assert(synth->ModelIndex()==1);
    // Real libDaisy CpuLoadMeter, deterministic fake timer: .25 ms then 1.5 ms.
    hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;
    auto request=[&](uint8_t value) { midi.incoming.push_back({daisy::ControlChange,15,{119,value}}); ServiceMidi(); };
    hw.control_ticks=250; request(2); tick(1);
    assert(midi.sent.empty()); // Audio IRQ never transmits.
    ServiceCpuTelemetry(); assert(midi.sent.size()==1);
    auto read=[&](unsigned cc,unsigned digits) {
        uint32_t value=0; const auto& packet=midi.sent.back(); assert(packet.size()==60);
        for(unsigned i=0;i<digits;++i) {
            const unsigned off=3*(cc-100+i);
            assert(packet[off]==0xbf && packet[off+1]==cc+i && packet[off+2]<128);
            value |= uint32_t(packet[off+2])<<(7*i);
        }
        return value;
    };
    assert(read(100,1)==1 && read(101,3)==2500 && read(104,3)==2500);
    assert(read(107,5)==1 && read(112,5)==0 && read(117,1)==1 && read(118,1)==4 && read(119,1)==64);
    hw.control_ticks=1500; request(1); tick(1); ServiceCpuTelemetry();
    assert(read(104,3)==15000 && read(107,5)==2 && read(112,5)==1);
    assert(read(101,3)>2500 && read(101,3)<15000); // Actual smoothed meter average.
    hw.control_ticks=1000; request(2); tick(1); ServiceCpuTelemetry();
    assert(read(104,3)==10000 && read(107,5)==1 && read(112,5)==0);
    daisy::System::ticks=UINT32_MAX-100; hw.control_ticks=250;
    request(2); tick(1); ServiceCpuTelemetry(); assert(read(104,3)==2500); // Timer wrap.
    const auto replies=midi.sent.size();
    midi.incoming.push_back({daisy::ControlChange,0,{119,1}}); // Wrong channel.
    request(64); tick(1); ServiceCpuTelemetry(); assert(midi.sent.size()==replies);
    hUsbDeviceHS.dev_state=0; request(1); tick(1); ServiceCpuTelemetry();
    assert(midi.sent.size()==replies && !cpu_reply_pending); // Offline: no TX/blocking.
    // Bootloader entry: S13+S14 alone for 3 s (no reply), or the SysEx request (answered).
    using daisy::System;
    for(auto& key:hw.pressed) key=false;
    tick(8); ServiceBootloader(); assert(System::bootloader_resets==0);
    hw.pressed[12]=hw.pressed[13]=true;
    tick(2990); ServiceBootloader(); assert(System::bootloader_resets==0);
    tick(20); ServiceBootloader();
    assert(System::bootloader_resets==1 && System::bootloader_mode==System::DAISY_INFINITE_TIMEOUT);
    assert(midi.sent.size()==replies);
    hw.pressed[12]=hw.pressed[13]=false; tick(8);
    hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;
    daisy::MidiEvent boot; boot.type=daisy::SystemCommon; boot.sc_type=daisy::SystemExclusive;
    std::copy(std::begin(bootloader::kRequestBody),std::end(bootloader::kRequestBody),boot.sysex_data);
    boot.sysex_message_len=sizeof(bootloader::kRequestBody);
    midi.incoming.push_back(boot); ServiceMidi(); assert(System::bootloader_resets==1);
    ServiceBootloader();
    assert(System::bootloader_resets==2 && midi.sent.size()==replies+1);
    assert(midi.sent.back()==std::vector<uint8_t>(std::begin(bootloader::kAck),std::end(bootloader::kAck)));
    boot.sysex_message_len--; midi.incoming.push_back(boot); // truncated: ignored
    ServiceMidi(); ServiceBootloader(); assert(System::bootloader_resets==2);
    hUsbDeviceHS.dev_state=0;
    puts("PASS bootloader entry: S13+S14 3 s, SysEx request with reply, truncated SysEx ignored");
    puts("PASS CPU average/peak, reset, overruns, timer wrap, coherent MIDI telemetry outside audio and offline guard");
    puts("PASS production callback: all keys/LEDs, switches, USB CC/bend, 300-event backpressure, no MIDI echo");
}
