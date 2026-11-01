# Mod on Daisy — Architecture and technical specification v0.1

**As of:** 23 October 2026 · **Status:** specification v0.1. Phase 2 is implemented in
software and checked on the host. The measurements on the hardware are still pending
([VALIDATION.md](VALIDATION.md)).
It is based on the [architecture decisions v0.1](DECISIONS.md) (D1–D11, addendum
N1–N6) and the objections named there (E1–E12).

Evidence classes: `VERIFIED` (checked in the code or in libDaisy), `MEASURED` (build or test),
`ESTIMATE`, `PROPOSAL`, `OPEN`.

**Principle:** one lean component:

* fixed sizes: 8 sources, 8 targets, 8 active connections;
* no heap, no virtual calls in the audio path, no audio rate;
* the module occupies **1 936 bytes of RAM** in the firmware (`MEASURED`), about 1 KiB of
  it in the audio path;
* estimate: less than 0.3 % CPU (§ 8).

---

## 1. Module structure

```text
modules/mod-on-daisy/
├── Makefile              host tests: make test | test-sanitize | test-thread
├── docs/                 DECISIONS, ARCHITECTURE, PROTOCOL, STORAGE, TEST_PLAN_PHASE2, VALIDATION
├── include/mod_on_daisy/ header-only, C++17, standard library only
│   ├── types.h           constants, Source/Shape/Sync, divisions, TargetInfo, InstrumentInfo, AllowedCells
│   ├── patch.h           Patch (grid + source parameters), Validate, Serialize (109 B, format 2: 111 B), CRC-32, SchemaHash
│   ├── sources.h         sine/shapes, Rng, Smoother, CV dead zone, ClockFollower; rate → increment (Main)
│   ├── program.h         Program and Compile(Patch) → Program (Main)
│   ├── mailbox.h         lock-free triple buffer
│   ├── notes.h           LiveState: CC1, aftertouch, pressure table, last note, clock/start/note-on counters
│   ├── engine.h          Engine (audio IRQ) and VoicePressure
│   ├── protocol.h        SysEx codec (writer/reader, frame, checksum) — also used by the editor
│   ├── controller.h      Controller (main loop): MIDI input, protocol, replies, monitor
│   └── module.h          Module = LiveState + Engine + Controller
├── adapters/mod_on_daisy_desktop.h   the only libDaisy dependency: CV/gate, MidiEvent, USB send
├── tests/                test_mod.cpp, test_thread.cpp, test_bench.cpp
├── bench/                measurement firmware ModBench (projects.mk: mod-bench)
└── tools/mod_probe.py    command-line client for SysEx tests and measurement runs

editors/mod-on-daisy/     (phase 3) JUCE editor, includes protocol.h and patch.h
```

**Dependency rules**

* `include/` depends neither on libDaisy nor on DaisySP. It runs on the host, in tests
  and in the editor.
* `adapters/` is the only place with `daisy::`.
* The editor and the test script use the same serialization. The check value
  `CRC(test patch) = E2340CC6` is computed identically in C++ (`test_mod.cpp`) and Python
  (`mod_probe.py selftest`).
* Instruments include the module with
  `C_INCLUDES += -I../../modules/mod-on-daisy/include -I../../modules/mod-on-daisy/adapters`.
  There is no additional archive.

## 2. Execution contexts and ownership

```text
 USB IRQ (libDaisy)      main loop                                 audio IRQ
 ──────────────────      ─────────────────────────────────         ───────────────────────────
 packet → MidiEvent ──►  usb_midi.PopEvent() → adapter::ToMessage  hw.UpdateAllControls()
 (FIFO 256)              ├─► controller.HandleMidi(m) ───────┐     adapter::ReadHardware(engine, hw)
                         │    · SysEx "MD": protocol          │     engine.Tick(n)
                         │    · CC1, ChAT, PolyAT → LiveState │      ├ fetch Program (triple buffer)
                         │    · notes, clock, start: observed │      ├ clocks, gates, retrigger
                         ├─► false: instrument processes it   │      ├ compute sources
                         └─► controller.Service(ms, sender)   │      └ global offsets
                              · ≤ 1 reply/ms, monitor         │   instrument DSP:
                              · patch → Compile → Publish ────┘     Apply / ApplyVoice
```

| State | Writer | Reader | Mechanism |
| --- | --- | --- | --- |
| `Patch`, reply buffer | Main | Main | none needed |
| `Program` | Main (`Compile`) | Audio | triple buffer, acq_rel |
| CC1, channel AT, last-note pressure, `pressure[128]` | Main | Audio | `atomic<uint8_t>`, relaxed |
| note-on, clock, start counters, clock count at start | Main | Audio | `atomic<uint32_t>`; start with release/acquire |
| engine state (phases, smoothing, clock followers, offsets) | Audio | Audio | sole ownership |
| monitor snapshot | Audio | Main | triple buffer, only while the monitor is active |

All atomics are checked with `static_assert(…::is_always_lock_free)`. ThreadSanitizer
runs on the interaction of both threads (`test_thread.cpp`).

## 3. Data structures

### 3.1 Sources (rows, fixed order D3)

| Row | Source | Range | Raw data | Conditioning |
| --- | --- | --- | --- | --- |
| 0 | LFO 1 | −1…+1 | internal | shape, rate or sync (§ 4.2) |
| 1 | LFO 2 | −1…+1 | internal | like LFO 1 |
| 2 | Random / S&H | −1…+1 | internal | stepped or smooth; rate or sync |
| 3 | CV 1 | −1…+1 | `hw.cv[0].Value()` | calibration, dead zone 0.02, 2 ms |
| 4 | CV 2 | −1…+1 | `hw.cv[1].Value()` | like CV 1 |
| 5 | Modwheel | 0…1 | CC1, all channels | 10 ms |
| 6 | Channel AT | 0…1 | channel pressure, all channels | 10 ms |
| 7 | Poly AT | 0…1 | poly key pressure per note | voice targets: per voice 10 ms (`VoicePressure`); global targets: last note, 10 ms |

### 3.2 Target description of the instrument

```cpp
struct TargetInfo {
    const char* name = nullptr;      // column header, ≤ 12 ASCII characters; nullptr = no column
    const char* span_text = nullptr; // meaning of depth 100, ≤ 8 ASCII characters, e.g. "+-12 st"
    float span = 1.0f;               // normalized excursion at depth 100 (N2), 0 < span ≤ 1
    bool per_voice = false;          // instrument uses ApplyVoice (poly AT per voice)
    bool latched = false;            // read only at note strike → poly AT cell locked
};
struct InstrumentInfo {
    const char* key; const char* version;   // ≤ 24 (kMaxKeyLength) or ≤ 12 characters
    TargetInfo targets[8];
    uint8_t unavailable_sources = 0;        // bit s: source occupied by the instrument
};
```

* The **column number is the target ID**. `SchemaHash(info)` is a CRC-32 over the key,
  all columns and the poly AT rule. Stored patches and editor caches are bound to it.
* **Allowed cells** (`AllowedCells`): target present, source available, and not poly
  AT on a `latched` target. Poly AT on global targets is allowed and uses the last-note
  pressure (N1).
* Strings are 7-bit ASCII because SysEx carries only 7-bit bytes. The instrument writes
  the character "±" as `+-`; the editor displays it as ±.

### 3.3 Patch (editable configuration)

```cpp
struct LfoParams    { uint16_t rate = 6000; uint8_t shape, sync, gate, division = 4, retrigger; };
struct RandomParams { uint16_t rate = 8000; uint8_t mode,  sync, gate, division = 4; };
struct Patch {
    uint8_t format = 1;
    int8_t  depth[8][8];   // −100…+100
    uint8_t enabled[8];    // bit t = cell (s, t) enabled
    LfoParams lfo[2]; RandomParams random;
    char name[16];
};   // serialized: 1 + 64 + 8 + 2·7 + 6 + 16 = 109 bytes, little endian, no padding
```

With `MOD_ON_DAISY_EXTENSIONS=1`, `LfoParams` additionally has `int8_t fm` (−100…+100,
LFO cross-modulation). A patch with `format = 2` appends `fm` of LFO 1 and 2
(111 bytes); `format = 1` must have `fm = 0`. A device with cross-modulation enabled
works with format 2 (PROTOCOL § 3.6).

* A cell is **active** when it is enabled and `depth ≠ 0`. A disabled cell keeps its
  depth.
* **`Validate(patch, allowed)`** rejects a patch as a whole if:
  * the format or depths are invalid (`Range`);
  * a parameter is out of range, including a division that does not fit the sync source
    (`Range`);
  * an active cell is not allowed (`NotAllowed`);
  * more than `kMaxActive` cells are active (`Limit`).
  Nothing is ever applied partially.

**Source parameters** (N4/N5; parameter numbers as in `SET_SOURCE`):

| No. | Parameter | LFO 1/2 | Random |
| --- | --- | --- | --- |
| 0 | Rate | 0…16383 → 0.02…20 Hz logarithmic, `f = 0.02 · 1000^(r/16383)`; applies with sync = free | like LFO: frequency of new random values |
| 1 | Shape / mode | 0 sine, 1 triangle, 2 saw rising, 3 saw falling, 4 square, 5 sample & hold | 0 stepped (S&H), 1 smooth |
| 2 | Sync | 0 free, 1 MIDI clock, 2 gate | like LFO |
| 3 | Gate | 0 gate 1, 1 gate 2 (with sync = gate) | like LFO |
| 4 | Division | MIDI: 0 4 bars, 1 2 bars, 2 1 bar, 3 1/2, 4 1/4, 5 1/4T, 6 1/8, 7 1/8T, 8 1/16, 9 1/16T, 10 1/32. Gate: 0 1 cycle per 4 pulses, 1 per 2, 2 per pulse, 3 two per pulse, 4 four per pulse | like LFO |
| 5 | Retrigger | 0 off, 1 restart on every note on (only with sync = free) | — |
| 6 | fm (extension) | −100…+100, transmitted as fm + 100: rate × 2^(fm/50 · value of the other LFO), i.e. up to ±2 octaves; only with sync = free | — |

When the sync type changes, the controller sets the division to the default of the new
type: MIDI clock 1/4, gate 1 cycle per pulse. The same index means something different
depending on the type; in the MIDI table, for example, 4 is 1/4, in the gate table 4/1.

### 3.4 Program (compiled form for the audio IRQ)

```cpp
struct Connection   { uint8_t source, target; float gain; };      // gain = depth/100 · span
struct SourceConfig { uint32_t inc; Ratio ratio; uint8_t shape, sync, gate, retrigger; };
struct Program {
    uint8_t count; bool active;          // active == false → all offsets exactly +0.0f
    Connection conn[kMaxActive];
    float voice_gain[8];                 // poly AT on per_voice targets
    SourceConfig lfo[2], random;
    float cv_offset[2];                  // device calibration, not part of the patch
};
```

`Compile()` runs only in the main loop and performs the following steps there:

* Poly AT on a `per_voice` target → `voice_gain[t]`.
* Everything else, including poly AT on global targets, becomes a `Connection`.
* Rate → phase increment (`powf`) and division → `Ratio`.

### 3.5 Hand-over main → audio

* A **triple buffer** (`mailbox.h`) with three `Program` instances: Main writes into
  `Back()` and publishes with `Publish()`; Audio picks it up at the start of `Tick()` with
  `Fetch()`. Neither side waits, and the newest state always wins.
* Phases and smoothing states live in the engine. A new Program does not reset them
  (test `TestProgramSwapKeepsPhase`).

## 4. Modulation processing

### 4.1 Sequence of a tick

`Tick(n)` runs once per audio block with the actual sample count `n`:

```text
1. Fetch Program from the triple buffer if new; recompute the coefficients if n changed.
2. Advance the clock followers: MIDI clock pulses, start, gate edges; note-on counter for retrigger.
3. !program.active → on the transition set offsets to +0.0f; advance free phases; done.
4. Compute LFO 1/2, Random, CV 1/2, Modwheel, Channel AT, Poly AT (last note) → source[8].
5. offset[0..7] = 0; for each connection c: offset[c.target] += c.gain · source[c.source].
```

With an active matrix all eight sources are always computed. This costs only a few
hundred cycles and keeps every source current as soon as a new connection is added.

The instrument reads:

```cpp
float Apply(t, base)            = Clamp01(base + offset[t])
float ApplyVoice(t, base, p)    = Clamp01(base + offset[t] + voice_gain[t] · p)
```

* **Bit-exactly neutral**: without an active connection, `offset[t] == +0.0f` and
  `voice_gain[t] == 0`. For every `base` in [0, 1] the matrix then returns exactly `base`
  (test `TestNeutral`, also after a connection was removed).
* Clamping happens **after** the sum.

### 4.2 LFO, Random and synchronization

* **Free**: `uint32_t` phase accumulator `phase += inc · n`. After the same number of
  samples the phase is bit-identical for all tick lengths (tested at 12, 32, 48, 64 and
  256 samples).
* **Shapes** from `p ∈ [0, 1)`:
  * sine via a polynomial, error 0.11 % (`MEASURED`);
  * triangle, saw and square directly from the phase;
  * sample & hold draws a new value at every period change (xorshift32, fixed seed).
* **Random**: new value at every period change. In smooth mode the value moves linearly
  from the previous to the new one.
* **Synchronization** (`ClockFollower`):
  * The follower counts pulses: MIDI clock at 24 ppqn, or gate edges, which are polled
    once per block.
  * It estimates the period (MIDI: smoothing 1/8, gate: 1/2).
  * The phase is `((pulses mod den) + fraction) · num / den`; the fraction interpolates
    between the pulses.
* **Synchronization behavior**:
  * **MIDI Start** (`FA`): the next clock pulse is the start of the bar; until then the
    phase stays at 0. Continue and Stop reset nothing.
  * **Clock or gate stops**: the fraction runs at most until shortly before the next
    expected pulse. The LFO **holds** and does not run away (test `TestMidiClockSync`).
  * **Resolution**: MIDI clock pulses and gate edges are captured with block resolution,
    i.e. 1 ms at block 48. The jitter is smoothed by the period estimate. Gate pulses
    shorter than one block can be lost (E8, measurement H8).
* **Retrigger** (free-running only): every note on sets the phase to 0.
* **Cross-modulation** (extension, free-running only): the increment of LFO i is
  multiplied per tick by `2^(fm_i/50 · LFO_(1−i))`. The value of the other LFO comes from
  the previous tick so that the order of computation does not matter. `Exp2Small` is a
  cubic polynomial with table steps, limited to ±2 octaves. With `fm = 0` the code path
  stays bit-identical to free running without cross-modulation.

### 4.3 CV conditioning

```text
x = cv − cal_offset[i]                              // calibration (SET_CALIBRATION, RAM in v0.1)
x = |x| < 0.02 ? 0 : sign(x) · (|x| − 0.02) / 0.98   // dead zone against noise of open jacks
y += a · (x − y)                                    // 2 ms
```

Dead zone and time constant are `PROPOSAL` and are checked on the device in H5. For that
purpose the monitor also shows the raw values.

### 4.4 MIDI sources and events (`Controller::HandleMidi`)

| Message | Effect | passed on to the instrument? |
| --- | --- | --- |
| CC1 | Modwheel | no |
| Channel Pressure | Channel AT | no |
| Poly Key Pressure | `pressure[note]`, only while the note is held; last note | no |
| Note On (v > 0) | pressure of the note = 0, note held, appended to the end of the last-note list, retrigger counter + 1 | yes |
| Note Off, Note On v = 0 | pressure = 0, note removed | yes |
| CC120, CC123 | clear all notes and pressures | yes |
| Clock `F8`, Start `FA` | clock counter or start of bar | yes |
| SysEx `7D 4D 44 …` | protocol | no |
| everything else | — | yes |

The last-note list holds 16 notes. On overflow the oldest one drops out; this affects
only the last-note rule.

### 4.5 Avoiding parameter jumps

| Cause | Measure |
| --- | --- |
| 7-bit MIDI steps | 10 ms smoothing (globally in the engine, per voice in `VoicePressure`) |
| CV noise | dead zone + 2 ms |
| depth change in the editor | offset jumps once per tick; the instrument's own parameter smoothing absorbs that (Physical Modeling 5 ms; Toy Piano v0.3 gets one) |
| S&H, random stepped, square | intended; random smooth as alternative |
| new Program | phases and smoothing keep running |
| load peaks | fixed loop bounds, no heap (test `TestNoAllocation`), no libm in the tick except `fabs` |

Stepped targets such as the model selection are not targets in v0.1.

## 5. Global and per-voice modulation

| Path | Sources | Result | Cost |
| --- | --- | --- | --- |
| **Global** | all 8, poly AT as last note | `offset[t]`, once per tick | ≤ `kMaxActive` multiplications and additions |
| **Voice** | poly AT only | `voice_gain[t] · VoicePressure` when read per voice | 1 multiplication and addition per voice and target |

* Only poly AT differs per voice. The engine therefore knows no voices; the voice → note
  mapping stays with the instrument (E3).
* `VoicePressure::Process(engine, note)` smooths the pressure of a voice's note
  (`note < 0` = silent → 0).

| Case | Behavior |
| --- | --- |
| Voice stealing | the new note starts with pressure 0; the helper glides there in 10 ms, immediately with `Reset()` |
| Same note in two voices | same pressure (MIDI 1.0 limit) |
| Note off, voice keeps sounding | pressure → 0, modulation glides to the base value |
| Poly AT without a held note | ignored |
| Monophonic instrument (Nebulae) | all targets global → last note |

## 6. Instrument interface

### 6.1 API

```cpp
namespace mod {
struct Module {                                       // one static object per instrument
    LiveState live; Engine engine; Controller controller;
    void Init(const InstrumentInfo&, float sample_rate, std::size_t block_size,
              uint8_t device_id = 0, Diagnostics* diagnostics = nullptr);
};
class Engine {                                        // audio IRQ
    void  SetHardware(float cv1, float cv2, bool gate1, bool gate2);   // via adapter::ReadHardware
    void  Tick(std::size_t samples);
    float Apply(std::size_t target, float base) const;
    float ApplyVoice(std::size_t target, float base, float voice_pressure) const;
    float NotePressure(uint8_t note) const;  float PressureCoeff() const;  bool Active() const;
};
class Controller {                                    // main loop
    bool   HandleMidi(const MidiMessage&);           // true = consumed
    void   Service(uint32_t now_ms, MidiSender&);
    Status LoadPatch(const Patch&);                  // local, e.g. key combination
    void   SetCalibration(float cv1, float cv2);
};
namespace adapter {                                   // libDaisy
    void ReadHardware(Engine&, daisy::desktop_devkit::Hardware&);
    bool ToMessage(const daisy::MidiEvent&, MidiMessage&);
    class UsbMidiSender : public MidiSender;          // checks USBD_STATE_CONFIGURED like Toy Piano
}
}
```

### 6.2 Integration (pattern: `bench/src/main.cpp`)

```cpp
mod::Module modulation;                    // optionally MOD_ON_DAISY_HOT for a different section
modulation.Init(kInfo, hw.seed.AudioSampleRate(), kBlock);
// audio callback
hw.UpdateAllControls();
mod::adapter::ReadHardware(modulation.engine, hw);
modulation.engine.Tick(size);
value = modulation.engine.Apply(t, base);  // or ApplyVoice(t, base, voice.pressure.Process(engine, note))
// main loop
while(usb_midi.HasEvents()) {
    const daisy::MidiEvent e = usb_midi.PopEvent();
    mod::MidiMessage m;
    if(mod::adapter::ToMessage(e, m) && !modulation.controller.HandleMidi(m))
        InstrumentMidi(e);
}
modulation.controller.Service(daisy::System::GetNow(), sender);
```

### 6.3 Target tables

**Toy Piano v0.3** (development environment, N3):

| Column | Target | Path | span | Note |
| --- | --- | --- | --- | --- |
| 0 | Pitch | voice | `+-1 st` | vibrato |
| 1 | Level | voice | 100 % | tremolo, poly AT volume |
| 2 | Harmonic | voice | 100 % | share of the 2nd overtone |
| 3 | Attack | global, `latched` | 100 % | takes effect at the next strike |
| 4 | Decay | global, `latched` | 100 % | takes effect at the next strike |
| 5–7 | free for experiments | — | — | otherwise greyed out |

**Rings (Physical Modeling)** (DSP change needed for the voice path, E1):

| Column | Target | Parameter today | Path | span |
| --- | --- | --- | --- | --- |
| 0 | Pitch | `Frequency` (0…1 = ±24 st) | voice | 0.25 = `+-12 st` |
| 1 | Structure | `Structure` | voice¹ | 100 % |
| 2 | Brightness | `Brightness` | voice | 100 % |
| 3 | Damping | `Damping` | voice | 100 % |
| 4 | Position | `Position` | voice | 100 % |
| 5 | Excitation | `Excitation` | global, `latched` | 100 % |
| 6 | Level | output level (base value 0.5 = gain 1) | global | `+-100%` (0…2); replaces reverb/`Decay` since the hardware test of 23.10.2026 |
| 7 | Dry/Wet | `Wet` | global | 100 % |

¹ `OPEN`: in chord models, Structure selects the chord for the whole part
(`ChordSelector`). Whether a per-voice Structure makes sense there is checked during
integration.

**Looper** (separate build `MOD=1`, [MODULATION.md](../../../instruments/looper/docs/MODULATION.md)):
one loop, no voices; all columns global, none `latched`, all 64 cells allowed. The base
value is the pot or CC value (whichever was moved last), plus the offset, clamped to the
range of the control.

| Column | Target | Base value | span |
| --- | --- | --- | --- |
| 0 | Speed | pot 0 / CC20, whole semitones (−24…+36) | 0.2 = `+-12 st`, modulation unrounded |
| 1 | Start | pot 3 / CC23 | 100 % |
| 2 | Size | pot 4 / CC24 | 100 % |
| 3 | Slice | pot 5 / CC25 | 100 % |
| 4 | Skip | pot 6 / CC26 | 100 % |
| 5 | Inertia | pot 1 / CC21 | 100 % |
| 6 | Fripp Decay | pot 7 / CC27, only effective in Frippertronics | 100 % |
| 7 | Mix | fixed 0.5 (dry and loop at 1) | 0.5 = `+-50%` (0 dry only, 1 loop only) |

**Nebulae**: Start, Speed, Size, Density, Pitch, Overlap, Blend and Window, all global.
Poly AT arrives there via the last-note rule; the effective rate is 187.5 Hz (E7).

## 7. Real-time rules

* **Audio IRQ**: no heap, no locks, no USB, no QSPI, no logging, no `powf`
  or `exp` (coefficients only when the tick length changes), no virtual calls,
  fixed loop bounds.
* **Main loop**: protocol, validation, `Compile()`, CRC. Replies go into a queue with
  **2 frames**, because the editor has at most one request outstanding. `Service()` sends
  at most one frame per millisecond.
* **Flash**: no write function in v0.1 (D8). `STORE`/`RECALL` → `NAK 9`.

## 8. Memory and performance

### 8.1 RAM and code (`MEASURED`, ARM build of the measurement firmware, `arm-none-eabi-nm`)

| Item | Bytes |
| --- | ---: |
| `mod::Module` total (kMaxActive 8) | **1 936** |
| `mod::Module` total (kMaxActive 64, load test) | 3 280 |
| of which Controller (patch, reply, repeat cache, 2 send frames) | ≈ 870 (`ESTIMATE` from the field sizes) |
| of which Engine (3 Programs, 3 monitor snapshots, state) | ≈ 850 |
| of which LiveState (pressure table 128 B, notes, counters) | ≈ 190 |
| stack `Engine::Tick` (`-fstack-usage`) | 40 |
| code `Engine::Tick` / `Controller::Execute` | 3.3 KiB / 8.4 KiB |

By default the module lives in `.bss` and thus, with BOOT_SRAM, in DTCM. Toy Piano still
has about 14.9 KiB free there. 1.9 KiB is acceptable, but is tracked in the ELF check
during integration.

### 8.2 CPU (`ESTIMATE`; measurement: TEST_PLAN § 3, VALIDATION.md)

The budget per 48-sample block is 1 ms, i.e. **480 000 cycles**.

| Scenario | Cycles per tick | Share at 1 kHz |
| --- | ---: | ---: |
| matrix inactive | ~30 + Apply | ≈ 0.015 % |
| 1–4 connections (all sources computed) | ~200–300 | ≈ 0.05 % |
| 8 connections | ~300 | ≈ 0.06 % |
| + voice path 4 voices × 5 targets | +~200 | ≈ 0.1 % |
| + voice path 32 voices × 5 targets | +~1 300 | ≈ 0.3 % |
| load test: 64 active cells, 32 voices | ~2 000 | ≈ 0.4 % |

The measurement firmware measures exactly this share with the DWT cycle counter (tick
plus all Apply calls), separately from the rest of the callback.

## 9. Status of the phases

| Phase | Status |
| --- | --- |
| 1 Specification | completed; decisions in DECISIONS.md |
| 2 Prototype and measurement | **implemented in software**: core, host tests (ASan/UBSan/TSan, 1 million fuzz frames), measurement firmware (3 variants), `mod_probe.py`, CI. **Open**: measurements S0–S8 and hardware tests H1–H8 on the Desktop Kit |
| 3 Editor | after the results of H3 and H4 |
| 4 Performance | with integrated instruments |
| 5 Integration | **Toy Piano v0.3 implemented**; **Physical Modeling implemented as a separate build (`MOD=1`) with voice path**; hardware tests pending; then Nebulae |
| 6 Extension | QSPI storage (STORAGE.md, separate approval), VST3, more sources |
