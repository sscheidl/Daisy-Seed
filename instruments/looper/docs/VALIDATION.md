# Validation — Looper v0.2

Date: 2026-10-20. Hardware validation: **PENDING — no run on the Seed3 yet.**

| Gate | Status | Evidence |
| --- | --- | --- |
| Host engine tests | PASS | `tests/test_engine.cpp`, 3.9 s at `-O2`, 15.5 s with ASan/UBSan |
| Production callback | PASS | `tests/test_callback.cpp` runs `src/main.cpp` with the kit fake and libDaisy's `DigitalControl` |
| ARM build | PASS | `make PROJECT=looper`, image check with `scripts/check-firmware.py` |
| Hardware | PENDING | see the list at the end |

## Toolchain

Local Windows build: xPack ARM GCC 13.2.1-1.1 and llvm-mingw 20261006
(clang 23.1.3) as host compiler, libDaisy `f715e48`, DaisySP `2c72eaf`. CI adds
the project to the firmware matrix (host tests, sanitizers, ARM build, image
check).

## What the host tests check

All tests use generated signals and run the engine in 48-frame blocks unless
stated otherwise. Each engine buffer sits between 4,096-frame canary guards that
are checked after every scenario; ASan covers the rest of the heap.

| Area | Test |
| --- | --- |
| Init | Null, overlapping, empty and oversize storage rejected; a failed engine stays silent |
| First take | 960-frame fade-in exact, identical data in both buffers, faded post-roll of 960 frames, automatic stop at capacity without post-roll, undo to empty and redo, erase during a take |
| Seams | 220 Hz tone on a loop that is not a whole number of periods, four passes: the largest sample step stays within the tone's own slope plus the fade slope, in window and crossfade mode, forward and reverse, inside Start/Size windows, and at the exact capacity (crossfade falls back to the gate). Sustained DC: the window dips to zero, the crossfade keeps the level within 1e-5. 200 seam-mode switches at varying times produce no step |
| Start/Size | Window size `1152 + size² × (len − 1152)` and start `len × start` exact, dead bands, 40 random Start/Size changes with jump crossfades stay click-free |
| Layers | Sound on Sound clamps to ±1; Replace leaves material before the punch untouched, reaches the input after the fade, has a monotonic 960-frame punch-in ramp with the expected midpoint |
| Undo/redo | Bit-exact previous state after the 1st, 2nd and 3rd overdub and exact redo, both after the background copy finished and immediately (layers placed far from frame 0 so undo reads uncopied blocks); a new layer waits while the undone state still borrows blocks; erase and undo of erase |
| Long loops | Full 87 s buffers (2 × 4,176,000 frames): record 42, 44, 86, 87 and 88 s (88 s stops at 87.0 s), overdub a full pass across the loop seam and the 43.5 s point, undo and redo bit-exact; loads of capacity + 1, 0 frames and a null source are rejected with data, flags and position unchanged |
| Exact capacity | `LoadLoop` of exactly 4,176,000 frames accepted, rejected while recording; overdub across the end of a full buffer and undo restore it exactly |
| Reverse | DC loop: linear fade to 0 within 480 frames, flip at zero, fade back in, head runs backwards afterwards; reverse wrap on a tone is click-free |
| Inertia | Time constant 2.0 s at a = 1 and 0.5015 s at a = 0.5 for start and tape stop (1 − e⁻¹, 1 − e⁻⁵, e⁻¹ within 0.002); slope fully right lags only increases, fully left only decreases; dead band and inertia off below 0.03 |
| Varispeed | +12, +7, −24, +36 semitones and clamped +50/−40: head increment, head travel over 6,000 samples (±1 frame) and the measured pitch of a 441 Hz loop by zero crossings; glide to +3 semitones follows `3 (1 − 0.999¹⁰⁰⁰)` after 1,000 samples |
| Bounds | Overdubs at ×8, ×¼, reverse, small windows, both loop and seam modes on a full buffer, block sizes 1, 7 and 129; stored data finite and within ±1 |
| Frippertronics | Decay knob 0 → k = 0.999, 0.25 → 0.749, 1 → 0, default 0.707; k smoothing 1 − 0.995²⁰⁰ after 200 samples; two silent passes give `old × k²` exactly (1e-5), the factor fades in with the punch (no step), the post-roll decays with the loop start; input adds as `old × k + input`; loud layers stay between 0.95 and 1 (soft limit); undo/redo ignored and reported unavailable, available again in Sound on Sound and then exact; erase works |
| Resample | Capture at +12 semitones equals the heard output sample for sample (with the 960-frame fade-in), new length and 960-frame post-roll, the new loop plays an octave up (882 Hz from 441 Hz); undo returns the original exactly, redo the capture; erase during a capture abandons it and undo restores the loop; automatic stop at the capacity without post-roll |
| Slice segmentation | 8 s window: 96,000 / 48,000 / 24,000 / 12,000 / 6,000 frames for slice 0, 0.25, 0.5, 0.7, 1; 5 s and 6 s windows (segments rounded to a power of two), 3 s window as one segment, 1,152-frame minimum |
| Slice/Skip statistics | 2,000 boundaries each with a fixed seed: repeat rate 0.6 × 5/6 at f = 0.895, none at the start of a divisor step, repeat window 1,152…1,500 frames; offsets 1/2 in zone 1 (both offset kinds), reverse 0.125 + 0.5 f in zone 2 with real reverse playback; pitch changes 1/4 in zones 3–6 with values in the expected sets (micro pitch range, ×1/×2/×½ with the head really at ×2, ±11 semitones, ±16 semitones with glides); same seed → same sequence, another seed → a different one |
| Slice/Skip audio | 100 Hz tone, slice 0.9 and skip in manual zones 3, 4 and 6, window and crossfade seams, whole loop and a Start/Size window: largest sample step within the slope of a 400 Hz tone plus the fade slope; overdubs at ×8, ×1 and ×¼ while slicing stay finite and within ±1, and undo after each is exact; Frippertronics with Skip offsets keeps the frames written during offset slices (Sound on Sound rule) |
| Callback | Keys S1–S7 through the debouncer: take start/stop, play/pause, undo/redo toggle, reverse, erase and undo of erase, loop and seam mode; knob mapping −24/+6/0/+36 semitones and inertia; S6 cycling and S9–S12 selecting the four loop modes with the mode LEDs; the undo key locked in Frippertronics; decay, Slice, Start and Size pots; a resample with the record key and its undo; held key with 1–6 ms dropouts does not retrigger; automatic stop of a 4 s buffer; output finite and within ±1 |

Mutation check (done by hand for v0.1 and again for v0.2, not part of
`make test`): disabling the seam window, the post-roll tail, copy on write, the
read redirection during the background copy, the jump crossfade, the reverse ramp
or the punch fade; inverting the decay knob, dropping the Frippertronics punch
ramp, its Sound-on-Sound rule during Skip offsets or the post-roll decay; the
resample fade-in; halving the repeat chance; not applying the repeat window, the
slice reverse or the slice pitch; rounding segments to non-powers of two; or not
crossfading when the head leaves an edge region each make the suite fail.

## Memory and image

* `Looper-v0.2.0.bin`: 109,916 bytes, SHA-256
  `785c557e6fa07ed089857517a08ab59b68eaf3a895bdfce98539f92bbefd068c` (local build,
  not a release). The slice counter uses `double`, which the `fpv5-d16` FPU runs
  in hardware (no soft-float helpers are linked).
* SDRAM: 66,816,000 bytes for the two buffers (99.56 % of 64 MiB). Delay lines or
  reverb for later effects must use internal SRAM.
* The engine object (4.3 KB, mostly the copy bitmap) is in `.data`; DTCM 58 KB,
  SRAM image 110 KB of 480 KB.
* `.sdram_bss` is NOLOAD; `main` zeroes both buffers after `hw.Init()`. The only
  static initializer in `main.cpp` constructs the kit object and does not touch
  SDRAM (checked in the disassembly).

## Deviations from the original (deliberate)

* Undo uses copy on write plus a background copy instead of the SD-card reload,
  so undo is correct after every layer and immediate.
* Undo after the first take returns to the empty state (the original toggles to
  an identical copy).
* Sound on Sound also punches with 960-frame input fades.
* The first take is always written at ×1.
* The crossfade seam mode and its post-roll are new; the window mode is the
  default and matches the analysis.
* A jump crossfade runs only when the head leaves the window or lands on a window
  edge; the original crossfades on every Start change.
* In window mode, the first take's physical seam inside a shifted window is not
  treated, as in the analysis; this can click if Start moves the seam into the
  window.
* Frippertronics follows the manual for the knob direction and the undo lock
  (README, "Decided for Frippertronics"), adds a soft limit, and keeps the seam
  window; the analysis reports no window in that mode, which would leave the
  first take's seam unfaded.
* Slices are computed on the Start/Size window, not the whole loop, and are at
  least 1,152 frames long. A repeat is placed inside the window at the head
  (ending at it when playing backwards). Clock-synced slices and Freeze are not
  implemented; the draw order of the random numbers is our own.
* Resample captures loop playback plus input (what is heard before the output
  limit) and switches with a 10 ms ramp after a 960-frame post-roll.
* The crossfade for moves also runs when the head leaves an edge region of the
  old window, so the end of a repeat never steps the edge gain.

## Needs hardware

1. SDRAM: both buffers zeroed at boot (duration), record and play an 87 s loop.
2. Callback deadline at 48 kHz / 48 frames: worst case ×8 overdub with copy on
   write and the background copy, with Slice/Skip moving the head and the jump
   crossfade running (`maximumCycles`, `overruns`, LED D16). The copy budget of
   32 frames per sample is a guess until measured.
3. Audible checks: seam in both modes on sustained material, reverse fade,
   tape stop and start with full inertia, varispeed range, punch in/out,
   Frippertronics decay feel, resample, repeats and Skip zones (the dip when a
   repeat starts in window mode).
4. Key and pot orientation, LED brightness, debounce feel.
5. Input level and soft limit with a line source; mono/stereo jack detection.
