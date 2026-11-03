# Streichwurst: sound fidelity per module

As of 01.11.2026, instrument 0.1.0, FX 0.1.0, product 0.1.0. Based on the
self-written findings in [research/](research/) with their evidence grades
(CONFIRMED, PLAUSIBLE, OPEN; in the German research notes BESTÄTIGT, PLAUSIBEL, OFFEN).
No manufacturer data, tables, presets or code translations; all data are own formulas
or derived from the documented calibration points. **Listening comparison with the original
plugin or device: NOT MEASURED.** Passed software tests do not prove sonic identity.

Columns: documented original relationship (finding ID) · implemented method ·
deviation · expected audible consequence · test · open reference question.

## Instrument (`streichwurst-instrument`)

| Module | Evidence | Method | Deviation | Expected sonic consequence | Test | Open reference question |
| --- | --- | --- | --- | --- | --- | --- |
| Clock generation | SF-C01 CONFIRMED: twelve shared clock objects, upper period = 40000/f(127) | Twelve free-running pitch-class phases at 2·f(highest note of the class); new notes do not reset a phase; pitch bend/Tune/matrix scale all clocks together (smoothed over 5 ms) | Float phases instead of Q24; own table replacement via `exp2` | Shared phases produce the typical absence of beating between equal pitch classes; bend bends all notes equally | `TestClocks`, `TestPitchAndSpectrum` | Edge paths of large bend/transpose values |
| Octave division | SF-C02: level access at a distance of 12 CONFIRMED; divide-down PLAUSIBLE | Binary counter per pitch class; bit b = square wave of note `top − 12b` | Interpretation as a divider chain is an own reading of the PLAUSIBLE finding | Square spectra per octave position; the sum of the footages forms a staircase saw | `TestPitchAndSpectrum` (octave 880 Hz present) | Full origin of each ΔA, octave transitions |
| Event kernel | SF-C02 CONFIRMED: eight contributions per event, row from 8 bits of the fractional time | Own 8-tap band-limiting kernel, 256 fractional rows (windowed sinc, row sum 1), ΔA = new − old output of the toggled note bits | Own coefficients instead of the unknown original kernels | Different aliasing/overtone behaviour near Nyquist; level changes take effect only at the next edge (as in the original event model) | `TestBandLimit` (alias of the 5th harmonic −30 dB below f0) | Kernel coefficients, kernel DC, ring cleanup |
| Leaky integration | SF-C02 CONFIRMED: `z[n] = z[n−1] + r − z/256` | Same recurrence in float, leak converted to 48 kHz (same time constant 6.4 ms) | No integer quantisation | High-pass ≈ 25 Hz: low square waves sag slightly | indirectly via all spectral tests | Edge copies, end-state clearing |
| Envelopes | SF-F01 CONFIRMED: asymmetric one-pole α=k/65536 every 0.4 ms, Crescendo for rising, Release for falling level | Same structure with a physical time constant; update every 16 samples (0.33 ms) with α from τ; cyclic cleanup of one index per update below 8192/2^26 | Control→τ curve: own log-linear curve through the three SF-F02 calibration points (0.577 ms / 151 ms / 4.37 s); intermediate values assumed | Timing close to the original at the three points, own interpolation in between | `TestEnvelopeTimes` (63.2 %/36.8 % after τ) | Full index/UI domain, rounding of small levels |
| Registration | SF-F03 CONFIRMED: z = clamp(256r+m), c = 355z/4096, interpolation of 13 frames of 8 weights each | Same index calculation in float; Animate offset m from the FX | **Own frame data** (formula: moving footage centre, weights halved per octave, sum 1) instead of the unknown original frames | Timbre progression dark→bright present, but not the original colours | `TestRegistrationAndOctaves` | Original frames and manufacturer labels |
| Octave switch | SF-F04 CONFIRMED: o=1 → 0.75·neighbour sum, o=2 → shift, limit 2^26 | Exactly this transformation (real-valued), limit 2 relative | — | "Both" is louder than "Base" (no level normalisation, as documented) | `TestRegistrationAndOctaves`, spectral test "Upper" | — |
| Note targets | SF-F05 PLAUSIBLE: T[n−12]+=W0·a, T[n]+=W1·a, further ones in octave steps | Footages −12, 0, +12 … +72 semitones; activation a ∈ (0,1]; targets limited to 4 | Loop end, limits and velocity assignment are own assumptions; strings velocity-independent by default (`string_velocity` 0) | High footages drop out above MIDI 127 | `TestRegistrationAndOctaves` (target = weight per footage) | Full loop invariant, velocity |
| Polyphony/steal strings | 128 logical note levels (no oscillators) | Full 128 levels; computational load depends on events, not on the number of keys | No limit to 16 pitches needed (own deviation from draft 0.2) | Paraphonic behaviour as in the original note model | `TestInvalidParamsAndLongRender` (24 keys, both octaves) | — |
| Solos | SF-A04: eight pool objects; wave generation, envelope modes, stealing OPEN | Eight own voices: PolyBLEP square/saw/pulse morph + two one-pole low-passes (Tone), Attack/Decay/Sustain/Release, modes Decay/Hold/Percussive, global tremolo | **Completely own replacement** | Solo sounds like an own synth, not like the original solo | `TestSoloAllocation`, routing tests | Solo waves, envelopes, tremolo, allocation |
| Events | SF-B02: separate string/solo changes | Sample-accurate segmentation, 16-channel counting, sustain, split/layer fixed per key, zero-block pending queue | Own contract (original offset fidelity undocumented) | — | `TestEventOrderAndBlocks`, `TestNotesSustainRetrigger`, `TestPendingQueue` | Retrigger/sustain edges of the original |

## FX (`streichwurst-fx`)

| Module | Evidence | Method | Deviation | Expected sonic consequence | Test | Open reference question |
| --- | --- | --- | --- | --- | --- | --- |
| Ensemble topology | SF-D01/D03 CONFIRMED: one line of the string sum; mode 0 three taps (first replaces input), 1 input+5, 2 input+3 | Exactly these sum forms on one line | — | The three modes differ in density and level as documented | `TestDcSumsAndStereo`, `TestImpulseNoFeedback` | — |
| Ensemble LFOs | SF-D04/D05 CONFIRMED: piecewise parabola, 0.81/7.77/0.32 Hz, phases ±0x55550000 resp. 0/+135°/−135° | Parabola 4v(1−|v|), exact rates and the documented phase offsets (119.998°), LFO advance every 16 samples | Float instead of integer LSB | Modulation patterns close to the original | `TestDelayRangesAndRates` (periods, ranges) | — |
| Delay positions | SF-D05 CONFIRMED (mode 2 closed form, 0/1 approximation): 248±… samples at 40 kHz | Delays in seconds from the 40 kHz count values (time-scaled), linear position ramp per 16-sample block | Own float interpolation at the real read position (the original quantises the difference before the multiplication) | Slightly less interpolation noise | `TestDelayRangesAndRates` (3.0–9.4 ms, 4.4–8.0 ms) | Correction loops of large position jumps |
| Stereo tap correction | SF-D07 CONFIRMED: L = F − 21/128·ta, R = F − 21/128·tb | Exact, after the phaser | Assignment first cache → left PLAUSIBLE | Stereo width of the original topology; mono sum not cancelled | `TestDcSumsAndStereo` | Channel assignment of the caches |
| Gain words | SF-F06 CONFIRMED: off 1, mode 0/1/2 = 0.8125/0.625/0.75, after solo and reverb | Product multiplies the total output by the (crossfaded) gain word | No own DC normalisation (departure from draft 0.2) | Ensemble on is louder than off (coherent ×2.30/3.65/2.88), solos become quieter along with it | `TestDcSumsAndStereo`, product test ratio 0.625 | Overall output dynamics |
| Bypass/mode change | SF-F08 CONFIRMED: the original freezes the line/phases, no crossfade | **Own rule:** line and LFOs always run; changes crossfade two tap sets of the same line over 20 ms | Deliberate deviation against frozen old delay data | No clicks on changes (original: possible jumps, unmeasured) | `TestBypassAndTransitions` | Audible clicks of the original |
| Animate | SF-D09 PLAUSIBLE: shaping before the ensemble; SF-F03: registration offset m | LFO (own rate) produces m = depth·8000·h; own band emphasis x + 0.3d(LP_fc − LP_200) | Shaping recurrence and depth curve are own | Moving timbre across the registration; not the original shaping | `TestAnimateAndPhaser` | Three shaping contexts, curve v=128(u²+640) |
| Phaser | SF-D10: position CONFIRMED, stages/topology OPEN | Four all-passes 80 Hz–3 kHz, feedback 0.5a, mix x+0.5a(y−x), own rate | Own phaser | Classic 4-stage phaser on the ensemble sum | `TestAnimateAndPhaser` | Number of stages, coefficient table, 0.9 recurrence |
| Reverb | SF-D11: 16384-float ring, 8+2 sub-stages, two phases; topology OPEN | Two diffuser all-passes + four-line FDN (Hadamard, damping 6.5 kHz), g = 0.1+0.83·Decay, Wet 0–50 % | Own, smaller, unmodulated reverb | Different sense of space, no modulation. Heard on the kit 01.11.2026 (human report): somewhat weak and metallic; improvement planned | `TestReverb`, product test (reverb sees solo) | Diffusion/feedback matrix, decay/wet curve; denser, modulated tail against the metallic sound |

## Product (`streichwurst`)

| Module | Evidence | Method | Deviation | Expected sonic consequence | Test | Open reference question |
| --- | --- | --- | --- | --- | --- | --- |
| Signal flow | Engine graph CONFIRMED: Ensemble→Phaser→tap correction, solo afterwards, reverb on the mix, gain afterwards | Exactly this order, solos bypass the string FX | — | — | `TestRouting` | — |
| Balance | SF-F07 CONFIRMED: b=r/64 up to 64, then linear up to exactly 2; centre = both factor 1; τ≈25.4 ms | Continuous form of the same curve, smoothing 25.4 ms | Float instead of high-32 products | No −3 dB centre | `TestBalanceCurve`, `TestBalanceMix` | — |
| Master | SF-F07: channel master smoothed with 1/128 (≈51 ms) | Smoothing 51 ms, own quadratic curve | Curve is own | — | Fingerprint | Channel master domain |
| Gain/headroom | OPEN (output dynamics) | Fixed bus factors 0.25 (strings) / 0.5 (solos), mix headroom 0.35 before the gain word | Own gain plan | Six-voice chord in mode 1 with reverb stays below the knee (peak 0.69) | `TestHeadroom` | Original levels and limiter |
| Output protection | OPEN (0x266E50) | Identity up to 0.8, tanh knee, |y|<1; NaN→0 counted; panic with 2 ms ramp | Own limiter | Soft limiting only when overdriven | `TestPanic` | Original limiting/quantisation curve |
| Sample rate | SF-A05 CONFIRMED: original 40 kHz/hop 16 + host resampler | Directly 48 kHz, all time quantities converted to seconds | No 40 kHz core in the product (rationale: [RATE_COMPARISON.md](RATE_COMPARISON.md)) | Different aliasing patterns than a 40 kHz core | `make compare-rates` | Original resampling filter |

The modules are kept replaceable: registration frames, event kernel, solo,
Animate shaping, phaser and reverb have their own places in the code and can be replaced
with later evidence without changing the module boundaries.
