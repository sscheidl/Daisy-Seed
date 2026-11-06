# Filter core

Reusable multimode filter: a zero-delay-feedback ladder of four trapezoidal one-poles, one
rational limiter, eight modes by tap mixing, and self-oscillation at one calibrated level.
Header-only, no hardware access, no allocation, no libm. Component ID `filter-core`.

| Status | |
| --- | --- |
| Version | 0.1.0 (`include/filter_core/version.h`) |
| Software | Host and sanitizer tests passed (see CHANGELOG) |
| Hardware | NOT MEASURED, first used by [Filter FX](../../effects/filter-fx/) |
| Design | [docs/DESIGN.md](docs/DESIGN.md): derivation, sources, measurements |

## Use

```cpp
#include "filter_core/ladder.h"
using namespace seed::dsp::filter;

Ladder filter;              // mono: stereo and voices are several instances
Parameters p;
p.cutoff_note = 72.5f;      // MIDI note units, fractions allowed
p.resonance = 0.8f;         // 0..1, self-oscillation from 4 / 4.4 = 0.91
p.bass_comp = 1.f;          // c in the input gain 1 + c k
p.mode = Mode::Lp24;
filter.Set(p);              // once per block
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
reach about 2.3, so a host needs its own output protection.

## Which path

| Path | Cost | Use |
| --- | --- | --- |
| `Process` | 110 to 185 cycles per sample, estimated | Effects, fast modulation, audio-rate input |
| `ProcessHeld` | 67 to 118 cycles per sample, estimated | Many voices, called every 8 to 48 samples |

These are static estimates, see [DESIGN.md](docs/DESIGN.md) 7. On the kit the cost per voice
is still NOT MEASURED.

## Build and test

```bash
make -C modules/filter-core test           # host tests, golden hash 0x150EB967
make -C modules/filter-core test-sanitize
make -C modules/filter-core measure        # the tables of DESIGN.md
make -C modules/filter-core cost           # static Cortex-M7 estimate, needs the ARM compiler
```

The golden hash pins the sound of this version. It changes only on purpose, with the reason
in the changelog.
