#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "modulation.h"
#include "mod_on_daisy_desktop.h"
#include "spsc_queue.h"
#include "toy_piano.h"
#include "usbd_core.h"
#include "version.h"
#include <atomic>
#if TOY_PIANO_FX
#include "ambient_host.h"
#include "../../../effects/reverb/src/rate_control.h"
#include "callback_diagnostics.h"
#include <new>
#elif TOY_PIANO_TAPE
#include "tape_host.h"
#include "callback_diagnostics.h"
#include <new>
#elif TOY_PIANO_FV1
#include "fv1_host.h"
#include "callback_diagnostics.h"
#include <new>
#if FV1VM_LOCAL_PROGRAMS
#include "fv1vm_programs.local.h" // generated, git-ignored (effects/fv1-vm/tools/gen_programs.py)
#endif
#elif TOY_PIANO_FILTER
#include "filter_host.h"
#include "callback_diagnostics.h"
#include <new>
#elif TOY_PIANO_CUMULONIMBUS
#include "cumulonimbus_host.h"
#include "callback_diagnostics.h"
#include "../../../effects/cumulonimbus/src/stage_meter.h"
#if TOY_PIANO_CUMULONIMBUS_PROFILE
#include "../../../effects/cumulonimbus/src/board_stack.h"
#endif
#include <new>
#endif
#define TOY_PIANO_MODULATION 1 // tests: call InitModulation() before the callback
// Regular Toy Piano (0.5), toy-piano-fv1 and Cumulonimbus: transposition and DIN MIDI input. Ambient and tape keep their own panel.
#define TOY_PIANO_PITCH_AND_DIN (!TOY_PIANO_FX && !TOY_PIANO_TAPE)

// libDaisy's USB device state for the external port (J7 on the carrier).
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace {
// Identifies a flashed image: `strings ToyPiano-v*.bin | grep TOYPIANO`.
// scripts/check-firmware.py compares it with src/version.h.
#if TOY_PIANO_FX
const char kFirmwareId[] = "TOYPIANO-AMBIENT-FW-VERSION=" TOY_PIANO_AMBIENT_VERSION;
#elif TOY_PIANO_TAPE
const char kFirmwareId[] = "TOYPIANO-TAPE-FW-VERSION=" TOY_PIANO_TAPE_VERSION;
#elif TOY_PIANO_FV1
const char kFirmwareId[] = "TOYPIANO-FV1-FW-VERSION=" TOY_PIANO_FV1_VERSION;
#if FV1VM_LOCAL_PROGRAMS
const char kProgramSource[] = "FV1VM-PROGRAMS=LOCAL-NOT-FOR-RELEASE";
#else
const char kProgramSource[] = "FV1VM-PROGRAMS=BUILTIN";
#endif
#elif TOY_PIANO_FILTER
const char kFirmwareId[] = "TOYPIANO-FILTER-FW-VERSION=" TOY_PIANO_FILTER_VERSION;
#elif TOY_PIANO_CUMULONIMBUS
const char kFirmwareId[] = "TOYPIANO-CUMULONIMBUS-FW-VERSION=" TOY_PIANO_CUMULONIMBUS_VERSION;
#define TP_STR2(x) #x
#define TP_STR(x) TP_STR2(x)
const char kBuildId[] = "TOYPIANO-CUMULONIMBUS-BUILD=RATE" TP_STR(CUMULONIMBUS_INTERNAL_RATE) " PROFILE="
    TP_STR(TOY_PIANO_CUMULONIMBUS_PROFILE) " COMPONENT=" CUMULONIMBUS_VERSION " CHAIN=" CUMULONIMBUS_CHAIN_VERSION;
#else
const char kFirmwareId[] = "TOYPIANO-FW-VERSION=" TOY_PIANO_VERSION;
#endif
daisy::desktop_devkit::Hardware hw;
toy_piano::ToyPiano piano;
#if TOY_PIANO_FX
alignas(toy_piano::AmbientHost) unsigned char DSY_SDRAM_BSS fx_storage[sizeof(toy_piano::AmbientHost)];
toy_piano::AmbientHost* fx = nullptr;
mod::adapter::CallbackDiagnostics callback_diagnostics;
#if AMBIENT_COMPACT_48K
// Diagnostic: all-pass and pitch lines in AXI SRAM behind the image (NOLOAD,
// cleared by Prepare). Requires ld/toy_piano_axi.lds.
#if defined(__arm__)
__attribute__((section(".tp_axi_bss"), aligned(32)))
#endif
ambient::FastMemory fast_memory;
#endif
bool InitializeFx() {
    std::fill_n(fx_storage, sizeof(fx_storage), static_cast<unsigned char>(0));
    fx = new(fx_storage) toy_piano::AmbientHost{};
#if AMBIENT_COMPACT_48K
    return fx->Prepare(&fast_memory);
#else
    return fx->Prepare();
#endif
}
#if AMBIENT_SDRAM_WRITE_ALLOCATE && defined(__arm__)
// Diagnostic: libDaisy maps SDRAM write-back without write-allocate, so every
// delay-line write is a single bus transaction. Same region, TEX=1 adds
// write-allocate. Only fx_storage lives in SDRAM; no DMA touches it.
void EnableSdramWriteAllocate() {
    SCB_CleanInvalidateDCache();
    HAL_MPU_Disable();
    MPU_Region_InitTypeDef region{};
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER1;
    region.BaseAddress = 0xC0000000;
    region.Size = MPU_REGION_SIZE_64MB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    region.IsCacheable = MPU_ACCESS_CACHEABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.SubRegionDisable = 0x00;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
#endif
#elif TOY_PIANO_TAPE
// Host incl. the 16 KiB delay lines in SDRAM: default .bss would be the nearly full DTCM.
alignas(toy_piano::TapeHost) unsigned char DSY_SDRAM_BSS fx_storage[sizeof(toy_piano::TapeHost)];
toy_piano::TapeHost* tape = nullptr;
mod::adapter::CallbackDiagnostics callback_diagnostics;
bool InitializeFx() {
    std::fill_n(fx_storage, sizeof(fx_storage), static_cast<unsigned char>(0));
    tape = new(fx_storage) toy_piano::TapeHost{};
    return tape->Prepare();
}
#elif TOY_PIANO_FV1
// Host incl. the 128 KiB FV-1 delay RAM in AXI SRAM behind the image (NOLOAD, cleared in InitializeFx; needs
// ld/toy_piano_axi.lds): the interpreter and the resamplers read it every sample, and SDRAM is much slower.
#if defined(__arm__)
__attribute__((section(".tp_axi_bss"), aligned(32)))
#endif
alignas(toy_piano::Fv1Host) unsigned char fx_storage[sizeof(toy_piano::Fv1Host)];
toy_piano::Fv1Host* fv1 = nullptr;
mod::adapter::CallbackDiagnostics callback_diagnostics;
// Main loop -> audio thread: program and telemetry requests (-1 none; fv1_firmware.h protocol).
std::atomic<int32_t> fv1_program_request{-1}, fv1_telemetry_request{-1};
// Audio thread -> main loop: one telemetry snapshot (release/acquire hand-off).
fv1_fw::Meter fv1_meter;
fv1_fw::Snapshot fv1_snapshot;
fv1_fw::Status fv1_status; // live controls, taken with the snapshot (same hand-off)
std::atomic<bool> fv1_snapshot_ready{false};
// Piano first (0.5.0): VR1 transposition (+-24 semitones), VR2 attack and VR3 decay.
toy_piano::PotFilter pitch_pot, attack_pot, decay_pot;
float piano_transpose = 1.f; // audio thread only; exactly 1 in the centre detent
bool InitializeFx() {
    std::fill_n(fx_storage, sizeof(fx_storage), static_cast<unsigned char>(0));
    fv1 = new(fx_storage) toy_piano::Fv1Host{};
#if FV1VM_LOCAL_PROGRAMS
    return fv1->Prepare({fv1vm_local::kPrograms, fv1vm_local::kCount, fv1vm_local::kGroupOffset,
                         fv1vm_local::kGroupSize, fv1vm_local::kGroupCount});
#else
    return fv1->Prepare({});
#endif
}
#elif TOY_PIANO_FILTER
// VR1 pitch, VR2 wave, VR3 attack, VR4 decay; the rest is in filter_host.h.
// 32 filters do not fit into the nearly full DTCM: AXI SRAM behind the image (NOLOAD, needs
// ld/toy_piano_axi.lds), like the FV-1 delay RAM. Cached, so the running voices stay fast.
#if defined(__arm__)
__attribute__((section(".tp_axi_bss"), aligned(32)))
#endif
alignas(toy_piano::FilterHost) unsigned char filter_storage[sizeof(toy_piano::FilterHost)];
toy_piano::FilterHost* filter_host = nullptr;
void InitializeFilter() {
    filter_host = new(filter_storage) toy_piano::FilterHost{};
    filter_host->Prepare();
}
mod::adapter::CallbackDiagnostics callback_diagnostics;
float piano_transpose = 1.f; // audio thread only; exactly 1 in the centre detent
std::array<float, toy_piano::kNumVoices> voice_pressure_now{}; // audio thread only, for the cutoff column
unsigned test_setting = ~0u; // audio thread only: load test settings; a change starts a new peak
std::atomic<uint32_t> test_leds{0}; // audio -> main: page 2 shows the load test instead of the keys
#elif TOY_PIANO_CUMULONIMBUS
// PCM16 grain ring (2 s mono) in SDRAM; the host (grain state, Clouds tables) and the reverb's 64 KiB
// in AXI SRAM behind the image (NOLOAD, needs ld/toy_piano_axi.lds): DTCM is nearly full.
DSY_SDRAM_BSS seed::fx::Cumulonimbus::Storage grain_memory;
#if defined(__arm__)
__attribute__((section(".tp_axi_bss"), aligned(32)))
#endif
seed::fx::TorusReverb::Storage reverb_memory;
#if defined(__arm__)
__attribute__((section(".tp_axi_bss"), aligned(32)))
#endif
alignas(toy_piano::CumulonimbusHost) unsigned char cumulo_storage[sizeof(toy_piano::CumulonimbusHost)];
toy_piano::CumulonimbusHost* cumulo = nullptr;
mod::adapter::CallbackDiagnostics callback_diagnostics;
float piano_transpose = 1.f; // audio thread only; exactly 1 in the centre detent
float harmonic_base = toy_piano::kHarmonicBase, level_base = toy_piano::kLevelBase; // audio thread only
// Main loop -> audio: external register writes from MIDI CCs (a full queue drops the write and counts it).
toy_piano::SpscQueue<toy_piano::ExternalWrite, 32> external_writes;
std::atomic<uint32_t> control_drops{0}; // main writes; profiling reads at audio window boundaries
// Source rates between the Mod on Daisy patch (main loop) and the Piano page (audio): last-value mailboxes.
toy_piano::RateMailboxes rate_boxes;
toy_piano::RateAudio rate_audio; // audio thread only
toy_piano::RateSync rate_sync;   // main loop only
static_assert(std::atomic<uint32_t>::is_always_lock_free);
bool InitializeFx() {
    std::fill_n(cumulo_storage, sizeof(cumulo_storage), static_cast<unsigned char>(0));
    cumulo = new(cumulo_storage) toy_piano::CumulonimbusHost{};
    return cumulo->Prepare(grain_memory, reverb_memory);
}
#if TOY_PIANO_CUMULONIMBUS_PROFILE
namespace diag = cumulonimbus::diag;
diag::StageMeter meter;
diag::Requests requests;
diag::TelemetrySender telemetry;
toy_piano::PianoLoad piano_load;  // audio thread only
uint32_t base_midi = 0, base_control = 0, base_stale = 0; // audio-owned window baselines (R11)
uint8_t scenario = diag::kScenarioFull; // audio thread only
uint32_t voices_peak = 0;               // audio thread only
std::atomic<uint32_t> midi_overruns{0}; // main writes; audio reads at reset/snapshot boundaries
uint32_t Cycles() { return DWT->CYCCNT; }
// Audio thread: restarts the statistics in this very block.
void RestartStatistics() {
    meter.ResetNow();
    cumulo->GetChain().GrainEffect().ResetCounters();
    voices_peak = 0;
    // The ISR preempts main on this single-core board. Atomic totals are captured here, not
    // when main later notices a reset, so post-reset drops cannot be subtracted away.
    base_midi = midi_overruns.load(std::memory_order_relaxed);
    base_control = control_drops.load(std::memory_order_relaxed);
    base_stale = rate_boxes.Stale();
}
#endif
#else
// 0.5: VR1 transposition, VR2 attack, VR3 decay.
toy_piano::PotFilter pitch_pot, attack_pot, decay_pot;
float piano_transpose = 1.f; // audio thread only; exactly 1 in the centre detent
#endif
std::atomic<uint32_t> led_mask{0};
daisy::MidiUsbHandler usb_midi;
#if TOY_PIANO_FX
void SendFxControl(const uint8_t* bytes, std::size_t size) {
    if(hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED)
        usb_midi.SendMessage(const_cast<uint8_t*>(bytes), size);
}
ambient::RateControl fx_rate("toy-piano-amb", TOY_PIANO_AMBIENT_VERSION, true, SendFxControl);
#endif
struct NoteIn { uint8_t note, velocity; };
// Main loop -> audio callback: notes received over USB.
toy_piano::SpscQueue<NoteIn, toy_piano::kMidiQueueSize> midi_in;
// Keys the host has been told are down. Main loop only.
uint32_t sent_mask = 0;
// Mod on Daisy: matrix state, its USB reply path and Poly AT smoothing per voice.
mod::Module modulation;
const mod::InstrumentInfo kModInfo = toy_piano::MakeModInfo();
mod::adapter::UsbMidiSender mod_sender(usb_midi);
mod::VoicePressure voice_pressure[toy_piano::kNumVoices];
#if !TOY_PIANO_FX
bool modulating = false; // audio callback only
#endif
// Software entry into the Daisy bootloader: S9+S16 for 5 s, or SysEx over J7.
bootloader::KeyCombo boot_keys; // audio callback only
bootloader::Request boot_request;
static_assert(std::atomic<uint32_t>::is_always_lock_free, "LED mailbox must be lock-free");
static_assert(daisy::desktop_devkit::kNumButtons == toy_piano::kNumKeys);
static_assert(daisy::desktop_devkit::kNumLeds == toy_piano::kNumKeys);

void InitModulation(float sample_rate) {
#if TOY_PIANO_FX
    callback_diagnostics.Init(sample_rate, toy_piano::kBlockSize);
    modulation.Init(kModInfo, sample_rate, toy_piano::kBlockSize, 0, &callback_diagnostics);
    modulation.EnableExtensions({true, true, true});
#elif TOY_PIANO_TAPE || TOY_PIANO_FV1 || TOY_PIANO_FILTER
    callback_diagnostics.Init(sample_rate, toy_piano::kBlockSize);
    modulation.Init(kModInfo, sample_rate, toy_piano::kBlockSize, 0, &callback_diagnostics);
    modulation.EnableExtensions({true, true, false});
#elif TOY_PIANO_CUMULONIMBUS
    // Extended monitor, LFO cross modulation and the page/SW18 modes.
    callback_diagnostics.Init(sample_rate, toy_piano::kBlockSize);
    modulation.Init(kModInfo, sample_rate, toy_piano::kBlockSize, 0, &callback_diagnostics);
    modulation.EnableExtensions({true, true, true});
#else
    modulation.Init(kModInfo, sample_rate, toy_piano::kBlockSize);
    // v0.4: extended monitor (pot positions, ranges) and LFO cross modulation, like
    // Physical Modeling MOD. No instrument modes: the keyboard has no model switch.
    modulation.EnableExtensions({true, true, false});
#endif
}

// Per-voice pitch, level and harmonic from the matrix. Key voices have no Poly AT.
void ModulateVoices(size_t size) {
    const mod::Engine& engine = modulation.engine;
#if TOY_PIANO_FX
    const float base = fx->PitchBase();
    const float base_ratio = toy_piano::PitchRatio(base);
    for(size_t v = 0; v < toy_piano::kNumVoices; ++v) {
        const int note = v < toy_piano::kNumKeys ? -1 : piano.MidiVoiceNote(v - toy_piano::kNumKeys);
        const float pressure = voice_pressure[v].Process(engine, note);
        float ratio = engine.Active() ? toy_piano::PitchRatio(engine.ApplyVoice(toy_piano::ModPitch, base, pressure)) : base_ratio;
        const float level = engine.ApplyVoice(toy_piano::ModLevel, toy_piano::kLevelBase, pressure);
        const float harmonic = engine.ApplyVoice(toy_piano::ModHarmonic, toy_piano::kHarmonicBase, pressure);
        float gain = toy_piano::LevelGain(level);
        const float increment = piano.BaseIncrement(v);
        if(increment * ratio >= .5f) {
            ratio = .49f / increment;
            gain = 0; // Extreme MIDI notes beyond Nyquist fade silent instead of folding.
        }
        piano.Modulate(v, ratio, gain, toy_piano::HarmonicWeight(harmonic), size);
    }
#elif TOY_PIANO_CUMULONIMBUS
    // Piano page: transposition, harmonic and level bases. At the reference values and without an active
    // matrix the voices render exactly as in the regular Toy Piano.
    const bool reference = piano_transpose == 1.f && harmonic_base == toy_piano::kHarmonicBase &&
                           level_base == toy_piano::kLevelBase;
    if(!engine.Active() && reference) {
        if(modulating)
            piano.ClearModulation(size);
        modulating = false;
        return;
    }
    modulating = true;
    for(size_t v = 0; v < toy_piano::kNumVoices; ++v) {
        float ratio = piano_transpose, gain = toy_piano::LevelGain(level_base);
        float harmonic = toy_piano::HarmonicWeight(harmonic_base);
        if(engine.Active()) {
            const int note = v < toy_piano::kNumKeys ? -1 : piano.MidiVoiceNote(v - toy_piano::kNumKeys);
            const float p = voice_pressure[v].Process(engine, note);
            ratio *= toy_piano::PitchRatio(engine.ApplyVoice(toy_piano::ModPitch, toy_piano::kPitchBase, p));
            gain = toy_piano::LevelGain(engine.ApplyVoice(toy_piano::ModLevel, level_base, p));
            harmonic = toy_piano::HarmonicWeight(engine.ApplyVoice(toy_piano::ModHarmonic, harmonic_base, p));
        }
        const float increment = piano.BaseIncrement(v);
        if(increment * ratio >= .5f) {
            ratio = .49f / increment;
            gain = 0; // Notes transposed beyond Nyquist fade silent instead of folding.
        }
        piano.Modulate(v, ratio, gain, harmonic, size);
    }
#elif TOY_PIANO_PITCH_AND_DIN
    // VR1 transposition times the matrix pitch. Without transposition and without an active matrix the
    // voices render exactly as in the regular Toy Piano.
    if(!engine.Active() && piano_transpose == 1.f) {
        if(modulating)
            piano.ClearModulation(size);
        modulating = false;
        return;
    }
    modulating = true;
    if(!engine.Active()) {
        // Transposition only. The matrix path below costs about 130 000 cycles per block on the Seed3 for 32
        // voices (MEASURED 2026-11-05: 28 % of the budget without a sounding voice), so skip it while it is empty.
        const float harmonic = toy_piano::HarmonicWeight(toy_piano::kHarmonicBase);
        for(size_t v = 0; v < toy_piano::kNumVoices; ++v) {
            float ratio = piano_transpose, gain = toy_piano::LevelGain(toy_piano::kLevelBase);
            const float increment = piano.BaseIncrement(v);
            if(increment * ratio >= .5f) {
                ratio = .49f / increment;
                gain = 0; // Notes transposed beyond Nyquist fade silent instead of folding.
            }
            piano.Modulate(v, ratio, gain, harmonic, size);
        }
        return;
    }
    for(size_t v = 0; v < toy_piano::kNumVoices; ++v) {
        const int note = v < toy_piano::kNumKeys ? -1 : piano.MidiVoiceNote(v - toy_piano::kNumKeys);
        const float p = voice_pressure[v].Process(engine, note);
#if TOY_PIANO_FILTER
        voice_pressure_now[v] = p;
#endif
        float ratio = piano_transpose * toy_piano::PitchRatio(engine.ApplyVoice(toy_piano::ModPitch, toy_piano::kPitchBase, p));
        const float level = engine.ApplyVoice(toy_piano::ModLevel, toy_piano::kLevelBase, p);
        const float harmonic = engine.ApplyVoice(toy_piano::ModHarmonic, toy_piano::kHarmonicBase, p);
        float gain = toy_piano::LevelGain(level);
        const float increment = piano.BaseIncrement(v);
        if(increment * ratio >= .5f) {
            ratio = .49f / increment;
            gain = 0; // Notes transposed beyond Nyquist fade silent instead of folding.
        }
        piano.Modulate(v, ratio, gain, toy_piano::HarmonicWeight(harmonic), size);
    }
#else
    if(!engine.Active()) {
        if(modulating)
            piano.ClearModulation(size);
        modulating = false;
        return; // no active connection: the voices render exactly as in v0.2
    }
    modulating = true;
    for(size_t v = 0; v < toy_piano::kNumVoices; ++v) {
        const int note = v < toy_piano::kNumKeys ? -1 : piano.MidiVoiceNote(v - toy_piano::kNumKeys);
        const float p = voice_pressure[v].Process(engine, note);
        const float pitch = engine.ApplyVoice(toy_piano::ModPitch, toy_piano::kPitchBase, p);
        const float level = engine.ApplyVoice(toy_piano::ModLevel, toy_piano::kLevelBase, p);
        const float harmonic = engine.ApplyVoice(toy_piano::ModHarmonic, toy_piano::kHarmonicBase, p);
        piano.Modulate(v, toy_piano::PitchRatio(pitch), toy_piano::LevelGain(level),
                       toy_piano::HarmonicWeight(harmonic), size);
    }
#endif
}

void AudioCallback(daisy::AudioHandle::InputBuffer,
                   daisy::AudioHandle::OutputBuffer out, size_t size) {
#if TOY_PIANO_FV1
    const uint32_t fv1_start = DWT->CYCCNT;
#endif
#if TOY_PIANO_CUMULONIMBUS && TOY_PIANO_CUMULONIMBUS_PROFILE
    const uint32_t cycle_start = DWT->CYCCNT;
#endif
#if TOY_PIANO_FX || TOY_PIANO_TAPE || TOY_PIANO_FV1 || TOY_PIANO_FILTER || TOY_PIANO_CUMULONIMBUS
    if(size != toy_piano::kBlockSize) {
        for(size_t i = 0; i < std::min(size, toy_piano::kBlockSize); ++i) out[0][i] = out[1][i] = 0;
        return;
    }
    callback_diagnostics.Begin();
#endif
#if TOY_PIANO_CUMULONIMBUS && TOY_PIANO_CUMULONIMBUS_PROFILE
    meter.Begin(cycle_start, daisy::System::GetNow());
    {
        const int32_t s = requests.scenario.exchange(-1, std::memory_order_relaxed);
        if(s >= 0) {
            scenario = uint8_t(s);
            cumulo->GetChain().SetRoute(diag::RouteFor(scenario));
            RestartStatistics();
        }
        const int32_t c = requests.cap.exchange(-1, std::memory_order_relaxed);
        if(c >= 0) {
            cumulo->GetChain().SetCap(c);
            RestartStatistics();
        }
        const int32_t st = requests.stimulus.exchange(-1, std::memory_order_relaxed);
        if(st >= 0) {
            piano_load.Select(uint8_t(st));
            RestartStatistics();
        }
        if(requests.reset.exchange(false, std::memory_order_acq_rel))
            RestartStatistics();
    }
#endif
    hw.UpdateAllControls(); // Shift registers + official debouncing + ADC values.
    mod::adapter::ReadHardware(modulation.engine, hw);
    modulation.engine.Tick(size);
    // Attack and decay are latched at the strike; the matrix shifts the pot value.
#if TOY_PIANO_FX
    fx_rate.AudioTick();
    fx->SetReverbRate(fx_rate.RateHz());
    std::array<float, 8> raw{};
    for(size_t i = 0; i < raw.size(); ++i) raw[i] = hw.pot[i].Value();
    fx->ReadControls(raw, hw.tog_sw17.Pressed(), hw.tog_sw18.Read());
    const float attack = modulation.engine.Apply(toy_piano::ModAttack, 0.f);
    const float decay = modulation.engine.Apply(toy_piano::ModDecay, fx->DecayBase());
    modulation.engine.ReportBase(toy_piano::ModPitch, fx->PitchBase());
    modulation.engine.ReportBase(toy_piano::ModLevel, toy_piano::kLevelBase);
    modulation.engine.ReportBase(toy_piano::ModHarmonic, toy_piano::kHarmonicBase);
    modulation.engine.ReportBase(toy_piano::ModAttack, 0.f);
    modulation.engine.ReportBase(toy_piano::ModDecay, fx->DecayBase());
    modulation.engine.ReportBase(toy_piano::ModHallDecay, fx->ReverbDecayBase());
    modulation.engine.ReportBase(toy_piano::ModHallMix, fx->MixBase());
    modulation.engine.ReportBase(toy_piano::ModHallX, fx->XBase());
    modulation.engine.ReportMode(uint8_t(fx->SelectedMode()), mod::kModeFromDevice);
    fx->SetModulated(modulation.engine.Apply(toy_piano::ModHallDecay, fx->ReverbDecayBase()),
                     modulation.engine.Apply(toy_piano::ModHallMix, fx->MixBase()),
                     modulation.engine.Apply(toy_piano::ModHallX, fx->XBase()));
#elif TOY_PIANO_TAPE
    std::array<float, 8> raw{};
    for(size_t i = 0; i < raw.size(); ++i) raw[i] = hw.pot[i].Value();
    tape->ReadControls(raw, hw.tog_sw17.Pressed(), hw.tog_sw18.Read());
    // All pots belong to the tape: attack 2 ms and decay 1.5 s are fixed bases the
    // matrix can still modulate. Pitch, level and harmonic are the Toy Piano bases.
    const float attack = modulation.engine.Apply(toy_piano::ModAttack, 0.f);
    const float decay = modulation.engine.Apply(toy_piano::ModDecay, toy_piano::kTapeDecayBase);
    modulation.engine.ReportBase(toy_piano::ModPitch, toy_piano::kPitchBase);
    modulation.engine.ReportBase(toy_piano::ModLevel, toy_piano::kLevelBase);
    modulation.engine.ReportBase(toy_piano::ModHarmonic, toy_piano::kHarmonicBase);
    modulation.engine.ReportBase(toy_piano::ModAttack, 0.f);
    modulation.engine.ReportBase(toy_piano::ModDecay, toy_piano::kTapeDecayBase);
#elif TOY_PIANO_FV1
    std::array<float, 8> raw{};
    for(size_t i = 0; i < raw.size(); ++i) raw[i] = hw.pot[i].Value();
    // VR4..VR6 POT0..2, VR7 dry/wet, VR8 rate, SW17 bypass, SW18 POT0 override
    fv1->ReadControls(raw, hw.tog_sw17.Pressed(), hw.tog_sw18.Read());
    const int32_t fv1_program = fv1_program_request.exchange(-1, std::memory_order_acquire);
    const bool fv1_changed = fv1_program >= 0;
    if(fv1_changed) fv1->Select(unsigned(fv1_program));
    // Piano first: VR1 transposition +-24 semitones (centre detent as in toy-piano-ambient), VR2 attack and
    // VR3 decay with the ranges of the regular Toy Piano.
    const float pitch_x = pitch_pot.Process(raw[0]);
    piano_transpose = std::fabs(pitch_x - .5f) < .005f ? 1.f : std::exp2((pitch_x - .5f) * 4.f);
    const float attack_base = attack_pot.Process(raw[1]);
    const float decay_base = decay_pot.Process(raw[2]);
    const float attack = modulation.engine.Apply(toy_piano::ModAttack, attack_base);
    const float decay = modulation.engine.Apply(toy_piano::ModDecay, decay_base);
    modulation.engine.ReportBase(toy_piano::ModPitch, toy_piano::kPitchBase);
    modulation.engine.ReportBase(toy_piano::ModLevel, toy_piano::kLevelBase);
    modulation.engine.ReportBase(toy_piano::ModHarmonic, toy_piano::kHarmonicBase);
    modulation.engine.ReportBase(toy_piano::ModAttack, attack_base);
    modulation.engine.ReportBase(toy_piano::ModDecay, decay_base);
#elif TOY_PIANO_FILTER
    std::array<float, 8> raw{};
    for(size_t i = 0; i < raw.size(); ++i) raw[i] = hw.pot[i].Value();
    filter_host->ReadControls(raw, hw.tog_sw17.Pressed(), hw.tog_sw18.Read());
    const toy_piano::FilterControls& panel = filter_host->Controls();
    piano_transpose = std::fabs(panel.pitch - .5f) < .005f ? 1.f : std::exp2((panel.pitch - .5f) * 4.f);
    const float attack = modulation.engine.Apply(toy_piano::ModAttack, panel.attack);
    const float decay = modulation.engine.Apply(toy_piano::ModDecay, panel.decay);
    modulation.engine.ReportBase(toy_piano::ModPitch, toy_piano::kPitchBase);
    modulation.engine.ReportBase(toy_piano::ModLevel, toy_piano::kLevelBase);
    modulation.engine.ReportBase(toy_piano::ModHarmonic, toy_piano::kHarmonicBase);
    modulation.engine.ReportBase(toy_piano::ModAttack, panel.attack);
    modulation.engine.ReportBase(toy_piano::ModDecay, panel.decay);
    modulation.engine.ReportBase(toy_piano::ModCutoff, panel.cutoff);
    modulation.engine.ReportBase(toy_piano::ModResonance, panel.resonance);
    modulation.engine.ReportBase(toy_piano::ModDrive, panel.drive);
#elif TOY_PIANO_CUMULONIMBUS
    std::array<float, 8> raw{};
    for(size_t i = 0; i < raw.size(); ++i) raw[i] = hw.pot[i].Value();
    cumulo->ReadControls(raw, hw.tog_sw17.Pressed(), hw.tog_sw18.Read());
    {
        toy_piano::ExternalWrite w;
        for(unsigned n = 0; n < 16 && external_writes.Pop(w); ++n)
            cumulo->Write(w);
    }
    toy_piano::CumulonimbusPanel& panel = cumulo->Panel();
    rate_audio.Apply(panel, rate_boxes);
    rate_audio.Publish(panel, rate_boxes);
    piano_transpose = toy_piano::TransposeRatio(panel.Value(toy_piano::kDestTranspose));
    harmonic_base = panel.Value(toy_piano::kDestHarmonic);
    level_base = panel.Value(toy_piano::kDestLevel);
    const float attack_base = panel.Value(toy_piano::kDestAttack);
    const float decay_base = panel.Value(toy_piano::kDestDecay);
    const float attack = modulation.engine.Apply(toy_piano::ModAttack, attack_base);
    const float decay = modulation.engine.Apply(toy_piano::ModDecay, decay_base);
    const float position = panel.Value(toy_piano::kDestPosition);
    const float grain_mix = panel.Value(toy_piano::kDestGrainMix);
    const float hall_mix = panel.Value(toy_piano::kDestHallMix);
    modulation.engine.ReportBase(toy_piano::ModPitch, toy_piano::kPitchBase);
    modulation.engine.ReportBase(toy_piano::ModLevel, level_base);
    modulation.engine.ReportBase(toy_piano::ModHarmonic, harmonic_base);
    modulation.engine.ReportBase(toy_piano::ModAttack, attack_base);
    modulation.engine.ReportBase(toy_piano::ModDecay, decay_base);
    modulation.engine.ReportBase(toy_piano::ModGrainPosition, position);
    modulation.engine.ReportBase(toy_piano::ModGrainMix, grain_mix);
    modulation.engine.ReportBase(toy_piano::ModHallMix, hall_mix);
    modulation.engine.ReportMode(uint8_t((panel.CurrentPage() == toy_piano::kPageFx ? 3 : 0) +
                                         (panel.Mode() == seed::fx::ChainMode::Freeze ? 1 :
                                          panel.Mode() == seed::fx::ChainMode::Bypass ? 2 : 0)),
                                 mod::kModeFromDevice);
    cumulo->SetModulated(modulation.engine.Apply(toy_piano::ModGrainPosition, position),
                         modulation.engine.Apply(toy_piano::ModGrainMix, grain_mix),
                         modulation.engine.Apply(toy_piano::ModHallMix, hall_mix));
#else
    // 0.5: VR1 transposition +-24 semitones (centre detent), VR2 attack, VR3 decay.
    const float pitch_x = pitch_pot.Process(hw.pot[0].Value());
    piano_transpose = std::fabs(pitch_x - .5f) < .005f ? 1.f : std::exp2((pitch_x - .5f) * 4.f);
    const float attack_base = attack_pot.Process(hw.pot[1].Value());
    const float decay_base = decay_pot.Process(hw.pot[2].Value());
    const float attack = modulation.engine.Apply(toy_piano::ModAttack, attack_base);
    const float decay = modulation.engine.Apply(toy_piano::ModDecay, decay_base);
    // Column values before modulation for the editor (extended monitor): fixed bases
    // for the voice targets, the pot positions for attack and decay.
    modulation.engine.ReportBase(toy_piano::ModPitch, toy_piano::kPitchBase);
    modulation.engine.ReportBase(toy_piano::ModLevel, toy_piano::kLevelBase);
    modulation.engine.ReportBase(toy_piano::ModHarmonic, toy_piano::kHarmonicBase);
    modulation.engine.ReportBase(toy_piano::ModAttack, attack_base);
    modulation.engine.ReportBase(toy_piano::ModDecay, decay_base);
#endif
    // Only convert times on a new note, then latch them for that one shot.
    static uint32_t held = 0;
    uint32_t triggers = 0;
    for(size_t key = 0; key < toy_piano::kNumKeys; ++key) {
        const uint32_t bit = 1u << key;
        // DigitalControl may report another rise after a brief contact dropout.
        if(hw.button[key].RisingEdge() && !(held & bit)) {
            triggers |= bit;
            held |= bit;
        }
        if(hw.button[key].FallingEdge())
            held &= ~bit; // No release event is sent to the envelope.
    }
    // Note-offs are ignored on purpose: every note is a one-shot AD envelope.
    NoteIn notes[toy_piano::kMaxMidiPerBlock];
    size_t num_notes = 0;
    while(num_notes < toy_piano::kMaxMidiPerBlock && midi_in.Pop(notes[num_notes]))
        ++num_notes;
    if(triggers || num_notes) {
        const float attack_seconds = toy_piano::LogTime(attack,
            toy_piano::kAttackMin, toy_piano::kAttackMax);
        const float decay_seconds = toy_piano::LogTime(decay,
            toy_piano::kDecayMin, toy_piano::kDecayMax);
        for(size_t key = 0; key < toy_piano::kNumKeys; ++key)
            if(triggers & (1u << key))
                piano.Trigger(key, attack_seconds, decay_seconds);
        for(size_t i = 0; i < num_notes; ++i)
            piano.NoteOn(notes[i].note, notes[i].velocity, attack_seconds, decay_seconds);
    }
    led_mask.store(held, std::memory_order_relaxed);
    if(boot_keys.Process(held, daisy::System::GetNow()))
        boot_request.Set(bootloader::Request::kKeys);
    ModulateVoices(size);
#if TOY_PIANO_FX
    std::array<float, toy_piano::kBlockSize> mono{};
    for(size_t i = 0; i < size; ++i) mono[i] = piano.Process();
    fx->Process(mono.data(), out[0], out[1], size);
    callback_diagnostics.End();
#elif TOY_PIANO_TAPE
    std::array<float, toy_piano::kBlockSize> mono{};
    for(size_t i = 0; i < size; ++i) mono[i] = piano.Process();
    tape->Process(mono.data(), out[0], out[1], size);
    callback_diagnostics.End();
#elif TOY_PIANO_FV1
    std::array<float, toy_piano::kBlockSize> mono{};
    for(size_t i = 0; i < size; ++i) mono[i] = piano.Process();
    fv1->Process(mono.data(), out[0], out[1], size);
    callback_diagnostics.End();
    // Whole-callback cycles; a program-change block (delay-RAM clear) is not measured.
    int32_t fv1_request = fv1_telemetry_request.load(std::memory_order_relaxed);
    if(fv1_request == 2 || fv1_changed) fv1_meter.Reset();
    if(!fv1_changed) fv1_meter.Add(DWT->CYCCNT - fv1_start);
    if(fv1_request > 0 && !fv1_snapshot_ready.load(std::memory_order_acquire) &&
       fv1_telemetry_request.compare_exchange_strong(fv1_request, -1, std::memory_order_relaxed)) {
        fv1_snapshot = fv1_meter.Take(uint16_t(fv1->Program()),
                                      fv1->LocalPrograms() ? uint8_t(fv1_fw::kLocalPrograms) : uint8_t(0));
        fv1_status.rate_hz = fv1->Rate();
        fv1_status.transpose_cents = 1200.f * std::log2(piano_transpose);
        fv1_status.mix = fv1->Mix();
        fv1_status.bypass = fv1->Bypass();
        fv1_snapshot_ready.store(true, std::memory_order_release);
    }
#elif TOY_PIANO_FILTER
    filter_host->HoldLoad(piano);
    // Matrix columns 5 to 7. The cutoff column reaches every voice; only it needs the loop.
    const mod::Engine& engine = modulation.engine;
    std::array<float, toy_piano::kNumVoices> cutoff;
    for(size_t v = 0; v < cutoff.size(); ++v)
        cutoff[v] = engine.Active() ? engine.ApplyVoice(toy_piano::ModCutoff, panel.cutoff, voice_pressure_now[v]) : panel.cutoff;
    filter_host->SetFilter(cutoff, engine.Apply(toy_piano::ModResonance, panel.resonance),
                          engine.Apply(toy_piano::ModDrive, panel.drive));
    filter_host->Process(piano, out[0], size);
    for(size_t i = 0; i < size; ++i) out[1][i] = out[0][i];
    // A new load test setting starts a new peak, so the LEDs and `stats` show this setting alone.
    const unsigned setting = panel.load_voices * 16 + unsigned(panel.model) * 2 + (panel.shared ? 1 : 0) + (panel.page2 ? 4096 : 0);
    if(setting != test_setting) {
        test_setting = setting;
        callback_diagnostics.ResetStats();
    }
    callback_diagnostics.End();
    test_leds.store(panel.page2 ? (panel.load_voices / 4) | 0x100u : 0u, std::memory_order_relaxed);
#elif TOY_PIANO_CUMULONIMBUS
    std::array<float, toy_piano::kBlockSize> mono{};
#if TOY_PIANO_CUMULONIMBUS_PROFILE
    piano_load.Process(piano);
    const uint32_t piano_start = DWT->CYCCNT;
#endif
    for(size_t i = 0; i < size; ++i) mono[i] = piano.Process();
#if TOY_PIANO_CUMULONIMBUS_PROFILE
    meter.Add(diag::kPiano, DWT->CYCCNT - piano_start);
#endif
    cumulo->Process(mono.data(), out[0], out[1], size);
    callback_diagnostics.End();
#if TOY_PIANO_CUMULONIMBUS_PROFILE
    {
        const auto& chain = cumulo->GetChain();
        const auto t = chain.GrainEffect().Telemetry();
        meter.Add(diag::kGrainWrite, t.write_cycles);
        meter.Add(diag::kGrainRender, t.render_cycles);
        meter.Add(diag::kConvert, t.convert_cycles);
        meter.Add(diag::kReverb, chain.ReverbCycles());
        const uint32_t voices = toy_piano::ActiveVoices(piano);
        if(voices > voices_peak) voices_peak = voices;
        meter.SetApp(diag::kScenario, scenario);
        meter.SetApp(diag::kCap, uint32_t(chain.GrainEffect().Cap()));
        meter.SetApp(diag::kStimulus, piano_load.Pattern());
        meter.SetApp(diag::kInternalRate, CUMULONIMBUS_INTERNAL_RATE);
        meter.SetApp(diag::kBlockFrames, toy_piano::kBlockSize);
        meter.SetApp(diag::kGrainsActive, uint32_t(t.active));
        meter.SetApp(diag::kGrainsPeak, uint32_t(t.peak_active));
        meter.SetApp(diag::kRejected, t.rejected);
        meter.SetApp(diag::kStartedHigh, t.started_high);
        meter.SetApp(diag::kStartedMedium, t.started_medium);
        meter.SetApp(diag::kVoicesActive, voices);
        meter.SetApp(diag::kVoicesPeak, voices_peak);
        uint32_t flags = diag::kFlagProfiling | diag::kFlagMono |
                         (CUMULONIMBUS_INTERNAL_RATE == 32000 ? uint32_t(diag::kFlagRate32) : 0u);
        flags |= chain.Mode() == seed::fx::ChainMode::Bypass ? uint32_t(diag::kFlagBypass) : 0u;
        flags |= chain.Mode() == seed::fx::ChainMode::Freeze ? uint32_t(diag::kFlagFreeze) : 0u;
        flags |= chain.Suspended() ? uint32_t(diag::kFlagSuspended) : 0u;
        flags |= cumulo->Panel().CurrentPage() == toy_piano::kPageFx ? uint32_t(diag::kFlagFxPage) : 0u;
        meter.SetApp(diag::kFlags, flags);
        meter.SetApp(diag::kMidiOverruns, midi_overruns.load(std::memory_order_relaxed) - base_midi);
        meter.SetApp(diag::kControlDrops, control_drops.load(std::memory_order_relaxed) - base_control);
        meter.SetApp(diag::kStaleRates, rate_boxes.Stale() - base_stale);
        const uint32_t total = DWT->CYCCNT - cycle_start;
        meter.End(total);
        meter.AddAccounting(DWT->CYCCNT - cycle_start - total); // the meter's own work, outside kTotal
    }
#endif
#else
    for(size_t i = 0; i < size; ++i) {
        const float sample = piano.Process();
        out[0][i] = sample;
        out[1][i] = sample;
    }
#endif
}

// Sends the key changes since the last call as one USB transfer. It compares the
// debounced key state with what the host was told, so a note-off cannot get lost
// in a queue. Changes beyond the packet limit follow on the next call.
void SendKeyChanges(uint32_t mask, bool connected) {
    uint32_t changed = mask ^ sent_mask;
    if(!changed)
        return;
    if(!connected) { // Nobody listens: keys stay local.
        sent_mask = mask;
        return;
    }
    uint8_t message[3 * toy_piano::kMaxKeyMessagesPerPacket];
    size_t size = 0;
    for(size_t key = 0; key < toy_piano::kNumKeys && size < sizeof(message); ++key) {
        const uint32_t bit = 1u << key;
        if(!(changed & bit))
            continue;
        const bool pressed = (mask & bit) != 0;
        message[size++] = static_cast<uint8_t>((pressed ? 0x90 : 0x80) | toy_piano::kMidiOutChannel);
        message[size++] = toy_piano::kMidiNotes[key];
        message[size++] = pressed ? toy_piano::kMidiOutVelocity : uint8_t{0};
        sent_mask ^= bit;
    }
    usb_midi.SendMessage(message, size);
}

#if TOY_PIANO_CUMULONIMBUS
#if TOY_PIANO_CUMULONIMBUS_PROFILE
bool snapshot_pending = false; // main loop only
#endif
// Main loop only: source rates between the patch and the Piano page, and the diagnostics telemetry.
void ServiceCumulonimbus(bool connected) {
    const mod::Patch& patch = modulation.controller.CurrentPatch();
    const uint16_t rates[toy_piano::kRateSources] = {patch.lfo[0].rate, patch.lfo[1].rate, patch.random.rate};
    rate_sync.FromPatch(rates, rate_boxes);
    uint16_t wanted[toy_piano::kRateSources];
    if(rate_sync.FromPots(rate_boxes, wanted)) {
        mod::Patch q = patch;
        q.lfo[0].rate = wanted[0];
        q.lfo[1].rate = wanted[1];
        q.random.rate = wanted[2];
        modulation.controller.LoadPatchLocal(q); // keeps the editor retry cache (MOD_ON_DAISY_LOCAL_EDITS)
    }
#if TOY_PIANO_CUMULONIMBUS_PROFILE
    if(snapshot_pending && meter.SnapshotReady() && !telemetry.Busy()) {
        diag::Metrics m{};
        diag::FillMeterMetrics(meter.Snapshot(), m); // statistics, window and metadata of one block
        meter.ReleaseSnapshot();
        m[diag::kStackFree] = cumulonimbus::board::StackFree();
        m[diag::kStackSize] = cumulonimbus::board::StackSize();
        telemetry.Start(m);
        snapshot_pending = false;
    }
    telemetry.Service([connected](const uint8_t* bytes, size_t size) {
        if(connected) usb_midi.SendMessage(const_cast<uint8_t*>(bytes), size);
    });
#else
    (void)connected;
#endif
}
#endif

// Main loop only: USB parsing happens in the USB interrupt, but sending blocks.
void ServiceUsbMidi() {
    usb_midi.Listen(); // Restarts reception after an overflow.
#if TOY_PIANO_PITCH_AND_DIN
    hw.midi.Listen(); // DIN MIDI in (5-pin), handled like USB below
#endif
#if TOY_PIANO_FV1
    // One remote state per input, so a Bank Select on one input never combines with a Program Change on the other.
    static fv1_fw::Remote fv1_remote_usb, fv1_remote_din;
#endif
#if TOY_PIANO_FX || TOY_PIANO_TAPE || TOY_PIANO_FV1
    static bool pending = false;
    static NoteIn next{};
    for(unsigned batch = 0; batch < 64; ++batch) {
        if(pending) {
            if(!midi_in.Push(next)) break;
            pending = false;
        }
#if !TOY_PIANO_FV1
        if(!usb_midi.HasEvents()) break;
#endif
#elif TOY_PIANO_PITCH_AND_DIN
    for(;;) {
#else
    while(usb_midi.HasEvents()) {
#endif
#if TOY_PIANO_PITCH_AND_DIN
        // USB first, then DIN MIDI (input only): notes, CC1 and aftertouch (and in toy-piano-fv1 Program
        // Change/Bank Select and the channel-16 CCs). SysEx (bootloader request, editors) is accepted only
        // over USB, which carries the replies.
        const bool from_usb = usb_midi.HasEvents();
        if(!from_usb && !hw.midi.HasEvents()) break;
        auto event = from_usb ? usb_midi.PopEvent() : hw.midi.PopEvent();
        if(!from_usb && event.type == daisy::SystemCommon) continue;
#else
        auto event = usb_midi.PopEvent();
#endif
        if(bootloader::IsRequest(event)) {
            boot_request.Set(bootloader::Request::kSysEx);
            continue;
        }
#if TOY_PIANO_FV1
        // Program Change (+ Bank Select CC0/CC32) on any channel; channel 16 CC22/23 program and CC119
        // telemetry. The other channel-16 remote commands are standalone-only and ignored here.
        fv1_fw::Remote& fv1_remote = from_usb ? fv1_remote_usb : fv1_remote_din;
        if(event.type == daisy::ProgramChange) {
            const fv1_fw::Action action = fv1_remote.HandleProgramChange(event.channel, event.data[0]);
            unsigned index; // groups of the local table: bank = group, program = position
            if(action.command == fv1_fw::Command::BankProgram && fv1->Resolve(action.index, action.value, index))
                fv1_program_request.store(int32_t(index), std::memory_order_release);
            continue;
        }
        if(event.type == daisy::ControlChange) {
            const fv1_fw::Action action = fv1_remote.Handle(event.channel, event.data[0], event.data[1]);
            if(action.command == fv1_fw::Command::Program)
                fv1_program_request.store(int32_t(action.value), std::memory_order_release);
            else if(action.command == fv1_fw::Command::Telemetry)
                fv1_telemetry_request.store(int32_t(action.value), std::memory_order_relaxed);
            if(event.channel == 15 && (action.command != fv1_fw::Command::None || event.data[0] == 22)) continue;
        }
#endif
#if TOY_PIANO_FX
        if(event.type == daisy::SystemCommon && event.sc_type == daisy::SystemExclusive &&
           fx_rate.HandleBody(event.sysex_data, event.sysex_message_len, daisy::System::GetNow()))
            continue;
#endif
#if TOY_PIANO_CUMULONIMBUS
        if(event.type == daisy::ControlChange) {
#if TOY_PIANO_CUMULONIMBUS_PROFILE
            const auto diagnostic = diag::HandleControlChange(requests, event.channel, event.data[0], event.data[1]);
            if(diagnostic.snapshot && !snapshot_pending) {
                meter.RequestSnapshot();
                snapshot_pending = true;
            }
            if(diagnostic.consumed)
                continue;
#endif
            toy_piano::ExternalWrite w;
            if(toy_piano::CcToWrite(event.channel, event.data[0], event.data[1], w)) {
                if(!external_writes.Push(w))
                    ++control_drops; // the pot keeps its value; counted for the board acceptance
                continue;
            }
        }
#endif
        mod::MidiMessage message; // Mod on Daisy sees its SysEx, CC1 and aftertouch.
        if(mod::adapter::ToMessage(event, message) && modulation.controller.HandleMidi(message))
            continue;
        if(event.type == daisy::NoteOn) {
            const auto note = event.AsNoteOn();
            if(note.velocity > 0) {
#if TOY_PIANO_CUMULONIMBUS && TOY_PIANO_CUMULONIMBUS_PROFILE
                if(!midi_in.Push({note.note, note.velocity}))
                    ++midi_overruns;
#elif TOY_PIANO_FX || TOY_PIANO_TAPE || TOY_PIANO_FV1
                if(!midi_in.Push({note.note, note.velocity})) {
                    next = {note.note, note.velocity};
                    pending = true;
                    break;
                }
#else
                midi_in.Push({note.note, note.velocity});
#endif
            }
        }
    }
    // Without an enumerated host the transmit path has no endpoint to use.
    const bool connected = hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED;
    SendKeyChanges(led_mask.load(std::memory_order_relaxed), connected);
    modulation.controller.Service(daisy::System::GetNow(), mod_sender); // at most one reply per ms
#if TOY_PIANO_CUMULONIMBUS
    ServiceCumulonimbus(connected);
#endif
#if TOY_PIANO_FX
    fx_rate.Service(daisy::System::GetNow(), connected);
#endif
#if TOY_PIANO_FV1
    if(fv1_snapshot_ready.load(std::memory_order_acquire)) {
        auto packet = fv1_fw::Encode(fv1_snapshot);
        auto status = fv1_fw::EncodeStatus(fv1_status); // CC80..89 right after the CPU packet
        fv1_snapshot_ready.store(false, std::memory_order_release);
        if(connected) {
            usb_midi.SendMessage(packet.data(), packet.size());
            usb_midi.SendMessage(status.data(), status.size());
        }
    }
#endif
}

#if TOY_PIANO_FILTER
// Main loop only. Page 1 shows the keys. Page 2 shows the load test: D1-D8 count the voices in
// fours, D9-D16 the peak callback load in steps of 12.5 %. All of D9-D16 means overrun territory.
uint32_t LedMask() {
    const uint32_t test = test_leds.load(std::memory_order_relaxed);
    if(!(test & 0x100u))
        return led_mask.load(std::memory_order_relaxed);
    const auto stats = callback_diagnostics.GetStats();
    const uint32_t bars = stats.overruns ? 8u : std::min<uint32_t>(8u, (uint32_t(stats.load_max) + 1249u) / 1250u);
    return ((1u << (test & 0xFFu)) - 1u) | (((1u << bars) - 1u) << 8);
}
#endif

// Main loop only: replies over USB if a host asked, then resets into the bootloader.
void ServiceBootloader() {
    bootloader::Service(boot_request, hw, [](uint8_t* bytes, size_t size) {
        if(hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED)
            usb_midi.SendMessage(bytes, size);
    });
}
}

int main() {
    asm volatile("" : : "r"(kFirmwareId)); // Keep the version string in the image.
#if TOY_PIANO_FV1
    asm volatile("" : : "r"(kProgramSource));
#endif
#if TOY_PIANO_CUMULONIMBUS
    asm volatile("" : : "r"(kBuildId));
#endif
    hw.Init();
#if TOY_PIANO_FX && AMBIENT_SDRAM_WRITE_ALLOCATE && defined(__arm__)
    EnableSdramWriteAllocate(); // Before any SDRAM content is initialized.
#endif
    hw.seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(toy_piano::kBlockSize);
    piano.Init(hw.seed.AudioSampleRate());
#if TOY_PIANO_FX || TOY_PIANO_TAPE || TOY_PIANO_FV1 || TOY_PIANO_CUMULONIMBUS
    if(!InitializeFx()) while(true) daisy::System::Delay(100);
#endif
#if TOY_PIANO_CUMULONIMBUS && TOY_PIANO_CUMULONIMBUS_PROFILE
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    meter.Init((SystemCoreClock / 48000u) * uint32_t(toy_piano::kBlockSize));
#if defined(__arm__)
    cumulonimbus::board::PaintFreeStack(); // before the audio interrupt runs
#endif
    cumulo->GetChain().SetClock(Cycles);
#endif
#if TOY_PIANO_FILTER
    InitializeFilter();
#endif
#if TOY_PIANO_FV1
    fv1_meter.Init((SystemCoreClock / 48000u) * uint32_t(toy_piano::kBlockSize));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
    InitModulation(hw.seed.AudioSampleRate());
    // Let the mux ADC settle and populate AnalogControl values before use.
    for(unsigned i = 0; i < 100; ++i) {
        for(auto& pot : hw.pot)
            pot.Process();
        daisy::System::Delay(1);
    }
    daisy::MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = daisy::MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_config);
    usb_midi.StartReceive();
#if TOY_PIANO_PITCH_AND_DIN
    hw.midi.StartReceive(); // DIN MIDI in; the UART is set up by hw.Init()
#endif
    hw.seed.StartAudio(AudioCallback);
    uint32_t last_led_update = daisy::System::GetNow();
    uint32_t previous_mask = 0;
    while(true) {
        ServiceUsbMidi();
        ServiceBootloader();
        const uint32_t now = daisy::System::GetNow();
        if(now - last_led_update >= toy_piano::kLedIntervalMs) {
            last_led_update = now;
#if TOY_PIANO_FILTER
            const uint32_t mask = LedMask();
#else
            const uint32_t mask = led_mask.load(std::memory_order_relaxed);
#endif
            if(mask != previous_mask) {
                for(size_t key = 0; key < toy_piano::kNumKeys; ++key)
                    hw.SetLed(key, (mask & (1u << key)) ? 1.0f : 0.0f);
                hw.UpdateLeds(); // I2C DMA/buffer swap stays outside audio IRQ.
                previous_mask = mask;
            }
        }
        daisy::System::Delay(1);
    }
}
