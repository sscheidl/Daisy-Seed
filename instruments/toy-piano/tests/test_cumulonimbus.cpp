// MIT. End-to-end host test of Toy Piano on Cumulonimbus: the production callback, both SW17 pages
// with pickup, external writes, the source-rate sync, Freeze/Run/Bypass, dry-reference equivalence
// and, in profiling builds, the scenario CCs and telemetry records.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

static bool in_audio = false;
static unsigned allocations = 0;
[[gnu::noinline]] void* operator new(std::size_t n) {
    if(in_audio) ++allocations;
    if(void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if(!(cond)) {                                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            ++failures;                                                                   \
        }                                                                                 \
    } while(0)

constexpr std::size_t kN = toy_piano::kBlockSize;
float left[kN]{}, right[kN]{};
void Tick(unsigned count = 1) {
    float* output[]{left, right};
    for(unsigned block = 0; block < count; ++block) {
        in_audio = true;
        AudioCallback(nullptr, output, kN);
        in_audio = false;
        ++daisy::System::now;
        ServiceUsbMidi();
        for(unsigned i = 0; i < kN; ++i) {
            CHECK(std::isfinite(left[i]) && std::isfinite(right[i]));
            CHECK(std::fabs(left[i]) <= 1.f && std::fabs(right[i]) <= 1.f);
        }
    }
}
void SendCc(unsigned channel, unsigned cc, unsigned value) {
    daisy::MidiEvent e;
    e.type = daisy::ControlChange;
    e.channel = int(channel);
    e.data[0] = uint8_t(cc);
    e.data[1] = uint8_t(value);
    usb_midi.incoming.push_back(e);
}
void SendNote(unsigned note, unsigned velocity) {
    daisy::MidiEvent e;
    e.type = daisy::NoteOn;
    e.data[0] = uint8_t(note);
    e.data[1] = uint8_t(velocity);
    usb_midi.incoming.push_back(e);
}
void SetPots(std::array<float, 8> x) {
    for(unsigned i = 0; i < 8; ++i) hw.pot[i].value = x[i];
}
toy_piano::CumulonimbusPanel& Panel() { return cumulo->Panel(); }

// 1. Defaults: the Piano page maps to the regular Toy Piano bases, the FX page starts low-wet.
void TestBootDefaults() {
    // Boot on the FX page: the Piano page keeps the reference bases, the FX page its defaults.
    hw.tog_sw17.value = true;
    hw.tog_sw18.value = 0; // centre: Run
    SetPots({.9f, .9f, .9f, .9f, .9f, .9f, .9f, .9f});
    Tick(30);
    CHECK(Panel().CurrentPage() == toy_piano::kPageFx);
    CHECK(Panel().Value(toy_piano::kDestHarmonic) == toy_piano::kHarmonicBase);
    CHECK(Panel().Value(toy_piano::kDestLevel) == toy_piano::kLevelBase);
    CHECK(Panel().Value(toy_piano::kDestTranspose) == .5f);
    CHECK(Panel().Value(toy_piano::kDestGrainMix) == seed::fx::FxDefaultPots()[4]); // not the pot (0.9)
    CHECK(piano_transpose == 1.f && harmonic_base == toy_piano::kHarmonicBase && level_base == toy_piano::kLevelBase);
    CHECK(std::string(kModInfo.key) == "toy-piano-cumulonimbus");
    CHECK(std::string(kModInfo.targets[toy_piano::ModGrainMix].name) == "GrainMix");
    CHECK(kModInfo.mode_count == 6);
}

// 1b. Booting on the Piano page: its pots take over, the FX page keeps its defaults, and the rate
// pots count as written so the patch follows them.
void TestPanelBoot() {
    toy_piano::CumulonimbusPanel panel;
    panel.Reset();
    std::array<float, 8> pots{};
    pots.fill(.7f);
    panel.Update(pots, false, 0);
    CHECK(panel.Value(toy_piano::kDestAttack) == .7f && panel.Value(toy_piano::kDestLfo2Rate) == .7f);
    CHECK(panel.Value(toy_piano::kDestGrainMix) == seed::fx::FxDefaultPots()[4]);
    CHECK(panel.TakePotWrite(toy_piano::kDestLfo1Rate) && panel.TakePotWrite(toy_piano::kDestRandomRate));
    CHECK(!panel.TakePotWrite(toy_piano::kDestLfo1Rate)); // taken once
    CHECK(!panel.TakePotWrite(toy_piano::kDestGrainMix));
}

// 2. Steady bypass and wet mixes at 0 both give the exact dry Toy Piano.
void TestDryReference() {
    hw.tog_sw18.value = seed::fx::kSwitch3Down; // FX Bypass
    Tick(seed::fx::GranularReverbChain::kTailBlocks + seed::fx::GranularReverbChain::kFadeBlocks + 5);
    CHECK(cumulo->GetChain().Suspended());
    toy_piano::ToyPiano reference;
    reference.Init();
    const unsigned keys[] = {0, 4, 7, 11, 15};
    uint32_t previous = led_mask.load();
    bool same = true;
    double energy = 0;
    const float attack = toy_piano::LogTime(Panel().Value(toy_piano::kDestAttack), toy_piano::kAttackMin, toy_piano::kAttackMax);
    const float decay = toy_piano::LogTime(Panel().Value(toy_piano::kDestDecay), toy_piano::kDecayMin, toy_piano::kDecayMax);
    for(unsigned block = 0; block < 1500; ++block) {
        for(auto& p : hw.pressed) p = false;
        if(block / 300 < 5 && block % 300 < 40) hw.pressed[keys[block / 300]] = true;
        Tick();
        const uint32_t held = led_mask.load();
        for(unsigned k = 0; k < 16; ++k)
            if((held & (1u << k)) && !(previous & (1u << k))) reference.Trigger(k, attack, decay);
        previous = held;
        for(unsigned i = 0; i < kN; ++i) {
            const float r = reference.Process();
            same = same && left[i] == r && right[i] == r;
            energy += double(r) * r;
        }
    }
    CHECK(energy > 1.0);
    CHECK(same);
    // Run with both mixes at 0 (CC74, CC77): after the 10 ms glide again the exact dry piano.
    hw.tog_sw18.value = 0;
    SendCc(0, 74, 0);
    SendCc(0, 77, 0);
    for(unsigned b = 0; b < 40; ++b) { // the reference keeps time with the firmware
        Tick();
        for(unsigned i = 0; i < kN; ++i) reference.Process();
    }
    CHECK(!cumulo->GetChain().Suspended());
    CHECK(Panel().Value(toy_piano::kDestGrainMix) == 0.f && Panel().Value(toy_piano::kDestHallMix) == 0.f);
    same = true;
    for(unsigned block = 0; block < 600; ++block) {
        for(auto& p : hw.pressed) p = false;
        if(block % 300 < 40) hw.pressed[2] = true;
        Tick();
        const uint32_t held = led_mask.load();
        for(unsigned k = 0; k < 16; ++k)
            if((held & (1u << k)) && !(previous & (1u << k))) reference.Trigger(k, attack, decay);
        previous = held;
        for(unsigned i = 0; i < kN; ++i) {
            const float r = reference.Process();
            if(same && !(left[i] == r && right[i] == r))
                std::printf("first difference block %u frame %u: %.9g %.9g ref %.9g\n", block, i, left[i], right[i], r);
            same = same && left[i] == r && right[i] == r;
        }
    }
    CHECK(same);
}

// 3. Wet: audible stereo spread from the mono piano and a tail after the notes stop.
void TestWetStereo() {
    SendCc(0, 74, 80);  // grain mix
    SendCc(0, 75, 127); // spread
    SendCc(0, 77, 60);  // hall mix
    SendCc(0, 70, 2);   // position: newest audio
    for(auto& p : hw.pressed) p = false;
    double side = 0, mid = 0;
    for(unsigned block = 0; block < 1500; ++block) {
        hw.pressed[(block / 100) % 16] = block % 100 < 20;
        if(block % 100 == 20) hw.pressed[(block / 100) % 16] = false;
        Tick();
        for(unsigned i = 0; i < kN; ++i) {
            side += std::fabs(left[i] - right[i]);
            mid += std::fabs(left[i] + right[i]);
        }
    }
    CHECK(mid > 1.0 && side / mid > .02);
    for(auto& p : hw.pressed) p = false;
    Tick(1500); // let the piano decay (3 s max) ...
    double tail = 0;
    SendNote(72, 100);
    Tick(400);
    for(unsigned block = 0; block < 100; ++block) {
        Tick();
        for(unsigned i = 0; i < kN; ++i) tail += double(left[i]) * left[i];
    }
    CHECK(tail > 1e-6);
    CHECK(cumulo->GetChain().GrainEffect().Telemetry().started_high + cumulo->GetChain().GrainEffect().Telemetry().started_medium > 0);
}

// 4. SW17 pickup in both directions and after external writes.
void TestPickup() {
    hw.tog_sw17.value = false; // Piano page
    SetPots({.3f, .5f, .5f, .2f, .5f, .4f, .4f, .4f});
    Tick(5);
    // Piano page: VR1 (attack) picks up only once it reaches the stored value.
    const float attack = Panel().Value(toy_piano::kDestAttack);
    hw.pot[0].value = attack > .5f ? attack - .3f : attack + .3f;
    Tick(250);
    if(!Panel().Picked(0))
        CHECK(Panel().Value(toy_piano::kDestAttack) == attack);
    hw.pot[0].value = attack; // cross the stored value
    Tick(250);
    hw.pot[0].value = .25f;
    Tick(250);
    CHECK(std::fabs(Panel().Value(toy_piano::kDestAttack) - .25f) < .01f);
    // FX page: the Piano values stay, VR5 (grain mix) keeps its value until reached.
    const float mix = Panel().Value(toy_piano::kDestGrainMix);
    hw.tog_sw17.value = true;
    hw.pot[4].value = mix < .5f ? .95f : .05f;
    Tick(250);
    CHECK(Panel().Value(toy_piano::kDestGrainMix) == mix);
    CHECK(std::fabs(Panel().Value(toy_piano::kDestAttack) - .25f) < .01f); // the Piano page is kept
    hw.pot[4].value = mix; // reach it
    Tick(250);
    hw.pot[4].value = .6f;
    Tick(250);
    CHECK(std::fabs(Panel().Value(toy_piano::kDestGrainMix) - .6f) < .01f);
    // Back to the Piano page: VR1 at .25 stays picked only after it is reached again; VR5 there is Level.
    const float level = Panel().Value(toy_piano::kDestLevel);
    hw.tog_sw17.value = false;
    Tick(250); // VR5 is at .6: Level must not jump to it
    if(std::fabs(level - .6f) > .05f)
        CHECK(Panel().Value(toy_piano::kDestLevel) == level);
    CHECK(std::fabs(Panel().Value(toy_piano::kDestGrainMix) - .6f) < .01f); // FX value kept
    // External write on the visible page: the pot must pick up again (no stale write).
    hw.tog_sw17.value = true;
    hw.pot[4].value = .6f;
    Tick(250); // VR5 at the stored .6: picked
    SendCc(3, 74, 25); // CC74 -> grain mix .197
    Tick(250);
    CHECK(std::fabs(Panel().Value(toy_piano::kDestGrainMix) - 25.f / 127.f) < 1e-6f);
    hw.pot[4].value = .61f; // a small wobble above: no takeover
    Tick(250);
    CHECK(std::fabs(Panel().Value(toy_piano::kDestGrainMix) - 25.f / 127.f) < 1e-6f);
    hw.pot[4].value = .1f; // crossing the external value takes over
    Tick(250);
    CHECK(std::fabs(Panel().Value(toy_piano::kDestGrainMix) - .1f) < .01f);
    toy_piano::ExternalWrite unused{};
    CHECK(!toy_piano::CcToWrite(15, 74, 1, unused)); // channel 16 carries the diagnostics
}

// 5. Source rates: pot -> patch, app edit -> register, and no stale pot write back over the edit.
void TestRateSync() {
    hw.tog_sw17.value = false; // Piano page, VR6 = LFO 1 rate
    float x = Panel().Value(toy_piano::kDestLfo1Rate);
    hw.pot[5].value = x; // reach the stored value
    Tick(250);
    hw.pot[5].value = .8f;
    Tick(250);
    CHECK(std::fabs(Panel().Value(toy_piano::kDestLfo1Rate) - .8f) < .01f);
    const uint16_t after_pot = modulation.controller.CurrentPatch().lfo[0].rate;
    CHECK(std::abs(int(after_pot) - int(toy_piano::RateFromPosition(Panel().Value(toy_piano::kDestLfo1Rate)))) <= 1);
    // App edit (SET_SOURCE): the register follows; the pot (still at .8) does not write it back.
    mod::Patch edited = modulation.controller.CurrentPatch();
    edited.lfo[0].rate = 1000;
    CHECK(modulation.controller.LoadPatch(edited) == mod::Status::Ok);
    Tick(250);
    CHECK(modulation.controller.CurrentPatch().lfo[0].rate == 1000);
    CHECK(std::fabs(Panel().Value(toy_piano::kDestLfo1Rate) - 1000.f / 16383.f) < 1e-5f);
    hw.pot[5].value = .81f; // wobble, no crossing
    Tick(250);
    CHECK(modulation.controller.CurrentPatch().lfo[0].rate == 1000);
    hw.pot[5].value = .02f; // crossing 1000/16383 = .061: takes over
    Tick(250);
    CHECK(modulation.controller.CurrentPatch().lfo[0].rate == toy_piano::RateFromPosition(Panel().Value(toy_piano::kDestLfo1Rate)));
    CHECK(modulation.controller.CurrentPatch().lfo[0].rate < 1000);
    // R9: a consumed boot pot must never reappear, even after many complete old-generation laps.
    for(bool defer_audio : {false, true}) {
        toy_piano::RateMailboxes box;
        toy_piano::RateSync sync;
        toy_piano::RateAudio audio;
        toy_piano::CumulonimbusPanel panel;
        panel.Reset();
        std::array<float, 8> pots{};
        pots.fill(.5f);
        panel.Update(pots, false, 0);
        uint16_t patch[3] = {6000, 6000, 8000}, out[3];
        sync.FromPatch(patch, box);
        audio.Publish(panel, box);
        CHECK(sync.FromPots(box, out));
        for(unsigned i = 0; i < 3; ++i) patch[i] = out[i];
        for(unsigned i = 0; i < 4096; ++i) {
            patch[0] = uint16_t(9000 + i % 2);
            sync.FromPatch(patch, box);
            if(!defer_audio) {
                audio.Apply(panel, box);
                audio.Publish(panel, box);
            }
            CHECK(!sync.FromPots(box, out));
            CHECK(out[0] == patch[0]);
        }
        audio.Apply(panel, box);
        CHECK(std::fabs(panel.Value(toy_piano::kDestLfo1Rate) - patch[0] / 16383.f) < 1e-6f);
        // Zero is a real edit, not an empty mailbox; latest edit wins without audio acknowledgement.
        patch[0] = 0;
        sync.FromPatch(patch, box);
        audio.Apply(panel, box);
        CHECK(panel.Value(toy_piano::kDestLfo1Rate) == 0.f);
        // Another source's pending editor write cannot suppress LFO 1's new pot.
        patch[1] = 9999;
        sync.FromPatch(patch, box);
        box.state[0].store(toy_piano::kRatePot | 1234);
        CHECK(sync.FromPots(box, out) && out[0] == 1234 && out[1] == 9999);
        CHECK(!sync.FromPots(box, out)); // consumed exactly once
        patch[0] = 1234;
        // An unconsumed old pot is replaced by an edit and counted exactly once.
        box.state[2].store(toy_piano::kRatePot | 7777);
        const uint32_t before = box.Stale();
        patch[2] = 500;
        sync.FromPatch(patch, box);
        CHECK(!sync.FromPots(box, out) && out[2] == 500 && box.Stale() == before + 1);
        CHECK(!sync.FromPots(box, out) && box.Stale() == before + 1);
        audio.Apply(panel, box);
        CHECK(std::fabs(panel.Value(toy_piano::kDestRandomRate) - 500.f / 16383.f) < 1e-6f);
    }
    // Main can publish an edit between audio Apply and Publish. That pending edit must win.
    {
        toy_piano::RateMailboxes box;
        toy_piano::RateSync sync;
        toy_piano::RateAudio audio;
        toy_piano::CumulonimbusPanel panel;
        panel.Reset();
        std::array<float, 8> pots{};
        pots.fill(.5f);
        panel.Update(pots, false, 0);
        uint16_t patch[3] = {6000, 6000, 8000}, out[3];
        sync.FromPatch(patch, box);
        audio.Apply(panel, box);
        patch[0] = 9000;
        sync.FromPatch(patch, box);
        audio.Publish(panel, box);
        sync.FromPots(box, out);
        CHECK(out[0] == 9000 && box.Stale() == 1);
        audio.Apply(panel, box);
        CHECK(std::fabs(panel.Value(toy_piano::kDestLfo1Rate) - 9000.f / 16383.f) < 1e-6f);
    }
    // Exercise the actual main/audio mailbox ownership concurrently; no panel access from main
    // until join. The final edit must reach audio even if intermediate stores coalesce.
    {
        toy_piano::RateMailboxes box;
        toy_piano::RateSync sync;
        toy_piano::RateAudio audio;
        toy_piano::CumulonimbusPanel panel;
        panel.Reset();
        uint16_t patch[3] = {6000, 6000, 8000}, out[3];
        sync.FromPatch(patch, box);
        std::atomic<bool> done{false};
        std::thread worker([&]() {
            while(!done.load(std::memory_order_acquire)) {
                audio.Apply(panel, box);
                audio.Publish(panel, box);
            }
            audio.Apply(panel, box);
        });
        for(unsigned i = 0; i < 20000; ++i) {
            patch[0] = uint16_t(9000 + i % 2);
            sync.FromPatch(patch, box);
            CHECK(!sync.FromPots(box, out));
        }
        done.store(true, std::memory_order_release);
        worker.join();
        CHECK(std::fabs(panel.Value(toy_piano::kDestLfo1Rate) - patch[0] / 16383.f) < 1e-6f);
    }
}

// R5 (PR #108 review): an editor write whose reply was lost is retried after a pot changed the same
// rate on the device. The retry is answered, not executed again over the pot, and its reply carries
// the current patch CRC, so the editor sees the difference and reloads.
void TestEditorRetryAfterPotEdit() {
    hw.tog_sw17.value = false; // Piano page, VR6 = LFO 1 rate
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    uint8_t payload[8];
    mod::proto::Writer w(payload, sizeof(payload));
    w.U7(0);    // LFO 1
    w.U7(0);    // rate
    w.U14(2000);
    uint8_t frame[mod::proto::kMaxFrame];
    const std::size_t size = mod::proto::BuildFrame(frame, 0, mod::proto::SetSource, 77, w.Data(), w.Size());
    auto receive = [&]() {
        daisy::MidiEvent event;
        event.type = daisy::SystemCommon;
        event.sc_type = daisy::SystemExclusive;
        std::memcpy(event.sysex_data, frame + 1, size - 2);
        event.sysex_message_len = uint8_t(size - 2);
        usb_midi.incoming.push_back(event);
    };
    receive();
    Tick(5);
    CHECK(modulation.controller.CurrentPatch().lfo[0].rate == 2000);
    usb_midi.sent.clear(); // the reply is lost on the way to the editor
    Tick(250);             // the edit reaches the register; VR6 must pick it up
    CHECK(std::fabs(Panel().Value(toy_piano::kDestLfo1Rate) - 2000.f / 16383.f) < 1e-5f);
    hw.pot[5].value = .9f;
    Tick(250);
    hw.pot[5].value = .05f; // crosses 2000/16383 = .122 and takes over
    Tick(250);
    const uint16_t pot_rate = modulation.controller.CurrentPatch().lfo[0].rate;
    CHECK(pot_rate != 2000 && pot_rate == toy_piano::RateFromPosition(Panel().Value(toy_piano::kDestLfo1Rate)));
    usb_midi.sent.clear();
    receive(); // the editor's retry: same sequence number, same bytes
    Tick(5);
    CHECK(modulation.controller.CurrentPatch().lfo[0].rate == pot_rate); // not executed again
    bool replied = false;
    for(const auto& m : usb_midi.sent) {
        if(m.size() < 4 || m[0] != 0xF0) continue;
        mod::proto::Frame f;
        if(mod::proto::ParseHeader(m.data() + 1, m.size() - 2, f) != mod::proto::Parse::Ok) continue;
        if(f.command != (mod::proto::SetSource | mod::proto::kReplyBit) || f.seq != 77) continue;
        mod::proto::Reader r(f.payload, f.length);
        r.U7();
        r.U7();
        CHECK(r.U32() == modulation.controller.CurrentCrc()); // the current CRC: the editor reloads
        replied = true;
    }
    CHECK(replied);
}

// 6. Held notes survive switch changes; nothing retriggers.
void TestHeldNotes() {
    for(auto& p : hw.pressed) p = false;
    Tick(10);
    hw.pressed[3] = true;
    Tick(20);
    CHECK(led_mask.load() & (1u << 3));
    CHECK(piano.Envelope(3).Active());
    const uint32_t note_ons = 0;
    (void)note_ons;
    for(int flip = 0; flip < 6; ++flip) {
        hw.tog_sw17.value = !hw.tog_sw17.value;
        hw.tog_sw18.value = flip % 3;
        Tick(10);
        CHECK(led_mask.load() & (1u << 3));
    }
    hw.pressed[3] = false;
    Tick(40);
    CHECK(!(led_mask.load() & (1u << 3)));
    hw.tog_sw18.value = 0;
}

// 7. Freeze holds the ring; leaving bypass from suspension has no jump.
void TestFreezeAndBypass() {
    hw.tog_sw17.value = true;
    SendCc(0, 74, 90);
    SendCc(0, 77, 40);
    hw.tog_sw18.value = seed::fx::kSwitch3Up; // Freeze
    Tick(20);
    CHECK(cumulo->GetChain().Mode() == seed::fx::ChainMode::Freeze);
    const int32_t head = cumulo->GetChain().GrainEffect().Ring(0).head();
    SendNote(60, 100);
    Tick(200);
    CHECK(cumulo->GetChain().GrainEffect().Ring(0).head() == head);
    hw.tog_sw18.value = seed::fx::kSwitch3Down;
    Tick(seed::fx::GranularReverbChain::kTailBlocks + 20);
    CHECK(cumulo->GetChain().Suspended());
    hw.tog_sw18.value = 0;
    float last_l = left[kN - 1];
    Tick(1);
    CHECK(std::fabs(left[0] - last_l) < .05f); // no click at the resume
    CHECK(!cumulo->GetChain().Suspended());
}

#if TOY_PIANO_CUMULONIMBUS_PROFILE
// R11: counters belong to the audio reset/snapshot boundaries, including uint32 wrap.
void TestCounterWindow() {
    auto callback = []() {
        float* output[]{left, right};
        in_audio = true;
        AudioCallback(nullptr, output, kN);
        in_audio = false;
        ++daisy::System::now;
    };
    auto flood = []() {
        for(unsigned i = 0; i < 40; ++i) SendCc(0, 70, i);
        for(unsigned i = 0; i < toy_piano::kMidiQueueSize + 8; ++i) SendNote(60, 80);
        ServiceUsbMidi();
    };
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    hw.tog_sw17.value = true;
    for(uint32_t initial : {uint32_t{123}, UINT32_MAX - 3}) {
        Tick(50); // drain prior queues and telemetry
        control_drops.store(initial);
        midi_overruns.store(initial);
        rate_boxes.stale.store(initial);
        requests.reset.store(true);
        callback(); // new window begins before main receives the overflowing MIDI batch
        flood();
        rate_boxes.stale.fetch_add(3);
        CHECK(control_drops.load() - initial == 8);
        CHECK(midi_overruns.load() - initial == 8);
        meter.RequestSnapshot();
        snapshot_pending = true;
        callback();
        CHECK(meter.SnapshotReady());
        CHECK(meter.Snapshot().app[diag::kControlDrops] == 8);
        CHECK(meter.Snapshot().app[diag::kMidiOverruns] == 8);
        CHECK(meter.Snapshot().app[diag::kStaleRates] == 3);
        usb_midi.sent.clear();
        // More drops after snapshot capture must not leak into its encoded telemetry.
        flood();
        rate_boxes.stale.fetch_add(3);
        for(unsigned i = 0; i <= diag::kRecords; ++i) ServiceCumulonimbus(true);
        diag::Metrics m{};
        unsigned records = 0;
        for(const auto& packet : usb_midi.sent) {
            if(packet.size() != 60 || packet[0] != 0xBF || packet[2] != diag::kProtocolId) continue;
            const unsigned record = packet[5];
            if(record >= diag::kRecords) continue;
            for(unsigned v = 0; v < 3; ++v) {
                uint64_t value = 0;
                for(unsigned d = 0; d < 5; ++d)
                    value |= uint64_t(packet[8 + 15 * v + 3 * d]) << (7 * d);
                m[record * 3 + v] = uint32_t(value);
            }
            ++records;
        }
        CHECK(records == diag::kRecords);
        CHECK(m[diag::kControlDrops] == 8 && m[diag::kMidiOverruns] == 8 && m[diag::kStaleRates] == 3);
        meter.RequestSnapshot();
        callback();
        CHECK(meter.Snapshot().app[diag::kControlDrops] > 8);
        CHECK(meter.Snapshot().app[diag::kMidiOverruns] > 8);
        CHECK(meter.Snapshot().app[diag::kStaleRates] == 6);
        meter.ReleaseSnapshot();
        requests.reset.store(true);
        callback();
        meter.RequestSnapshot();
        callback();
        CHECK(meter.Snapshot().app[diag::kControlDrops] == 0);
        CHECK(meter.Snapshot().app[diag::kMidiOverruns] == 0);
        CHECK(meter.Snapshot().app[diag::kStaleRates] == 0);
        meter.ReleaseSnapshot();
    }
}

// 8. Profiling: scenario/cap/stimulus CCs, stage isolation and the telemetry records.
void TestProfiling() {
    SendCc(15, 20, cumulonimbus::diag::kScenarioDry);
    SendCc(15, 21, 8);
    SendCc(15, 22, 4); // 16 MIDI + 16 key voices
    Tick(600);
    CHECK(!cumulo->GetChain().Route().grains && !cumulo->GetChain().Route().reverb);
    CHECK(cumulo->GetChain().GrainEffect().Cap() == 8);
    CHECK(toy_piano::ActiveVoices(piano) == 32);
    usb_midi.sent.clear();
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    SendCc(15, 119, 1);
    Tick(30);
    std::vector<uint8_t> bytes;
    for(const auto& m : usb_midi.sent) bytes.insert(bytes.end(), m.begin(), m.end());
    unsigned records = 0;
    std::array<uint32_t, cumulonimbus::diag::kMetricCount> metrics{};
    for(std::size_t i = 0; i + 60 <= bytes.size(); i += 3) {
        if(bytes[i] != 0xBF || bytes[i + 1] != 100 || bytes[i + 2] != cumulonimbus::diag::kProtocolId) continue;
        const unsigned record = bytes[i + 5];
        for(unsigned v = 0; v < 3; ++v) {
            uint64_t value = 0;
            for(unsigned d = 0; d < 5; ++d) value |= uint64_t(bytes[i + 3 * (2 + v * 5 + d) + 2]) << (7 * d);
            metrics[record * 3 + v] = uint32_t(value);
        }
        ++records;
        i += 57;
    }
    CHECK(records == cumulonimbus::diag::kRecords);
    CHECK(metrics[cumulonimbus::diag::kScenario] == cumulonimbus::diag::kScenarioDry);
    CHECK(metrics[cumulonimbus::diag::kCap] == 8);
    CHECK(metrics[cumulonimbus::diag::kVoicesPeak] == 32);
    CHECK(metrics[cumulonimbus::diag::kInternalRate] == CUMULONIMBUS_INTERNAL_RATE);
    CHECK(metrics[cumulonimbus::diag::kBlocks] > 0);
    CHECK(metrics[cumulonimbus::diag::kWindowMs] > 0);
    CHECK(metrics[cumulonimbus::diag::kFlags] & cumulonimbus::diag::kFlagProfiling);
    CHECK(std::abs(int(metrics[cumulonimbus::diag::kWindowMs]) - int(metrics[cumulonimbus::diag::kBlocks])) <= 30);
    SendCc(15, 20, cumulonimbus::diag::kScenarioFull);
    SendCc(15, 22, 0);
    Tick(5);
}
#endif

int main() {
    hw.Init();
    piano.Init();
    CHECK(InitializeFx());
    InitModulation(48000);
#if TOY_PIANO_CUMULONIMBUS_PROFILE
    meter.Init(480000);
#endif
    for(auto& p : hw.pot) p.value = .5f;
    hw.tog_sw18.value = 0;
    TestPanelBoot();
    TestBootDefaults();
    TestDryReference();
    TestWetStereo();
    TestPickup();
    TestRateSync();
    TestEditorRetryAfterPotEdit();
    TestHeldNotes();
    TestFreezeAndBypass();
#if TOY_PIANO_CUMULONIMBUS_PROFILE
    TestProfiling();
    TestCounterWindow();
#endif
    CHECK(allocations == 0);
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("toy-piano-cumulonimbus (%u Hz, profile %d): all tests passed\n", unsigned(CUMULONIMBUS_INTERNAL_RATE),
                int(TOY_PIANO_CUMULONIMBUS_PROFILE));
    return 0;
}
