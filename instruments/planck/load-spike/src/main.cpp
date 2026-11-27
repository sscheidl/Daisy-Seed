// S1 load spike for the Beads port study (README.md). Not an instrument: runs the unchanged
// Beads processor through a fixed parameter sweep and records the callback cycles per case.
//
// MEM=A: BOOT_SRAM, buffer in SDRAM. MEM=B: BOOT_QSPI, buffer in AXI + D2 SRAM as on the module.
// ITCM=1 (AI, BI): hot DSP code in ITCM; BI also has the hot lookup tables in DTCM and the synth
// wavetable in SDRAM. At run time (CC116, README.md): audio block 12 or 48 frames,
// and the product load (controls and Mod on Daisy matrix in a 1 kHz timer, MIDI in main).
#include "daisy_seed.h"
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "mod_on_daisy_desktop.h"
#include "mod_on_daisy/mailbox.h"
#include "product.h"
#include "spike.h"
#include "version.h"
#include <atomic>
#include <new>

#if defined(TEXTURE_SPIKE_MEMORY_B)
#if defined(TEXTURE_SPIKE_ITCM)
#define SPIKE_VARIANT "BI"
#else
#define SPIKE_VARIANT "B"
#endif
namespace {
// The module's layout: banks 0 and 1 in the 512 KiB AXI SRAM, bank 2 in D2 SRAM (beads/beads.cc).
uint8_t __attribute__((section(".spike_d1"), aligned(32))) bank_d1[2 * spike::kBankSize];
uint8_t __attribute__((section(".spike_d2"), aligned(32))) bank_d2[spike::kBankSize];
uint8_t* banks[spike::kNumBanks] = {bank_d1, bank_d1 + spike::kBankSize, bank_d2};
} // namespace
#else
#if defined(TEXTURE_SPIKE_ITCM)
#define SPIKE_VARIANT "AI"
#else
#define SPIKE_VARIANT "A"
#endif
namespace {
uint8_t DSY_SDRAM_BSS __attribute__((aligned(32))) bank_sdram[spike::kNumBanks * spike::kBankSize];
uint8_t* banks[spike::kNumBanks] = {bank_sdram, bank_sdram + spike::kBankSize, bank_sdram + 2 * spike::kBankSize};
} // namespace
#endif

#if defined(TEXTURE_SPIKE_ITCM)
// The hot DSP functions run from ITCM (src/memory_a_itcm.lds, src/memory_b_itcm.lds). Copied from
// their load address in the image (AXI SRAM for AI, QSPI for BI) before any constructor runs; this
// function itself stays in the image. BI also copies the hot lookup tables to DTCM.
extern "C" uint32_t _sitcm_text, _eitcm_text, _siitcm_text;
#if defined(TEXTURE_SPIKE_MEMORY_B)
extern "C" uint32_t _sdtcm_rodata, _edtcm_rodata, _sidtcm_rodata;
extern "C" uint32_t _ssdram_rodata, _esdram_rodata, _sisdram_rodata;
#endif
namespace {
void CopyWords(volatile uint32_t* dst, const volatile uint32_t* src, const uint32_t* end) {
    while(dst < end)
        *dst++ = *src++;
}
} // namespace
extern "C" void TextureSpikeCopyItcm() {
    CopyWords(&_sitcm_text, &_siitcm_text, &_eitcm_text);
#if defined(TEXTURE_SPIKE_MEMORY_B)
    CopyWords(&_sdtcm_rodata, &_sidtcm_rodata, &_edtcm_rodata);
#endif
    __DSB();
    __ISB();
}
__attribute__((section(".preinit_array"), used)) void (*const texture_spike_copy_itcm)() = TextureSpikeCopyItcm;
constexpr uint32_t kItcm = 1;
// BI: the synth wavetable to SDRAM, which works only after hw.Init() (src/memory_b_itcm.lds).
void CopySdramTables() {
#if defined(TEXTURE_SPIKE_MEMORY_B)
    CopyWords(&_ssdram_rodata, &_sisdram_rodata, &_esdram_rodata);
    __DSB();
    const volatile uint32_t* src = &_sisdram_rodata;
    for(const volatile uint32_t* dst = &_ssdram_rodata; dst < &_esdram_rodata; ++dst, ++src)
        if(*dst != *src)
            while(true) {} // a broken copy would change the sound: stop, identify then fails
#endif
}
#else
void CopySdramTables() {}
constexpr uint32_t kItcm = 0;
#endif

namespace {
const char kFirmwareId[] = "TEXTURE-SPIKE-VERSION=" TEXTURE_SPIKE_VERSION "-" SPIKE_VARIANT;
// As on the module: the reverb buffer in D3 SRAM, in every variant.
uint16_t __attribute__((section(".spike_d3"), aligned(32))) reverb_buffer[spike::kReverbWords];

constexpr float kMonitorGain = 0.125f; // -18 dB, only with SW17 on
constexpr uint8_t kIdle = 0, kRunning = 1, kDone = 2, kFailed = 3;
constexpr uint8_t kRunAllKey = 12;      // S13
constexpr std::size_t kBarFirst = 4;    // LEDs S5..S12: peak load bar
constexpr std::size_t kOverrunLed = 12; // S13 lights after an overrun
constexpr std::size_t kMaxBlock = 48;

daisy::desktop_devkit::Hardware hw;
// 37 KiB of event queues, not used by the audio path: in SDRAM, so .bss in DTCM keeps room for
// the engine state, where the module has it too. Constructed after hw.Init() has started the
// SDRAM; same in every variant.
alignas(daisy::MidiUsbHandler) uint8_t DSY_SDRAM_BSS usb_midi_storage[sizeof(daisy::MidiUsbHandler)];
daisy::MidiUsbHandler* usb_midi = nullptr;
alignas(mod::adapter::UsbMidiSender) uint8_t mod_sender_storage[sizeof(mod::adapter::UsbMidiSender)];
mod::adapter::UsbMidiSender* mod_sender = nullptr; // replies of the matrix controller
spike::Engine engine;
spike::Result results[spike::kNumCases];
mod::Module modulation;
const mod::InstrumentInfo kModInfo = spike::MakeModInfo();

// Per case: how it ran, and the cost outside the audio callback (product load only).
struct RunInfo {
    uint8_t block = 0, product = 0;
    uint32_t budget = 0;
};
struct ControlStats { // the 1 kHz control timer: controls, matrix tick, offsets
    uint8_t c = 0;
    uint32_t ticks = 0, peak = 0;
    uint64_t sum = 0;
};
struct MainStats { // main loop: MIDI into the matrix controller plus its Service()
    uint32_t events = 0, peak = 0;
};
RunInfo run_info[spike::kNumCases];
ControlStats control_result[spike::kNumCases];
MainStats main_result[spike::kNumCases];

// ---- main loop <-> audio <-> control timer ----
std::atomic<bool> active{false}; // audio owns engine and results[running] while true
std::atomic<uint8_t> running{0};
std::atomic<uint8_t> state[spike::kNumCases];
std::atomic<uint32_t> live_peak{0}, wrong_size{0}, block_frames{spike::kBlock}, budget{0};
std::atomic<bool> monitor{false}, product{false}, control_reset{false};
std::atomic<uint32_t> held_keys{0}, rising_keys{0};
mod::TripleBuffer<spike::OffsetFrame> offsets_box; // timer -> audio
mod::TripleBuffer<ControlStats> control_box;       // timer -> main
spike::OffsetFrame audio_offsets;                  // audio only
ControlStats control_acc;                          // timer only
bootloader::KeyCombo boot_keys;                    // main loop only
bootloader::Request boot_request;
uint8_t patch_status = 0xFF;
static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<uint8_t>::is_always_lock_free);

inline uint32_t Cycles() { return DWT->CYCCNT; }

void EnableCycleCounter() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55; // Cortex-M7 lock access register
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t BudgetFor(uint32_t frames) {
    return uint32_t(uint64_t(SystemCoreClock) * frames / uint32_t(spike::kSampleRate));
}

void AudioCallback(daisy::AudioHandle::InputBuffer, daisy::AudioHandle::OutputBuffer out, std::size_t size) {
    const uint32_t start = Cycles();
    const uint32_t frames = block_frames.load(std::memory_order_relaxed);
    if(size != frames || size % spike::kBlock || size > kMaxBlock || !active.load(std::memory_order_acquire)) {
        if(size != frames)
            wrong_size.fetch_add(1, std::memory_order_relaxed);
        for(std::size_t i = 0; i < size; ++i)
            out[0][i] = out[1][i] = 0.0f;
        return;
    }
    const float* offset = nullptr;
    if(product.load(std::memory_order_relaxed)) {
        if(offsets_box.Fetch())
            audio_offsets = offsets_box.Front();
        offset = audio_offsets.value;
    }
    // Blocks of 48 run the engine four times with 12 frames (spike.h RenderCallback).
    const float gain = monitor.load(std::memory_order_relaxed) ? kMonitorGain : 0.0f;
    const int point = spike::RenderCallback(engine, out[0], out[1], size, offset, gain);
    const uint32_t cycles = Cycles() - start;

    const uint8_t c = running.load(std::memory_order_relaxed);
    spike::Result& r = results[c];
    r.Add(point, cycles, budget.load(std::memory_order_relaxed));
    live_peak.store(r.peak, std::memory_order_relaxed);
    if(engine.done()) {
        state[c].store(kDone, std::memory_order_release);
        active.store(false, std::memory_order_release);
    }
}

// Controls: from the main loop without product load (as in 0.1.0), from the timer with it.
void PollControls() {
    hw.UpdateAllControls();
    uint32_t held = 0, rising = 0;
    for(std::size_t key = 0; key < daisy::desktop_devkit::kNumButtons; ++key) {
        held |= hw.button[key].Pressed() ? 1u << key : 0u;
        rising |= hw.button[key].RisingEdge() ? 1u << key : 0u;
    }
    held_keys.store(held, std::memory_order_relaxed);
    rising_keys.fetch_or(rising, std::memory_order_relaxed);
    monitor.store(hw.tog_sw17.Pressed(), std::memory_order_relaxed);
}

// Product load, 1 kHz on libDaisy's lowest interrupt priority (as Nebulae's control timer):
// controls, matrix inputs, one matrix tick, offsets to the audio callback.
void ControlTick(void*) {
    const uint32_t start = Cycles();
    PollControls();
    mod::adapter::ReadHardware(modulation.engine, hw);
    modulation.engine.Tick(spike::kTickFrames);
    spike::OffsetFrame& frame = offsets_box.Back();
    for(std::size_t t = 0; t < spike::kNumTargets; ++t)
        frame.value[t] = modulation.engine.Offset(t);
    offsets_box.Publish();
    const uint32_t cycles = Cycles() - start;

    if(control_reset.exchange(false, std::memory_order_relaxed))
        control_acc = ControlStats{};
    control_acc.c = running.load(std::memory_order_relaxed);
    if(active.load(std::memory_order_relaxed)) {
        ++control_acc.ticks;
        control_acc.sum += cycles;
        control_acc.peak = cycles > control_acc.peak ? cycles : control_acc.peak;
    }
    control_box.Back() = control_acc;
    control_box.Publish();
}

// ---- main loop only ----
bool SetBlock(uint32_t frames) {
    if(active.load(std::memory_order_acquire) || (frames != 12 && frames != 48))
        return false;
    hw.seed.StopAudio();
    hw.seed.SetAudioBlockSize(frames);
    const bool ok = hw.seed.AudioBlockSize() == frames;
    if(ok) {
        block_frames.store(frames, std::memory_order_relaxed);
        budget.store(BudgetFor(frames), std::memory_order_relaxed);
    }
    hw.seed.StartAudio(AudioCallback);
    return ok;
}

bool SetProduct(bool on) {
    if(active.load(std::memory_order_acquire) || on == product.load())
        return false;
    if(on) {
        product.store(true, std::memory_order_relaxed);
        hw.StartLowPriorityCallback(ControlTick, 1000);
    } else {
        hw.StopLowPriorityCallback();
        product.store(false, std::memory_order_relaxed);
    }
    return true;
}

bool StartCase(uint8_t c) {
    if(active.load(std::memory_order_acquire) || c >= spike::kNumCases)
        return false;
    results[c] = spike::Result{};
    control_result[c] = ControlStats{};
    main_result[c] = MainStats{};
    run_info[c].block = uint8_t(block_frames.load());
    run_info[c].product = product.load() ? 1 : 0;
    run_info[c].budget = budget.load();
    live_peak.store(0, std::memory_order_relaxed);
    running.store(c, std::memory_order_relaxed);
    control_reset.store(true, std::memory_order_relaxed);
    if(!engine.Start(spike::Case(c))) {
        state[c].store(kFailed, std::memory_order_relaxed);
        return false;
    }
    state[c].store(kRunning, std::memory_order_relaxed);
    active.store(true, std::memory_order_release);
    return true;
}

// ---- USB-MIDI readout, channel 16 (README.md) ----
void Send(uint8_t* bytes, std::size_t size) { usb_midi->SendMessage(bytes, size); }

void SendCcs(const uint32_t (&cc)[20]) {
    uint8_t m[60];
    for(unsigned i = 0; i < 20; ++i) {
        m[3 * i] = 0xBF;
        m[3 * i + 1] = uint8_t(100 + i);
        m[3 * i + 2] = uint8_t(cc[i] & 127);
    }
    Send(m, sizeof(m));
}

void Put(uint32_t (&cc)[20], unsigned first, uint32_t value, unsigned digits) {
    for(unsigned i = 0; i < digits; ++i)
        cc[first - 100 + i] = (value >> (7 * i)) & 127;
}

void SendIdentity() {
    uint32_t cc[20] = {};
    Put(cc, 100, 16, 1); // format: identity
    Put(cc, 101, SPIKE_VARIANT[0], 1);
    Put(cc, 102, SystemCoreClock / 1000000u, 2);
    Put(cc, 104, block_frames.load(), 1);
    Put(cc, 105, budget.load(), 3);
    Put(cc, 108, TEXTURE_SPIKE_VERSION_MAJOR, 1);
    Put(cc, 109, TEXTURE_SPIKE_VERSION_MINOR, 1);
    Put(cc, 110, TEXTURE_SPIKE_VERSION_PATCH, 1);
    Put(cc, 111, wrong_size.load(std::memory_order_relaxed), 3);
    Put(cc, 114, product.load() ? 1 : 0, 1);
    Put(cc, 115, kItcm, 1);
    Put(cc, 116, patch_status, 1);
    Put(cc, 119, 64, 1); // end marker
    SendCcs(cc);
}

bool Finished(uint8_t c) {
    return state[c].load(std::memory_order_acquire) != kRunning || !active.load(std::memory_order_acquire)
           || running.load() != c;
}

// Results only once a case is done; while it runs, the state and the live peak.
void SendResult(uint8_t c) {
    uint32_t cc[20] = {};
    const uint8_t s = state[c].load(std::memory_order_acquire);
    const bool own = Finished(c);
    const spike::Result& r = results[c];
    const uint32_t b = run_info[c].budget;
    const uint32_t peak = own ? r.peak : live_peak.load(std::memory_order_relaxed);
    Put(cc, 100, 17, 1); // format: case result
    Put(cc, 101, peak, 3);
    Put(cc, 104, own ? r.average() : 0, 3);
    Put(cc, 107, spike::Load(peak, b), 3);
    Put(cc, 110, own ? r.overruns : 0, 3);
    Put(cc, 113, c + 1u, 1);
    Put(cc, 114, own ? spike::Load(r.average(), b) : 0, 3);
    Put(cc, 117, own ? r.worst_point : 0, 1);
    Put(cc, 118, s | (run_info[c].product << 3) | (run_info[c].block == 48 ? 1u << 4 : 0u), 1);
    Put(cc, 119, 64, 1);
    SendCcs(cc);
}

// Cost outside the audio callback, for a case run with the product load.
void SendProductResult(uint8_t c) {
    uint32_t cc[20] = {};
    const bool own = Finished(c);
    const ControlStats& k = control_result[c];
    const MainStats& m = main_result[c];
    Put(cc, 100, 18, 1); // format: product load of a case
    Put(cc, 101, own ? k.peak : 0, 3);
    Put(cc, 104, own && k.ticks ? uint32_t(k.sum / k.ticks) : 0, 3);
    Put(cc, 107, own ? k.ticks : 0, 3);
    Put(cc, 110, own ? m.events : 0, 3);
    Put(cc, 113, c + 1u, 1);
    Put(cc, 114, own ? m.peak : 0, 3);
    Put(cc, 117, run_info[c].product, 1);
    Put(cc, 118, run_info[c].block, 1);
    Put(cc, 119, 64, 1);
    SendCcs(cc);
}

// Per-point peaks of a finished case: one 4-CC message per point, then the end marker.
void SendProfile(uint8_t c) {
    if(state[c].load(std::memory_order_acquire) != kDone)
        return;
    const spike::Result& r = results[c];
    for(std::size_t i = 0; i < spike::PointCount(spike::Case(c)); ++i) {
        const uint32_t v = r.point_peak[i];
        uint8_t m[12] = {0xBF, 96, uint8_t(i), 0xBF, 97, uint8_t(v & 127), 0xBF, 98, uint8_t((v >> 7) & 127),
                         0xBF, 99, uint8_t((v >> 14) & 127)};
        Send(m, sizeof(m));
        daisy::System::Delay(1);
    }
    uint8_t end[3] = {0xBF, 95, uint8_t(c + 1)};
    Send(end, sizeof(end));
}

// Requests on channel 16 (README.md): CC119 = 1..4 result of case a..d, 5 identity, 6..9 product
// load of case a..d; CC118 = 1..4 start case a..d, 5 run all; CC117 = 1..4 per-point profile;
// CC116 = 1 block 12, 2 block 48, 3 product load off, 4 product load on (only while idle).
bool run_all = false;
uint8_t next_case = 0;
void HandleCc(uint8_t cc, uint8_t value) {
    const bool idle = !run_all && !active.load();
    if(cc == 119 && value >= 1 && value <= 4)
        SendResult(uint8_t(value - 1));
    else if(cc == 119 && value == 5)
        SendIdentity();
    else if(cc == 119 && value >= 6 && value <= 9)
        SendProductResult(uint8_t(value - 6));
    else if(cc == 118 && value >= 1 && value <= 4 && !run_all)
        StartCase(uint8_t(value - 1));
    else if(cc == 118 && value == 5 && idle) {
        run_all = true;
        next_case = 0;
    } else if(cc == 117 && value >= 1 && value <= 4)
        SendProfile(uint8_t(value - 1));
    else if(cc == 116 && idle && (value == 1 || value == 2))
        SetBlock(value == 1 ? 12 : 48);
    else if(cc == 116 && idle && (value == 3 || value == 4))
        SetProduct(value == 4);
}

void ShowLeds(uint32_t now) {
    const bool blink = (now / 250) & 1;
    for(uint8_t c = 0; c < spike::kNumCases; ++c) {
        const uint8_t s = state[c].load(std::memory_order_relaxed);
        hw.SetLed(c, s == kDone ? 1.0f : (s == kRunning || s == kFailed) && blink ? 1.0f : 0.0f);
    }
    // Bar of the running or last case: 8 LEDs, 12.5 % each, the last one dimmed by the rest.
    const uint8_t c = running.load(std::memory_order_relaxed);
    const uint32_t b = run_info[c].budget ? run_info[c].budget : budget.load();
    const float load = float(spike::Load(live_peak.load(std::memory_order_relaxed), b)) / 10000.0f;
    for(std::size_t i = 0; i < 8; ++i) {
        float level = load * 8.0f - float(i);
        level = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
        hw.SetLed(int(kBarFirst + i), level);
    }
    const bool own = !active.load(std::memory_order_acquire);
    hw.SetLed(kOverrunLed, own && results[c].overruns ? 1.0f : 0.0f);
    hw.SetLed(13, product.load() ? 1.0f : 0.0f);
    hw.SetLed(14, block_frames.load() == 48 ? 1.0f : 0.0f);
    hw.SetLed(15, run_all ? 1.0f : 0.0f);
    hw.UpdateLeds();
}
} // namespace

int main() {
    asm volatile("" : : "r"(kFirmwareId));
    hw.Init();
    CopySdramTables(); // before anything reads beads::wav_integrated_waves
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(spike::kBlock);
    if(hw.seed.AudioBlockSize() != spike::kBlock)
        while(true) {} // reject silent driver clamping
    EnableCycleCounter();
    budget.store(BudgetFor(spike::kBlock), std::memory_order_relaxed);
    for(auto& s : state)
        s.store(kIdle, std::memory_order_relaxed);
    engine.Init(banks, reverb_buffer);
    modulation.Init(kModInfo, spike::kSampleRate, spike::kTickFrames);
    patch_status = uint8_t(modulation.controller.LoadPatch(spike::MakeProductPatch()));

    daisy::MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = daisy::MidiUsbTransport::Config::EXTERNAL;
    usb_midi = new(usb_midi_storage) daisy::MidiUsbHandler;
    usb_midi->Init(usb_config);
    usb_midi->StartReceive();
    mod_sender = new(mod_sender_storage) mod::adapter::UsbMidiSender(*usb_midi);
    hw.ClearLeds();
    hw.UpdateLeds();
    hw.seed.StartAudio(AudioCallback);

    uint32_t last_led = 0;
    while(true) {
        if(!product.load(std::memory_order_relaxed))
            PollControls();
        else if(control_box.Fetch()) {
            const ControlStats& k = control_box.Front();
            if(k.c < spike::kNumCases && state[k.c].load() != kIdle)
                control_result[k.c] = k;
        }
        const uint32_t now = daisy::System::GetNow();

        usb_midi->Listen();
        const uint8_t c = running.load(std::memory_order_relaxed);
        const bool measuring = active.load(std::memory_order_relaxed);
        const uint32_t midi_start = Cycles();
        while(usb_midi->HasEvents()) {
            const daisy::MidiEvent event = usb_midi->PopEvent();
            if(bootloader::IsRequest(event)) {
                boot_request.Set(bootloader::Request::kSysEx);
                continue;
            }
            if(event.type == daisy::ControlChange && event.channel == 15 && event.data[0] >= 116) {
                HandleCc(event.data[0], event.data[1]);
                continue;
            }
            mod::MidiMessage m;
            if(product.load(std::memory_order_relaxed) && mod::adapter::ToMessage(event, m)) {
                modulation.controller.HandleMidi(m);
                if(measuring)
                    ++main_result[c].events;
            }
        }
        if(product.load(std::memory_order_relaxed))
            modulation.controller.Service(now, *mod_sender);
        const uint32_t midi_cycles = Cycles() - midi_start;
        if(measuring && product.load(std::memory_order_relaxed) && midi_cycles > main_result[c].peak)
            main_result[c].peak = midi_cycles;

        const uint32_t held = held_keys.load(std::memory_order_relaxed);
        const uint32_t rising = rising_keys.exchange(0, std::memory_order_relaxed);
        if(boot_keys.Process(held, now))
            boot_request.Set(bootloader::Request::kKeys);
        bootloader::Service(boot_request, hw, [](uint8_t* bytes, std::size_t size) { Send(bytes, size); });

        if(!run_all) {
            for(uint8_t k = 0; k < spike::kNumCases; ++k)
                if(rising & (1u << k))
                    StartCase(k);
            if((rising & (1u << kRunAllKey)) && !active.load()) {
                run_all = true;
                next_case = 0;
            }
        }
        if(run_all && !active.load(std::memory_order_acquire)) {
            if(next_case < spike::kNumCases)
                StartCase(next_case++);
            else
                run_all = false;
        }

        // LEDs: 20 Hz while idle, 4 Hz during a run (fewer I2C interrupts in the measurement).
        const uint32_t period = active.load(std::memory_order_relaxed) ? 250 : 50;
        if(now - last_led >= period) {
            last_led = now;
            ShowLeds(now);
        }
        daisy::System::Delay(1);
    }
}
