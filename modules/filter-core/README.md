# Filter core

Reusable multimode filter: a zero-delay-feedback ladder of four trapezoidal one-poles, eight
modes by tap mixing, and self-oscillation at one calibrated level. On top of the clean core:
adjustable saturation inside the loop (`drive`) and analogue imperfection (`age`).
Header-only, no hardware access, no allocation, no libm. Component ID `filter-core`.

| Status | |
| --- | --- |
| Version | 0.1.0 (`include/filter_core/version.h`) |
| Software | Host and sanitizer tests passed (see CHANGELOG) |
| Hardware | NOT MEASURED, first used by [`toy-piano-filter`](../../instruments/toy-piano/docs/FILTER.md) |
| Design | [docs/DESIGN.md](docs/DESIGN.md): derivation, sources, measurements |

## Use

```cpp
#include "filter_core/ladder.h"
using namespace seed::dsp::filter;

Ladder filter(seed);        // mono: stereo and voices are several instances, each with its own seed
Parameters p;
p.cutoff_note = 72.5f;      // MIDI note units, fractions allowed
p.resonance = 0.8f;         // 0..1, self-oscillation from 4 / 4.4 = 0.91
p.bass_comp = 1.f;          // c in the input gain 1 + c k
p.mode = Mode::Lp24;
p.drive = 0.4f;             // 0 clean .. 1 clearly driven, inside the loop
p.model = Model::Stages;    // or Model::Sum (cheaper), Model::Clean (ignores drive)
p.age = 0.2f;               // 0 new .. 1 broken: tolerance, drift, jitter
filter.Set(p);              // when something changes
filter.SetCutoff(note);     // per-voice cutoff input: envelopes and matrix add up here
filter.Process(in, out, n);             // cutoff ramps per sample
filter.Process(in, out, n, fm);         // plus fm[i] semitones: audio-rate cutoff input
filter.ProcessHeld(in, out, 8);         // cheaper: coefficients held for the call
```

| Mode | Response |
| --- | --- |
| `Lp6`, `Lp12`, `Lp18`, `Lp24` | Low-pass, 6 to 24 dB/oct |
| `Bp` | Band-pass, 12 dB/oct each side |
| `Hp` | High-pass, 24 dB/oct |
| `Notch` | Exact notch, narrowed by resonance. Does not self-oscillate |
| `Phaser` | Two notches around a resonant centre |

Resonance, mode and bass compensation settle over 3 ms without a step, so all of them can
be changed while running. The cutoff ramps linearly from the previous call's target to this
one's. Input outside ±10 000 and NaN count as silence. A -120 dBFS noise inside the core
starts self-oscillation from silence and keeps the state out of the denormal range.

Self-oscillation peaks at 0.5 (`kOscLevel`). A full-scale tone on the oscillating cutoff can
reach about 2.3, and far more at full drive, so a host needs its own output protection.

Drive 0 and age 0 are the clean core, bit for bit. With drive, harder input pushes back on the
resonance. See [DESIGN.md](docs/DESIGN.md) 5 and 6, also for the choice of models.

## Which path

| Setting | Cycles per sample, estimated |
| --- | --- |
| Clean, `ProcessHeld` | 67 to 118 |
| Clean, `Process` | 110 to 185 |
| Age on, no drive | 132 to 232 |
| Drive, `Model::Sum` | 164 to 280 |
| Drive, `Model::Stages` | 272 to 480 |

Use `Process` for effects, fast modulation and audio-rate input, and `ProcessHeld` for many
voices, called every 8 to 48 samples. These are static estimates, see
[DESIGN.md](docs/DESIGN.md) 9. On the kit the cost per voice is still NOT MEASURED.

## Build and test

```bash
make -C modules/filter-core test           # host tests, golden hash 0x150EB967
make -C modules/filter-core test-sanitize
make -C modules/filter-core measure        # the tables of DESIGN.md
make -C modules/filter-core cost           # static Cortex-M7 estimate, needs the ARM compiler
```

The golden hash pins the sound of this version. It changes only on purpose, with the reason
in the changelog.
