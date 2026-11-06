# Filter core design

How the filter works, where its numbers come from, and what was measured. All numbers
here are host measurements on the real core (`make -C modules/filter-core measure` and
`test`), **MEASURED 2026-11-06 on the build machine**. Kit results are **NOT MEASURED**.

## 1. Structure

Four trapezoidal (TPT) one-poles in series with global feedback from the fourth stage. With
`g = tan(pi f / fs)`, `G = g / (1 + g)` and the stored states `s1..s4`, one stage is

```text
y = G x + s / (1 + g)        s <- 2 y - s
```

so the fourth stage is `y4 = G^4 u + S` with `S = (G^3 s1 + G^2 s2 + G s3 + s4) / (1 + g)`.
The loop input `u = x - k y4` then has the closed-form solution

```text
u = (x - k S) / (1 + k G^4)
```

No iteration is needed, since the loop is linear within a sample. Zavalishin derives the
same solve for the ladder in *The Art of VA Filter Design*, chapter on ladder filters.

## 2. Why k = 4 at every cutoff

The bilinear transform maps the analog response onto the digital one exactly at the warped
frequency, so at the cutoff each stage is exactly `1 / (1 + j)`: gain `1 / sqrt(2)`, phase
-45 degrees. Four stages give -180 degrees and gain 1/4. The loop therefore reaches unity
at `k = 4` for every cutoff up to Nyquist, with no tuning table for resonance.

## 3. The limiter and its level

The only nonlinearity is `f(x) = x a^2 / (a^2 + x^2)` on the new value of `s1`, held at
`a / 2` beyond `x = a`, where the curve would otherwise fall again. It acts on a stored
state, so it takes effect on the next sample and the solve above stays linear.

A sine of amplitude `A` through `f` has the describing function

```text
N(r) = 2 (1 - 1 / sqrt(1 + r^2)) / r^2,   r = A / a,   N = 1 - 3 r^2 / 4 + ...
```

Multiplying the state by `rho = N` makes the first stage leak:

```text
H1(z) = G (1 + rho z^-1) / (1 - rho (1 - 2 G) z^-1)
```

Differentiating at `rho = 1` and at the cutoff gives a closed form:

```text
d ln H1 / d rho = (1 + g^2) (1 - j) / (4 g)
```

With `delta = 1 - rho`, the leak costs gain and adds phase in equal parts. The phase has to
be taken back by the other stages: the oscillation moves up by `du = delta c / 2` in units
of the warped frequency, with `c = (1 + g^2) / (4 g)`, and that costs as much gain again.
The loop balances when the total loss equals the excess feedback `ln(k / 4)`:

```text
delta = ln(k / 4) 2 g / (1 + g^2) = ln(k / 4) sin(w)        du = ln(k / 4) / 4
```

So the state must be limited by an amount proportional to `sin(w)`, and for a low cutoff
that is very little. Since `delta` grows with `r^2`, this is where the `1 / sqrt(g)` in the
level comes from. In small-signal form, for a level `A` at the fourth stage:

```text
a = A (1 + g^2) sqrt(3 / (g ln(k / 4)))
```

`note_table.h` uses the exact inverse of `N(r)` rather than this form. The level is fixed
for `k = kMaxFeedback`, not for the current `k`, so between 4 and the maximum the level grows
as `sqrt(ln(k / 4))`: the oscillation fades in as resonance rises.

The same `du` means the oscillation runs sharp by a factor `1 + ln(k / 4) / 4`, 41 cents at
`k = 4.4`, independent of the cutoff. The core takes back `3 log2(k / 4)` semitones.

## 4. Tap mixing and level per mode

With `w = 1 / (1 + s)` per stage, the taps are `y_i = w^i u` and the closed loop divides by
`1 + k w^4`.

| Mode | Mix | Notes |
| --- | --- | --- |
| LP 6/12/18/24 | `y1` .. `y4` | Input gain `1 + c k` |
| BP | `4 (y2 - 2 y3 + y4)` | 12 dB/oct each side, 0 dB at the cutoff for k = 0 |
| HP | `u - 4 y1 + 6 y2 - 4 y3 + y4` | 24 dB/oct |
| Notch | `x - alpha (y1 - y2)`, `alpha = 2 (1 - k / 4)` | Exact zero at the cutoff for every k, unity on both sides, resonance narrows it |
| Phaser | `(1 + (2 w - 1)^4) / 2 + c k (2 w^3 - 2 w^2 + w)` | Notches at 0.41 and 2.41 times the cutoff, the resonance peak between them |

Two terms need explaining:

* **Notch:** the classic mix `u - 2 y1 + 2 y2` has its zero on the resonance pole. At `k = 4`
  they cancel and the notch vanishes while the loop still rings. Using the input `x` and
  scaling the band-pass by `alpha` keeps both shelves at unity. The notch cannot oscillate
  audibly, so its feedback stops at `kNotchFeedback = 3.8`. **Notch is the one mode that does
  not self-oscillate, by design.**
* **Phaser:** `2 w^3 - 2 w^2 + w` is 1 at DC, 0 at infinity and 0 at the cutoff, so `c k` times
  this term restores the bass without touching the resonance peak. It does fill the notches
  as `k` rises: `c` trades deep notches (`c = 0`) against full bass (`c = 1`).

The input gain `1 + c k` applies to the low-passes only. Applied to the others it would just
raise the top end by 14 dB. `c` is 0, 0.5 or 1 in Filter FX; the core takes any value.

Each mode's limiter level is divided by its tap gain, the mode's level relative to `y4` at
the oscillation frequency, which sits at `u = 1 + ln(k / 4) / 4 = 1.024`: `|1 + j u|^n` for the
low-passes, `4 u^2` for BP. HP (0.9) and Phaser (4.4) are measured values, because the leaking
first stage weighs most in those mixes. `kLevelTrim` (1.06, +0.5 dB) centres the remaining
error, which is a single constant and the only fitted value in the level formula.

### Limiter level follows the feedback

The first version kept `a` the same at all resonance values. The tests showed the cost: the
limiter's leak disturbs the cancelling tap mixes. At -20 dBFS the HP stopband stopped at
-76 dB, and since the leak grows with the square of the level, that extrapolates to about
-36 dB at full scale. The leak is only needed near oscillation, so `a` now scales with
`kMaxFeedback / k`. At `k = 4.4` nothing changes. At `k = 0` the filter is linear.

## 5. Choice of the maximum feedback

More excess makes the oscillation start sooner, because the growth rate is about
`2 pi f ln(k / 4) / 4`, and less keeps it purer and flatter. Prototype without pitch or level
correction, LP24, MEASURED 2026-11-06:

| k at 100 % | Level error, 30 Hz to 20 kHz | Pitch error | THD at 1 kHz |
| --- | --- | --- | --- |
| 4.13 | -0.05 to -0.23 dB | +14 cents | -70 dB |
| **4.4 (used)** | -0.15 to -0.61 dB | +40 cents | -62 dB |
| 5.0 | -0.34 to -1.38 dB | +91 cents | -54 dB |

## 6. Measurements

**Self-oscillation level** in dB against the 0.5 target, resonance 100 %, no input,
fundamental amplitude:

| Hz | LP6 | LP12 | LP18 | LP24 | BP | HP | Phaser |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 30 | 0.34 | 0.33 | 0.35 | 0.34 | 0.34 | 0.40 | 0.75 |
| 125 | 0.34 | 0.32 | 0.34 | 0.34 | 0.34 | 0.39 | 0.74 |
| 500 | 0.30 | 0.29 | 0.30 | 0.30 | 0.30 | 0.36 | 0.71 |
| 2000 | 0.16 | 0.15 | 0.17 | 0.16 | 0.16 | 0.23 | 0.57 |
| 8000 | -0.33 | -0.34 | -0.33 | -0.34 | -0.33 | -0.18 | 0.08 |
| 16000 | -0.57 | -0.59 | -0.59 | -0.60 | -0.56 | -0.29 | -0.16 |
| 19000 | -0.49 | -0.64 | -0.53 | -0.59 | -0.50 | -0.18 | -0.10 |

Range -0.64 to +0.75 dB over all modes and cutoffs.

**LP24 oscillation:** pitch, purity and the time to reach the level from silence, started
only by the -120 dBFS noise. An input signal starts it much sooner.

| Hz | Pitch error (cents) | THD (dB) | Start (s) |
| --- | --- | --- | --- |
| 30 | -1.0 | -55.0 | 4.13 |
| 125 | -0.9 | -45.8 | 0.94 |
| 500 | -1.0 | -56.6 | 0.22 |
| 2000 | -1.3 | -60.8 | 0.05 |
| 8000 | -6.6 | -81.0 | 0.015 |
| 16000 | -22.4 | below -100 | 0.015 |
| 19000 | -29.1 | below -100 | 0.021 |

The correction is applied in note units and the error is in `g`, which is why it grows at
the top. The THD figures at low cutoffs include leakage of the measurement window.

**Other results from `make test`:**

| Check | Result |
| --- | --- |
| Response of all 8 modes against the exact closed-loop formula, k up to 3.5, c 0 and 1 | Largest error 0.013 dB |
| Slopes at 100 Hz cutoff | LP 6.0 / 12.0 / 18.0 / 24.0, HP 23.9, BP 11.8 / 11.9 dB/oct |
| Note table against libm | Largest error 0.38 cents |
| Notch depth at resonance 0 / 50 / 100 % | -80.8 / -70.4 / -45.2 dB |
| Phaser notches, resonance 0 | -54.7 / -40.4 dB, centre 0.0 dB |
| HP stopband four octaves down, full scale, resonance 0 | -94.7 dB |
| Full-scale noise, resonance 100 %, cutoff jumping over the whole range every sample | Finite in all modes and both paths; largest output 20.5 times the input peak |
| Full-scale sine on the oscillating cutoff (40 Hz, 1 kHz, 15 kHz) | Output peak 1.84 / 1.79 / 2.27 |
| Cutoff sweep, zipper residue (fourth difference) | Ramped -117.1 dB, held per block -76.3 dB |
| 30 s of silence after a signal | No subnormal value in state or output |

**Distortion, sine at the cutoff, bass compensation 1.** THD in dB. Resonance 0 is linear
down to the numeric floor. The Notch column is omitted, because the tone sits in the notch.

| Resonance | Level | LP6 | LP12 | LP18 | LP24 | BP | HP | Phaser |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 % | 1.0 | -114 | -127 | -141 | -155 | -110 | -114 | -100 |
| 50 % | 0.25 | -38 | -51 | -64 | -77 | -53 | -57 | -49 |
| 50 % | 1.0 | -24 | -34 | -45 | -56 | -33 | -33 | -31 |

## 7. Cost

Static estimate from the ARM compiler's output with the firmware's flags, **not a kit
measurement** (`make -C modules/filter-core cost`, 2026-11-06). The low figure assumes one
instruction per cycle, the high one two, both plus 16 cycles per division. Budget at
480 MHz and 48 kHz: 10 000 cycles per sample.

| Path | Instructions | Divisions | Square roots | Cycles per sample | Budget |
| --- | --- | --- | --- | --- | --- |
| `Process` (ramped, with or without FM) | about 80 | 2 | 0 | 110 to 185 | 1.1 to 1.9 % |
| `ProcessHeld` | 52 | 1 | 0 | 67 to 118 | 0.7 to 1.2 % |

`tan`, `G`, the limiter level and its reciprocal come from the note table, which is why the
ramped path has no square root and only one division beyond the limiter's. The figures do
not include the rest of an instrument, and a held coefficient set must be recomputed per
call (one division, one table look-up).

## 8. Limits

* 48 kHz only: the table is built for that rate.
* No oversampling. The limiter is the only nonlinearity and sits inside the loop, so
  aliasing is low in the measurements above, but it has not been judged by ear.
* Below about 60 Hz the oscillation takes seconds to start from silence.
* The pitch correction assumes full oscillation, and it leaves up to 29 cents at 19 kHz.

## Sources

* Vadim Zavalishin, *The Art of VA Filter Design*, Native Instruments: trapezoidal
  integrators, zero-delay feedback and the ladder solve.
* Antti Huovilainen, "Non-linear digital implementation of the Moog ladder filter", DAFx 2004:
  ladder tap behaviour and feedback limit.
* Arthur Gelb and Wallace Vander Velde, *Multiple-Input Describing Functions and Nonlinear
  System Design*, McGraw-Hill, 1968: the describing function method used in section 3.

The limiter level, the mode mixes and all constants here are derived or measured in this
repository.
