// Nebulae on Daisy: performance personality (audio, USB-MIDI, local read-only SD) and
// the dispatch into SD management (hw_glue.cpp) by the boot intent. Host-tested by
// tests/test_callback.cpp, which includes this file with fakes.
#include "desktop_devkit.h"
#if NEBULAE_MOD
// Before the SD headers: on a Windows host they bring in windows.h and its SendMessage macro.
#include "mod_on_daisy_desktop.h"
#include "mod_bridge.h"
#include "modulation.h"
#endif
#include "bootloader_daisy.h"
#include "engine.h"
#include "banks.h"
#include "sd_files.h"
#include "boot_intent.h"
#include "control_sysex.h"
#include "hw_glue.h"
#include "ro_sd.h"
#include "usbd_core.h"
#include "version.h"
#if NEBULAE_PROFILE
#include "profile.h"
#endif
#include <new>

// Vocoder scheduling of this product (engine.h Schedule): 0 common grid, 1 plain
// stagger, 2 compensated stagger. Listening candidates of 2026-11-09 build 1 and 2.
#ifndef NEBULAE_VOCODER_SCHEDULE
#define NEBULAE_VOCODER_SCHEDULE 0
#endif
// AXI SRAM section of the DSP state; the host fakes define it empty.
#ifndef NEB_DSP_SRAM
#define NEB_DSP_SRAM __attribute__((section(".neb_dsp")))
#endif
extern "C" USBD_HandleTypeDef hUsbDeviceHS;
extern "C" uint8_t __neb_file_start[],__neb_file_end[];
namespace {
#if NEBULAE_MOD
const char firmware_id[]="NEBULAE-MOD-VERSION=" NEBULAE_VERSION;
#else
const char firmware_id[]="NEBULAE-FW-VERSION=" NEBULAE_VERSION;
#endif
#if NEBULAE_PROFILE
const char profile_id[]="NEBULAE-PROFILE-BUILD=1";  // the diagnostic image announces itself
#endif
daisy::desktop_devkit::Hardware hw;
daisy::MidiUsbTransport usb;
neb::MidiParser usb_parser;
daisy::SdmmcHandler sd;
// The Engine lives in AXI SRAM (memory.ld .neb_dsp), not in SDRAM: with its state in
// SDRAM one vocoder hop took 14 ms per channel on the kit, 4.4 times as long
// (docs/OVERLOAD_DIAGNOSIS.md). The section is NOLOAD and not zeroed; the constructor
// and Init() write every member. The PCM of the Live banks and of the file stays in SDRAM.
alignas(32) uint8_t NEB_DSP_SRAM engine_storage[sizeof(neb::Engine)];
alignas(32) int16_t DSY_SDRAM_BSS live_banks[2][neb::kLiveFrames*2];
// SDMMC1 IDMA bounce buffer: isolated, cache-line aligned AXI SRAM sector (memory.ld).
alignas(32) uint8_t sector_buffer[512] __attribute__((section(".neb_sd")));
neb::Engine* engine=nullptr;
neb::FileBridge bridge;
neb::Loader loader;
neb::Queue<neb::ControlFrame,32> controls;
neb::Queue<neb::Midi,64> midi;
neb::Queue<neb::Midi,128> usb_events;
std::atomic<unsigned> leds{0}, key_state{0}, ui_mode{1}, card{0}, occupied{0}, request{0};
std::atomic<unsigned> bank_request{0}, shown_bank{0}, bank_view{0}, engine_flags{0};
std::atomic<bool> midi_panic{false}, control_panic{false}, gate_pulse{false};
std::atomic<bool> usb_overflow{false}, fade_out{false}, faded{false};
bool usb_ready=false;
bool sent_notes[128]{};
float master_gain=1;
// Inspect through a debugger or STATUS SysEx; values are NOT measurements until the
// human runs them on hardware (HARDWARE_TEST_PLAN.md).
volatile uint32_t callback_peak_cycles=0, callback_overruns=0;
// Overload protection. Every libDaisy interrupt that matters (audio DMA, USB, I2C) has
// priority 0, so a callback that keeps missing its deadline starves USB, the control
// timer and the main loop: the kit looks dead. A second overrun within kOverloadWindow
// blocks leaves the vocoder, the only stage that can exceed a block by itself, out of the
// next kOverloadSkip blocks. One isolated overrun only counts.
constexpr unsigned kOverloadWindow=16, kOverloadSkip=8;
unsigned blocks_since_overrun=kOverloadWindow;  // audio callback only
// Software entry into the Daisy bootloader: S9+S16 for 5 s (NEBULAE mode only, F7),
// or SysEx over J7 in every mode.
bootloader::KeyCombo boot_keys;     // control timer only
bootloader::SysExMatcher boot_sysex; // USB interrupt only
bootloader::Request boot_request;
neb::sysex::Collector control_sysex; // USB interrupt fills, main answers
#if NEBULAE_MOD
// Mod on Daisy (separate build, docs/MODULATION.md). One owner per state:
//   mod::Engine         control timer (PollControls): Tick, CV/gate, base report
//   controller, live    main loop: SysEx, compile, replies, monitor
//   ui.params           audio callback, as in the regular build
// The timer can be interrupted by audio mid-Tick, so audio never calls Engine::Apply;
// it takes a complete frame from mod_offsets. Bases travel the other way.
mod::Module modulation;
const mod::InstrumentInfo mod_info=neb::columns::MakeModInfo();
neb::modbridge::MidiTap mod_tap;                // USB interrupt fills, main drains
neb::modbridge::TickClock mod_clock;            // control timer only
mod::TripleBuffer<neb::columns::Frame> mod_offsets; // control timer -> audio
mod::TripleBuffer<neb::columns::Frame> mod_bases;   // audio -> control timer
class ModSender final : public mod::MidiSender {
  public:
    bool Send(const uint8_t* bytes,size_t size) override {
        if(hUsbDeviceHS.dev_state!=USBD_STATE_CONFIGURED) return false;
        usb.Tx(const_cast<uint8_t*>(bytes),size);
        return true;
    }
};
ModSender mod_sender;
void InitModulation() {
    modulation.Init(mod_info,float(neb::kRate),neb::columns::kTickFrames);
    // Extended monitor (base values) and LFO cross modulation; Nebulae reports no modes.
    modulation.EnableExtensions({true,true,false});
    mod_clock.Start(daisy::System::GetNow());
}
// Control timer, after hw.UpdateAllControls(). hw.gate_in[].State() reads the pin and
// leaves the edge memory of Trig() alone, so the gate 1 grain trigger keeps its pulses.
void TickModulation(uint32_t now) {
    if(mod_bases.Fetch()) neb::columns::ReportBases(modulation.engine,mod_bases.Front());
    mod::adapter::ReadHardware(modulation.engine,hw);
    const uint32_t due=mod_clock.Due(now);
    if(!due) return;
    for(uint32_t i=0;i<due;++i) modulation.engine.Tick(neb::columns::kTickFrames);
    neb::columns::Frame& frame=mod_offsets.Back();
    for(size_t t=0;t<mod::kTargets;++t) frame.value[t]=modulation.engine.Offset(t);
    mod_offsets.Publish();
}
// Main loop only, bounded per pass. Everything the matrix sees is also still parsed by
// the instrument, so nothing here can change note priority or the CC20-27 path.
void ServiceModulation(uint32_t now) {
    neb::modbridge::LiveEvent event;
    for(unsigned i=0;i<32 && mod_tap.Pop(event);++i) {
        mod::MidiMessage m;
        m.status=event.status;m.data0=event.data0;m.data1=event.data1;
        modulation.controller.HandleMidi(m);
    }
    neb::modbridge::SysExFrame frame;
    for(unsigned i=0;i<2 && mod_tap.Pop(frame);++i) {
        mod::MidiMessage m;
        m.status=0xF0;m.sysex=frame.body;m.sysex_size=frame.size;
        modulation.controller.HandleMidi(m);
    }
    if(mod_tap.Overflowed()) modulation.live.AllNotesOff();  // a Note Off may be among the lost
    modulation.controller.Service(now,mod_sender);
}
#endif

#if NEBULAE_PROFILE
// Diagnostic build (docs/OVERLOAD_DIAGNOSIS.md): per-block cycle splits folded into
// peaks by the audio callback, read by the main loop for the PROFILE reply, plus a boot
// benchmark that runs one vocoder channel once from SDRAM and once from AXI SRAM.
namespace prof {
volatile uint32_t blocks=0,total_peak=0,vocoder_peak=0,grains_peak=0,other_peak=0;
volatile uint32_t fill_peak=0,fft_peak=0,phase_peak=0,hop_blocks=0,hop_vocoder_min=0xffffffffu;
volatile uint32_t skipped=0,last_total=0;
volatile bool reset_request=false;  // main loop sets, audio callback clears
uint32_t bench[4]={};               // SDRAM hop, AXI SRAM hop, SDRAM plain, AXI SRAM plain
struct BenchRig {neb::MincerWork work;neb::Mincer mincer;};
static_assert(sizeof(BenchRig)<=sizeof(neb::Engine),"the AXI SRAM benchmark borrows engine_storage");
alignas(32) uint8_t DSY_SDRAM_BSS bench_sdram[sizeof(BenchRig)];
inline void Peak(volatile uint32_t& peak,uint32_t v) {if(v>peak) peak=v;}
void Reset() {
    blocks=total_peak=vocoder_peak=grains_peak=other_peak=0;
    fill_peak=fft_peak=phase_peak=hop_blocks=skipped=last_total=0;
    hop_vocoder_min=0xffffffffu;
}
// Audio callback, after Engine::Process. `cycles` is the whole callback so far.
void Record(uint32_t cycles) {
    if(reset_request) {Reset();reset_request=false;}
    const neb::profile::Split& s=neb::profile::block;
    ++blocks;last_total=cycles;
    Peak(total_peak,cycles);Peak(vocoder_peak,s.vocoder);Peak(grains_peak,s.grains);
    Peak(other_peak,cycles-s.vocoder-s.grains);
    Peak(fill_peak,s.fill);Peak(fft_peak,s.fft);Peak(phase_peak,s.phase);
    if(s.hops) {++hop_blocks;if(s.vocoder<hop_vocoder_min) hop_vocoder_min=s.vocoder;}
    if(s.skipped) ++skipped;
}
// Boot, before the control timer and audio start (USB and SysTick interrupts can still
// fall into it, hence the minimum): eight blocks through one freshly initialised vocoder
// channel reading PCM from SDRAM (bank 0, whatever it holds) at pitch 1; even blocks
// compute a hop (cnt_ starts at HOP).
void Bench(void* storage,const int16_t* pcm,uint32_t& hop,uint32_t& plain) {
    BenchRig* rig=new(storage) BenchRig;
    rig->work.Init();rig->mincer.Init(float(neb::kRate),rig->work);
    const neb::PcmView view{pcm,neb::kLiveFrames,neb::kRate,2};
    static double positions[neb::kBlock];static float out[neb::kBlock];
    double pos=100000.0;hop=plain=0xffffffffu;
    for(unsigned block=0;block<8;++block) {
        for(unsigned s=0;s<neb::kBlock;++s) {positions[s]=pos;pos+=1.0;}
        const uint32_t start=DWT->CYCCNT;
        rig->mincer.Process(positions,view,0,1.f,1.f,out,int(neb::kBlock));
        const uint32_t cycles=DWT->CYCCNT-start;
        uint32_t& slot=block%2?plain:hop;
        if(cycles<slot) slot=cycles;
    }
    asm volatile("" : : "r"(out));  // the output is the point, keep it
}
// The AXI SRAM run borrows engine_storage: call it before the Engine is constructed.
void RunBench(const int16_t* pcm) {
    Bench(bench_sdram,pcm,bench[0],bench[2]);
    Bench(engine_storage,pcm,bench[1],bench[3]);
}
}
#endif

// ---- SD: one owner (main loop), read-only FatFs R0.16 from common/sd ----
FATFS fs;
neb::BankScanner banks;
neb::FatDirectory directory;
bool CardPresent() {return card.load(std::memory_order_relaxed)!=0;}
bool sd_failed=false;
void SdFailed() {sd_failed=true;}
neb::FatFile file(banks,CardPresent,SdFailed);

enum EngineFlag : unsigned {kLive=1,kRecording=2};
void ServiceUsb();
void ReceiveUsb(uint8_t* bytes,size_t size,void*) {
    neb::Midi event;
    for(size_t i=0;i<size;++i) {
        if(boot_sysex.Byte(bytes[i])) boot_request.Set(bootloader::Request::kSysEx);
        control_sysex.Byte(bytes[i]);
#if NEBULAE_MOD
        mod_tap.Byte(bytes[i]);
#endif
        if(usb_parser.Byte(bytes[i],event) && !usb_events.Push(event))
            usb_overflow.store(true,std::memory_order_release);
    }
}

void PollControls(void*) {
    hw.UpdateAllControls();
    static uint16_t held=0;
    for(unsigned i=0;i<16;++i) {
        if(hw.button[i].RisingEdge()) held |= uint16_t(1u<<i);
        if(hw.button[i].FallingEdge()) held &= uint16_t(~(1u<<i));
    }
    neb::ControlFrame f;f.keys=held;f.ms=daisy::System::GetNow();f.toggle=hw.tog_sw18.Read();
    f.toggle17=hw.tog_sw17.Pressed();
    // F7: S9+S16 are C5+C6 in KEYBOARD and slots 9/16 in SAMPLE. Only NEBULAE mode,
    // where both keys are reserved, evaluates the bootloader gesture.
    const bool nebulae_mode=ui_mode.load(std::memory_order_relaxed)==0;
    if(boot_keys.Process(nebulae_mode?held:0,f.ms)) boot_request.Set(bootloader::Request::kKeys);
    for(unsigned i=0;i<8;++i) f.pot[i]=hw.pot[i].Value();
    if(!controls.Push(f)) control_panic.store(true,std::memory_order_release);
    card.store(hw.sd_detect.Pressed(),std::memory_order_relaxed);
    if(hw.gate_in[0].Trig()) gate_pulse.store(true,std::memory_order_relaxed);
#if NEBULAE_MOD
    TickModulation(f.ms);
#endif
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
    engine->selected_bank=shown_bank.load(std::memory_order_relaxed);
#if NEBULAE_MOD
    if(mod_offsets.Fetch()) engine->SetControlOffsets(mod_offsets.Front().value);
#endif
    engine->Process(in[0],in[1],out[0],out[1],unsigned(n));
#if NEBULAE_MOD
    neb::columns::Frame& bases=mod_bases.Back();
    for(size_t t=0;t<mod::kTargets;++t) bases.value[t]=engine->ui.params.primary[t];
    mod_bases.Publish();
#endif
    // SD management requested: fade to silence within one block, then stay silent.
    if(fade_out.load(std::memory_order_acquire)) {
        const float step=1.f/float(n?n:1);
        for(size_t s=0;s<n;++s) {
            master_gain=master_gain>step?master_gain-step:0.f;
            out[0][s]*=master_gain;out[1][s]*=master_gain;
        }
        if(master_gain==0.f) faded.store(true,std::memory_order_release);
    }
    if(engine->requested_slot) {request.store(engine->requested_slot,std::memory_order_release);engine->requested_slot=0;}
    if(engine->requested_bank) {bank_request.store(engine->requested_bank,std::memory_order_release);engine->requested_bank=0;}
    ui_mode.store(engine->ui.mode,std::memory_order_relaxed);
    bank_view.store(engine->ui.bank_view,std::memory_order_relaxed);
    key_state.store(engine->ui.mode==2?engine->ui.keyboard:0,std::memory_order_relaxed);
    leds.store(engine->Status(),std::memory_order_relaxed);
    engine_flags.store((engine->LiveLength()?unsigned(kLive):0u)|(engine->recording||engine->finalizing?unsigned(kRecording):0u),
                       std::memory_order_relaxed);
    const uint32_t cycles=DWT->CYCCNT-start;
    if(cycles>callback_peak_cycles) callback_peak_cycles=cycles;
    if(cycles>uint32_t((uint64_t(SystemCoreClock)*n)/neb::kRate)) {
        ++callback_overruns;
        if(blocks_since_overrun<kOverloadWindow) engine->SkipVocoder(kOverloadSkip);
        blocks_since_overrun=0;
    } else if(blocks_since_overrun<kOverloadWindow) ++blocks_since_overrun;
#if NEBULAE_PROFILE
    prof::Record(cycles);
#endif
}
void ServiceUsb() {
    if(!usb_ready) return;
    static bool recovering=false;
    auto panic=[&] {recovering=true;midi_panic.store(true,std::memory_order_release);};
    if(!usb.RxActive()) {usb.FlushRx();usb.StartRx(ReceiveUsb,nullptr);panic();}
    if(usb_overflow.exchange(false,std::memory_order_acquire)) panic();
    const bool connected=hUsbDeviceHS.dev_state==USBD_STATE_CONFIGURED;
    static bool was_connected=false;
    if(was_connected && !connected) {
        panic();for(auto& v:sent_notes) v=false;
#if NEBULAE_MOD
        modulation.live.AllNotesOff();
#endif
    }
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

// ---- Performance main loop state ----
struct Perf {
    bool mounted=false, mount_pending=false, initial=false, restarting=false, share=false;
    uint32_t remount_at=0, last_led=0;
    unsigned pending=0, restore_slot=0;
    neb::LedFrame last_leds{0xffff,0xffff};
};
Perf perf;

// "major.minor.patch" from version.h at compile time (no stdio in the image).
constexpr unsigned VersionPart(const char* v,unsigned part) {
    unsigned value=0;
    for(;*v && part;++v) if(*v=='.') --part;
    for(;*v>='0' && *v<='9';++v) value=value*10+unsigned(*v-'0');
    return value;
}
constexpr uint8_t kVersion[3]={uint8_t(VersionPart(NEBULAE_VERSION,0)),uint8_t(VersionPart(NEBULAE_VERSION,1)),
                               uint8_t(VersionPart(NEBULAE_VERSION,2))};
static_assert(VersionPart(NEBULAE_VERSION,0)<128 && VersionPart(NEBULAE_VERSION,1)<128
              && VersionPart(NEBULAE_VERSION,2)<128,"STATUS version must fit MIDI data bytes");
neb::sysex::Status CurrentStatus() {
    neb::sysex::Status s;
    for(unsigned i=0;i<3;++i) s.fw[i]=kVersion[i];
    const unsigned flags=engine_flags.load(std::memory_order_relaxed);
    s.card=CardPresent();
    s.live=flags&kLive;
    s.recording=flags&kRecording;
    s.loading=loader.state!=neb::Loader::State::Idle;
    s.scanning=banks.Busy();
    s.overrun=callback_overruns!=0;
    s.bank=uint8_t(shown_bank.load(std::memory_order_relaxed));
    s.banks=uint8_t(banks.BankCount());
    s.playing_bank=uint8_t(bridge.bank);
    s.playing_slot=uint8_t(loader.active);
    s.bank_mask=banks.BankMask();
    s.slot_mask=uint16_t(occupied.load(std::memory_order_relaxed));
    s.load_error=uint8_t(loader.error);
    s.overruns=uint16_t(callback_overruns>65535?65535:callback_overruns);
    const uint64_t deadline=(uint64_t(SystemCoreClock)*neb::kBlock)/neb::kRate;
    const uint64_t load=deadline?uint64_t(callback_peak_cycles)*100/deadline:0;
    s.peak_load=uint8_t(load>127?127:load);
    return s;
}
#if NEBULAE_PROFILE
neb::sysex::Profile CurrentProfile() {
    using namespace neb::sysex;
    Profile p;
    p.value[kProfClock]=SystemCoreClock;
    p.value[kProfDeadline]=uint32_t((uint64_t(SystemCoreClock)*neb::kBlock)/neb::kRate);
    p.value[kProfBlocks]=prof::blocks;p.value[kProfTotalPeak]=prof::total_peak;
    p.value[kProfVocoderPeak]=prof::vocoder_peak;p.value[kProfGrainsPeak]=prof::grains_peak;
    p.value[kProfOtherPeak]=prof::other_peak;p.value[kProfFillPeak]=prof::fill_peak;
    p.value[kProfFftPeak]=prof::fft_peak;p.value[kProfPhasePeak]=prof::phase_peak;
    p.value[kProfHopBlocks]=prof::hop_blocks;
    p.value[kProfHopVocoderMin]=prof::hop_blocks?prof::hop_vocoder_min:0;
    p.value[kProfSkipped]=prof::skipped;p.value[kProfOverruns]=callback_overruns;
    p.value[kProfBenchSdramHop]=prof::bench[0];p.value[kProfBenchSramHop]=prof::bench[1];
    p.value[kProfBenchSdramPlain]=prof::bench[2];p.value[kProfBenchSramPlain]=prof::bench[3];
    p.value[kProfEnginePlacement]=uint32_t(reinterpret_cast<uintptr_t>(engine)>>20);
    p.value[kProfLastTotal]=prof::last_total;
    p.value[kProfSourceChannels]=engine->live?2u:(bridge.published.frames?bridge.published.channels:0u);
    p.value[kProfSchedule]=engine->ScheduleMode();
    return p;
}
#endif
void ServiceControl() {
    uint8_t body[neb::sysex::kMaxBody];size_t size=0;
    if(!control_sysex.Take(body,size)) return;
    neb::sysex::Reply reply(0);bool share=false;
#if NEBULAE_PROFILE
    bool reset=false;
    if(!neb::sysex::Handle(body,size,CurrentStatus(),reply,share,CurrentProfile(),reset)) return;
    if(reset) prof::reset_request=true;
#else
    if(!neb::sysex::Handle(body,size,CurrentStatus(),reply,share)) return;
#endif
    const uint8_t* bytes=reply.Finish();
    if(hUsbDeviceHS.dev_state==USBD_STATE_CONFIGURED) usb.Tx(const_cast<uint8_t*>(bytes),reply.Size());
    if(share) perf.share=true;
}

void Unmount() {
    directory.Close();file.Close();
    if(perf.mounted) ro_sd_unmount();
    perf.mounted=false;file.SetMounted(false);
}

// PREPARE_SHARE (V0_2_ARCHITECTURE.md): silence, notes off, stop audio and SD, then
// restart this image into SD management. Never returns on hardware.
void PrepareShare() {
    fade_out.store(true,std::memory_order_release);
    for(unsigned i=0;i<50 && !faded.load(std::memory_order_acquire);++i) {daisy::System::Delay(1);ServiceUsb();}
    // All Notes Off for the host: release every note this device sent, then CC123.
    if(hUsbDeviceHS.dev_state==USBD_STATE_CONFIGURED) {
        for(unsigned note=0;note<128;++note) if(sent_notes[note]) {
            uint8_t off[3]={0x80,uint8_t(note),0};usb.Tx(off,3);sent_notes[note]=false;
        }
        uint8_t all_off[3]={0xB0,123,0};usb.Tx(all_off,3);
    }
    hw.seed.StopAudio();
    Unmount();
    neb_sd_release();  // SDMMC1 powered down: management re-initialises it from scratch
    for(unsigned i=0;i<16;++i) hw.SetLed(i,0.f);
    hw.UpdateLeds();hw.UpdateLeds();
    neb::BootIntent intent;
    intent.personality=neb::Personality::SdManagement;
    intent.bank=uint8_t(bridge.bank);intent.slot=uint8_t(bridge.bank?loader.active:0);
    neb::StoreBootIntent(neb_intent_words(),intent);
    daisy::System::Delay(20);  // SysEx ACK and MIDI note-offs leave before the reset
    perf.restarting=true;
    daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_SKIP_TIMEOUT);
}

// One main-loop iteration of the performance personality.
void PerformanceStep(uint32_t now) {
    ServiceUsb();
    ServiceBootloader();
    ServiceControl();
    if(perf.share) {perf.share=false;PrepareShare();return;}
#if NEBULAE_MOD
    // After the NB and BL replies, and never during PREPARE_SHARE: a monitor frame must
    // not hold the transmit path while the host's note offs go out.
    ServiceModulation(now);
#endif
    const unsigned selected=request.exchange(0,std::memory_order_acquire);
    if(selected) perf.pending=selected; // latest wins
    // Card insertion/removal. Removal clears the tables; Live keeps working.
    static bool previous_card=!CardPresent();
    const bool present=CardPresent();
    if(present!=previous_card) {
        previous_card=present;
        Unmount();banks.Reset();occupied.store(0);
        perf.mount_pending=present;perf.remount_at=now;perf.initial=true;
    }
    if(sd_failed) {  // an SD read failed: the volume is invalid until remounted
        sd_failed=false;Unmount();
        perf.mount_pending=present;perf.remount_at=now+2000;
    }
    if(perf.mount_pending && loader.state==neb::Loader::State::Idle && int32_t(now-perf.remount_at)>=0) {
        perf.mount_pending=false;
        perf.mounted=ro_sd_mount(&fs)==FR_OK;
        file.SetMounted(perf.mounted);
        if(perf.mounted) {
            const unsigned restore=shown_bank.load()?shown_bank.load():1;
            banks.Start(restore);
        } else if(present) {
            perf.mount_pending=true;perf.remount_at=now+2000;  // retry slowly
        }
    }
    const unsigned bank=bank_request.exchange(0,std::memory_order_acquire);
    if(bank && perf.mounted && banks.SelectBank(bank)) {shown_bank.store(bank);perf.pending=0;}
    // Directory work only while no file is being read; one entry per iteration.
    if(perf.mounted && loader.state==neb::Loader::State::Idle && banks.Busy()) banks.Step(directory);
    if(!banks.Busy() && banks.Discovered()) {
        if(banks.FilesBank() && shown_bank.load()!=banks.FilesBank()) shown_bank.store(banks.FilesBank());
        occupied.store(banks.FilesBank()==shown_bank.load()?banks.SlotMask():0);
        if(perf.initial && banks.FilesReady()) {
            perf.initial=false;
            const uint16_t mask=banks.SlotMask();
            unsigned slot=perf.restore_slot && (mask&(1u<<(perf.restore_slot-1)))?perf.restore_slot:0;
            for(unsigned i=0;!slot && i<16;++i) if(mask&(1u<<i)) slot=i+1;
            perf.restore_slot=0;
            if(slot && !perf.pending) perf.pending=slot;
        }
    }
    if(loader.state==neb::Loader::State::Idle && perf.pending && !banks.Busy()) {
        if(!present || !perf.mounted || banks.FilesBank()==shown_bank.load()) {loader.Request(perf.pending);perf.pending=0;}
    }
    loader.Service();
    if(now-perf.last_led>=10) {
        perf.last_led=now;
        neb::LedState s;
        s.mode=ui_mode.load();s.bank_view=bank_view.load()!=0;s.keys=uint16_t(key_state.load());
        s.active=bridge.bank==shown_bank.load()?loader.active:0;s.target=loader.target;
        s.loading=loader.state!=neb::Loader::State::Idle && loader.state!=neb::Loader::State::Publish;
        s.error=loader.error;s.ms=now;s.status=uint16_t(leds.load());
        s.bank=shown_bank.load();s.bank_mask=banks.BankMask();s.scanning_bank=banks.ScanningBank();
        s.bank_failed=banks.FilesFailed() && banks.FilesBank()==s.bank;
        s.overrun=callback_overruns!=0;
        const neb::LedFrame f=neb::PanelLeds(s);
        if(f.on!=perf.last_leds.on || f.dim!=perf.last_leds.dim) {
            for(unsigned i=0;i<16;++i) hw.SetLed(i,f.on&(1u<<i)?1.f:f.dim&(1u<<i)?neb::kDimLevel:0.f);
            hw.UpdateLeds();perf.last_leds=f;
        }
    }
}

// The bank and slot a restart brought back from SD management; the first scan loads them.
void ArmRestore(const neb::BootIntent& intent) {
    shown_bank.store(intent.bank);
    perf.restore_slot=intent.slot;
    perf.mount_pending=CardPresent();perf.initial=true;
}
void InitPerformance(const neb::BootIntent& intent) {
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(neb::kBlock);
    if(hw.seed.AudioBlockSize()!=neb::kBlock) while(true) {} // reject silent driver clamping
    for(unsigned i=0;i<100;++i) {hw.UpdateAllControls();daisy::System::Delay(1);}
    card.store(hw.sd_detect.Pressed(),std::memory_order_relaxed);
    const uintptr_t begin=reinterpret_cast<uintptr_t>(__neb_file_start),end=reinterpret_cast<uintptr_t>(__neb_file_end);
    if(begin<0xc0000000u || end>0xc4000000u-neb::kReserveBytes || begin>=end) while(true) {}
#if NEBULAE_PROFILE
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    prof::RunBench(live_banks[0]);  // borrows engine_storage: before the Engine exists
#endif
    engine=new(engine_storage) neb::Engine;
    engine->Init(live_banks[0],live_banks[1],neb::kLiveFrames,reinterpret_cast<int16_t*>(begin),bridge);
    engine->SetSchedule(NEBULAE_VOCODER_SCHEDULE);
    loader.Init(file,reinterpret_cast<int16_t*>(begin),end-begin,bridge);
    daisy::SdmmcHandler::Config sd_config;sd_config.Defaults();sd_config.speed=daisy::SdmmcHandler::Speed::STANDARD;
    sd.Init(sd_config);
    RoSdConfig ro{};
    ro.bounce=sector_buffer;ro.bounce_bytes=sizeof sector_buffer;
    ro.transfer_timeout_ms=20;  // per sector, as v0.1
    ro.card_present=[] {return CardPresent()?1:0;};
    ro.service=[] {ServiceUsb();};  // USB MIDI between sectors; never re-enters FatFs
    ro.abort=neb_sd_abort;          // RCC-reset abort, hardware-tested in the card reader
    if(ro_sd_init(&ro)!=0) while(true) {}
    ArmRestore(intent);
#if NEBULAE_MOD
    InitModulation();  // before USB and the control timer start
#endif
    daisy::MidiUsbTransport::Config cfg;cfg.periph=daisy::MidiUsbTransport::Config::EXTERNAL;
    usb.Init(cfg);usb.StartRx(ReceiveUsb,nullptr);usb_ready=true;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    hw.StartLowPriorityCallback(PollControls,1000);
    hw.seed.StartAudio(Audio);
}
}
#if NEBULAE_PROFILE
uint32_t neb::profile::Now() {return DWT->CYCCNT;}
#endif
int main() {
    asm volatile("" : : "r"(firmware_id));
#if NEBULAE_PROFILE
    asm volatile("" : : "r"(profile_id));
#endif
    hw.Init();
    // The boot intent is taken (read and cleared) before anything else is decided:
    // without a valid record this is always the performance personality.
    const neb::BootIntent intent=neb::TakeBootIntent(neb_intent_words());
    if(intent.personality==neb::Personality::SdManagement) neb_run_sd_management(hw,intent);
    InitPerformance(intent);
    while(!perf.restarting) {
        PerformanceStep(daisy::System::GetNow());
        daisy::System::Delay(1);
    }
    while(true) {}
}
