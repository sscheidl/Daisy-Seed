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
