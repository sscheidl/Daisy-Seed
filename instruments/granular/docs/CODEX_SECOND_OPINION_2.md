# Nebulae 0.2.2 — second opinion on vocoder scheduling

2026-11-09. Source reviewed at `cbd4062` on `claude/nebulae-overload-9c0df4`.
This answers the [second brief](CODEX_SECOND_OPINION_2_BRIEF.md). The owner reports
that the kit is running diagnostic image `5e62053b…`. The quantitative evidence
below comes from the separately identified candidates in the
[2026-11-09 measurement log](OVERLOAD_DIAGNOSIS.md#10-fix-in-022-software-host-tested-hardware-not-measured).
B, C, D, the listening comparison, P2 and P3 are **NOT MEASURED**. This review
used source and the recorded results.

## 1. Plain stagger (B) — agree on the stereo effect, disagree on a fixed comb

Let `p` be the left hop's position in **source** frames and
`r = p − 512 × floor(p/512)` its fractional position inside the grid cell.
With settled controls, no wrap and no retrigger, the right hop sees
`p + Δ`, where `Δ = 256 × SPEED × source_rate / 48000`. The signed change
in grid cells is `floor((r + Δ)/512)`. Both the forward and backward window
origins move with that grid point. Their spacing also depends on the
effective pitch of the block in which the hop runs.

| Settled SPEED, 48 kHz source | Right window's grid cell relative to left |
| --- | --- |
| FREEZE or 0 | Same cell while position and START stay fixed |
| +1 | Same for `r < 256`. Next for `r ≥ 256`. |
| −1 | Previous for `r < 256`. Same for `r ≥ 256`. |
| +2 / −2 | Always one cell ahead / behind |
| +4 / −4 | Always two cells ahead / behind |

At +1 and 48 kHz the phase on this grid can stay fixed across successive
left hops, so the choice need not alternate. At +1 and 44.1 kHz, `Δ = 235.2`
and the next-cell threshold is `276.8`. The left hop's cell phase can drift
from hop to hop. At a loop wrap, START/SIZE move, retrigger, or pitch bend,
compare the actual two block inputs. A loop wrap can jump much farther than
one grid cell. PITCH can change the window samples even when both channels
use the same grid point. [Position and pitch](../src/engine.cpp#L175),
[window origin and lag](../src/reference_dsp.cpp#L241),
[SPEED law](../src/reference_dsp.cpp#L343).

The hop **schedule** has a fixed offset of 256 output samples. The **audio**
is a pure 5.333 ms right delay only when the channels otherwise generate the
same shifted sequence. A two-channel dual-mono file under settled FREEZE and
fixed pitch is a useful example. With identical channels and equal-gain
fold-down, the pure-delay sum has magnitude `|cos(π f × 256/48000)|`.
Its first null is **93.75 Hz** and later nulls are **187.5 Hz apart**. Unequal
channels give shallower nulls. Changing windows, pitch or phase history
can make the colour time-varying and shift or obscure the nulls.

* **Decorrelated stereo.** A weaker fixed comb is expected. Width, depth and
  location of individual attacks can still change.
* **Near-mono stereo.** A shared voice or drum may lean towards the leading
  channel or broaden. A mono sum is more likely to expose spectral colour.
* **Transients.** Listen for a softened or doubled attack, or a moving image
  when the two channels use different source windows.
* **FREEZE.** It removes the position difference once settled. With coherent
  input and fixed pitch this makes the delay and mono colour easier to isolate.
  Sweep or release FREEZE separately to expose changes in window choice.
  A true one-channel WAV uses the mono shortcut and cannot expose B's stereo
  stagger. A dual-mono WAV must actually have **two** channels.
  [Mono shortcut](../src/engine.cpp#L208).

## 2. Compensated stagger (C) — agree for the wet pair under explicit conditions

For uninterrupted playback, C can reproduce the current **wet** output with an
exact one-block shift. Each Mincer must see the same sequence of logical block
inputs, including plain blocks, in the same order. Today each channel owns its
phase and overlap-add state, and the shared FFT/window work area is scratch.
Run left for logical block N in physical block N. In physical block N+1, replay
logical block N into right and output that right block with the saved left N.
At startup, emit one zero wet block before the first valid pair. Replaying
only the right hops would miss the increments and overlap-add of the plain blocks.
[Channel state and scratch](../src/reference_dsp.cpp#L223),
[per-sample advance](../src/reference_dsp.cpp#L308).

* Snapshot the source view, source identity, effective pitch after the rate
  conversion, wet amplitude, position input and frame count. Right needs the
  positions from the block it is replaying. It does not need to copy the
  left channel's phase state. On the 256-frame hardware path a hard reset
  starts the hop at sample 0 and subsequent hops are block-aligned. A scalar
  position or hop descriptor suffices with a checked block-alignment contract.
  The current host-facing Engine accepts smaller blocks, so a reduced
  snapshot needs an explicit contract or a path that preserves those callers.
* Invalidate the pending pair and re-prime both channels on a DSP reset,
  SOURCE, Clear, relevant file generation change, Live bank publication,
  file detach, and overload entry/restart. Treat `S4 RESET` and note retrigger
  separately. Today they change position and start an output ramp, while
  keeping the Mincer phase/tails. Do not accidentally turn those gestures into
  a hard DSP reset. Define how their ramp aligns to the delayed wet pair.
* A queued `PcmView` is a borrowed pointer. Cancel old work before file
  detach releases the loader ACK or a Live bank swap allows that bank to be
  written again. Keep the pair and the source epoch together. At an invalidation
  boundary, use the existing transition policy with explicit warm-up and
  verify that no stale half-pair reaches the mix. Dropping a pending pair
  means the exact shifted relationship applies within uninterrupted epochs.
* For a true mono source, compute left once, apply the same 256-sample wet
  delay, and copy the **delayed** output to right. Exercise mono → stereo,
  stereo → mono and the state-copy fallback if the latter paths can run
  without a hard reset. Never pair a delayed mono block with a fresh channel.
  [Resets and Live bank publication](../src/engine.cpp#L14),
  [generation handling](../src/engine.cpp#L151),
  [mono transition](../src/engine.cpp#L211),
  [detach ACK](../src/engine.cpp#L271), [loader write gate](../src/wav.cpp#L69).

The current mix and overdub still use the current block's BLEND, input, grains,
write position and output ramps. The wet-only pipeline is therefore not a
bit-identical one-block delay of the complete output or recorded bank. The wet
component is written to a later position along the record timeline. In the
special case of a repeated unity-speed wet-only recapture, a component can
shift by one more block per pass, modulo the loop length. Ordinary feedback
mixes, resets, grain scheduling, phase history and dry input make it more
complicated than a universal `k × 256` sample shift. Verify the audible and
recorded result across several complete circular passes, a short loop and a
partial overdub. If preserving the full original mix timing is required, any
compensation must align the other signals, controls and write timeline too.
That is a separate latency contract. [Mix and write path](../src/engine.cpp#L232).

## 3. C audibility — agree that the relative offset can matter

The 2048-sample window spans 42.667 ms. That does not predict how clearly a
5.333 ms shift relative to a dry attack or a grain transient is heard. With a
steady wet-only passage the main new effect is overall delay. With File
BLEND around **0.5**, both wet and grains are audible and an attack can change
shape or separate. In Live mode **0.5 is dry only**. Test **0.25** for an
approximately equal-power wet/dry blend, or sweep across the wet half.
Listen for attack alignment, coloration with correlated layers, a shifted
short-loop seam, and changes after repeated overdub. Audibility remains
**NOT MEASURED**. [File and Live mix](../src/engine.cpp#L195),
[Live BLEND law](../src/reference_dsp.cpp#L375).

## 4. C CPU — agree on reducing the hop peak, unsure on the bound

A 256-frame left float block is 1 KiB. A full 256-double position snapshot is
2 KiB plus metadata. Bounded copies in on-chip RAM are a small amount of
traffic relative to a measured millisecond-scale hop. Reuse the existing
scratch and account for the copies in the map and callback profile. Do not
copy spectra, full overlap-add state or source PCM each block. Alternating
channels changes cache residency. A right hop can be slower than half the
present stereo hop, especially with SDRAM strides, MIDI/control work,
publication or the up-to-32-KiB overdub finalization copy. Plain-block
overlap-add still runs for the other channel.

The brief's **≈3.8 ms** is a plausible planning case
(`4.1/2 + 1.57 + 0.17 = 3.79 ms`). Splitting a 4.75 ms stereo hop the same
way gives **≈4.12 ms** before copy/cache and driver effects. Both calculations
assume a symmetric split and use separately reported costs. Neither predicts
the measured 7.8 ms worst callback in the original near-end run. Measure
even/odd, hop/no-hop, high-stride wrap, source change and Live finalization
on the actual candidate, with total time and missed-period/overload counts.
[Current vocoder and grains dispatch](../src/engine.cpp#L201),
[overdub copy](../src/engine.cpp#L263),
[near-end measurement](OVERLOAD_DIAGNOSIS.md#10-fix-in-022-software-host-tested-hardware-not-measured).

## 5. Listening method — disagree that a runtime switch is needed

Use **one fixed-mode listening image per variant and one DAW capture session**.
The owner flashes each image once, records its takes, then switches
between matched recordings immediately in the DAW. This gives quick A/B/C
switching without an in-firmware mode switch, without another MIDI client, and
without hearing a switch/reset transient on every comparison. Use the same
commit/toolchain, MOD settings and build instrumentation across the three
images. Prefer production-style listening builds and run equivalent PROFILE
builds separately if fine timing is needed. Take the images and SHA-256 records
from the central firmware store. A host render of the same scripted excerpts
can screen B/C before flashing. The existing golden-dump hook is a
starting point for host renders. On-kit captures still decide the product sound
and timing. [Firmware store](../../../docs/FIRMWARE_BINARIES.md),
[host golden-dump hook](../tests/test_golden.cpp#L101).

| Method | Assessment |
| --- | --- |
| **Fixed images + captures** | Recommended. Three fixed builds and one flash per variant for capture. Instant offline audition, repeatable cues and an easy hidden order. |
| Fixed images + live recollection | Same build simplicity, but reboot and reloading separate the stimuli. Useful for feel and controls after the capture comparison. |
| SW17 in a listening-only image | Only two positions for three variants. A chord, cycling state or UI feedback adds a new interaction. It is available for a test selector in NEBULAE mode and already debounced with key suppression. The switch position also cues the listener. |
| Diagnostic SysEx | Can address all three modes and conceal the selection if someone else operates it. Requires a command, mode dispatch and safe audio-boundary reset. A switch while playing still requires a fixed restart and warm-up. It offers little benefit for one comparison. |

If many live iterations later justify a switch, apply the selection in the
audio thread at a defined restart and keep the test control out of production.
For this comparison, matched takes with hidden labels are enough. This uses
the hidden-reference idea in a practical single-listener check.
[SW17 and mode handling](../src/control.cpp#L45),
[ITU-R BS.1116-3](https://www.itu.int/rec/R-REC-BS.1116-3-201502-I/en).

## Listening sheet

**Status** — NOT MEASURED. Date / listener / monitoring / interface — __________

**Record once per variant.** Record L/R together at a fixed interface gain, with
monitor routing and levels held constant. Start each take from the same source,
transport cue and DSP initial state. Reload the sample or restart when needed
for a hard reset. S4 alone only resets the position and ramps the output. Use
the same scripted control changes or recorded settings, settle for at least
one second before steady scoring, and capture the same 10–20 s of audio. For
attack or restart rows, keep the cue and capture from before the event instead.
Keep an independent cue on the DAW timeline and align by it. Do not align
individual wet tracks by their peaks, since that would hide C's latency. Hold
analogue gain fixed. If playback level matching is needed for a preference
pass, apply one linked gain per stereo take and log it. Preserve raw captures,
L/R balance and the unrenormalized mono fold-down.

**Controls.** Start in File playback with START minimum, SIZE maximum, SPEED
and PITCH at their centre detents, fixed WINDOW, no active modulation or held
notes and identical secondary settings. Keep DENSITY/OVERLAP low for the
wet-only pass. Log the actual pot/CC values and the bank/slot. At File
BLEND minimum the vocoder is audible alone. At File BLEND maximum its output
is muted even though its work still runs. Keep a real mono file separate from
two-channel dual mono. Use the same card contents, sample rate and source files
for every take. [File controls and BLEND](../README.md#eight-fixed-pots),
[PROFILE source channels](CONTROL_PROTOCOL.md#L84).

| Take | Material and controls | Listen for |
| --- | --- | --- |
| 1 | 48 kHz **two-channel dual mono**, centred voice or dry drums, File BLEND 0. Capture settled forward and settled FREEZE. Replay each in stereo and `(L+R)/2`. | Centre and attack shape in stereo. Hollow or changing colour in mono. Stable FREEZE tone. |
| 2 | 48 kHz stereo percussion or a near-mono mix with obvious attacks. File BLEND 0, then around 0.5 with repeatable moderate grains. Use the same cue and controls. | Attack width and pull to one side. Wet/grain timing, softness or a double attack. |
| 3 | Decorrelated stereo ambience or music, ideally 44.1 kHz. File BLEND 0, with matching forward/reverse or freeze/release cues. | Image stability, movement, depth and colour across the wrap or gesture. |
| 4, control | Same material as take 1 encoded as **one-channel** PCM WAV. File BLEND 0. | Centre stays stable. With a timing cue, compare whole-wet onset including C's fixed latency. |
| 5, after P2 | Same prerecorded DAW input each time. Short Live loop, BLEND around 0.25, then several equal circular overdubs. Keep the start/publish cues. | Wet/dry attack alignment, seam change, drift or build-up over successive passes. |
| Separate load run | Stereo file, PITCH/DENSITY/OVERLAP maximum and BLEND maximum. Repeat at START 0 and near the end. Then test a wet-audible mixed BLEND separately. | Log overload counts, skips and reachability. In the mixed pass, note gaps separately from stereo/timing differences. Exclude skipped/overrun takes from the normal-load preference comparison. |

**Blind pass.** Have the person preparing the takes hide image/hash labels
behind X/Y/Z, shuffle playback order for each excerpt and keep the key until
notes are finished. For a solo session, use a shuffled playlist with the mapping
hidden until the end if available. Otherwise mark the pass open. Listen once
in stereo and once in mono where indicated. Repeat close comparisons in reverse
order and optionally include a hidden duplicate of one take. First log whether
a difference is repeatable and where it occurs, then preference. “No reliable
difference” is a valid result.

| Excerpt / repeat | Blind label / order | Stereo or mono | Time / heard difference | Preference and confidence | Overrun/skip delta |
| --- | --- | --- | --- | --- | --- | --- |
| ___ | ___ | ___ | ___ | ___ | ___ |
| ___ | ___ | ___ | ___ | ___ | ___ |
| ___ | ___ | ___ | ___ | ___ | ___ |

**Build and settings log.** Variant key (keep hidden) / commit / full BIN
SHA-256 / bank-slot / file and channel count / pot or CC values / modulation
and trigger state / gain / cue / PROFILE window or STATUS deltas — __________

## 6. Arithmetic (D) — unsure about the saving, keep it independent of C

Saving 0.5–0.8 ms is 240,000–384,000 cycles at 480 MHz. There are 4,100
normalizations across a stereo hop. Attributing all of that saving to the
phase loops would require about 59–94 cycles saved per normalization. The
reported phase budget is only about 0.65 ms in total, including bin work and
overlap-add, so the high end requires a material fill saving. The 0.5–0.8 ms
combined figure is a hypothesis. Even if realized, subtracting it from the
5.7 ms planning case leaves 4.9–5.2 ms (92–98%) before additional worst-case
effects. It would not cover the recorded 7.8 ms peak. [Measured split](OVERLOAD_DIAGNOSIS.md#10-fix-in-022-software-host-tested-hardware-not-measured),
[phase loops and overlap-add](../src/reference_dsp.cpp#L262).

Benchmark the approximation and fill separately with the pinned ARM compiler.
Two Newton steps cost arithmetic and the core already supports hardware square
root. In the first loop `1/(sqrt(s)+1e-20)` has an epsilon **after** the root.
A bare `rsqrt(s)` changes the zero and near-zero cases. The second loop has
a tiny-bin fallback and defines a phase for exact cancellation. Preserve
these behaviours in any candidate. Do not narrow the absolute source position
to float. A long mono file can exceed 2²⁴ frames, and fractional interpolation
is lost earlier, especially at slow pitch stride. If there is useful time to
save, measure a bounded local fractional cursor with an integer frame index and
explicit wraps first. SDRAM misses persist unless the access pattern changes.
Run the current phase-aware and phase-independent fidelity checks, with near-zero,
long files, wrap and control changes. Host numerical tests and owner audition
address different failure modes. Combine D with C only if C still needs margin
and the isolated D change earns it.
[Reference rounding spread](OVERLOAD_DIAGNOSIS.md#10-fix-in-022-software-host-tested-hardware-not-measured),
[reader and wrap](../src/pcm.h#L19), [vocoder checks](../tests/test_vocoder.cpp).

## 7. Measurements — agree on the limits

Keep the recorded 3.2–4.1 ms stereo wet times as context. Treat a minimum as a
best observed hop and the 5.7 ms split sum as a planning case. Individual stage
maxima can come from different callbacks. The near-end run actually reached
**7.8 ms** (MEASURED 2026-11-09 on `f41dd580…`). The transient peak after CC
changes is a valid stress case if those changes are supported during play.
Measure settled and transition windows separately. The diagnostic reset clears
the PROFILE window after its reply, while overrun totals are cumulative.
Use deltas with one probe client and check the right build/hash, source channel
count (field 20), window boundaries and skip count. Verify both phases of C
and keep a real stereo stress file, since a mono result cannot certify stereo.
Include wrapper/IRQ time or count elapsed DMA periods independently if the
margin is tight. The earlier mono shortcut result on hardware is **NOT MEASURED**.
[Recorded candidates](OVERLOAD_DIAGNOSIS.md#10-fix-in-022-software-host-tested-hardware-not-measured),
[PROFILE and reset](CONTROL_PROTOCOL.md#L63),
[overrun guard and callback timing](../src/main.cpp#L245).

## Decision order

1. **C** is the first production candidate if both stereo channels stay aligned,
   the owner accepts the wet-layer delay, and the full load/transition and
   record checks pass. A raw wet A/C comparison should be bit-identical after
   shifting by 256 samples within a stable epoch with the guard idle.
2. **D** is the next engineering option if C's latency is objectionable or if
   profiling shows a cheap independent gain is needed. It earns a production
   choice only through measured worst-case headroom and listening.
3. **A** is the reference for every comparison and the conservative fallback for
   normal settings. Keeping it as the shipped extreme-setting behaviour would
   be an explicit decision to accept guard interruptions there.
4. **B** is worth hearing as a deliberate stereo character. Its mono and
   transient behaviour make it the last choice for the fidelity fix.

Claude can prepare the fixed comparison candidates and validate C's scheduling
on the host after the owner's decision. The owner conducts the capture/listening
session. P2 and P3 stay separate follow-ups.
