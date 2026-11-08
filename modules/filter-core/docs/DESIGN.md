# Filter core design

How the filter works, where its numbers come from, and what was measured. Numbers are host
measurements on the real core (`make -C modules/filter-core measure` and `test`) unless a
table says otherwise, **MEASURED 2026-11-06 on the build machine**. Kit results are **NOT
MEASURED**.

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

In the clean core the only nonlinearity is `f(x) = x a^2 / (a^2 + x^2)` on the new value of `s1`, held at
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

## 5. Drive: saturation inside the loop

`drive` goes from the clean core (0) to clearly driven (1). The clean core stays as it is: at
drive 0 the same code runs, bit for bit, so the calibration above stays testable.

The saturator is the shared curve of `common/dsp/saturation.h` written as a gain,
`t(x) = (27 + x^2) / (27 + 9 x^2)`, so that `sat(x) = x t(x)`. It costs one division and no
`tanh`. It needs no clamp here: its slope has a double zero at `x = 3` and then rises to
1/9, so the curve stays monotonic and the loop gain never reaches zero.

Each saturator would make the loop nonlinear. Instead `t` is taken from the previous sample
and used as a gain, which leaves a linear solve with per-stage coefficients:

```text
y_i = (s_i + g t_(i-1) y_(i-1)) / (1 + g t_i)      y_0 = x - k y4
y4  = (P x + Q) / (1 + k P)
```

This follows the analogue stage `dy/dt = w (sat(in) - sat(y))`. It is the cheap non-linear
zero-delay method described by Teemu Voipio. Saturating the output of a stage lowers its
cutoff when the signal is loud; saturating the input limits its slew.

### Which model

Prototype, four variants, full-level sine at the given drive gain, MEASURED 2026-11-06. C
with a second pass is the reference for what iteration would add. Cost in divisions per
sample, including the solve and the limiter.

| Variant | Divisions | H3 / H5 / H7 at gain 8 | Stable at k 8, gain 16 |
| --- | --- | --- | --- |
| A: summing point only | 3 | -20 / -34 / -42 dB | Yes |
| B: input of each stage | 3 with shared division | -13 / -22 / -30 dB | Yes |
| C: both sides of each stage | 4 with shared division | -19 / -22 / -24 dB | Yes |
| C, two passes | 8 | -19 / -21 / -23 dB | Yes |

* **`Model::Stages` is C:** the richest upper harmonics, and the cutoff really moves with level.
* **No iteration:** the second pass changes harmonics by about 1 dB at twice the cost. Its one
  gain is pitch at high cutoffs, which matters little in this use.
* **`Model::Sum` is A, the cheap path:** it keeps the pushback at about 60 % of the cost.
* B dropped out: it costs about the same as C and does not move the cutoff.

Divisions are shared: one division gives five reciprocals (the batch inversion trick), which
is why C costs 4 rather than 10.

### Gain structure

| | |
| --- | --- |
| Input gain | up to +30 dB (`kDriveOctaves`) |
| Knee | fades in over the first 12.5 % of the control, then stays |
| Output | takes back half of the gain below the oscillation, so quiet signals get louder with drive and loud ones saturate at about the same level |
| Feedback at 100 % | rises from 4.4 to 8 with the knee, since the saturators lose loop gain. The threshold stays at 91 % |
| Output at 100 % | takes back nothing, which holds self-oscillation near its level |

**Pushback:** a loud input drives the summing point into saturation, its gain `t` falls, and so
does the loop gain. Gain at the cutoff, resonance 82 %, drive 50 %: 15.5 dB for a quiet tone
and -12.9 dB at full scale, against 8.1 and 0.1 dB in the clean core.

### Aliasing

Measured on the real core, cutoff 8 kHz, resonance 60 %, level 0.7. Worst single fold-back
relative to the tone. Oversampling was measured on the prototype at drive gain 8.

| Tone | Drive 25 % | Drive 50 % | Drive 100 % |
| --- | --- | --- | --- |
| 987 Hz, Sum | below -160 dB | -119 dB | -53 dB |
| 987 Hz, Stages | below -160 dB | -84 dB | -45 dB |
| 4987 Hz, Sum | -44 dB | -20 dB | -15 dB |
| 4987 Hz, Stages | -35 dB | -17 dB | -18 dB |

2 times oversampling improves the 5 kHz case by about 20 dB, 4 times by about 60 dB. **Not
used:** it doubles the cost of every voice, the normal playing range sits around 1 kHz where
aliasing is already low, and high notes under full drive are the rare case. Judge this
by ear on the kit, because a measurement cannot say whether it sounds bad.

## 6. Age

`age` goes from 0 (new, the identical filter) to 1 (broken on purpose). One value drives three
imperfections from a seed, so a run can be repeated. It lives in the core and needs no host.

| Imperfection | At 100 % | Scales with | Cost |
| --- | --- | --- | --- |
| Component tolerance: `g` per stage, and the feedback | ±25 % | age^3 | Per call |
| Drift: slow noise on the cutoff, new target every 100 ms, 300 ms glide | ±8 semitones | age^3 | Per call |
| Jitter: fast noise on the cutoff, per call | ±2 semitones | age^5 | Per call |
| Noise at the summing point, which also starts the oscillation | -60 dBFS | age^3 | None |

The steep curves keep the useful range wide. Jitter is steepest because fast noise on the
cutoff becomes audible pitch shake at high resonance. Resonance peak offset over eight
instances: up to 25 cents at 30 %, 75 cents at 60 %, 250 cents at 100 %.

Tolerance needs per-stage coefficients, so any age above 0 uses the general path. That costs
more than drift and jitter do; see section 9.

## 7. Choice of the maximum feedback

More excess makes the oscillation start sooner, because the growth rate is about
`2 pi f ln(k / 4) / 4`, and less keeps it purer and flatter. Prototype without pitch or level
correction, LP24, MEASURED 2026-11-06:

| k at 100 % | Level error, 30 Hz to 20 kHz | Pitch error | THD at 1 kHz |
| --- | --- | --- | --- |
| 4.13 | -0.05 to -0.23 dB | +14 cents | -70 dB |
| **4.4 (used)** | -0.15 to -0.61 dB | +40 cents | -62 dB |
| 5.0 | -0.34 to -1.38 dB | +91 cents | -54 dB |

## 8. Measurements

**Self-oscillation level at drive 0** in dB against the 0.5 target, resonance 100 %, no
input, fundamental amplitude:

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

**Self-oscillation with drive:** -3.3 to +4.1 dB around 0.5, over Sum and Stages, drive 3 to
100 %, LP24, LP12, BP and HP, at 125 Hz, 1 kHz and 8 kHz. It is no longer in tune: Stages runs
up to 102 cents flat at 125 Hz and 76 cents sharp at 8 kHz, Sum up to 124 cents sharp at 8 kHz.
The level is highest at low drive, where the knee is only partly in.

**Harmonics against drive**, 220 Hz at level 0.5, cutoff 2 kHz, resonance 45 %, no bass
compensation, relative to the fundamental:

| Drive | Stages H3 / H5 / H7 | Sum H3 / H5 / H7 |
| --- | --- | --- |
| 0 % | -90 / -151 / -147 dB | -90 / -154 / -153 dB |
| 25 % | -55 / -92 / -118 dB | -58 / -103 / -141 dB |
| 50 % | -39 / -59 / -70 dB | -42 / -72 / -93 dB |
| 75 % | -21 / -25 / -27 dB | -24 / -40 / -49 dB |
| 100 % | -12 / -19 / -24 dB | -13 / -21 / -30 dB |

A voice at full level is 6 dB hotter than this tone, so the same harmonics come earlier on the
control. All curves are symmetrical: only odd harmonics.

**Distortion at drive 0, sine at the cutoff, bass compensation 1.** THD in dB. Resonance 0 is linear
down to the numeric floor. The Notch column is omitted, because the tone sits in the notch.

| Resonance | Level | LP6 | LP12 | LP18 | LP24 | BP | HP | Phaser |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 % | 1.0 | -114 | -127 | -141 | -155 | -110 | -114 | -100 |
| 50 % | 0.25 | -38 | -51 | -64 | -77 | -53 | -57 | -49 |
| 50 % | 1.0 | -24 | -34 | -45 | -56 | -33 | -33 | -31 |

## 9. Cost

Static estimate from the ARM compiler's output with the firmware's flags, **not a kit
measurement** (`make -C modules/filter-core cost`, 2026-11-06). Low assumes one instruction per
cycle, high two, both plus 16 cycles per division. Both sides of a branch count, so the
figures lean high. Budget at 480 MHz and 48 kHz: 10 000 cycles per sample.

| One sample | Instructions | Divisions | Cycles | Budget |
| --- | --- | --- | --- | --- |
| Clean, held | 52 | 1 | 67 to 118 | 0.7 to 1.2 % |
| Clean, ramped | about 80 | 2 | 110 to 185 | 1.1 to 1.9 % |
| Age on, no drive | 102 | 2 | 132 to 232 | 1.3 to 2.3 % |
| Drive, `Sum` | 119 | 3 | 164 to 280 | 1.6 to 2.8 % |
| Drive, `Stages` | 212 | 4 | 272 to 480 | 2.7 to 4.8 % |

Each call adds about 290 to 560 cycles for the moving values and the table look-up, which is
18 to 35 cycles per sample at 16 samples per call. There are no square roots anywhere.

`tan`, `G`, `g`, the limiter level and its reciprocal come from the note table.

## 10. Limits

* 48 kHz only: the table is built for that rate.
* No oversampling; see the aliasing table in section 5.
* Below about 60 Hz the clean oscillation takes seconds to start from silence.
* The pitch correction assumes the clean core at full oscillation. With drive the oscillation
  is not in tune.
* The saturators are symmetrical, so there are no even harmonics.
* Drive values stay constant within a call and settle over 3 ms. The cutoff is the only input
  with audio-rate support.
* Age 100 %, drive 100 %, resonance 100 % and full-scale noise reached an output of 129: bounded,
  but a host needs its own output protection.

## Sources

* Vadim Zavalishin, *The Art of VA Filter Design*, Native Instruments: trapezoidal
  integrators, zero-delay feedback and the ladder solve.
* Antti Huovilainen, "Non-linear digital implementation of the Moog ladder filter", DAFx 2004:
  ladder tap behaviour, feedback limit and saturation in each stage.
* Teemu Voipio (mystran), "Cheap non-linear zero-delay filters", KVR Audio forum, 2012: the
  nonlinearity as a gain from the previous sample.
* Arthur Gelb and Wallace Vander Velde, *Multiple-Input Describing Functions and Nonlinear
  System Design*, McGraw-Hill, 1968: the describing function method used in section 3.

The limiter level, the mode mixes and all constants here are derived or measured in this
repository.
