// SPDX-License-Identifier: MIT
// Cumulonimbus standalone diagnostic firmware for the Seed3 Desktop Dev Kit: stereo line input
// or a built-in deterministic test source -> stereo grains -> Torus reverb -> stereo output.
// The same GranularReverbChain as Toy Piano on Cumulonimbus. Build: RATE=48 (F48) or RATE=32 (F32).
// No storage writes. USB-MIDI on J7: bootloader request and the channel-16 diagnostics
// (scenario CC20, grain cap CC21, stimulus CC22, telemetry CC119; tools/cumulonimbus_probe.py).
#include "chain.h"
#include "diagnostics.h"
#include "fx_panel.h"
#include "stage_meter.h"
#include "version.h"
#define CUMULONIMBUS_STR2(x) #x
#define CUMULONIMBUS_STR(x) CUMULONIMBUS_STR2(x)
const char cumulonimbus_firmware_version[] = "CUMULONIMBUS-FX-VERSION=" CUMULONIMBUS_FX_FIRMWARE_VERSION;
const char cumulonimbus_build[] = "CUMULONIMBUS-BUILD=RATE" CUMULONIMBUS_STR(CUMULONIMBUS_INTERNAL_RATE) " COMPONENT=" CUMULONIMBUS_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include <array>
#include <atomic>
#include <new>
using namespace daisy;
namespace diag = cumulonimbus::diag;
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace fx_fw {
constexpr std::size_t kBlockSize = 48;
constexpr unsigned kScenarioKeys = diag::kScenarioCount; // S1..S7
constexpr unsigned kOverrunLed = 7;                       // S8
constexpr unsigned kCapKey0 = 8;                          // S9..S12: caps 8, 16, 24, 32
constexpr int32_t kCaps[4] = {8, 16, 24, 32};
constexpr unsigned kTriggerKey = 13, kStimulusKey = 14;   // S14, S15 (S16 stays free: bootloader combo)
constexpr int32_t kDefaultCap = 16;
} // namespace fx_fw

static desktop_devkit::Hardware hw;
static DSY_SDRAM_BSS ::seed::fx::Cumulonimbus::Storage grain_memory; // PCM16 rings, 2 s per channel
// Grain state, Clouds tables and the reverb's 64 KiB in AXI SRAM behind the image (NOLOAD).
#if defined(__arm__)
__attribute__((section(".tp_axi_bss"), aligned(32)))
#endif
static ::seed::fx::TorusReverb::Storage reverb_memory;
#if defined(__arm__)
__attribute__((section(".tp_axi_bss"), aligned(32)))
#endif
alignas(::seed::fx::GranularReverbChain) static unsigned char chain_storage[sizeof(::seed::fx::GranularReverbChain)];
static ::seed::fx::GranularReverbChain* chain = nullptr;
static diag::StageMeter meter;
static diag::Requests requests;
static diag::TestSource source;
static diag::TelemetrySender telemetry;
static std::array<std::atomic<uint32_t>, diag::kMetricCount> live{}; // audio -> main, per block
static std::atomic<uint32_t> ui_state{0};
static uint8_t scenario = diag::kScenarioFull;
static int32_t cap = fx_fw::kDefaultCap;
static uint32_t down_mask = 0;
static bool overrun_seen = false;
static bootloader::KeyCombo boot_keys;
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static std::atomic<uint32_t> reset_epoch{0}; // audio -> main: statistics restarted
static uint32_t Cycles() { return DWT->CYCCNT; }

static void ApplyScenario(uint8_t s) {
    scenario = s < diag::kScenarioCount ? s : diag::kScenarioFull;
    chain->SetRoute(diag::RouteFor(scenario));
    meter.RequestReset(); // a new setting starts new statistics
    reset_epoch.fetch_add(1, std::memory_order_relaxed);
}
static void ApplyCap(int32_t c) {
    cap = c;
    chain->SetCap(cap);
    meter.RequestReset();
    reset_epoch.fetch_add(1, std::memory_order_relaxed);
}

// Audio thread only.
static void Controls() {
    const int32_t s = requests.scenario.exchange(-1, std::memory_order_relaxed);
    if(s >= 0) ApplyScenario(uint8_t(s));
    const int32_t c = requests.cap.exchange(-1, std::memory_order_relaxed);
    if(c >= 0) ApplyCap(c);
    const int32_t st = requests.stimulus.exchange(-1, std::memory_order_relaxed);
    if(st >= 0) source.Select(uint8_t(st));
    if(requests.telemetry.exchange(-1, std::memory_order_relaxed) == 2) {
        chain->GrainEffect().ResetCounters();
        meter.RequestReset();
        reset_epoch.fetch_add(1, std::memory_order_relaxed);
        overrun_seen = false;
    }
    hw.UpdateAllControls();
    uint32_t presses = 0;
    for(unsigned i = 0; i < 16; ++i) {
        const uint32_t bit = 1u << i;
        if(hw.button[i].FallingEdge()) down_mask &= ~bit;
        if(hw.button[i].RisingEdge() && !(down_mask & bit)) {
            down_mask |= bit;
            presses |= bit;
        }
    }
    if(boot_keys.Process(down_mask, System::GetNow()))
        boot_request.Set(bootloader::Request::kKeys);
    for(unsigned k = 0; k < fx_fw::kScenarioKeys; ++k)
        if(presses & (1u << k)) ApplyScenario(uint8_t(k));
    for(unsigned k = 0; k < 4; ++k)
        if(presses & (1u << (fx_fw::kCapKey0 + k))) ApplyCap(fx_fw::kCaps[k]);
    if(presses & (1u << fx_fw::kTriggerKey)) chain->Trigger();
    if(presses & (1u << fx_fw::kStimulusKey)) source.Select(uint8_t((source.Pattern() + 1) % diag::TestSource::kPatterns));
    std::array<float, 8> pots{};
    for(unsigned i = 0; i < 8; ++i) pots[i] = hw.pot[i].Value();
    const ::seed::fx::FxControls fx = ::seed::fx::MapFxPots(pots);
    chain->SetMode(::seed::fx::ModeFromSwitch(hw.tog_sw18.Read()));
    chain->SetGrainParameters(fx.grains);
    chain->SetSettings(fx.chain);
    uint32_t ui = uint32_t(scenario) | (uint32_t(source.Pattern()) << 4) | (overrun_seen ? 0x100u : 0u);
    for(unsigned k = 0; k < 4; ++k)
        if(cap == fx_fw::kCaps[k]) ui |= 0x1000u << k;
    ui_state.store(ui, std::memory_order_relaxed);
}

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t n) {
    const uint32_t start = DWT->CYCCNT;
    if(n != fx_fw::kBlockSize) {
        for(size_t i = 0; i < n; ++i) out[0][i] = out[1][i] = 0.f;
        return;
    }
    meter.Begin(start);
    Controls();
    float l[fx_fw::kBlockSize], r[fx_fw::kBlockSize];
    if(source.External()) {
        // A mono source in the left jack feeds both channels (as the other FX firmwares).
        const float* right = hw.right_detect.Pressed() ? in[1] : in[0];
        for(size_t i = 0; i < n; ++i) {
            l[i] = in[0][i];
            r[i] = right[i];
        }
    } else {
        source.Process(l, r, n);
    }
    chain->Process(l, r, out[0], out[1], n);
    const auto t = chain->GrainEffect().Telemetry();
    meter.Add(diag::kGrainWrite, t.write_cycles);
    meter.Add(diag::kGrainRender, t.render_cycles);
    meter.Add(diag::kConvert, t.convert_cycles);
    meter.Add(diag::kReverb, chain->ReverbCycles());
    live[diag::kScenario].store(scenario, std::memory_order_relaxed);
    live[diag::kCap].store(uint32_t(cap), std::memory_order_relaxed);
    live[diag::kStimulus].store(source.Pattern(), std::memory_order_relaxed);
    live[diag::kGrainsActive].store(uint32_t(t.active), std::memory_order_relaxed);
    live[diag::kGrainsPeak].store(uint32_t(t.peak_active), std::memory_order_relaxed);
    live[diag::kRejected].store(t.rejected, std::memory_order_relaxed);
    live[diag::kStartedHigh].store(t.started_high, std::memory_order_relaxed);
    live[diag::kStartedMedium].store(t.started_medium, std::memory_order_relaxed);
    uint32_t flags = diag::kFlagProfiling | (CUMULONIMBUS_INTERNAL_RATE == 32000 ? uint32_t(diag::kFlagRate32) : 0u);
    flags |= chain->Mode() == ::seed::fx::ChainMode::Bypass ? uint32_t(diag::kFlagBypass) : 0u;
    flags |= chain->Mode() == ::seed::fx::ChainMode::Freeze ? uint32_t(diag::kFlagFreeze) : 0u;
    flags |= chain->Suspended() ? uint32_t(diag::kFlagSuspended) : 0u;
    live[diag::kFlags].store(flags, std::memory_order_relaxed);
    const uint32_t total = DWT->CYCCNT - start;
    if(total > meter.Budget()) overrun_seen = true;
    meter.End(total);
}

static void RenderLeds(uint32_t ui) {
    for(unsigned i = 0; i < 16; ++i) {
        bool on = false;
        if(i < fx_fw::kScenarioKeys) on = (ui & 0xFu) == i;
        else if(i == fx_fw::kOverrunLed) on = ui & 0x100u;
        else if(i >= fx_fw::kCapKey0 && i < fx_fw::kCapKey0 + 4) on = ui & (0x1000u << (i - fx_fw::kCapKey0));
        else if(i == fx_fw::kStimulusKey) on = ((ui >> 4) & 0xFu) != 0;
        hw.SetLed(i, on ? .5f : 0.f);
    }
    hw.UpdateLeds();
}

static void Send(const uint8_t* bytes, size_t size) {
    if(hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED)
        usb_midi.SendMessage(const_cast<uint8_t*>(bytes), size);
}

static uint32_t reset_ms = 0;
static bool snapshot_pending = false;
// Main loop only.
static void ServiceUsbMidi() {
    usb_midi.Listen();
    while(usb_midi.HasEvents()) {
        const auto event = usb_midi.PopEvent();
        if(bootloader::IsRequest(event)) {
            boot_request.Set(bootloader::Request::kSysEx);
            continue;
        }
        if(event.type == ControlChange &&
           diag::HandleControlChange(requests, event.channel, event.data[0], event.data[1])) {
            if(requests.telemetry.load(std::memory_order_relaxed) == 1) {
                requests.telemetry.store(-1, std::memory_order_relaxed);
                meter.RequestSnapshot();
                snapshot_pending = true;
            }
        }
    }
    static uint32_t seen_epoch = 0;
    const uint32_t epoch = reset_epoch.load(std::memory_order_relaxed);
    if(epoch != seen_epoch) { // statistics restarted: the measured window starts now
        seen_epoch = epoch;
        reset_ms = System::GetNow();
    }
    if(snapshot_pending && meter.SnapshotReady() && !telemetry.Busy()) {
        diag::Metrics m{};
        for(unsigned i = 0; i < diag::kMetricCount; ++i) m[i] = live[i].load(std::memory_order_relaxed);
        diag::FillMeterMetrics(meter.Snapshot(), m);
        meter.ReleaseSnapshot();
        m[diag::kInternalRate] = CUMULONIMBUS_INTERNAL_RATE;
        m[diag::kBlockFrames] = fx_fw::kBlockSize;
        m[diag::kWindowMs] = System::GetNow() - reset_ms;
        telemetry.Start(m);
        snapshot_pending = false;
    }
    telemetry.Service(Send);
}
static void ServiceBootloader() {
    bootloader::Service(boot_request, hw, [](uint8_t* bytes, size_t size) { Send(bytes, size); });
}

int main() {
    asm volatile("" : : "r"(cumulonimbus_firmware_version));
    asm volatile("" : : "r"(cumulonimbus_build));
    hw.Init();
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(fx_fw::kBlockSize);
    for(auto& p : hw.pot) p.SetSampleRate(hw.seed.AudioCallbackRate());
    std::fill_n(chain_storage, sizeof(chain_storage), static_cast<unsigned char>(0));
    chain = new(chain_storage) ::seed::fx::GranularReverbChain{};
    ::seed::fx::Cumulonimbus::Options options;
    options.channels = 2;
    options.cap = fx_fw::kDefaultCap;
    if(!chain->Prepare(grain_memory, reverb_memory, options))
        while(true) System::Delay(100);
    chain->SetClock(Cycles);
    const ::seed::fx::FxControls start = ::seed::fx::MapFxPots(::seed::fx::FxDefaultPots());
    chain->SetGrainParameters(start.grains);
    chain->SetSettings(start.chain);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    meter.Init((SystemCoreClock / 48000u) * uint32_t(fx_fw::kBlockSize));
    live[diag::kBudgetCycles].store(meter.Budget(), std::memory_order_relaxed);
    MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_config);
    usb_midi.StartReceive();
    reset_ms = System::GetNow();
    hw.seed.StartAudio(AudioCallback);
    uint32_t previous_ui = ~0u, last_led = System::GetNow();
    while(true) {
        ServiceUsbMidi();
        ServiceBootloader();
        const uint32_t now = System::GetNow();
        if(now - last_led >= 10) {
            last_led = now;
            const uint32_t ui = ui_state.load(std::memory_order_relaxed);
            if(ui != previous_ui) {
                RenderLeds(ui); // I2C stays out of the audio interrupt
                previous_ui = ui;
            }
        }
        System::Delay(1);
    }
}
