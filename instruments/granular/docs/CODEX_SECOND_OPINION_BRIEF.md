# Brief for Codex: second opinion on the Nebulae audio overload and its fix

Written 2026-11-09 by Claude at the owner's request ("give the findings, the analysis data
and the fix recommendations to Codex; he shall give a second opinion"). This is a design
review **before** any fix is written. Read-only: do not change code, firmware, protocols
or tests, do not flash or probe the kit (it currently runs the diagnostic image and the
MIDI port must not get a second client during a measurement), do not dispatch CI.

I would rather be told where I am wrong than be confirmed. Sections 2 and 3 are data;
section 4 is my reasoning and section 5 my recommendation, both open to attack.

## 1. What happened and where to look

Branch `claude/nebulae-overload-9c0df4` (base `main` 241f583), commits e6ad1e5, b1cdab4,
ad54726. No PR yet.

Nebulae on Daisy (`instruments/granular`, also built as `granular-mod`) ran on the Seed3
Desktop Kit for the first time on 2026-11-08, in any version, and was unusable: about
0.25 s after the first sample starts playing, STATUS replies stop, pots, keys and LEDs are
dead and the audio is a distorted loop; a reboot with the card does not help. Without a
card everything works. DENSITY and OVERLAP at 0 change nothing. RECORD (S1) without a
card also freezes the kit (P2, not clarified further). Editor and panel on the PC crash
when the kit stalls (P3, not part of this brief).

Read first:

1. `instruments/granular/docs/OVERLOAD_DIAGNOSIS.md`: record, reasoning, measurements
   (section 6), fix candidates (section 7).
2. `instruments/granular/src/reference_dsp.cpp` / `.h`: `RFFT` (plain radix-2, separate
   `re_`/`im_`, `Cfft` at line 99) and `Mincer::Process` (line 160; adapted from Csound
   `pvlock.c`), FFT 2048, hop 512, 4x overlap, phase locking always on.
3. `instruments/granular/src/engine.cpp`: `Engine::Process` (line 139; both vocoder
   channels then `RenderGrains`), `engine.h` (the object layout).
4. `instruments/granular/src/main.cpp`: `engine_storage` in SDRAM (line 41,
   `DSY_SDRAM_BSS`), `Audio()`, and under `NEBULAE_PROFILE` the instrumentation, the
   budget guard and the boot benchmark.
5. `instruments/granular/src/memory.ld`, `docs/MEMORY_BUDGET.md`,
   `libDaisy/src/sys/system.cpp` (`ConfigureMpu`), `libDaisy/src/sys/dma.c`,
   `libDaisy/src/usbd/usbd_conf.c`, `libDaisy/src/per/tim.cpp` (interrupt priorities).
6. `instruments/granular/docs/CONTROL_PROTOCOL.md` (PROFILE `03`), `tools/nb_probe.py`.

Build facts: `-O3`, `-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard`, BOOT_SRAM (image in
AXI SRAM at 0x24000000, 480 KiB region), 48 kHz, 256-frame blocks (5.333 ms), core clock
480 MHz, D-cache and I-cache on, SDRAM region cacheable write-back. CMSIS-DSP sources are
in `libDaisy/Drivers/CMSIS-DSP` (include path and `ARM_MATH_CM7` are set by the libDaisy
Makefile) but I found no `arm_cfft`/`arm_rfft` in `libdaisy.a`: using them means compiling
those sources into the product.

## 2. Facts from the code (not measured)

* `neb::Engine` is 247,664 B (247,736 B in the MOD build), placed whole in SDRAM at
  0xc1000000. Per channel a `Mincer` holds: Hann window 8 KB, three 8 KB spectra
  (`fwin_`, `bwin_`, `prev_`), 32 KB overlap-add frames, and an `RFFT` of 40 KB (cos and
  sin tables 8 KB each, 8 KB `int` bit-reversal table, 16 KB complex workspace). The
  twiddle, bit-reversal and Hann tables of the two channels are identical copies.
* The hop is computed inside `Mincer::Process` at the sample where `cnt_ == HOP`. Both
  channels share the position stream, so both hops fall into the **same** 256-frame
  block, every second block. Per channel and hop: 4,096 `PcmView::Read` calls (double
  position, `%` per sample, PCM16 in SDRAM), one packed complex forward FFT and one
  complex inverse FFT of 2048 points, two loops of 1,025 `hypotf` calls each, overlap-add.
* The vocoder runs whenever a source has frames, independent of BLEND.
* libDaisy sets the audio DMA interrupt, OTG_HS, SDMMC and I2C to NVIC priority 0, TIM5
  (control timer) to 0x0f. `Audio()` runs in the DMA interrupt.
* Free memory after the MOD image: AXI SRAM about 279 KB (image ends 0x24033da0 of
  0x24078000), DTCM about 13 KB above the 16 KiB stack reserve (`_ebss` 0x20018d2c).

## 3. Measurements (owner's kit, 2026-11-09, firmware DWT counters)

Image `NebulaeModProfile-v0.2.1.bin`, SHA-256 `be1296fb…f919`, commit e6ad1e5, read
with `nb_probe.py profile`. 2,560,000 cycles = one block = 5.333 ms.

| Measurement | Cycles | Time | Of a block |
| --- | ---: | ---: | ---: |
| Idle, no card: callback peak | 174,179 | 0.363 ms | 6.8 % |
| Boot benchmark, one `Mincer`, hop block, state in **SDRAM** | 6,726,467 | 14.013 ms | 262.8 % |
| Boot benchmark, same, state in **AXI SRAM** (`.neb_dsp`) | 1,518,325 | 3.163 ms | 59.3 % |
| Boot benchmark, block without hop, SDRAM / AXI SRAM | 34,953 / 10,542 | 0.073 / 0.022 ms | 1.4 / 0.4 % |
| Playing, Engine in SDRAM, DENSITY/OVERLAP/BLEND 0: vocoder in a hop block, both channels (min to peak over 142 hops) | 14,764,501 to 15,548,386 | 30.8 to 32.4 ms | 577 to 607 % |
| of that: FFTs / window fill / phase loops plus overlap-add (peaks) | 11,006,874 / 3,121,637 / 1,483,615 | 22.9 / 6.5 / 3.1 ms | |
| Playing, DENSITY/OVERLAP/BLEND 127: vocoder hop block peak | 16,407,665 | 34.2 ms | 641 % |
| of that: FFTs / fill / phase (peaks) | 11,072,576 / 4,042,143 / 1,470,329 | 23.1 / 8.4 / 3.1 ms | |
| Grains peak, minimum to maximum settings | 197,406 to 978,803 | 0.41 to 2.04 ms | 7.7 to 38.2 % |
| Rest of the callback, steady peak | 213,263 | 0.444 ms | 8.3 % |

Notes on validity:

* The benchmark runs before the control timer and audio start; both variants read PCM
  from SDRAM bank 0 (uninitialised content) with pitch 1.0, minimum of four hop blocks.
* While playing, the budget guard skipped the vocoder in 1,130 of 1,414 counted blocks;
  each hop block spans about six block periods, so those DMA interrupts never reached the
  callback. The kit stayed reachable over USB the whole time.
* The fill/FFT/phase split exists only for the SDRAM placement. **The split with the
  state in AXI SRAM is NOT MEASURED**, and so is the grain cost there.
* One measurement series was discarded: a second STATUS poller on the same MIDI port
  received the replies too, and the probe took a STATUS reply for its PROFILE answer.
  The probe now accepts only the reply to its own command (ad54726); the table holds only
  values taken after that fix or from single-client runs.
* Listening: NOT MEASURED. P2 (RECORD) on the kit: NOT MEASURED.

## 4. My analysis

1. **Why the whole kit dies**: one hop needs about 31 ms of work every 10.67 ms. The DMA
   interrupt is pending again whenever the callback returns; among pending interrupts of
   equal priority the NVIC takes the lowest number, and DMA1_Stream0 (11) is below OTG_HS
   (77), so USB, TIM5 and the main loop never run. I did not verify the NVIC ordering on
   the device, only the observed symptom and the fact that the guard cures it.
2. **Root cause**: the vocoder state in SDRAM. The same code is 4.4 times faster in AXI
   SRAM. Grains and the rest are not significant.
3. **Placement alone is not enough**: two channels in AXI SRAM are 2 x 3.16 = 6.3 ms in
   the hop block (119 %) plus grains and 0.44 ms. The average over the hop fits (59 %),
   the hop block does not, and an overrunning block still loses a DMA period.
4. My unmeasured estimate of the AXI SRAM hop (1.52 M cycles): two complex FFTs maybe
   0.7 to 0.9 M, the fill 0.3 to 0.4 M (double arithmetic and two `%` per `Read`), the
   phase loops 0.3 M (libm `hypotf`, 2,050 calls). These are guesses.

## 5. My recommendation (the owner has not decided)

1. Move the DSP state to AXI SRAM (whole Engine, or the Mincers with shared tables).
   Bit-identical output.
2. Make the hop cheaper while keeping both channels on the same hop grid, as the
   original instrument has it: a faster FFT (CMSIS-DSP `arm_cfft_f32`/`arm_rfft_fast_f32`
   or an own radix-4 with interleaved data), `sqrtf(x*x+y*y)` instead of `hypotf`, a fill
   loop that steps an integer index and a fraction instead of calling `Read` twice per
   sample. Output changes at float rounding level; a host A/B test bounds the error (the
   earlier FFT packing change measured -88 dB).
3. Fallback if 1 + 2 miss the budget: stagger channel 1 by one block (its frames half a
   hop later). Halves the hop block, changes the stereo hop relation.
4. Not recommended as the fix: skipping the vocoder when BLEND mutes it (no help at the
   default BLEND 0); lowering FFT size or overlap (changes the instrument).

Target: hop block at or below about 70 % of a block with maximum grains, measured with
the same PROFILE counters, then a listening test by the owner.

## 6. Questions for you

1. Is the diagnosis complete? Is there a second cause I may be masking with the SDRAM
   finding (cache configuration, the MPU region attributes, SDRAM timing or refresh,
   denormals in the phase loops with uninitialised benchmark PCM, `-O3` code size and the
   I-cache, the 16 KB D-cache against the 16 KB `re_`/`im_` workspace plus tables even in
   AXI SRAM)? Would you expect the 4.4x ratio, or is 14 ms for this hop in SDRAM itself
   suspicious?
2. Is 3.16 ms per channel in AXI SRAM plausible for this code at 480 MHz, and where
   would you expect the time to go? Which measurement would you add before optimising?
   Is DTCM (13 KB free) worth using for the 16 KB complex workspace by sharing it between
   channels and shrinking something else?
3. Which FFT would you use here and what hop time do you expect: CMSIS-DSP (which
   function, with the Csound packing `[DC, Nyq, re1, im1, …]` and the packed
   forward pair), or an own implementation? Licence and policy: CMSIS-DSP is Apache-2.0
   inside the pinned libDaisy; pulling its sources into this product must not bump or
   patch libDaisy.
4. Fidelity: do `sqrtf` for `hypotf`, another FFT rounding and an incremental fill stay
   inaudible in a phase vocoder with phase locking, where errors feed back through
   `prev_`? How would you bound it in a host test (error against the current
   implementation over long runs, freeze, extreme pitch)?
5. Staggering the channels: how audible is a half-hop offset between left and right
   analysis frames for stereo material (transients, mono compatibility)? Is there a
   better way to spread the work without changing the result, for example computing the
   next frame one block early from a predicted position, given that the position is
   quantised to the hop grid anyway but depends on controls smoothed per block?
6. Robustness independent of the fix: should the production image keep an overload
   guard (for example skip or hold the vocoder after an overrun) or raise the USB
   interrupt above audio, so that a future overload cannot make the kit unreachable
   again? What are the risks of either on libDaisy (priorities are set inside the
   library)?
7. Anything in the instrumentation that could mislead: DWT reads inside the hop, the
   guard changing cache behaviour, the benchmark using pitch 1.0 and sequential
   positions while real use can read with pitch up to 32 (stride through SDRAM PCM).
8. Version and process: PR #107 (`claude/nebulae-24bit`) claims Nebulae 0.3.0 for 24-bit
   loading and is independent. Does a real-time fix as 0.2.2 before it make sense?

## 7. Constraints

* Sound must stay faithful to the original Nebulae v2 instrument; deliberate changes are
  recorded in the changelog with their measured error. The golden test compares builds
  (regular, MOD, profile); it does not pin a constant.
* No allocation, locks or blocking calls in the audio callback. Modular policy
  (`docs/MODULAR_DEVELOPMENT_POLICY.md`), pinned libDaisy/DaisySP/toolchain.
* Hardware statements are MEASURED (with date) or NOT MEASURED. Flashing, card, keys and
  listening are the owner's.
* The owner's private static analysis of the original instrument is not in this
  repository and must stay out of it; only what `OVERLOAD_DIAGNOSIS.md` already states
  about the original (Csound `mincer`, N 2048, hop 512, 4x overlap, double precision on
  a quad-core 1.2 GHz Raspberry Pi) may be used here.

## 8. What to deliver

`instruments/granular/docs/CODEX_SECOND_OPINION.md` on this branch (or a comment the owner
passes on): for each question your answer with the evidence (file and line, a calculation,
or a host measurement you ran), marked **agree**, **disagree** or **unsure**; a ranked
fix proposal of your own with expected hop times; and the measurements you want before
the fix is written. Host commands, if you want to run something (Windows notes in
`docs/dcp/CODEX_REVIEW_BRIEF.md` section 3 apply; in this worktree `libDaisy` and
`DaisySP` are junctions, so git needs `--ignore-submodules=all`):

```bash
make -C instruments/granular test HOST_CXX=clang++ HOST_CC=clang
```

```bash
make -C instruments/granular MOD=1 PROFILE=1 -j4
```
