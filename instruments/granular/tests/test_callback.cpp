#define main firmware_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cstdio>
extern "C" {
USBD_HandleTypeDef hUsbDeviceHS;
uint8_t __neb_file_start[1],__neb_file_end[1];
}
static std::array<float,neb::kBlock> input_l{},input_r{},output_l{},output_r{};
static void Block() {
    const float* input[]={input_l.data(),input_r.data()};float* output[]={output_l.data(),output_r.data()};
    const unsigned previous=fake_sd::calls;
    Audio(input,output,neb::kBlock);
    assert(previous==fake_sd::calls); // Production callback never performs a file read.
    for(float x:output_l) assert(std::isfinite(x) && std::abs(x)<=.95f);
}
static void Advance(unsigned n) {
    for(unsigned i=0;i<n;++i) {++daisy::System::now;PollControls(nullptr);if(i%10==9) Block();ServiceUsb();}
    Block();
}
static void Send(std::initializer_list<uint8_t> data) {std::vector<uint8_t> b(data);ReceiveUsb(b.data(),b.size(),nullptr);ServiceUsb();Block();}
int main() {
    std::memset(engine_storage,0xa5,sizeof engine_storage);
    engine=new(engine_storage) neb::Engine;
    engine->Init(live_banks[0],live_banks[1],8192,reinterpret_cast<int16_t*>(live_banks[1])+32768,bridge);
    hw.Init();hw.pot[neb::Speed].value=.5f;hw.pot[neb::Pitch].value=.5f;hw.pot[neb::Size].value=1;
    usb_ready=true;usb.StartRx(ReceiveUsb,nullptr);hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;
    Advance(30);assert(engine->ui.mode==1);
    hw.pressed[15]=true;Advance(15);assert(request.exchange(0)==16);hw.pressed[15]=false;Advance(15);
    hw.tog_sw18.value=2;Advance(30);assert(engine->ui.mode==2);
    for(unsigned i=0;i<16;++i) {
        hw.pressed[i]=true;Advance(15);assert(engine->notes.Current()==neb::kNotes[i]);assert(key_state.load()==1u<<i);
        hw.pressed[i]=false;Advance(15);assert(engine->notes.Current()==-1 && key_state.load()==0);
    }
    assert(usb.sent.size()>=32);
    hw.pressed[0]=true;Advance(15);hw.tog_sw18.value=1;Advance(30);assert(engine->ui.mode==0 && engine->notes.Current()==-1);
    hw.pressed[0]=false;Advance(15);
    Send({0x90,60,100,0xf8,64,100});assert(engine->notes.Current()==64);
    Send({0x80,60,0});assert(engine->notes.Current()==64);Send({0x90,64,0});assert(engine->notes.Current()==-1);
    Send({0xb0,20,90,0xf8,21,80});assert(engine->ui.params.primary[neb::Start]>.7f);
    Send({0x90,60,100});Send({0xb0,123,0});assert(engine->notes.Current()==-1);
    Send({0xf0,1,2,3,0xf7,0x90,67,100,0xff});assert(engine->notes.Current()==-1);
    Send({0x90,67,100});hUsbDeviceHS.dev_state=0;ServiceUsb();Block();assert(engine->notes.Current()==-1);
    hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;
    ServiceUsb();Block(); // finish disconnect recovery
    std::vector<uint8_t> flood;
    for(unsigned i=0;i<180;++i) {flood.push_back(0x90);flood.push_back(60);flood.push_back(100);}
    flood.insert(flood.end(),{0x80,60,0});
    ReceiveUsb(flood.data(),flood.size(),nullptr);
    for(unsigned i=0;i<10;++i) {ServiceUsb();Block();}
    assert(engine->notes.Current()==-1); // stale Note Ons cannot resurrect after panic
    // Exercise the exact FatFS adapter and production Loader with a real PCM header.
    fake_sd::slots[0]={ 'R','I','F','F',40,0,0,0,'W','A','V','E',
        'f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,
        'd','a','t','a',4,0,0,0,0,0,0xff,0x7f };
    loader.Init(file,live_banks[1]+32768,4096,bridge);
    assert(loader.Request(1));assert(fake_sd::last_path=="0:/NEBULAE/SAMPLES/01.wav");
    for(unsigned i=0;i<32 && loader.state!=neb::Loader::State::Idle;++i) {loader.Service();Block();}
    assert(loader.active==1 && bridge.published.channels==1 && bridge.published.frames==2);
    assert(live_banks[1][32769]==32767);
    card.store(0);assert(!loader.Request(2));assert(loader.error==neb::FileError::NoCard && loader.active==1);
    // Bootloader entry: S13+S14 alone for 3 s (no reply), or the raw SysEx request (answered).
    using daisy::System;
    hw.tog_sw18.value=0;Advance(30);usb.sent.clear();
    hw.pressed[12]=hw.pressed[13]=hw.pressed[0]=true;Advance(4000);ServiceBootloader();
    assert(System::bootloader_resets==0);
    hw.pressed[0]=false;Advance(2990);ServiceBootloader();assert(System::bootloader_resets==0);
    Advance(20);ServiceBootloader();
    assert(System::bootloader_resets==1 && System::bootloader_mode==System::DAISY_INFINITE_TIMEOUT);
    hw.pressed[12]=hw.pressed[13]=false;Advance(15);usb.sent.clear();
    const std::vector<uint8_t> ack(std::begin(bootloader::kAck),std::end(bootloader::kAck));
    Send({0xf0,0x7d,0x42,0x4c,0x01,0x01,0xf8,'B','O','O','T',0xf7,0x90,60,100});ServiceBootloader(); // clock inside
    assert(System::bootloader_resets==2 && usb.sent.size()==1 && usb.sent[0]==ack);
    Send({0xf0,0x7d,0x42,0x4c,0x01,0x41,0xf7});Send({0xf0,0x7d,0x42,0x4c,0x01,0x01,'B','O','O',0xf7});
    ServiceBootloader();assert(System::bootloader_resets==2); // echoed reply, truncated request
    std::puts("PASS production callback/adapter: real DigitalControl debouncing, 16 keys, mode release, raw MIDI/running status/RT/reset/disconnect, FAT path and PCM load, poisoned SDRAM construction, bootloader keys/SysEx");
}
