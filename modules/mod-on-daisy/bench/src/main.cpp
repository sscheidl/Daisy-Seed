// Mod on Daisy measurement firmware (TEST_PLAN_PHASE2.md § 3). Not an instrument:
// a sine voice bank whose eight parameters run through the matrix, plus
// cycle counters for the matrix share, the whole callback and the main loop.
#include "daisy_seed.h"
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "util/CpuLoadMeter.h"
#include "mod_on_daisy_desktop.h"
#include "scenarios.h"
#include "version.h"
#include <atomic>
#include <cmath>

#ifndef MOD_BENCH_BLOCK
#define MOD_BENCH_BLOCK 48
#endif

namespace {
const char kFirmwareId[] = "MOD-BENCH-VERSION=" MOD_BENCH_VERSION;
constexpr std::size_t kBlock = MOD_BENCH_BLOCK;
constexpr float kSampleRate = 48000.0f;

daisy::desktop_devkit::Hardware hw;
daisy::MidiUsbHandler usb_midi;
MOD_ON_DAISY_HOT mod::Module modulation;
const mod::InstrumentInfo kInfo = bench::MakeInfo();
mod::adapter::UsbMidiSender sender(usb_midi);
daisy::CpuLoadMeter load_meter;

// ---- main loop -> audio ----
std::atomic<uint32_t> voice_count{4};
std::atomic<bool> bypass{false};
std::atomic<bool> reset_stats{true};
std::atomic<int8_t> voice_note[bench::kMaxVoices];
// ---- audio -> main loop ----
std::atomic<uint32_t> key_scenario{0xFF}; // S1..S9 pressed: scenario 0..8
mod::TripleBuffer<mod::Stats> stats_box;
uint8_t current_scenario = 1;
uint32_t midi_lost = 0, dispatch_max = 0, service_max = 0;
// Software entry into the Daisy bootloader: S13+S14 for 3 s, or SysEx over J7.
bootloader::KeyCombo boot_keys; // audio callback only
bootloader::Request boot_request;
static_assert(std::atomic<int8_t>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free);

struct Voice {
    float phase = 0.0f, increment = 0.0f;
    mod::VoicePressure pressure;
};
Voice voices[bench::kMaxVoices];
float voice_param[bench::kMaxVoices][5];
float global_param[3];

inline uint32_t Cycles() { return DWT->CYCCNT; }

void EnableCycleCounter() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55; // Cortex-M7 lock access register
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

// Audio statistics, owned by the audio callback.
struct AudioStats {
    uint32_t ticks = 0, min = 0xFFFFFFFFu, max = 0, overruns = 0;
    uint64_t sum = 0;
} audio_stats;
uint32_t budget_cycles = 0;

void AudioCallback(daisy::AudioHandle::InputBuffer, daisy::AudioHandle::OutputBuffer out, std::size_t size) {
    const uint32_t start = Cycles();
    load_meter.OnBlockStart();
    hw.UpdateAllControls();
    for(std::size_t key = 0; key < bench::kScenarioCount; ++key)
        if(hw.button[key].RisingEdge())
            key_scenario.store(uint32_t(key), std::memory_order_relaxed);
    uint32_t held = 0;
    for(std::size_t key = 0; key < daisy::desktop_devkit::kNumButtons; ++key)
        held |= hw.button[key].Pressed() ? 1u << key : 0u;
    if(boot_keys.Process(held, daisy::System::GetNow()))
        boot_request.Set(bootloader::Request::kKeys);
    if(reset_stats.exchange(false, std::memory_order_relaxed)) {
        audio_stats = AudioStats{};
        load_meter.Reset();
    }

    const std::size_t count = voice_count.load(std::memory_order_relaxed);
    // ---- measured matrix share: Tick plus every Apply/ApplyVoice ----
    const bool skip = bypass.load(std::memory_order_relaxed);
    const uint32_t matrix_start = Cycles();
    if(!skip) {
        mod::adapter::ReadHardware(modulation.engine, hw);
        modulation.engine.Tick(size);
        for(std::size_t v = 0; v < count; ++v) {
            const float p = voices[v].pressure.Process(modulation.engine, voice_note[v].load(std::memory_order_relaxed));
            for(std::size_t t = 0; t < 5; ++t)
                voice_param[v][t] = modulation.engine.ApplyVoice(t, bench::kBase[t], p);
        }
        for(std::size_t t = 0; t < 3; ++t)
            global_param[t] = modulation.engine.Apply(bench::Master + t, bench::kBase[bench::Master + t]);
    } else {
        for(std::size_t v = 0; v < count; ++v)
            for(std::size_t t = 0; t < 5; ++t)
                voice_param[v][t] = bench::kBase[t];
        for(std::size_t t = 0; t < 3; ++t)
            global_param[t] = bench::kBase[bench::Master + t];
    }
    const uint32_t matrix_cycles = Cycles() - matrix_start;

    // ---- the bench instrument (not measured as matrix share) ----
    float gain_l[bench::kMaxVoices], gain_r[bench::kMaxVoices];
    const float voice_gain = 0.25f / std::sqrt(float(count));
    for(std::size_t v = 0; v < count; ++v) {
        const int note = voice_note[v].load(std::memory_order_relaxed);
        const float semitones = float(note < 0 ? 48 + 3 * int(v % 12) : note) - 69.0f
            + (voice_param[v][bench::Pitch] - 0.5f) * 24.0f;
        voices[v].increment = 440.0f * std::exp2(semitones / 12.0f) / kSampleRate;
        const float pan = voice_param[v][bench::Pan];
        const float level = voice_param[v][bench::Level] * voice_gain;
        gain_l[v] = level * (1.0f - pan);
        gain_r[v] = level * pan;
    }
    const float master = global_param[0], width = global_param[1], drive = 1.0f + 4.0f * global_param[2];
    for(std::size_t i = 0; i < size; ++i) {
        float l = 0.0f, r = 0.0f;
        for(std::size_t v = 0; v < count; ++v) {
            Voice& voice = voices[v];
            voice.phase += voice.increment;
            if(voice.phase >= 1.0f)
                voice.phase -= 1.0f;
            float h2 = voice.phase * 2.0f, h3 = voice.phase * 3.0f;
            h2 -= float(int(h2));
            h3 -= float(int(h3));
            const float s = mod::SinePhase(voice.phase) + voice_param[v][bench::Harmonic] * mod::SinePhase(h2)
                + voice_param[v][bench::Tone] * 0.5f * mod::SinePhase(h3);
            l += s * gain_l[v];
            r += s * gain_r[v];
        }
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width * 2.0f;
        out[0][i] = master * std::tanh(drive * (mid + side)) / drive;
        out[1][i] = master * std::tanh(drive * (mid - side)) / drive;
    }

    load_meter.OnBlockEnd();
    const uint32_t total = Cycles() - start;
    AudioStats& a = audio_stats;
    ++a.ticks;
    a.sum += matrix_cycles;
    a.min = matrix_cycles < a.min ? matrix_cycles : a.min;
    a.max = matrix_cycles > a.max ? matrix_cycles : a.max;
    if(total > budget_cycles)
        ++a.overruns;
    if((a.ticks & 63) == 0) {
        mod::Stats& s = stats_box.Back();
        s.ticks = a.ticks;
        s.cycles_min = a.min;
        s.cycles_max = a.max;
        s.cycles_avg = uint32_t(a.sum / a.ticks);
        s.load_avg = uint32_t(load_meter.GetAvgCpuLoad() * 10000.0f + 0.5f);
        s.load_max = uint32_t(load_meter.GetMaxCpuLoad() * 10000.0f + 0.5f);
        s.overruns = a.overruns;
        stats_box.Publish();
    }
}

bool ApplyScenario(uint8_t number) {
    bench::Scenario s;
    if(!bench::MakeScenario(number, s) || modulation.controller.LoadPatch(s.patch) != mod::Status::Ok)
        return false;
    voice_count.store(uint32_t(s.voices), std::memory_order_relaxed);
    bypass.store(s.bypass, std::memory_order_relaxed);
    current_scenario = number;
    midi_lost = dispatch_max = service_max = 0;
    reset_stats.store(true, std::memory_order_relaxed);
    return true;
}

struct BenchDiagnostics : mod::Diagnostics {
    mod::Stats GetStats() override {
        stats_box.Fetch();
        mod::Stats s = stats_box.Front();
        s.midi_lost = midi_lost;
        s.dispatch_max = dispatch_max;
        s.service_max = service_max;
        return s;
    }
    void ResetStats() override {
        midi_lost = dispatch_max = service_max = 0;
        reset_stats.store(true, std::memory_order_relaxed);
    }
    bool SetScenario(uint8_t scenario) override { return ApplyScenario(scenario); }
} diagnostics;

// Plain-CC fallback in case SysEx replies do not arrive (TEST_PLAN H3): request
// with CC119=1 on channel 16, reply CC100..119 on channel 16 (bench/README.md).
void SendCcStats() {
    const mod::Stats s = diagnostics.GetStats();
    uint8_t m[60];
    for(unsigned i = 0; i < 20; ++i) {
        m[3 * i] = 0xBF;
        m[3 * i + 1] = uint8_t(100 + i);
        m[3 * i + 2] = 0;
    }
    auto put = [&](unsigned cc, uint32_t value, unsigned digits) {
        for(unsigned i = 0; i < digits; ++i)
            m[3 * (cc - 100 + i) + 2] = uint8_t((value >> (7 * i)) & 127);
    };
    put(100, 1, 1);
    put(101, s.cycles_max, 3);
    put(104, s.cycles_avg, 3);
    put(107, s.load_max, 3);
    put(110, s.overruns, 3);
    put(113, current_scenario, 1);
    put(114, s.load_avg, 3);
    put(117, s.midi_lost > 127 ? 127 : s.midi_lost, 1);
    put(118, 0, 1);
    put(119, 64, 1); // end marker, never a request value
    sender.Send(m, sizeof(m));
}

uint8_t next_voice = 0;
int last_sequence = -1;
void BenchMidi(const mod::MidiMessage& m) {
    const uint8_t type = m.status & 0xF0;
    if(type == 0x90 && m.data1) {
        voice_note[next_voice].store(int8_t(m.data0), std::memory_order_relaxed);
        next_voice = uint8_t((next_voice + 1) % bench::kMaxVoices);
    } else if(type == 0xB0 && m.data0 == 3) { // numbered test stream: count gaps
        if(last_sequence >= 0 && m.data1 != ((last_sequence + 1) & 127))
            ++midi_lost;
        last_sequence = m.data1;
    } else if(m.status == 0xBF && m.data0 == 119 && m.data1 == 1) {
        SendCcStats();
    }
}
}

int main() {
    asm volatile("" : : "r"(kFirmwareId));
    hw.Init();
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(kBlock);
    if(hw.seed.AudioBlockSize() != kBlock)
        while(true) {} // reject silent driver clamping
    EnableCycleCounter();
    budget_cycles = uint32_t(float(SystemCoreClock) * float(kBlock) / kSampleRate);
    load_meter.Init(hw.seed.AudioSampleRate(), int(kBlock));
    for(auto& n : voice_note)
        n.store(-1, std::memory_order_relaxed);
    modulation.Init(kInfo, hw.seed.AudioSampleRate(), kBlock, 0, &diagnostics);
    ApplyScenario(1);
    for(unsigned i = 0; i < 100; ++i) { // let the ADC settle before the CV inputs are used
        for(auto& cv : hw.cv)
            cv.Process();
        daisy::System::Delay(1);
    }
    daisy::MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = daisy::MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_config);
    usb_midi.StartReceive();
    hw.seed.StartAudio(AudioCallback);

    uint8_t shown = 0xFF;
    uint32_t last_led = 0;
    while(true) {
        usb_midi.Listen();
        const uint32_t dispatch_start = Cycles();
        while(usb_midi.HasEvents()) {
            const daisy::MidiEvent event = usb_midi.PopEvent();
            if(bootloader::IsRequest(event)) {
                boot_request.Set(bootloader::Request::kSysEx);
                continue;
            }
            mod::MidiMessage m;
            if(mod::adapter::ToMessage(event, m) && !modulation.controller.HandleMidi(m))
                BenchMidi(m);
        }
        const uint32_t dispatch = Cycles() - dispatch_start;
        dispatch_max = dispatch > dispatch_max ? dispatch : dispatch_max;
        const uint32_t now = daisy::System::GetNow();
        const uint32_t service_start = Cycles();
        modulation.controller.Service(now, sender);
        const uint32_t service = Cycles() - service_start;
        service_max = service > service_max ? service : service_max;

        bootloader::Service(boot_request, hw, [](uint8_t* bytes, std::size_t size) { sender.Send(bytes, size); });
        const uint32_t key = key_scenario.exchange(0xFF, std::memory_order_relaxed);
        if(key != 0xFF)
            ApplyScenario(uint8_t(key));
        if(current_scenario != shown && now - last_led >= 10) { // LED k = scenario k
            last_led = now;
            shown = current_scenario;
            for(int i = 0; i < 16; ++i)
                hw.SetLed(i, i == shown ? 1.0f : 0.0f);
            hw.UpdateLeds();
        }
    }
}
