# Texture load spike (S1)

Measurement firmware for one decision of the Beads port study: where the 768 KB audio buffer can live on the Seed3
Desktop Kit (decision D8 of the port concept). **Not an instrument and not the port.** The port itself is not ordered.

* Concept: L2A case `beads`, `DAISY_PORT_CONCEPT.md`, step S1 (local to the L2A checkout, not in this repository).
* Code: the unchanged Beads processor and synth from `../third_party/eurorack/` (MIT, one documented change), driven by
  `src/spike.h`.
* Status 2026-11-17: host test **MEASURED** (pass). Kit **MEASURED**: A and B (0.1.0), A and AI (0.2.0), BI (0.3.0,
  0.4.0). BI 0.4.0 runs all four cases without a single overrun, with and without product load (results below).
* **Decision 2026-11-17 (owner): D8 = BI.** The spike covers two of Beads' four quality modes (16-bit DIGITAL, 8-bit
  DIRTY_TAPE). The two 12-bit modes are not measured, and in BI their code stays in QSPI; the port must place the active
  mode's code in fast memory and measure them.

## Stored images

The measured images are kept in the firmware store of the main checkout ([FIRMWARE_BINARIES.md](../../../docs/FIRMWARE_BINARIES.md)),
`firmware/texture-load-spike/`, the newest measured one per line: `0.2.0+a97569f-A-dirty`, `0.2.0+a97569f-AI-dirty`,
`0.1.0+a97569f-B-dirty` and `0.4.0+e35905a-BI` (SHA-256 `835c2dfd…`, running on the kit since 2026-11-17). The `-dirty`
ones were built from the uncommitted tree before this branch was committed and flashed from `build/` before storing; the
superseded A 0.1.0 (`d6a287d4…`) and BI 0.3.0 (`3414be75…`) are not stored. Paths in the table below are build outputs.

## The two images

| Image | Boot type | Code | Audio buffer (768 KB) | Reverb buffer | Engine state |
| --- | --- | --- | --- | --- | --- |
| **A** `build/a/TextureSpikeA-v0.1.0.bin` | `BOOT_SRAM`, as every product here | AXI SRAM | SDRAM, cached | D3 SRAM | DTCM |
| **B** `build/b/TextureSpikeB-v0.1.0.bin` | `BOOT_QSPI`, new for this repository | QSPI flash, executed in place | 512 KB AXI SRAM + 256 KB D2 SRAM, exactly as on the module (`beads/beads.cc`) | D3 SRAM | DTCM |

**0.2.0 adds** (owner order 2026-11-15, after A failed case a; original sound mandatory, latency acceptable, 80 % a soft
target):

* **Image AI** `build/a-itcm/TextureSpikeAI-v0.2.0.bin`: as A, but the hot DSP functions of the four cases (55 KB:
  granular cores of the 16-bit and 8-bit modes, processor, delay controller and taps, sample-rate conversion, reverb,
  grain scheduling, triggers, synth, the callback) run from ITCM. `src/memory_a_itcm.lds` places them; a
  `.preinit_array` entry copies them before any constructor. Output unchanged.
* **Block 12 or 48 at run time** (CC116, audio restarted): with 48 frames the callback runs the engine four times with
  12 frames, the same calls as with 12, so the same output (host test: identical hashes). 1 ms of extra latency.
* **Product load at run time** (CC116): as in Nebulae, a 1 kHz timer on libDaisy's lowest interrupt priority reads the
  controls, ticks the Mod on Daisy matrix (eight connections: two LFOs, random, both CVs, mod wheel, channel pressure;
  `src/product.h`) and hands the offsets to the audio callback; MIDI goes through the matrix controller in the main loop.
  `spike_probe.py run-all --midi-load` sends CC1 and channel pressure at a few hundred messages per second meanwhile.
  Reported per case: callback peak and mean as before, control tick peak/mean, MIDI events, main-loop MIDI peak. SIZE is
  not modulated, so case b stays in delay mode.

**0.3.0 adds** (owner order 2026-11-16, after AI): **image BI** `build/b-itcm/TextureSpikeBI-v0.3.0.bin`, the layout of B
(`BOOT_QSPI`, buffer in AXI + D2 SRAM) with the hot code of AI in ITCM and the lookup tables that code reads
(`lut_sine`, `lut_pitch_ratio_high`/`_low`, 7 KB) in DTCM, both loaded from QSPI and copied in the same `.preinit_array`
entry (`src/memory_b_itcm.lds`). The rest of the code stays in QSPI. A, AI and B are unchanged apart from the version;
`src/version.h` now checks that the version string and the numbers in the CC identity agree.

**0.4.0 changes BI only** (owner order 2026-11-16, "Ja, Fall d angehen"): the synth wavetable `wav_integrated_waves`
(99 KB) moves from QSPI to cached SDRAM (`.sdram_rodata` in `src/memory_b_itcm.lds`). SDRAM works only after
`hw.Init()`, so `main()` copies it right there, before the engine starts, and compares the copy word by word with QSPI;
on a mismatch the firmware stops, so a broken copy cannot change the sound unnoticed.

Everything else is identical: same source, same flags, the USB-MIDI handler in SDRAM in both (it does not take part in
audio). B uses `src/memory_b.lds`, a copy of libDaisy's QSPI script with `.data`, `.bss` and the heap moved to DTCM.

**What the comparison shows:** A pays for SDRAM latency on up to 32 grains that read at scattered positions; B pays for
running the code from QSPI through the 16 KB instruction cache. Which cost is larger is the open question.

## What one run does

Each case starts from cleared buffers and fixed random seeds, runs 2 s of warm-up (not counted in the peak), then a fixed
sweep with **1 s per point**. The callback is measured with the DWT cycle counter over the whole callback body: engine
plus output copy. Budget: 12 frames at 48 kHz = 250 µs = 120 000 cycles at 480 MHz.

| Case | Setting | Sweep | Time |
| --- | --- | --- | --- |
| a | Hi-fi 16 bit, granular, SEED latched, reverb full | DENSITY 0, 0.2, 0.4, 0.6, 0.8, 1 × SIZE 0, 0.5, 0.97 × PITCH 0, +24 × TIME random off/on = 72 points | 74 s |
| b | Hi-fi 16 bit, delay mode (SIZE fully clockwise), SEED off | DENSITY 6 values × PITCH 0, +24 × TIME 0.2, 0.9 × TIME random off/on = 48 points | 50 s |
| c | µ-law, 48 kHz with 2:1 conversion, wow and flutter, SEED latched | as a | 74 s |
| d | Wavetable synth mode, SEED latched | as a | 74 s |

Fixed for all: stereo, FREEZE off, SHAPE 0.5, FEEDBACK 0.5, DRY/WET 1, REVERB 1, input white noise at −12 dBFS
(internal, no cable needed). **Run all: about 4.5 minutes.**

Why these points: DENSITY 0 and 1 give the highest grain rate; SIZE 0 and 0.97 the longest grains (4 s), so the pool of 32
fills; PITCH +24 reads the buffer four times faster; TIME randomization scatters grains over the whole buffer. Grains need
the SEED gate: the pulse train only runs while it is high, so the granular cases latch SEED. The host test confirms the
pool reaches 32 grains and that case b is in delay mode for the whole sweep.

## Build (Claude, done 2026-11-15)

```
make -C instruments/texture/load-spike MEM=A -j8
make -C instruments/texture/load-spike MEM=B -j8
make -C instruments/texture/load-spike test HOST_CXX=clang++          # host test, full length
make -C instruments/texture/load-spike test-sanitize HOST_CXX=clang++
python instruments/texture/load-spike/tools/check_images.py instruments/texture/load-spike/build   # vectors, buffer placement
```

On 2026-11-15 the check printed for both images: 306 484 bytes, reset vector in AXI SRAM (A) or QSPI (B), engine and
`.bss` in DTCM with a 30 KiB stack reserve, buffers where the table above says.

Not in `projects.mk` and not in CI: a one-off measurement firmware, not a product.

## Procedure on the kit (owner)

Needs `pip install mido python-rtmidi` and USB on J7. Studio One or another DAW must not hold the MIDI port.

1. **Volume down.** The output is muted unless **SW17** is on; then it plays at −18 dB (granular noise, loud at high
   density). Leave SW17 off for the measurement.
2. Put the kit into the bootloader (S9 + S16 held for 5 s, or the editor's *Firmware update*), then flash A:
   `make -C instruments/texture/load-spike MEM=A program-dfu`.
3. After the restart: `python instruments/texture/load-spike/tools/spike_probe.py identify` must print variant `A`,
   480 MHz, block 12.
4. `python instruments/texture/load-spike/tools/spike_probe.py run-all --md spike-A.md`. Do not touch the kit for about
   4.5 minutes; the script waits and prints the table.
5. Optional: `spike_probe.py profile 1` (to 4) lists every sweep point of a case by peak load.
6. Flash your usual firmware back.

**0.2.0 series** (A and AI, B is ruled out): flash the image, then
`python instruments/texture/load-spike/tools/spike_probe.py series --md spike-<image>-0.2.md`. It runs all four cases
four times, block 12 and 48, each without and with product load and MIDI stream, about 19 minutes, and leaves the kit at
block 12 without product load. Image A first, then AI (`make -C instruments/texture/load-spike ITCM=1 program-dfu`).
0.3.0 and 0.4.0: the same series with BI (`make -C instruments/texture/load-spike MEM=B ITCM=1 program-dfu`).

**Image B only if A is too slow** (owner decision 2026-11-15). A is the preferred option of the concept and uses the boot
type of every product here. If A stays below the limit in all four cases without overruns (proposal: peak below about 80 %;
the owner sets the limit), D8 is decided for A and B is not needed. Only if A fails:

1. Bootloader, `make -C instruments/texture/load-spike MEM=B program-dfu`. The command writes to the same QSPI address as
   for A; only the start differs: the Daisy bootloader runs B directly from QSPI instead of copying it to SRAM.
2. `identify` must print `B`, then `run-all --md spike-B.md`, then flash your usual firmware back.

**If B does not start** (LEDs stay dark, no MIDI port "Daisy Seed"): B is the first `BOOT_QSPI` image in this repository.
The Daisy bootloader in internal flash is not touched: use the recovery sequence of
[FLASHING.md](../../../docs/FLASHING.md#recovery-reset-and-boot) (RESET, then BOOT within about two seconds) and flash any
other image. Report it; that itself is a result for D8.

Without a PC: S1-S4 start case a-d, S13 runs all four. LED S1-S4: blinking while running, on when done. LEDs S5-S12: peak
load of the running or last case as a bar, 12.5 % per LED. LED S13: an overrun happened. LED S16: run-all active.

## MIDI readout (channel 16, plain control change)

| Request | Reply |
| --- | --- |
| CC119 = 5 | identity: CC100 = 16, CC101 variant `A`/`B` (`I` added when CC115 = 1), CC102-103 MHz, CC104 block, CC105-107 budget cycles, CC108-110 version, CC111-113 callbacks with a wrong block size |
| CC119 = 1..4 | case a..d: CC100 = 17, CC101-103 peak cycles, CC104-106 mean cycles, CC107-109 peak load (0.01 %), CC110-112 overruns, CC113 case, CC114-116 mean load, CC117 worst point, CC118 state (0 not run, 1 running, 2 done, 3 failed) |
| CC119 = 6..9 (0.2.0) | product load of case a..d: CC100 = 18, CC101-103 control tick peak cycles, CC104-106 mean, CC107-109 ticks, CC110-112 MIDI events, CC113 case, CC114-116 main-loop MIDI peak cycles, CC117 product load on, CC118 block |
| CC116 = 1 / 2 / 3 / 4 (0.2.0, idle only) | block 12 / block 48 / product load off / on. Identity then shows CC104 block, CC114 product load, CC115 ITCM image, CC116 patch status (0 = loaded) |
| CC118 = 1..4 / 5 | start case a..d / run all |
| CC117 = 1..4 | per point: CC96 index, CC97-99 peak cycles; then CC95 = case |
| — | every reply of CC119 ends with CC119 = 64; numbers are 7-bit groups, lowest first |

## What the numbers include and what not

* Included: the whole user callback, and any interrupt that preempts it (USB, I2C LED transfer, ADC DMA). LEDs update
  only at 4 Hz during a run and the probe sends nothing while it waits, to keep that small.
* Not included: libDaisy's own audio interrupt work around the callback (DMA half-transfer handling, int/float
  conversion). The same holds for the load figures of the other firmwares here.
* Different from the module: no AGC, no jack-sensing, no pot/CV reading in the callback, no main-loop
  `WasteCPUCycles` (Beads keeps the memory bus busy from the main loop; the port would not), stereo only, case c at 48 kHz
  instead of the module's 24 kHz codec rate. Not swept: mono, the 32 kHz 12-bit mode, FREEZE, a clocked SEED, CV.
* Peak is a maximum over single blocks of a fixed sweep, not a proof of the worst case of all settings.

**Go/no-go** (concept, a proposal): peak below about 80 % of the block in the worst case, for at least one option. The
owner sets the limit.

## Results

| Image | a granular | b delay | c dirty tape | d synth | Overruns | Date |
| --- | --- | --- | --- | --- | --- | --- |
| A | **116.81 %** peak, 43.65 % mean | 64.55 % / 54.64 % | 65.44 % / 36.75 % | 84.75 % / 45.07 % | **29 126 in a**, 0 in b-d | MEASURED 2026-11-15 |
| B | **129.59 %** peak, 53.62 % mean | 99.57 % / 79.00 % | **136.83 %** / 86.27 % | **114.40 %** / 51.23 % | 8 774 in a, 1 in b, **105 967 in c**, 1 143 in d | MEASURED 2026-11-15 |

**A, MEASURED 2026-11-15** (21:23-21:28 UTC, flashed by Claude with the owner's consent, kit in the Daisy bootloader):
`identify` gave variant A, 480 MHz, block 12, budget 120 000 cycles, no wrong block sizes. Peak / mean load per case as
above. Case a fails: 12 of its 72 points peak above 100 %, 25 above 80 %; the 29 126 overruns are about 10 % of its
288 000 measured blocks. 9 of the 12 points above 100 % have PITCH +24, the worst is point 63 (DENSITY 1, SIZE 0,
PITCH +24, TIME random). Cases b and c stay below 66 % at every point, case d peaks at 84.75 % with 19 points above 80 %.
Not proven by A alone: how much of case a is SDRAM latency. Case d reads its wavetables from AXI SRAM, not from the
SDRAM buffer, and still reaches 85 %, so 32 grains at 48 kHz are expensive even without SDRAM.

**B, MEASURED 2026-11-15** (21:40-21:45 UTC, same procedure, kit put into the bootloader by the owner): B boots under the
Daisy bootloader v6.4 without problems; `identify` gave B, 480 MHz, block 12. B is worse than A in every case, in peak and
in mean: a 129.59 % (29 of 72 points above 100 %), b 99.57 % (all 48 points above 80 %), c 136.83 % (58 of 72 points above
100 %, mean 86.27 %), d 114.40 % (32 points above 100 %). Case c, which runs the most different code per block
(conversion filters, µ-law, wow and flutter), suffers most. Inferred, not proven: fetching code from QSPI through the
16 KB instruction cache costs more than SDRAM data costs in A. Case a has fewer overruns in B (8 774) than in A (29 126),
which fits a data-latency share in A, but B's peak and mean are higher.

**Result for D8:** A is the better option; B is ruled out in this form. A still fails case a (peak 116.81 %).

**A with 0.2.0, MEASURED 2026-11-16** (19:23-19:46 UTC, `series`, table `spike-A-0.2.md` kept in the L2A case), peak /
mean per case:

| Block, product load | a granular | b delay | c dirty tape | d synth | Overruns in a |
| --- | --- | --- | --- | --- | --- |
| 12, off | 115.43 / 43.59 % | 63.78 / 54.85 % | 64.31 / 36.43 % | 84.83 / 45.12 % | 28 822 |
| 12, on | 115.34 / 44.18 % | 64.12 / 55.23 % | 65.10 / 36.73 % | 85.07 / 45.35 % | 27 192 |
| 48, off | 112.02 / 43.48 % | 61.94 / 54.74 % | 61.86 / 36.27 % | 82.24 / 45.03 % | 7 243 (of 72 000 callbacks) |
| 48, on | 111.07 / 43.77 % | 61.86 / 54.94 % | 61.53 / 36.42 % | 81.55 / 45.19 % | 5 124 |

* Repeatable: block 12 without product load matches the 0.1.0 run of 2026-11-15 within 1.4 points.
* **Block 48 hardly helps:** peaks drop by 2-3 points; overruns stay at about 10 % of the callbacks. The heavy stretches
  last longer than a millisecond, so grouping four blocks does not average them out.
* **Product load costs little inside the callback:** mean +0.2 to +0.6 points, peaks unchanged within the run-to-run
  spread. The MIDI stream reached the kit: about 185 messages per second, 12 000-14 000 events per case.
* The control tick figures are not the tick's own cost: the tick runs on the lowest priority, and audio preempts it, so
  its cycles include that audio time (peaks above 100 % of 1 ms). Inferred from the mean (2.0-2.3 % in b-d) times the
  share of time audio leaves free: the tick itself costs roughly 1 % of the CPU.
* **Observed:** the "1 kHz" low-priority callback ran at **2 kHz** (148 000 ticks in the 74 s of case c, 100 000 in the
  50 s of case b). `libDaisy/src/desktop_devkit.cpp:140` derives the period from `GetPClk2Freq()`; TIM5 sits on APB1 with
  a doubled timer clock (inferred, not checked in the HAL). Nebulae counts ticks by time (`mod_clock.Due`), so it does
  not depend on this; a port must do the same. Here the matrix ticked twice per millisecond, so its cost is if anything
  overstated.

**AI (hot code in ITCM) with 0.2.0, MEASURED 2026-11-16** (19:52-20:11 UTC, `spike-AI-0.2.md` in the L2A case): AI boots,
the ITCM copy works. Peak / mean case a: block 12 off 115.55 / 43.30 %, on 112.95 / 43.85 %; block 48 off 111.93 /
43.25 %, on 111.78 / 43.63 %; overruns again about 10 % of the callbacks. b ≤ 63.79 %, c ≤ 63.26 %, d ≤ 84.11 %.
**ITCM changes nothing that matters:** at most 1.5 points on the peaks and 0.3-0.8 on the means compared with A. With the
code in AXI SRAM, instruction fetch is not what limits case a; what is left is data access (the scattered grain reads in
SDRAM, worst with PITCH +24) and the arithmetic itself. Inferred: B (buffer in internal SRAM) had a third of A's overruns
in case a but lost everything to code fetched from QSPI; B with the hot code in ITCM has not been measured.

**BI (B's layout, hot code in ITCM, hot tables in DTCM) with 0.3.0, MEASURED 2026-11-16** (22:36-22:54 UTC, `series`,
flashed by Claude with the owner's consent, `spike-BI-0.3.md` in the L2A case; MIDI stream 51 866 / 51 348 sent, none
refused), peak / mean per case:

| Block, product load | a granular | b delay | c dirty tape | d synth | Overruns |
| --- | --- | --- | --- | --- | --- |
| 12, off | 87.44 / 38.12 % | 54.03 / 51.71 % | 56.94 / 33.87 % | 85.40 / 45.32 % | 0 |
| 12, on | 87.42 / 38.01 % | 54.29 / 51.77 % | 56.58 / 33.91 % | **101.60** / 47.76 % | 5, all in d |
| 48, off | 84.88 / 38.05 % | 53.18 / 51.66 % | 54.16 / 33.84 % | 83.21 / 45.20 % | 0 |
| 48, on | 84.63 / 38.08 % | 53.23 / 51.74 % | 53.53 / 33.82 % | 96.57 / 47.76 % | 0 |

* **Case a: 28 points lower than A** (87.44 against 115.43 % at block 12, without product load), **no overrun** in any
  of the four runs; the mean drops from 43.6 to 38.1 %. Block 48 takes another 2.6 points. The soft target of 80 % is
  missed by 4.6-7.4 points.
* b and c are 8-10 points lower than A; d without product load equals A within 0.6 points.
* **New weak spot, d with product load:** 101.60 % at block 12 (5 overruns, worst point 3: density 0, size 0, pitch +24),
  96.57 % at block 48; A had 85.07 / 81.55 % there. Only d gets worse when the matrix modulates.
* Inferred, not measured: the grains of case a read the buffer from AXI and D2 SRAM much faster than from SDRAM, and with
  the hot code in ITCM, QSPI no longer costs what it cost B. In d, all code of the case runs from ITCM (the wavetable
  voices are inlined into `Synth::Render`; checked with `nm`: the Beads functions left in QSPI are instances the four cases do not run (12-bit,
  other 8- and 16-bit layouts) and `Init`/`Configure`). What d still reads from QSPI is the 99 KB table `wav_integrated_waves`, which
  fits neither ITCM nor DTCM; modulated SHAPE and PITCH spread the reads over more of it. Not profiled.

**BI with 0.4.0 (synth wavetable in SDRAM), MEASURED 2026-11-17** (18:35-18:54 UTC, `series`, flashed by Claude, kit put
into the bootloader by the owner, `spike-BI-0.4.md` in the L2A case; MIDI stream 51 476 / 50 996 sent, none refused),
peak / mean per case:

| Block, product load | a granular | b delay | c dirty tape | d synth | Overruns |
| --- | --- | --- | --- | --- | --- |
| 12, off | 87.50 / 38.12 % | 54.25 / 51.77 % | 56.79 / 33.87 % | 84.82 / 45.15 % | 0 |
| 12, on | 87.44 / 37.98 % | 54.46 / 51.88 % | 56.87 / 33.93 % | 94.36 / 47.04 % | 0 |
| 48, off | 84.90 / 38.06 % | 53.22 / 51.72 % | 54.15 / 33.84 % | 82.79 / 45.04 % | 0 |
| 48, on | 84.71 / 38.07 % | 53.40 / 51.84 % | 53.72 / 33.82 % | 94.72 / 47.14 % | 0 |

* **No overrun in any case or run.** The copy check passed (the firmware answered `identify`).
* d with product load: 94.36 % at block 12 (0.3.0: 101.60 %, 5 overruns), 94.72 % at block 48 (0.3.0: 96.57 %). The
  table out of QSPI removed the overruns, but d under modulation stays about 9-13 points above A (85.07 / 81.55 %).
* a, b, c and d without product load equal 0.3.0 within 0.6 points (run-to-run spread).
* Inferred, not measured: in A the table lay in AXI SRAM, here in SDRAM, whose cache misses cost more; modulated SHAPE
  and PITCH spread the reads over the table. Not profiled.
* The 80 % soft target is still missed: a by 4.7-7.5 points, d with modulation by 14-15 points.

**Host test, MEASURED 2026-11-15** (Windows, LLVM-MinGW clang++, `-O2`, and again with ASan + UBSan): pass. 72/48/72/72
points per case, all outputs finite and non-silent, up to 32 grains active (about 30 at the fastest rate with the longest
grains, where the scheduler keeps stealing), case b in delay mode throughout, case c at 48 kHz with ratio 2, a repeated run
of case a gives the same output hash. Output hashes are equal at `-O2` and under the sanitizers. PC timings
(about 12 µs per block at most) say nothing about the kit.
