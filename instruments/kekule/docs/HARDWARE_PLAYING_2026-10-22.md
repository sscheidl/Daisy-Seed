# Practical playing and automation — 2026-10-22

**Repeated model-switch callback deadline overruns found at ordinary note rates.**

User assessment: changing models during performance is uncommon in the intended
use. The measured switch-time spikes are accepted as a documented limitation of
the current four-voice instrument, not a blocker for that use. No audible problem
was reported in the earlier functional test.

After the [settled-model measurement](HARDWARE_RESULTS_2026-10-22.md), the user
requested automated simulation of normal playing and knob movements. These
follow-ups used actual Daisy callback telemetry and conservative MIDI schedules.
Final master remained zero. No firmware was changed or flashed.

## Results

| Scenario | Duration | Requested notes per chord | Moving controls | Highest smoothed average (%) | Callback peak (%) | Deadline overruns | Captured blocks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Initial single-knob rehearsal | 180 s | 4 | 1 | 43.27 | 141.54 | 12 | 180053 |
| Two-handed control scenario | 60 s | 4 | 2 | 43.20 | 141.58 | 4 | 60019 |
| MIDI automation | 60 s | 8 | 8 | 42.64 | 136.16 | 4 | 60013 |

All runs processed **four actual DSP voices**. Eight requested MIDI notes exercise
four-voice allocation/stealing; this is not an eight-voice CPU result.
No sixteen-note case or event flood was used.

There were **20 deadline overruns in 300085 captured callbacks**, all in snapshot
intervals containing switches into models 2, 3, 5 or 6. In every complete pass,
those four transitions each added one overrun; entering Modal and FM added none.
The worst measured callback took approximately **1.416 ms against a 1 ms budget**.
Peaks are cumulative per run, not per-model peaks. The average column is the
maximum reported library-smoothed average, not the worst individual callback.
The earlier settled-model zero-overrun result does not cover model changes.

## Schedule and scope

- One chord per second; two per second during five-second passages starting
  20 seconds into each 45-second cycle. Four-note runs request at most eight
  Note Ons per second; the eight-note MIDI run at most sixteen. Total Note Ons:
  800, 260 and 520, respectively.
- Four-note offsets: 0, 4, 7, 12 semitones; eight-note chords add 16, 19, 24, 28.
  Roots cycle through MIDI 48, 53, 55, 60, 57, 50; velocities through
  72, 85, 100, 112. Note Off is sent after 65% of each interval; resonators
  intentionally continue ringing.
- Models advance 1 through 6 every ten seconds, repeating through CC28.
  One counter reset per run preserves all subsequent switch costs.
- Moving controllers: CC21, 22, 23, 24, 25, 26, 27, 20, the eight physical knob
  functions. Each twelve-second gesture moves from its starting position toward
  zero over four seconds, toward full scale over four seconds, then back over
  four seconds. Updates are approximately 10 Hz; actual sampled ranges are in
  metadata, and not every endpoint was hit exactly.
- One-knob mode rotates individual controls; two-knob mode rotates adjacent pairs
  in that list; the MIDI scenario moves all eight together. The latter represents
  DAW automation, not eight simultaneous physical hand movements.
- Normal targets: CC20–24=64, CC25=100, CC26=64, CC27=32, CC30=64, CC31=88,
  CC32=51, CC33=0; pitch bend centered. CC20=64 is approximately +0.189 semitones.
  Other controls retain normal values. Muted master excludes the final output
  limiter's nonlinear knee.
- Telemetry polling is 1 Hz, with a final request after playing ends; final
  snapshots cover the full requested intervals. Normal parameters and model 1
  are restored afterward, with output still muted.

MIDI supplies the events. Timing covers controls, MIDI consumption and synthesis
inside the callback; USB parsing/serialization outside it is excluded. Physical
pot movement/filtering, switch debounce and audible clicks are not simulated.

## Interpretation and next action

Running synthesis has headroom. The repeated switch cost is a known transition
limitation, accepted by the user for the intended four-voice use. The user's
earlier report of click-free operation remains valid; muted testing cannot
determine whether these measured overruns produce audible dropouts.

Source inspection suggests synchronous initialization as a candidate:
`Part::ConfigureResonators()` initializes all eight internal string processors
inside the callback on entering the four string-based models; `String::Init()`
clears their delay lines. This matches the affected model groups, but these
measurements do not separately profile those initialization calls.

Profiling/reducing switch-time work is an optional future optimization if seamless
live model changes become a requirement; no current redesign is requested.
Any such change must preserve the global reverb tail. Six/eight actual voices and
a modulation matrix remain unimplemented/unmeasured and require their own normal
playing measurements. Retain the transition result when evaluating those changes.

## Raw evidence

CSV bytes are preserved. JSON contains schedule metadata, commanded model-change
times, sampled controller ranges, event counts and the final snapshot.

| Capture | CSV | Metadata | CSV SHA-256 |
| --- | --- | --- | --- |
| One knob | [CSV](measurements/2026-10-22-one-knob.csv) | [JSON](measurements/2026-10-22-one-knob.json) | `3eb62b85b733988a68199b73700cbfb2fe5903a67a41c22f99f4c54f57c15e16` |
| Two knobs | [CSV](measurements/2026-10-22-two-knobs.csv) | [JSON](measurements/2026-10-22-two-knobs.json) | `43bad8fa25ce39cf08bb3ae28b95d84d399bead9187aa14f00f8535c2a53d1ba` |
| Eight-knob MIDI | [CSV](measurements/2026-10-22-midi-eight-knobs.csv) | [JSON](measurements/2026-10-22-midi-eight-knobs.json) | `2260d98d8cfc850ff4d6293afe03fb81d73786b643a0a3581a4296c6081f680b` |
