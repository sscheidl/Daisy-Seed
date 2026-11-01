# Mod on Daisy — Architecture decisions v0.1 and technical objections

**As of:** 23 October 2026 · **Phase:** 1 completed, phase 2 implemented in software

This document records the decisions of the project owner. They are followed by the
technical objections that came up when checking against the actual code of the
repository, and by the changes proposed as a result. The specification itself is in:

| Document | Content |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | module architecture, data structures, processing, global and per-voice modulation, instrument interface, memory and CPU estimate |
| [PROTOCOL.md](PROTOCOL.md) | MIDI protocol v1 (live layer and SysEx configuration) |
| [STORAGE.md](STORAGE.md) | QSPI layout, erase and write procedure, data integrity (specification only) |
| [TEST_PLAN_PHASE2.md](TEST_PLAN_PHASE2.md) | host tests, measurement firmware, measurement scenarios and acceptance for phase 2 |
| [VALIDATION.md](VALIDATION.md) | results of phase 2 (host measured, hardware pending) |
| [../../../editors/mod-on-daisy/docs/GUI.md](../../../editors/mod-on-daisy/docs/GUI.md) | grid GUI and editor architecture (JUCE) |

**Evidence classes:** `VERIFIED` = checked in the repository or in the pinned libDaisy;
`ESTIMATE` = calculated, not measured; `PROPOSAL` = still to be confirmed;
`OPEN` = can only be settled by a hardware test.

## 1. Decisions v0.1

| No. | Topic | Decision |
| --- | --- | --- |
| D1 | Structure | Module under `modules/mod-on-daisy/`, editor under `editors/mod-on-daisy/`. Existing instruments stay unchanged for now. |
| D2 | Matrix | Graphical **8×8 grid matrix** modelled on MatrixBrute/MiniFreak. No assignment via menus. **At most 8 active connections** (provisional guideline). Depth **−100 … +100** per cross point. |
| D3 | Sources | 1 LFO 1 · 2 LFO 2 · 3 Random/S&H · 4 CV 1 · 5 CV 2 · 6 Modwheel (CC1) · 7 Channel AT · 8 Poly AT. Modwheel, Channel AT and Poly AT are mandatory. Poly AT is processed per voice. |
| D4 | Targets | 8 columns, instrument-specific. Unsupported targets and connections are greyed out. The engine stays universal. |
| D5 | Rate | **No audio rate in v0.1**, no infrastructure for it in advance. Control rate 1 kHz. Later possibly audio rate for the CV inputs, after a separate review. |
| D6 | Editor | JUCE, initially standalone for Windows. VST3 for Studio One should remain possible later. Communication via USB MIDI. |
| D7 | Pilot | Toy Piano for technical integration tests, then Rings (Physical Modeling) with poly AT, later Nebulae. |
| D8 | Storage | QSPI in the long term. First specify layout, procedure and integrity; **no flash write functions yet**. |
| D9 | Protocol | SysEx ID `0x7D` provisionally. Own project identifier, version, device ID, reliable transfer of the matrix. Respect the libDaisy buffer limits. |
| D10 | Performance | First measurement in phase 2: inactive matrix, few connections, maximum. Document callback times, peaks and memory in a traceable way. |
| D11 | Next step | Phase 1 only: architecture and specification. |

Compared with the first concept of 22.10., this drops: 32 slots, via modulation,
envelopes as sources, freely assignable CCs, the audio-rate path, transactions over
many individual commands, the self-describing sources and the triple buffering of a
large program. This makes the concept considerably leaner (see § 3).

**Additions to D3 (24.10.2026, owner decision):**

* **Aftertouch display:** "Normally either Channel AT or Poly AT is active. The editor
  should automatically show the received data under 'Aftertouch'. Under the hood the
  paths stay separate." The editors therefore show one common display "Aftertouch"
  with the suffix "Channel" or "Poly · Note". It follows the most recently received
  message type, which the device reports in the extended monitor (PROTOCOL § 3.6).
  Rows 7 (Channel AT) and 8 (Poly AT) stay separate and editable, unchanged, in the
  matrix, in patches and in the audio path; a dot marks the row currently shown.
  Firmware without this report gets no guessed automatic mode, but the hint
  "Channel/Poly separate".
* **LFO cross-modulation:** LFO 1 and LFO 2 can modulate each other's rate
  (parameter `fm`, ±2 octaves, free-running only). This is a source parameter, not a
  new row or column; the 8×8 matrix stays unchanged (PROTOCOL § 3.6).

## 2. Technical objections

No objection fundamentally calls a decision into question. Several, however, concern
assumptions that the existing code does not meet and that should be known before
integration.

### E1 — Poly AT on Rings requires a change to the Torus DSP (`VERIFIED`)

`torus::Part::Process()` receives **a single `Patch`** (Structure, Brightness,
Damping, Position) for all voices. `RenderModalVoice`, `RenderFMVoice` and
`RenderStringVoice` derive the same values from it for every voice
(`instruments/physical-modeling/torus/dsp/part.cpp`). Independent voice modulation
is therefore **not possible** without changing the Torus layer.

* **Effort**: small. `Part::Process` gets an array of 4 voice patches and a pitch
  offset per voice. The resonators recompute their filters on every call anyway
  (`Resonator::Process` → `ComputeFilters()`). Different values per voice therefore
  cost **no additional computing time** (`ESTIMATE`).
* **Consequence**: this is a change to upstream DSP. It belongs in phase 5 and must be
  documented in `torus/UPSTREAM.md`.
* **Useful poly AT targets on Rings**: Pitch, Structure, Brightness, Damping and
  Position. **Excitation** is taken over at the strike (`velocity * excitation` per
  slot); pressure only arises after the strike, so poly AT has no effect there.
  Reverb and Dry/Wet are global (see E2).

### E2 — Poly AT on global targets needs a rule

A global target such as Reverb, Dry/Wet or all targets of Nebulae has no voice whose
pressure it could take over. `PROPOSAL`: on global targets the **pressure of the most
recently struck note that is still held** applies (last note). This matches the note
priority of Nebulae (`Notes::Current()`) and makes poly AT usable on monophonic
instruments too. The alternative would be to grey out these cells.
→ **Decision needed** (§ 4, question 1).

### E3 — MIDI 1.0 addresses notes, not voices

Poly AT arrives as *(channel, note, pressure)*. Only the instrument knows the mapping to
the voice. The engine therefore keeps a **pressure table per note**, and the instrument
queries the pressure of its note for each voice (ARCHITECTURE § 5). Two limits follow
from this that cannot be avoided:

* Two voices on the same note share the pressure. This does happen: on Toy Piano C5 is
  on two keys, and Rings distributes repeated unison notes to different voices.
* v0.1 is omni: equal note numbers on different channels share the table. True
  independence of equal notes would only be possible with MPE (possible later).

### E4 — Toy Piano has no continuously modulatable parameters (`VERIFIED`)

Toy Piano has only **Attack and Decay**, and both are taken over at the strike
(`ToyPiano::Trigger/NoteOn(…, attack, decay)`). Apart from Note On it processes no MIDI
messages. A meaningful integration test needs at least 2–3 new continuous targets, for
example **Level per voice** (tremolo, poly AT), **Pitch per voice** (vibrato) and
**Harmonic** (2nd overtone). This changes Toy Piano v0.2, a release version accepted by
the owner.

`PROPOSAL`:

* The measurements in phase 2 run with a **separate measurement firmware** in the
  module. It changes no instrument (TEST_PLAN § 3).
* The Toy Piano integration becomes a separate step as version v0.3. Without an active
  connection it sounds bit-exactly like v0.2, which a test proves.
* Toy Piano remains a good poly AT test bench: 16 MIDI voices with note mapping
  (`MidiVoiceNote()`) and voice stealing.

### E5 — The depth resolution ±100 is sufficient for pitch only with a per-target range

On Rings, `Frequency` maps the normalized range 0…1 to **48 semitones**
(`Transpose()` in `config.h`). If ±100 referred to this full range, one depth step
would be **0.48 semitones**. That is too coarse for vibrato.

`PROPOSAL`: each target defines a **modulation range** (`span`), i.e. the value that
depth 100 corresponds to. For Rings pitch that would be ±12 semitones, one step thus
0.12 semitones; for cutoff-like targets the full range. The editor shows the range; the
grid values stay −100…+100. If that is not enough, a quadratic depth curve would be the
next stage; it is not planned for v0.1.

### E6 — The limit of 8 active connections is not a CPU question

Even all 64 cells active are estimated to cost only about 0.1 % CPU at 1 kHz
(ARCHITECTURE § 8). The limit makes sense as an **operating and sound guideline**, not
as a technical necessity. `PROPOSAL`: patch, protocol and storage format always
represent **the full 8×8 grid**, and the limit is a single constant (`kMaxActive = 8`)
in the validation. It can be raised later without a format change. The measurement in
phase 2 additionally checks 64 active cells as a load test.

### E7 — The effective control rate depends on the block size (`VERIFIED`)

The engine computes with the actual sample count per tick, so the LFO frequencies are
always correct. An instrument, however, reads its parameters only once per own block.
For Toy Piano, Looper and Physical Modeling (block 48) that is 1 kHz, for Ambient
Reverb (block 32) 1.5 kHz, for **Nebulae (block 256) only 187.5 Hz**. A 20 Hz LFO would
have about 9 steps per period there. `PROPOSAL`: LFO range in v0.1 **0.02–20 Hz**.
Nebulae measures and assesses this separately when it is integrated.

### E8 — CV inputs: sampling, noise and calibration (`VERIFIED` / `OPEN`)

* `AnalogControl::Process()` for CV runs only in `UpdateAllControls()`, i.e. once per
  audio block. The bandwidth is therefore limited to half the block rate. This supports
  D5: audio rate via CV would need its own ADC path.
* Open CV jacks and ADC noise produce constant **parameter jitter** without a
  countermeasure. Hence a dead zone around 0 and smoothing of about 2 ms
  (ARCHITECTURE § 4.3).
* `OPEN`: voltage range, zero point and level of the open Desktop jacks. They are
  measured in phase 2 and stored as **device calibration**, separate from the patch
  (STORAGE § 3).

### E9 — MIDI limits of the pinned libDaisy (`VERIFIED`)

* **SysEx receive**: buffer 128 bytes. Longer messages are truncated and still reported
  as complete. → frames ≤ 120 bytes with a length field and checksum. The whole 8×8 grid
  fits into **one** frame (99 bytes, PROTOCOL § 3.3).
* **SysEx send**: `MidiToUsb` splits before `F7`. According to a code reading this is
  not compliant with the specification, `OPEN` under Windows. This is checked as the
  **first hardware test in phase 2**. If necessary, a separate USB framing in the
  adapter follows, without changing the libDaisy submodule.
* **Event FIFO**: 256 entries; when the FIFO is full, new events are discarded
  (`FIFO::PushBack`). A poly AT stream can easily exceed 1 000 messages per second.
  The main loop must therefore drain it completely in every iteration and must never
  block for long. This mainly concerns a later QSPI erase of up to 400 ms, during which
  MIDI can be lost (STORAGE § 5).

### E10 — Windows: the editor and Studio One cannot simply share the MIDI port

Classic Windows MIDI ports (WinMM) can be used **by only one application at a time**.
If Studio One has "Daisy Seed External" open, the standalone editor cannot open the
port, and vice versa. The Toy Piano documentation already describes competing
applications. Possible ways:

* disable the port in Studio One while editing;
* Windows MIDI Services with multi-client access, if available on the system
  (`OPEN`, to be checked);
* later the VST3 path, in which the plug-in holds the port instead of Studio One.

This belongs as a test at the start of phase 3, but is already known now.

### E11 — QSPI storage: bootloader, cache and blocking (`VERIFIED` / `OPEN`)

* The Daisy bootloader leaves the **first four 64 KiB sectors untouched** and reserves
  them for future functions. Programs start at `0x90040000` and are erased in
  **64 KiB blocks**. BOOT_SRAM programs are at most 480 KiB in size
  (`libDaisy/doc/md/_a7_Getting-Started-Daisy-Bootloader.md`, `STM32H750IB_sram.lds`).
* libDaisy's `PersistentStorage` erases and writes without CRC and without a double
  copy. On power loss the content can be lost. → own procedure (STORAGE § 4).
* libDaisy sets up **no MPU region** for `0x90000000`. According to the ARMv7-M default
  map the region is cacheable. After erase or write operations the D-cache of the
  region must be invalidated, otherwise stale data is read. `OPEN`: the STM32H7 issue of
  speculative accesses to QSPI in indirect mode must be checked before implementation.
* A sector erase takes **up to 400 ms** according to the libDaisy constant
  (`IS25LP064A_SECTOR_ERASE_MAX_TIME`). Audio keeps running because with BOOT_SRAM the
  code executes from SRAM. The main loop, and with it the USB service, stalls during
  this time.

### E12 — DTCM usage (`VERIFIED`)

With BOOT_SRAM, `.data` and `.bss` live in **DTCM** (128 KiB) by default. Toy Piano
already uses 88.6 %, leaving about 14.9 KiB, including for the stack. The engine state,
at ≤ 1 KiB in the audio path, is small enough (ARCHITECTURE § 8.1). It is nevertheless
tracked per instrument in the ELF check.

## 3. Resulting architecture changes

| No. | Change compared with the concept of 22.10. | Reason |
| --- | --- | --- |
| A1 | Fixed grid `depth[8][8]` + active mask instead of a slot list | D2, E6 |
| A2 | Fixed source list without a source description protocol; targets stay self-describing | D3, D4 |
| A3 | Two modulation paths: global offsets and a **per-voice poly AT path** with a pressure table per note | D3, E1–E3 |
| A4 | `span` per target | E5 |
| A5 | No audio-rate code, no via, no envelopes, no polarity options | D5, "lean" |
| A6 | Whole grid in one SysEx frame; every reply confirms with the CRC-32 of the patch | D9, E9 |
| A7 | Own power-loss-safe QSPI procedure instead of `PersistentStorage`; device calibration separate from the patch | D8, E8, E11 |
| A8 | Measurement firmware `modules/mod-on-daisy/bench/` in phase 2; Toy Piano integration as a separate step v0.3 | D1, D10, E4 |

## 4. Addendum: decisions of 23.10.2026

The open points from the first version of this document have been decided:

| No. | Topic | Decision | Implementation |
| --- | --- | --- | --- |
| N1 | Poly AT on global targets | **Pressure of the most recently struck, held note** (last note) | `LiveState`, `AllowedCells`; only `latched` targets stay locked for poly AT |
| N2 | Meaning of depth 100 | **defined per target** (`span`), because each parameter has a different musical meaning | `TargetInfo::span`, `span_text`; the editor shows the range in the column header |
| N3 | Toy Piano | may get new targets. It is a **development environment**, not an actual instrument; this is where things are tried out | Toy Piano v0.3 as the next integration step (ARCHITECTURE § 6.3) |
| N4 | LFO parameters | Waveforms **sine, triangle, saw rising and falling, square, sample & hold**; **switchable synchronization**: free-running, MIDI clock or one of the gate inputs | parameters Rate, Shape, Sync, Gate, Division, Retrigger (ARCHITECTURE § 3.3, § 4.2) |
| N5 | Random/S&H | same sync options as the LFOs (free, MIDI clock, gate); mode stepped or smooth | an addition of mine for a uniform operating concept, with little effort because the same clock logic is used. Can be removed again |
| N6 | Measurement firmware three levels deep | `common/desktop.mk` sets `DAISY_ROOT ?= ../..` instead of `:=` (backwards compatible) | implemented; all instruments build unchanged |

Phase 2 is thereby approved and implemented in software. The status is in
[VALIDATION.md](VALIDATION.md).

**Clarifications on my part** (without influence on the decisions, can be changed if
needed):

* **Divisions with MIDI clock**: 4 bars to 1/32 including the triplets 1/4T, 1/8T,
  1/16T.
* **Divisions with gate**: 1 cycle per 4, per 2 or per 1 pulse; 2 or 4 cycles per
  pulse. Between the pulses the measured period interpolates.
* **MIDI Start** sets synchronized LFOs to the start of the bar.
* **Clock stop**: if the clock stops, synchronized LFOs halt instead of continuing to
  run freely.
* **Retrigger** only affects free-running LFOs.
