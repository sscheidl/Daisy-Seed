# Nebulae on Daisy: audio overload on first hardware contact (2026-11-08)

Working record for the P1/P2/P3 findings of the first kit test. Every hardware statement
is **MEASURED (date)** or **NOT MEASURED**; nothing here was flashed or heard by the agent.

## 1. Observed (MEASURED 2026-11-08, owner's kit, remote NB STATUS probes)

Image `NebulaeMod-v0.2.1.bin`, SHA-256 `02e52f88…97a727` (reproduced bit-for-bit from
`main` with the pinned toolchain on 2026-11-08). Nebulae had never run on hardware
before, in any version: this is first contact, not a regression.

| | Result |
| --- | --- |
| Without a card | pots, keys, LEDs, NEBULAE and KEYBOARD modes work; STATUS peak 6 %, 0 overruns |
| Card inserted | bank scan, then loading; peak up to 23 %, 0 overruns |
| First sample plays (`playing 1:1`) | about 0.25 s later STATUS stops answering; pots, keys and LEDs dead; audio a distorted ~1 s loop; reboot with the card does not help |
| DENSITY (CC23) and OVERLAP (CC25) at 0 before the card | identical: the grains are not the cause |
| S1 RECORD without a card (P2) | the kit freezes; not yet clarified whether on the first press or once a Live loop exists |
| PC side (P3) | after the stall WinMM throws on SysEx sends (`MidiOutWinMM::sendMessage: error sending sysex message` / `error preparing sysex header`); the Mod on Daisy editor and the Daisy Diagnostic panel crash |

## 2. Why the whole kit dies, not only the audio (from the code, not measured)

* libDaisy runs the audio DMA interrupt, USB OTG_HS, SDMMC and I2C all at NVIC priority
  0; the control timer TIM5 at 0x0f; the main loop below everything
  (`libDaisy/src/sys/dma.c`, `usbd/usbd_conf.c`, `per/tim.cpp`).
* When one callback is longer than its 5.333 ms block, the next DMA interrupt is already
  pending when it returns. Among pending interrupts of equal priority the NVIC takes the
  lower number: DMA1_Stream0 (11) always wins against OTG_HS (77). So a callback that
  overruns permanently starves USB, the control timer and the main loop at once: no
  STATUS reply, no pots, no LEDs. Answering from the USB interrupt would not help.
* The audio keeps the DMA buffers it last wrote while the CPU is busy: the "distorted
  loop" is consistent with this.

## 3. Suspect (from the code, NOT MEASURED)

`neb::Engine` (247,664 B) is placed in external SDRAM (`engine_storage`,
`DSY_SDRAM_BSS`, `src/main.cpp`), including both phase vocoders `neb::Mincer`: per
channel 8 KB window, 3 x 8 KB spectra, 32 KB overlap-add frames and a 40 KB `RFFT`
(twiddles, bit-reversal table, 16 KB complex workspace). The hop (every 512 frames =
every second block, both channels in the **same** block) runs four 2048-point complex
FFTs (45,056 butterflies), 8,192 interpolated PCM reads and the phase loops, all on data
that does not fit the 16 KB data cache. SDRAM is cacheable write-back (libDaisy MPU
region 1) on a 32-bit bus; a cache-missing radix-2 FFT there is several times slower
than in AXI SRAM. The vocoder runs whenever a source plays, independent of BLEND, which
matches "DENSITY/OVERLAP 0 changes nothing" and P2 (Live loop = source).

The original instrument runs the same `mincer` (N 2048, hop 512, 4x overlap) in Csound
double precision on a quad-core 1.2 GHz Raspberry Pi with `ksmps 64`, `-b 512`: the
algorithm is faithful, the budget is not comparable.

## 4. Diagnostic build (software, host-tested)

`make -C instruments/granular MOD=1 PROFILE=1` builds
`build/mod-profile/NebulaeModProfile-v0.2.1.bin` (`-DNEBULAE_PROFILE=1`, marker
`NEBULAE-PROFILE-BUILD=1`). It differs from the MOD image only by:

* **Cycle splits** (DWT) of every block: vocoder (both channels), grains, the rest, and
  inside a hop: window fill, FFTs, phase loops plus overlap-add (`src/profile.h`).
* **Budget guard**: after a block above 80 % of the deadline the vocoder is left out of the
  next 8 blocks (grains and dry path still render). The sound is wrong in that case; the
  main loop, USB and LEDs stay alive, so the counters can be read.
* **Boot benchmark**: before audio starts, one `Mincer` is run for eight blocks from SDRAM
  and one from AXI SRAM (section `.neb_dsp`), both reading PCM from SDRAM; minimum cycles
  of the hop blocks and of the plain blocks are kept.
* **PROFILE SysEx** `F0 7D 4E 42 01 03 [01] F7` ([protocol](CONTROL_PROTOCOL.md)); the
  optional `01` clears the peaks after the reply. STATUS is unchanged.

Host tests: `tests/test_profile.cpp` (splits, hop detection, guard trip and resume,
reply, reset, benchmark on the fake counter), `test_control_sysex` in both builds, and
`test_golden` in the profile build must print the regular hash (the instrumentation is
inert while the guard does not trip). The regular and the MOD image must stay
byte-identical to `main` (`cbc4c376…`, `02e52f88…`).

Readout from the PC (each probe in its own process with a timeout, so a stalled kit
cannot hang the tool): `python instruments/granular/tools/nb_probe.py profile`.

Image handed to the owner on 2026-11-08: `NebulaeModProfile-v0.2.1.bin`, 217,572 B,
SHA-256 `be1296fbadb2e737c2d73ee507eb92da7ac936d187d398faed9898c24671f919`, built from
commit `e6ad1e5` (store line `granular-mod`, variant `-profile`, status `candidate`).
Its `.neb_dsp` benchmark storage ends at 0x2404f640 of the 0x24078000 AXI SRAM limit.

## 5. Procedure for the owner

1. Flash `NebulaeModProfile-v0.2.1.bin` (from the firmware store, SHA-256 in `SHA256SUMS`).
2. Power on **without** a card. `nb_probe.py profile`: the boot benchmark values
   (`bench_*`) and the idle split.
3. Insert the card, wait for the sample, `nb_probe.py profile` twice ten seconds apart:
   hop cost (`hop_vocoder_min`, `fft_peak`, `fill_peak`, `phase_peak`), `skipped_blocks`
   (guard activity) and `overruns`.
4. P2: without a card, press S1 once, probe; press S1 again (Live loop exists), probe.
5. Optional: BLEND fully to grains (CC26 = 127) and DENSITY/OVERLAP max, probe.

The owner flashes, inserts and listens; the agent runs the probes and records the values.

## 6. Results

Flashed on 2026-11-09 20:01 by the agent with the owner's approval ("Ja, flashen"), over J7
through the existing external Daisy bootloader (`dfu-util -a 0 -s 0x90040000:leave`,
device `0483:df11` with the QSPI map `@Flash /0x90000000/...`); no bootloader write.
Readings are firmware DWT counters read with `nb_probe.py`, 480 MHz core clock, block
deadline 2,560,000 cycles = 5.333 ms.

| Step | Value | Status |
| --- | --- | --- |
| idle without a card: callback peak | 174,179 cycles = 0.363 ms = 6.8 %, 0 overruns | MEASURED 2026-11-09 |
| boot benchmark SDRAM hop / plain | 6,726,467 cycles = 14.013 ms = 262.8 % / 34,953 cycles = 0.073 ms (one channel) | MEASURED 2026-11-09 |
| boot benchmark AXI SRAM hop / plain | 1,518,325 cycles = 3.163 ms = 59.3 % / 10,542 cycles = 0.022 ms (one channel) | MEASURED 2026-11-09 |
| card inserted, bank 1 slot 1 plays (`playing 1:1`) | the kit keeps answering STATUS and PROFILE: the budget guard works; two STATUS requests went unanswered while the card was inserted and mounted (20:03:02, 20:03:19) | MEASURED 2026-11-09 |
| hop block, both channels, Engine in SDRAM, DENSITY/OVERLAP/BLEND = 0 (10 s, 142 hop blocks) | vocoder 14,764,501 to 15,548,386 cycles = 30.8 to 32.4 ms = 577 to 607 %; of that FFTs 22.9 ms, window fill 6.5 ms, phase loops and overlap-add 3.1 ms; callback peak 33.2 ms | MEASURED 2026-11-09 |
| same, DENSITY/OVERLAP/BLEND = 127 (10 s, 141 hop blocks) | vocoder 31.3 to 34.2 ms; FFTs 23.1 ms, fill 8.4 ms, phase 3.1 ms; callback peak 36.6 ms | MEASURED 2026-11-09 |
| grains peak | 197,406 cycles = 0.41 ms = 7.7 % (minimum settings) to 978,803 cycles = 2.04 ms = 38.2 % (maximum) | MEASURED 2026-11-09 |
| rest of the callback, peak | 0.44 ms = 8.3 % steady; 1.29 ms once right after the first hop (control frames queued during the overrun) | MEASURED 2026-11-09 |
| guard activity in 10 s playing | 1,130 of 1,414 counted blocks without vocoder; about 1,000 of the 1,875 block periods never reached the callback (each hop block spans about 6 block periods) | MEASURED 2026-11-09 |
| listening | | NOT MEASURED (the guard removes most of the vocoder: the sound of this image means nothing) |
| P2: S1 first press / second press | | NOT MEASURED |

### What the numbers say

* **P1 root cause, measured.** One hop needs 30.8 ms of work every 10.67 ms (two blocks):
  288 % average load from the vocoder alone, 600 % in the hop block. Nothing else is
  significant: grains at most 38 %, the rest 8 %. The unguarded image can never return
  from the audio interrupt, which (section 2) takes USB, timer and main loop with it.
* **SDRAM placement is the main factor, but not the whole fix.** The same hop code runs
  4.4 times faster with its state in AXI SRAM (3.16 ms instead of 14.0 ms per channel).
  Both channels in one block are then 6.3 ms = 119 % of a block plus grains and the
  rest: the average fits (59 %), the hop block does not.
* **Inside the hop** (SDRAM split): FFTs 72 %, window fill 20 %, phase loops and
  overlap-add 10 %. The split with the state in AXI SRAM is NOT MEASURED.
* P2 follows from the same mechanism in the code (a Live loop is a source, the vocoder
  runs on it) but was not exercised on the kit.

## 7. Fix candidates after the measurement (owner's decision pending)

Budget: a hop block must stay clearly below 5.33 ms including grains (up to 2.0 ms in
SDRAM, NOT MEASURED in AXI SRAM) and the rest (0.44 ms).

1. **Required: DSP state out of SDRAM.** The whole Engine fits the free AXI SRAM (~279 KB
   after the MOD image), or only the two Mincers; sharing the identical twiddle,
   bit-reversal and Hann tables between the channels saves another 32 KB. Bit-identical
   output. Alone it leaves the hop block at about 119 % plus grains: not enough.
2. **Make the hop cheaper, same hop timing for both channels** (faithful to the original
   stereo relation): a faster FFT than the plain radix-2 with separate real/imaginary
   arrays, `sqrtf(x*x+y*y)` instead of libm `hypotf` in the two phase loops (2,050 calls
   per channel and hop), a window fill without per-sample modulo. Output changes at
   float rounding level only; proven by a host A/B test as for F3 (-88 dB then).
3. **Stagger the channels by one block** (channel 1's frames half a hop later): halves
   the hop block at once, but changes the stereo hop timing against the original.
   Fallback if 1 + 2 do not reach the budget.
4. Skip the vocoder while BLEND mutes it (file mode BLEND = 1, Live mode BLEND >= 0.5):
   saves CPU only in those settings; no help for the default BLEND = 0.

The golden test compares builds, it does not pin a constant; any rounding change is
recorded in the changelog with its measured error.

## 8. P3: host applications and a stalled device

NOT MEASURED in this session yet. Plan: reproduce with the stalled 0.2.1 image before
the profile image is flashed, capture the panel traceback and the editor behaviour, then
make both tolerate a MIDI output that never completes (send off the GUI thread with a
timeout, report "device not answering", never wait in the driver on the GUI thread).
Since 0.2.2 the firmware no longer stalls on overload (section 10), so the reproduction
needs the 0.2.1 image from the store.

## 9. Second opinion (Codex, 2026-11-09) and what was done with it

[Brief](CODEX_SECOND_OPINION_BRIEF.md), [report](CODEX_SECOND_OPINION.md). Assessment:

| Codex's point | Verdict | Consequence in 0.2.2 |
| --- | --- | --- |
| Diagnosis holds; the audio callback hangs on DMA1 Stream 1 (receive), not Stream 0; SDMMC1 is lowered to priority 2 by the read-only SD driver | agreed; section 2 above names the wrong stream, the argument (IRQ 12 below OTG_HS 77) is unchanged | none |
| A freeze on the *first* RECORD press without a source would need another cause than the vocoder | agreed: the code has no wet source then | P2 stays NOT MEASURED; to be separated on the kit: first press, first stop, second recording |
| Budget: at a 70 % target only about 1.25 ms remain for both vocoder channels next to 2.04 ms grains and 0.44 ms rest, about 5x on the wet path | agreed; this is why placement alone was never an option | hop optimised and grain loop relieved |
| Measure a placement-only image first | not followed: Codex's own budget shows it cannot pass, and it costs the owner a flash cycle. Instead the old vocoder stays as the numerical oracle on the host and the profile build measures the new split | one image to flash |
| `hypotf` from the nano libm rescales with two divides; `PcmView::Read` emits divide/remainder sequences | agreed (disassembly evidence) | `sqrtf`, `PcmReader` |
| CMSIS-DSP real FFTs from the pinned libDaisy; compare with a packed complex forward | taken: three `arm_rfft_fast_f32` per channel and hop. The packed variant was not built; open if the FFT share is still too high | `src/fft_cmsis.cpp` |
| Squaring can underflow near zero; do not infer sound equivalence; A/B on the full stereo output and the final mix, long runs, extremes | taken, and it paid off: the A/B exposed that the algorithm itself amplifies rounding (section 10) | `tests/test_vocoder.cpp`, `tests/compare_audio.cpp` |
| Staggering the channels is no first fallback; prefer a synchronised two-block pipeline with one block of latency | agreed | not needed so far; decision only if the kit measurement fails |
| Keep audio priority; bounded guard rather than USB priority | agreed | overload protection in every image |
| The boot benchmark is not uninterrupted (USB, SysTick); peaks are independent maxima, not one block's breakdown; the callback timer excludes libDaisy's wrapper | agreed | comment corrected; limits apply to every number in section 6 |
| The profile map cannot hold the Engine next to an AXI benchmark object | agreed | the AXI run borrows the Engine's storage before the Engine is constructed |
| The ELF checker expects the Engine in SDRAM | agreed | checker updated |
| 0.2.2 for the repair, 0.3.0 stays with PR #107, which inherits P1 until it is rebased | agreed | version 0.2.2 |

## 10. Fix in 0.2.2 (software, host-tested; hardware NOT MEASURED)

What changed is listed in the [changelog](../CHANGELOG.md). Evidence on the host
(clang, Windows, 2026-11-09):

* The engine around the old vocoder reproduces the 0.2.1 golden hash
  (`63576b07d2f2b999`): grain loop, placement and protection are bit-identical.
* FFT backend against the radix-2 reference: worst relative error 2.5e-7 forward,
  4.8e-7 round trip. `PcmReader`: 225,232 reads bit-identical to `PcmView::Read`.
* **The vocoder algorithm amplifies last-bit rounding.** Phase locking normalises the
  sum of three neighbouring bins, which can nearly cancel, and the result feeds back
  through the phase state. The 0.2.1 vocoder compared with *itself*, with nothing but
  its two forward FFTs unpacked, gives -120 dB error/signal on steady material at
  pitch 1 and speed 1, -57 dB after 6 s of a tone at pitch 1.3, -45 dB after three
  minutes of music and +1 dB (uncorrelated) on noise played in reverse at half pitch.
  The 0.2.0 packing change therefore already altered such passages; its test covered
  1.6 s of one benign case. A sample comparison cannot be the acceptance criterion
  for any change to this vocoder, including a port to double precision.
* New vocoder against the old one, 25 scenarios: -91 dB or better in the ten scenarios
  where the old one agrees with itself; elsewhere at most 4.5 dB further from it than
  it is from itself; long-term band energies within 0.54 dB (the old one against
  itself: 0.46 dB). Final mix of the golden performance: total energy -0.018 dB, band
  energies within 0.70 dB, 85 ms window levels within 2.63 dB.

Expected cost per channel and hop in AXI SRAM, from operation counts only (NOT MEASURED):
fill about 0.15 ms, three real FFTs about 0.5 ms, phase loops about 0.2 ms; both
channels about 1.8 ms of a 5.33 ms block, plus grains and the rest.

### First 0.2.2 candidate on the kit (`d487eeff…`, commit f8239eb), MEASURED 2026-11-09 21:07-21:09

Flashed by the agent with the owner's approval; the kit was already in the bootloader.
Firmware DWT counters read with `nb_probe.py`, Engine reported in AXI SRAM.

| Step | Value | Status |
| --- | --- | --- |
| boot benchmark, new vocoder, one channel: SDRAM hop / AXI SRAM hop | 4.233 ms (was 14.013) / **1.554 ms** (was 3.163) | MEASURED |
| idle without a sample, callback peak | 0.876 ms = 16.4 % (card scan and load running) | MEASURED |
| playing, pots as found, 15 s | hop block vocoder 6.69 to 6.76 ms = 126 %: fill 4.63, FFT 1.45, phase 0.64 ms; 2,593 of 2,917 blocks skipped, overruns climbing | MEASURED |
| same with DENSITY/OVERLAP/BLEND 127 | vocoder 6.75 ms, grains 1.53 ms, callback peak 8.49 ms | MEASURED |
| same with DENSITY/OVERLAP/BLEND 0 | vocoder 6.77 ms, grains 0.18 ms, rest 0.23 ms | MEASURED |
| PITCH centre, SPEED centre, START 0, SIZE max, grains 0 | hop block vocoder **3.22 ms = 60 %** steady (6.5 ms peak from the first blocks before the CCs took effect), **no new overruns**, grains 0.55 ms | MEASURED |
| PITCH maximum (4x) | vocoder 4.09 to 4.18 ms = 77 to 78 %: fill 2.00, FFT 1.46, phase 0.69 ms; callback peak 4.96 ms = 93 %; no overruns | MEASURED |
| PITCH minimum (1/8) | vocoder 3.13 to 3.18 ms = 59 %: fill 1.01, FFT 1.45, phase 0.69 ms; callback peak 3.89 ms; no overruns | MEASURED |
| P2: S1 first press, first stop, second recording | | NOT MEASURED |
| listening | | NOT MEASURED (profile image) |

What the numbers say: FFT (1.45 ms for six real transforms) and phase loops (0.65 ms) are
now as estimated; the window fill is the cost driver, and above all when read positions
run past the end of the file (START near the end, long windows at high pitch): every such
read took the `std::fmod` path of `PcmView::Read`, about 270 cycles, and the "as found"
pot positions produced 4.6 ms of fill per hop. Within the file the fill costs about
60 cycles per read, mostly the double-to-integer conversions and the SDRAM cache misses
at high pitch. Second candidate (same day): `WrapFmod`, an exact replacement of
`std::fmod` for the wrap path (one divide and a correction, bit-identical, host-tested on
207,171 values), and the mono shortcut (a mono source feeds both channels the same
samples, so channel 1 is a copy of channel 0; its premises are host-tested). Both keep the
golden hashes.

### Second 0.2.2 candidate on the kit (`f41dd580…`, commit 361a5ea), MEASURED 2026-11-09 21:21-21:23

Flashed by the agent (the kit was in the bootloader again). Stereo 48 kHz test file, bank 1
slot 1. "hop block vocoder" is the steady minimum over the hop blocks of the run; the
peaks of a run include the blocks right after a CC change while the smoothing settles.

| Step | Value | Status |
| --- | --- | --- |
| START 124/127 (near the file end), SIZE max, PITCH max, DENSITY/OVERLAP/BLEND 127 | hop block vocoder 3.96 ms (74 %), fill peak 3.99, FFT 1.38, phase 0.65 ms; grains 1.57 ms; callback peak 7.8 ms; 2,597 of 2,921 blocks skipped, overruns climbing | MEASURED |
| START 0, otherwise the same | hop block vocoder 3.95 ms, fill peak 3.34 ms; still overruns and skips | MEASURED |
| 60 s at the worst settings | overruns +2,554, 10,098 of 11,361 blocks skipped: the protection carries the kit, the sound has vocoder gaps | MEASURED |
| experiment, not kept: `PLD` prefetch 16 reads ahead in the fill (`9128e63a…`, build dir only) | hop block vocoder 3.67 ms at PITCH max (from 4.1), 3.54 to 3.58 ms at PITCH centre (from 3.2): the hint costs more at normal pitch than it saves at high pitch | MEASURED 21:28-21:30 |
| mono file: hop block vocoder | | NOT MEASURED (needs the owner to select bank 3) |
| P2: S1 first press, first stop, second recording | | NOT MEASURED |
| listening | | NOT MEASURED |

Budget on the common hop grid after both candidates (stereo file, both channels in one
block): vocoder 3.2 ms at PITCH centre to 4.1 ms at PITCH maximum (+0.65 ms with START
at the file end), grains 0.17 to 1.57 ms, rest 0.17 ms. Typical settings stay well below
the block (PITCH centre, moderate grains: about 4 ms = 75 %); PITCH centre with maximum
grains reaches 4.9 ms = 92 %; PITCH maximum with maximum grains reaches 5.7 ms = 107 %
and runs into the overload protection. The FFT (1.35 ms for six transforms) and the
phase loops (0.65 ms) are at their expected cost; the window fill (1.0 to 2.0 ms) is bound
by SDRAM cache misses, which no bit-identical change removes. The 70 % target of the
second opinion is not reachable on the common grid with bit-identical changes; the
options are the synchronised two-block pipeline (one block of vocoder latency, stereo
unchanged), the half-hop channel stagger (no latency, stereo analysis grid of the right
channel shifted by 256 samples; dual-mono files are unaffected because of the mono
shortcut), rounding-level arithmetic changes (fast inverse square root, float fill;
perhaps 0.5 to 0.8 ms), or accepting the protection as the behaviour at the extreme
settings. The owner decides.
