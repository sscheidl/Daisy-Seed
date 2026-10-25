#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "engine.h"
#include "fatfs.h"
#include "usbd_core.h"
#include "version.h"
#include <new>
#include <cstdio>

extern "C" USBD_HandleTypeDef hUsbDeviceHS;
extern "C" uint8_t __neb_file_start[],__neb_file_end[];
namespace {
const char firmware_id[]="NEBULAE-FW-VERSION=" NEBULAE_VERSION;
daisy::desktop_devkit::Hardware hw;
daisy::MidiUsbTransport usb;
neb::MidiParser usb_parser;
daisy::SdmmcHandler sd;
daisy::FatFSInterface fs;
alignas(32) uint8_t DSY_SDRAM_BSS engine_storage[sizeof(neb::Engine)];
alignas(32) int16_t DSY_SDRAM_BSS live_banks[2][neb::kLiveFrames*2];
neb::Engine* engine=nullptr;
neb::FileBridge bridge;
neb::Loader loader;
neb::Queue<neb::ControlFrame,32> controls;
neb::Queue<neb::Midi,64> midi;
neb::Queue<neb::Midi,128> usb_events;
std::atomic<unsigned> leds{0}, key_state{0}, ui_mode{1}, card{0}, occupied{0}, request{0};
std::atomic<bool> midi_panic{false}, control_panic{false}, gate_pulse{false};
std::atomic<bool> usb_overflow{false};
bool usb_ready=false, mounted=false;
bool sent_notes[128]{};
// Inspect through a debugger; values are NOT measurements until run on hardware.
volatile uint32_t callback_peak_cycles=0, callback_overruns=0;
// Software entry into the Daisy bootloader: S13+S14 for 3 s, or SysEx over J7.
bootloader::KeyCombo boot_keys;     // control timer only
bootloader::SysExMatcher boot_sysex; // USB interrupt only
bootloader::Request boot_request;
void ServiceUsb();
void ReceiveUsb(uint8_t* bytes,size_t size,void*) {
    neb::Midi event;
    for(size_t i=0;i<size;++i) {
        if(boot_sysex.Byte(bytes[i])) boot_request.Set(bootloader::Request::kSysEx);
        if(usb_parser.Byte(bytes[i],event) && !usb_events.Push(event))
            usb_overflow.store(true,std::memory_order_release);
    }
}
class SdFile : public neb::File {
    FIL handle_{}; bool open_=false;
  public:
    neb::FileError Open(unsigned slot) override {
        Close();
        if(!card.load(std::memory_order_relaxed)) {mounted=false;return neb::FileError::NoCard;}
        if(!mounted) {
            if(f_mount(&fs.GetSDFileSystem(),fs.GetSDPath(),1)!=FR_OK) return neb::FileError::Io;
            mounted=true;
        }
        char path[40];std::snprintf(path,sizeof(path),"0:/NEBULAE/SAMPLES/%02u.wav",slot);
        const FRESULT r=f_open(&handle_,path,FA_READ);
        if(r==FR_NO_FILE || r==FR_NO_PATH) return neb::FileError::Missing;
        if(r!=FR_OK) {mounted=false;return neb::FileError::Io;}
        open_=true;return neb::FileError::None;
    }
    uint32_t Size() const override {return f_size(&handle_);}
    bool Read(uint32_t offset,void* dst,uint32_t bytes) override {
        if(!open_ || !card.load(std::memory_order_relaxed) || f_lseek(&handle_,offset)!=FR_OK) return false;
        UINT got=0;
        if(f_read(&handle_,dst,bytes,&got)!=FR_OK || got!=bytes) {mounted=false;return false;}
        return true;
    }
    void Close() override {if(open_) f_close(&handle_);open_=false;}
} file;

void PollControls(void*) {
    hw.UpdateAllControls();
    static uint16_t held=0;
    for(unsigned i=0;i<16;++i) {
        if(hw.button[i].RisingEdge()) held |= uint16_t(1u<<i);
        if(hw.button[i].FallingEdge()) held &= uint16_t(~(1u<<i));
    }
    neb::ControlFrame f;f.keys=held;f.ms=daisy::System::GetNow();f.toggle=hw.tog_sw18.Read();
    if(boot_keys.Process(held,f.ms)) boot_request.Set(bootloader::Request::kKeys);
    for(unsigned i=0;i<8;++i) f.pot[i]=hw.pot[i].Value();
    if(!controls.Push(f)) control_panic.store(true,std::memory_order_release);
    card.store(hw.sd_detect.Pressed(),std::memory_order_relaxed);
    if(hw.gate_in[0].Trig()) gate_pulse.store(true,std::memory_order_relaxed);
}
void Audio(daisy::AudioHandle::InputBuffer in,daisy::AudioHandle::OutputBuffer out,size_t n) {
    const uint32_t start=DWT->CYCCNT;
    neb::ControlFrame frame;
    if(control_panic.exchange(false,std::memory_order_acquire)) engine->notes.AllOff();
    for(unsigned i=0;i<32 && controls.Pop(frame);++i) engine->ui.Process(frame,*engine);
    neb::Midi event;
    if(midi_panic.exchange(false,std::memory_order_acquire)) {
        for(unsigned i=0;i<64 && midi.Pop(event);++i) {}
        engine->Disconnect();
    }
    for(unsigned i=0;i<32 && midi.Pop(event);++i) engine->MidiEvent(event);
    if(gate_pulse.exchange(false,std::memory_order_relaxed) && engine->trigger_mode) engine->TriggerGrain();
    engine->occupied=uint16_t(occupied.load(std::memory_order_relaxed));
    engine->Process(in[0],in[1],out[0],out[1],unsigned(n));
    if(engine->requested_slot) {request.store(engine->requested_slot,std::memory_order_release);engine->requested_slot=0;}
    ui_mode.store(engine->ui.mode,std::memory_order_relaxed);
    key_state.store(engine->ui.mode==2?engine->ui.keyboard:0,std::memory_order_relaxed);
    leds.store(engine->Status(),std::memory_order_relaxed);
    const uint32_t cycles=DWT->CYCCNT-start;
    if(cycles>callback_peak_cycles) callback_peak_cycles=cycles;
    if(cycles>uint32_t((uint64_t(SystemCoreClock)*n)/neb::kRate)) ++callback_overruns;
}
void ServiceUsb() {
    if(!usb_ready) return;
    static bool recovering=false;
    auto panic=[&] {recovering=true;midi_panic.store(true,std::memory_order_release);};
    if(!usb.RxActive()) {usb.FlushRx();usb.StartRx(ReceiveUsb,nullptr);panic();}
    if(usb_overflow.exchange(false,std::memory_order_acquire)) panic();
    const bool connected=hUsbDeviceHS.dev_state==USBD_STATE_CONFIGURED;
    static bool was_connected=false;
    if(was_connected && !connected) {panic();for(auto& v:sent_notes) v=false;}
    was_connected=connected;
    neb::Midi v;
    if(recovering) {
        // Discard both queues across the audio panic acknowledgement. A stale
        // Note On must not survive the panic after its Note Off was dropped.
        for(unsigned i=0;i<128 && usb_events.Pop(v);++i) {}
        if(!midi_panic.load(std::memory_order_acquire)) recovering=false;
        return;
    }
    for(unsigned i=0;i<32 && usb_events.Pop(v);++i) {
        if(connected && !midi.Push(v)) {panic();return;}
    }
    static uint32_t last=0;
    const uint32_t now=daisy::System::GetNow();
    if(!connected || now==last) return;
    last=now;
    bool wanted[128]{};
    const unsigned keys=key_state.load(std::memory_order_relaxed);
    for(unsigned i=0;i<16;++i) if(keys&(1u<<i)) wanted[neb::kNotes[i]]=true;
    uint8_t bytes[24];size_t count=0;
    for(unsigned note=0;note<128 && count<sizeof(bytes);++note) {
        if(wanted[note]==sent_notes[note]) continue;
        bytes[count++]=wanted[note]?0x90:0x80;bytes[count++]=note;bytes[count++]=wanted[note]?100:0;
        sent_notes[note]=wanted[note];
    }
    if(count) usb.Tx(bytes,count);
}
// Main loop only: replies over USB if a host asked, then resets into the bootloader.
// SD access is read-only, so a load in progress may be cut off.
void ServiceBootloader() {
    bootloader::Service(boot_request,hw,[](uint8_t* bytes,size_t size) {
        if(hUsbDeviceHS.dev_state==USBD_STATE_CONFIGURED) usb.Tx(bytes,size);
    });
}
}
extern "C" void neb_sd_service(void) {ServiceUsb();}
extern "C" int neb_card_present(void) {return card.load(std::memory_order_relaxed)!=0;}
int main() {
    asm volatile("" : : "r"(firmware_id));
    hw.Init();
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(neb::kBlock);
    if(hw.seed.AudioBlockSize()!=neb::kBlock) while(true) {} // reject silent driver clamping
    for(unsigned i=0;i<100;++i) {hw.UpdateAllControls();daisy::System::Delay(1);}
    card.store(hw.sd_detect.Pressed(),std::memory_order_relaxed);
    const uintptr_t begin=reinterpret_cast<uintptr_t>(__neb_file_start),end=reinterpret_cast<uintptr_t>(__neb_file_end);
    if(begin<0xc0000000u || end>0xc4000000u-neb::kReserveBytes || begin>=end) while(true) {}
    engine=new(engine_storage) neb::Engine;
    engine->Init(live_banks[0],live_banks[1],neb::kLiveFrames,reinterpret_cast<int16_t*>(begin),bridge);
    loader.Init(file,reinterpret_cast<int16_t*>(begin),end-begin,bridge);
    daisy::SdmmcHandler::Config sd_config;sd_config.Defaults();sd_config.speed=daisy::SdmmcHandler::Speed::STANDARD;
    sd.Init(sd_config);fs.Init(daisy::FatFSInterface::Config::MEDIA_SD);
    daisy::MidiUsbTransport::Config cfg;cfg.periph=daisy::MidiUsbTransport::Config::EXTERNAL;
    usb.Init(cfg);usb.StartRx(ReceiveUsb,nullptr);usb_ready=true;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    hw.StartLowPriorityCallback(PollControls,1000);
    hw.seed.StartAudio(Audio);
    uint32_t last_led=0,last_scan=0;unsigned last_mask=0,pending=0,scan=1;
    bool initial=true,previous_card=card.load()!=0;
    while(true) {
        ServiceUsb();
        ServiceBootloader();
        const unsigned selected=request.exchange(0,std::memory_order_acquire);
        if(selected) pending=selected;
        const uint32_t now=daisy::System::GetNow();
        const bool present=card.load()!=0;
        if(present!=previous_card) {mounted=false;scan=1;occupied.store(0);initial=present;previous_card=present;}
        if(loader.state==neb::Loader::State::Idle && pending) {loader.Request(pending);pending=0;}
        loader.Service();
        if(present && loader.state==neb::Loader::State::Idle && now-last_scan>=20) {
            last_scan=now;
            const auto e=file.Open(scan);
            if(e==neb::FileError::None) {file.Close();occupied.fetch_or(1u<<(scan-1));if(initial) {pending=scan;initial=false;}}
            else occupied.fetch_and(~(1u<<(scan-1)));
            scan=scan%16+1;
        }
        if(now-last_led>=10) {
            last_led=now;
            const unsigned mask=neb::SlotLeds(ui_mode.load(),uint16_t(key_state.load()),loader.active,loader.target,
                loader.state!=neb::Loader::State::Idle && loader.state!=neb::Loader::State::Publish,
                loader.error,now,uint16_t(leds.load()));
            if(mask!=last_mask) {
                for(unsigned i=0;i<16;++i) hw.SetLed(i,mask&(1u<<i)?1.f:0.f);
                hw.UpdateLeds();last_mask=mask;
            }
        }
        daisy::System::Delay(1);
    }
}
