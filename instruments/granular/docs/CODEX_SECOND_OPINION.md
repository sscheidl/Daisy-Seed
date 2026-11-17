# Nebulae audio overload — Codex second opinion

2026-11-09. Source review at `d34bfd1` on `claude/nebulae-overload-9c0df4`.
Timing marked **MEASURED 2026-11-09** comes from the owner's
[diagnostic run](OVERLOAD_DIAGNOSIS.md#6-results). I checked the existing MOD and MOD
PROFILE maps, disassembled the existing PROFILE ELF with the pinned xPack objdump,
and verified that the local PROFILE BIN has the recorded SHA-256
`be1296fbadb2e737c2d73ee507eb92da7ac936d187d398faed9898c24671f919`.
I ran no new builds, host suites or hardware tests. Candidate timings, P2, P3 and
listening are **NOT MEASURED**. This answers the [brief](CODEX_SECOND_OPINION_BRIEF.md).

## 1. P1 diagnosis — agree

The vocoder alone misses the 5.333 ms deadline by a factor of 5.8–6.4 on hop
blocks. Even the 30.8 ms minimum would use 289% of the CPU if incurred every
10.667 ms at the intended cadence. That explains P1 without invoking the grains,
SD reads during playback or a separate crash. Grains still matter for the repaired
image. The measured 14.013 → 3.163 ms single-channel A/B, with PCM still in SDRAM,
shows that the location of the mutable DSP state materially affects cost. It does
not isolate which accesses account for that cost.

The source identifies two details that refine the IRQ explanation. On Seed3 the SAI1 B **receive** DMA is
`DMA1_Stream1_IRQn` (12), which drives the audio callback, while Stream 0 is SAI1 A
transmit. J7 uses external USB, `OTG_HS_IRQn` (77). Both audio DMA streams and
USB have priority 0, TIM5 has 15, and the application read-only SD driver lowers
SDMMC1 to 2. Equal-priority USB cannot preempt the DMA and loses arbitration if the
receive IRQ is pending at return. Each callback can return and the DMA can then
run again before the main loop gets a turn. That is a source-based explanation for
the starvation, consistent with the guard restoring reachability. The exact
IRQ/half-buffer sequence on the unguarded image is **NOT MEASURED**.
[Seed3 SAI direction](../../../libDaisy/src/daisy_seed.cpp#L303),
[DMA and receive callbacks](../../../libDaisy/src/per/sai.cpp#L234),
[IRQ numbers](../../../libDaisy/Drivers/CMSIS-Device/ST/STM32H7xx/Include/stm32h750xx.h#L72),
[DMA priority](../../../libDaisy/src/sys/dma.c#L15),
[USB priority](../../../libDaisy/src/usbd/usbd_conf.c#L134),
[TIM5](../../../libDaisy/src/per/tim.cpp#L289),
[SD priority override](../../../common/sd/ro_sd.c#L115),
[J7 setup](../src/main.cpp#L543),
[Arm Cortex-M7 Devices Generic User Guide, §2.3.5–6](https://documentation-service.arm.com/static/61efd8352dd99944d05142d1).

The 4.4× ratio is plausible. One radix-2 transform does 11,264 butterflies,
including large-stride passes and a bit reversal over 16 KiB of mutable complex
workspace. The workspace alone fills the nominal 16 KiB D-cache before twiddles,
bit-reversal indices, PCM and the rest of the working set. AXI SRAM still takes
misses, with cheaper refills and writebacks. The configured SDRAM MPU region is
normal, cacheable write-back (TEX=0, C=1, B=1, S=0).
Actual MPU registers and bootloader version matter because Seed startup can
inherit the bootloader's clock and MPU setup. I would verify effective attributes
and clock once and test sequential versus strided access if the split still looks
anomalous. There is no positive evidence here for a refresh fault, disabled cache,
I-cache regression or a denormal-induced stall. PCM16 words always decode to
finite values, even in an uninitialized bank. Content still changes spectral
cancellation and floating-point paths. P2 could share this mechanism **after**
the first Live take is published. Starting the first empty capture has no wet
source, so a freeze on that first press would need separate diagnosis.
[FFT](../src/reference_dsp.cpp#L99), [MPU](../../../libDaisy/src/sys/system.cpp#L521),
[bootloader condition](../../../libDaisy/src/daisy_seed.cpp#L110),
[SDRAM configuration](../../../libDaisy/src/dev/sdram.cpp#L68),
[record publication](../src/engine.cpp#L24), [wet source test](../src/engine.cpp#L188).

## 2. AXI timing and DTCM — agree on plausibility, unsure on the split

1,518,325 cycles is plausible for two 2048-point complex transforms, 4,096 PCM
interpolations, 2,050 normalizations and overlap-add. It is a one-channel boot
minimum, so doubling it to 6.33 ms is a planning baseline, not a stereo runtime
measurement. I would measure the full stereo callback with an otherwise unchanged
Engine in AXI first. In the same block record fill, forward, phase normalization,
inverse, overlap-add, grains, record/copy work and total. A stereo peak can differ
from twice the mono minimum because of cache state, data locality and other
work in the block. The SDRAM split cannot rank these costs in AXI.

Two concrete costs show up in the existing ELF. In `Mincer::Process` the inlined
`PcmView::Read` paths emit integer divide/remainder sequences and take the
`fmod` call on out-of-range positions. The linked `hypotf` comes from
`libstdc++_nano.a(math_stubs_float.o)`. Its nonzero path rescales with **two
floating divides**, calls the `sqrtf` wrapper and rescales back. The phase loops
also divide by the magnitude. That makes phase normalization a credible target
even with hot tables. This is disassembly evidence, with no timing attributed to
those individual instructions.
[PCM reader](../src/pcm.h#L19), [phase loops](../src/reference_dsp.cpp#L183).

The existing MOD map leaves 279,136 B in AXI after `.neb_sd`, including the empty
`.neb_dsp` start. A 247,736 B Engine leaves 31,400 B before additional code/tables
and alignment. The current PROFILE map leaves only 166,336 B **after** its AXI
benchmark object, so adding the whole Engine there as well will not fit. Reuse
the Engine's storage for the AXI benchmark before normal initialization, omit
the redundant AXI benchmark in the placement variant, or split storage explicitly.
Keep PCM and Live banks in SDRAM and keep SD IDMA in its dedicated aligned sector.
Recheck the file boundary/capacity, stack and both product maps. The ELF checker
currently looks for both `live_banks` and `engine_storage` in SDRAM and must be
updated for the deliberate placement change while continuing to verify the banks.

The DTCM headroom above the 16 KiB stack floor is 13,012 B for MOD and 9,844 B for
MOD PROFILE. Sharing the current 16 KiB complex scratch between channels removes
duplication but still does not fit DTCM. Prefer shared scratch in AXI, under
single audio ownership, or consider an 8 KiB real-FFT workspace in DTCM only if
the chosen transform uses it and the target A/B shows a gain. Keep a checked
stack margin and never pass that scratch to DMA.
[Linker regions](../src/memory.ld#L1), [ELF checker](../../../scripts/check-granular-elf.py#L66).

## 3. FFT — agree on pinned CMSIS-DSP, prefer comparing two layouts

Start with **two 2048-point real forwards and one real inverse per channel** via
`arm_rfft_fast_f32`. Each uses a 1024-point CFFT internally. The pinned scalar
implementation uses the same `[DC, Nyq, re1, im1, …]` convention. Use distinct
input/output buffers for the merge/split and check scaling with impulse, DC,
Nyquist, single-bin and round-trip tests. Preserve the Mincer's existing special
bin handling and overlap-add gain. The current complex inverse scales by
`1/N`; CMSIS normalizes the inverse, so do not add a second normalization.

Benchmark this against **one interleaved 2048-point `arm_cfft_f32` forward pair
plus one 2048-point real inverse**. Keep the existing `Z=A+jB` conjugate split
and request natural order with the bit-reversal flag. The forward pair is for
backward and forward windows of *one channel*, so the alternative still runs
once per channel. The two-real-forward route has a smaller hot complex workspace
but adds two extraction passes. The packed route has a larger transform and
its own split pass. Operation counts alone cannot settle which wins on M7.
Keep the reference backend available for A/B. Compile the required pinned
Apache-2.0 sources/tables in the product build, keep the library and toolchain
pins, and inspect the MAP for retained table sizes. The M7 uses the scalar
backend. A custom radix-4 is lower priority unless a profiled bottleneck in the
CMSIS adapter warrants it. Per-hop time for either backend is **NOT MEASURED**.
The planning ranges and acceptance budget are below.
[Reference FFT](../src/reference_dsp.cpp#L125),
[pinned CFFT dispatch and scaling](../../../libDaisy/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_cfft_f32.c#L1151),
[pinned real packing and implementation](../../../libDaisy/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_rfft_fast_f32.c#L500),
[2048 initialization](../../../libDaisy/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c#L220).

## 4. Fidelity — unsure about audibility until tested

Measure placement alone first and require bit identity for the same build
variant and input. Then test each arithmetic change separately and together.

* **Window fill.** First keep the same double positions, floor-to-hop grid,
  fractional interpolation and wrap semantics. Use an in-range path with direct
  PCM indices and a conditional neighbour wrap, sharing the index/fraction for
  L/R where useful. An incremental wrapped cursor can follow if needed. Test
  negative backward-window start, a one-frame source, lengths below HOP and
  below the stride, non-power-of-two lengths and wrap at the file end. Avoid
  a single-subtract wrap assumption on tiny clips. Keep sample rate conversion,
  signed scan speed and positive pitch stride distinct.
* **Magnitude.** Bounded PCM16-derived FFT bins give ample overflow headroom
  for the sum of squares. Near zero, squaring can underflow and the phase
  normalization feeds back through `prev_`. Preserve the existing epsilons.
  The second loop's real offset can also cancel, so explicitly test the
  denominator and NaN behaviour on low-energy or cancelling bins. Do not infer
  sound equivalence from the bound on input magnitude. Check the final ARM
  disassembly for `vsqrt.f32` on the hot path. The current ELF reaches it through
  the `sqrtf` wrapper, which can call `__errno` on a negative argument. Prefer
  local code generation for a known nonnegative radicand over global fast-math.
* **A/B.** Extend the existing F3 method to the full stereo wet output and the
  final mix. Render identical event streams and seeds for long runs (minutes)
  at 44.1/48 kHz source rates, unity and effective pitch extremes, forward,
  reverse, freeze/release, retrigger, notes/bend/modulation, source changes and
  repeated short-loop wraps. Use silence, an impulse, coherent dual mono,
  anticorrelated and independent stereo, quiet bins, tones and transients.
  Compare per-channel max absolute error, overall and worst short-window
  error/signal dB with a silence floor, and stereo sum/difference. Check
  finiteness before the output limiter can hide a failure and inspect
  error versus time for phase drift. The current F3 test's 2e-4 peak and
  −80 dB energy gates are a starting point for comparable fixtures, not a
  guarantee of inaudibility. Build-parity goldens do not compare against
  the old DSP if every variant changes together. The owner's listening test
  remains a separate gate.
  [Fill and phase](../src/reference_dsp.cpp#L165),
  [pitch and source rate](../src/engine.cpp#L163),
  [existing F3 A/B](../tests/test_nebulae.cpp#L373).

## 5. Hop scheduling — disagree with staggering as the first fallback

A half-hop shifts one channel's analysis/synthesis schedule by 256 samples
(5.333 ms). It is not equivalent to a constant 5.333 ms stereo delay because
the source window is quantized and controls, sample position and phase state can
change. Transients and mono fold-down are the most revealing cases. A coherent
dual-mono source should stay coherent. Audibility is **NOT MEASURED**. Even as a
CPU fix, plain staggering would put roughly 3.185 ms of vocoder in each block
using the two mono boot numbers (one hop plus the other channel's plain block).
Adding the reported 2.04 ms grains and 0.44 ms other peaks gives about 5.67 ms.
The sum is a conservative scenario using separate peaks, but staggering alone
does not establish enough margin.

Computing a full hop one block early can use a different input. The hop consumes the current block's source generation, exact quantized position
and pitch after MIDI, control smoothing and MOD offsets. A source swap, note
retrigger or loop boundary can invalidate the prediction. Speculate only with an
exact input key and a bounded miss path. A miss that computes both channels in
the deadline block leaves the worst-case problem intact.

If synchronized optimization falls short, try a deterministic **stereo** pipeline
before changing interchannel timing. Capture the real hop inputs and phase state
on the original grid, split the compute into bounded stages over two callbacks,
and publish both channels together with one block of additional latency. Preserve
the wet/dry/grain and record alignment or explicitly accept the latency contract
change. Invalidate pending work at resets and bank retirement, and retain source
ownership until the last read. Budget each stage with its concurrent grain/record
work and verify timing for the first block after a transition. This can preserve
the DSP sequence modulo a fixed delay, but costs state and changes product
latency. If the owner instead chooses staggered stereo, keep it as a separately
evaluated sound change.
[Block controls and source generation](../src/engine.cpp#L139),
[hop inputs and immediate output](../src/reference_dsp.cpp#L165),
[mix/record coupling](../src/engine.cpp#L212).

## 6. Recovery — agree on a bounded guard, disagree on USB priority as the fix

Keep audio priority. USB is already at 0, so giving USB preemption would require
demoting the relevant audio DMA IRQs. It would add interrupt jitter and nesting
while leaving `ServiceControl` and MIDI dispatch in the main loop. USB can
receive bytes without the application producing a STATUS reply. Any priority
experiment would need a board-composition override after peripheral init,
including the related IRQs and both USB personalities in validation. It does
not belong in the first repair.

Keep the existing eight-block guard in PROFILE as a diagnostic tool. It reacts
after a miss or near miss and pauses the Mincer timeline while the source playhead
advances. For production, consider a separately tested bounded fallback after
the primary path meets budget. Reserve cycles before starting a whole stereo hop
using the measured worst case of the remaining callback. If it cannot fit, keep
both channels in sync and use a specified short tail/fade or bounded bypass with
observable fault counters. After repeated misses latch into a bounded recovery
state with an explicit retry condition. Test resync, file detach, reset, dry/wet
alignment and what gets recorded during fallback. A guard on the vocoder alone
cannot recover an overrun in another stage. Do not treat recovery as passing the
normal-load acceptance test. P3 remains a separate host-side resilience task.
[Diagnostic guard](../src/main.cpp#L149), [guarded processing](../src/engine.cpp#L188),
[USB receive](../src/main.cpp#L198), [STATUS reply path](../src/main.cpp#L391).

## 7. Instrumentation — agree, with additional limits

The boot comparison is the minimum of four hop blocks, using arbitrary PCM,
one channel, pitch 1 and fixed SDRAM-first order. The comment that nothing
interrupts it is too strong. The application starts audio and TIM5 later, but
USB has already been initialized and system interrupts are possible. Minimum
timing reduces the effect of some interruptions and still gives no worst-case
bound. Cache warmup, FFT working-set alignment, the repeated short sequence and
high-stride or wrapping PCM can materially affect results. At high pitch or near
a short-source wrap, the `fmod` path and SDRAM access pattern differ.

PROFILE's hot-path DWT reads and accumulator updates can change code generation
and memory traffic. The callback timer begins after libDaisy's IRQ entry and
input conversion, and stops before PROFILE accounting and output conversion.
It misses any DMA periods that did not enter the callback. Independent maxima
of fill/FFT/phase/grains/other are not a same-block breakdown. Guard skips change
cache history, wet scheduling and which cases enter the sample population. Use
single-client reset windows with explicit hop/skip/overrun counts and capture
the stage breakdown for the worst total blocks. Compare a lightweight full IRQ
measurement or GPIO timing with the detailed PROFILE build and count expected
versus observed DMA half/full periods. Recheck callback plus wrapper overhead
with the guard inactive for acceptance. Record actual clock, cache/MPU and FPU
mode, and use known PCM and warm/cold A/B order for microbenchmarks.
[Boot benchmark and reset](../src/main.cpp#L149),
[USB starts before benchmark](../src/main.cpp#L543),
[callback timing](../src/main.cpp#L232),
[libDaisy conversion wrapper](../../../libDaisy/src/hid/audio.cpp),
[PROFILE fields and reset](CONTROL_PROTOCOL.md#L63).

## 8. Version and sequence — agree

Use **0.2.2** as a focused real-time repair for both `granular` and
`granular-mod`, subject to the owner's implementation and hardware decision.
The local snapshot of PR #107 at `eee0721` adds WAV loader formats and still
publishes PCM16 to the engine. It therefore inherits P1 until the real-time
repair is integrated. Keep the two reviews distinct, then rebase or merge
the 0.3.0 work on the accepted fix, resolving shared version/changelog and
test hunks. Validate both playback paths and repeat the CPU gate on 0.3.0 with
actual 24-bit fixtures. Update the product manifests and binary-store metadata
at each version. Treat 0.2.2 as a patch while the DSP contract is preserved.
A deliberate stereo change or added latency calls for an explicit version and
compatibility decision. Candidate firmware/listening acceptance and publication
remain the owner's decisions.
[Version source](../src/version.h), [firmware store](../../../docs/FIRMWARE_BINARIES.md).

## Ranked fix and timing budget

The deadline is **2.560 M cycles (5.333 ms)**. A 70% target is **1.792 M cycles
(3.733 ms)** for the **whole** callback/IRQ. With the measured 2.04 ms grain and
0.44 ms steady other peaks as a conservative placeholder, the target leaves
**1.25 ms (about 0.60 M cycles) for both wet channels**, before libDaisy wrapper
overhead. The hard deadline leaves about **2.85 ms** on that same assumption.
Those peaks were measured with SDRAM state and need replacement by coincident
AXI measurements. The reported 1.29 ms other-path transient is an additional
case to cover. Peak grain cost is not negligible.

| Rank | Change | Vocoder time in a hop block |
| --- | --- | --- |
| 1 | AXI placement only, original arithmetic and synchronized stereo. Use as the baseline measurement. | **≈6.33 ms** stereo extrapolation from the measured mono boot minimum. Runtime **NOT MEASURED**. |
| 2 | Keep the grid. Start with low-risk indexed PCM fill and pinned CMSIS FFT trials, ordered by the AXI split. Change magnitude math if the measured gain merits it. Share tables/scratch as needed for the link. | **≈2.1–3.2 ms stereo** in a planning scenario where these changes yield 2–3× over 6.33 ms. Backend speedup **NOT MEASURED**. With 2.48 ms of other work this is ≈4.6–5.7 ms total and may still fail. The 70% target requires ≈5× on the wet path if the other costs stay at 2.48 ms, or a reduction in those costs. |
| 3 | If needed, trim the measured non-vocoder peaks while preserving output. For example, profile a bounded PCM reader for grains or separate one-off reset/copy costs from steady hop work. | Same wet time as rank 2. The total must be measured. A 2.1 ms stereo hop leaves only ≈1.63 ms for everything else at 70%. |
| 4 | If headroom still fails, evaluate the synchronized two-block pipeline. A half-hop interchannel offset is a separate, owner-approved sound trade-off. | With 2.1–3.2 ms wet work and an even split, ≈1.05–1.6 ms wet per block **plus scheduling/OLA overhead as applicable**. Unoptimized plain staggering is ≈3.19 ms wet per block. Both are estimates, with actual peak **NOT MEASURED**. |

## Measurements before choosing the implementation

1. On the host, use the pinned toolchain to compare maps and full stack/call
   usage for the baseline and a placement-only PROFILE candidate. Preserve the
   SD IDMA sector, published PCM lifetime and the file-region limit. Keep the
   old DSP as the numerical oracle. Archive reproducible maps and the exact BIN
   hash for any later kit measurements.
2. On an owner-authorized later session, compare mono and stereo with the same
   known PCM, pitches and positions in SDRAM and AXI. Capture both hop and
   plain blocks, separate the forward/inverse/phase/fill/OLA stages and report
   median and worst after startup, with and without fine probes. Include mono
   and stereo, long and tiny files, reverse/freeze, unity and effective
   pitch 32, and fractional positions near both wrap boundaries. Measure whole
   callback plus libDaisy wrapper and count DMA periods independently.
3. Run with both quiet and maximal grain workloads, active MOD/MIDI and normal
   SD activity, and capture the co-occurring costs for resets, source publication,
   file detach and Live record/overdub finalization. The overdub path copies up
   to 32 KiB of SDRAM in a callback. For P2, separate first press with no source,
   first stop/publish, and a second recording with a Live source. Compare
   absolute maxima, missed periods, skips and reachability. Select the DSP
   changes only after the AXI split. Accept only after both variants meet the
   normal-load timing gate with zero missed periods/guard interventions and the
   owner checks the sound.
   [Record and copy path](../src/engine.cpp#L218).
