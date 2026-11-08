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

| Step | Value | Status |
| --- | --- | --- |
| boot benchmark SDRAM hop / plain | | NOT MEASURED |
| boot benchmark AXI SRAM hop / plain | | NOT MEASURED |
| hop block: vocoder min, fill, FFT, phase | | NOT MEASURED |
| grains peak, other peak | | NOT MEASURED |
| guard skips, overruns after 10 s playing | | NOT MEASURED |
| P2 first press / second press | | NOT MEASURED |

## 7. Fix candidates, to be chosen from the numbers

1. Move the hot DSP state out of SDRAM: the whole Engine fits the free AXI SRAM
   (~279 KB free after the MOD image), or only the two Mincers; sharing the identical
   twiddle, bit-reversal and Hann tables between the channels saves another 32 KB.
   Bit-identical output, golden hashes unchanged.
2. Spread the hop: compute channel 1's frame in the block after channel 0's (changes the
   stereo hop timing; needs a deliberate golden update), or compute the frame one hop
   early.
3. Skip the vocoder while BLEND mutes it (file mode BLEND = 1, Live mode BLEND >= 0.5):
   saves CPU only in those settings, output identical where the vocoder gain is 0.

## 8. P3: host applications and a stalled device

NOT MEASURED in this session yet. Plan: reproduce with the stalled 0.2.1 image before
the profile image is flashed, capture the panel traceback and the editor behaviour, then
make both tolerate a MIDI output that never completes (send off the GUI thread with a
timeout, report "device not answering", never wait in the driver on the GUI thread).
