# Validation — Looper engine core v0.1

Date: 2026-10-20. Hardware validation: **PENDING — no run on the Seed3 yet.**

| Gate | Status | Evidence |
| --- | --- | --- |
| Host engine tests | PASS | `tests/test_engine.cpp`, 2.2 s at `-O2`, 5.2 s with ASan/UBSan |
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
| Callback | Keys S1–S7 through the debouncer: take start/stop, play/pause, undo/redo toggle, reverse, erase and undo of erase, loop and seam mode; knob mapping −24/+6/0/+36 semitones and inertia; held key with 1–6 ms dropouts does not retrigger; automatic stop of a 4 s buffer; output finite and within ±1 |

Mutation check (done once by hand, not part of `make test`): disabling the seam
window, the post-roll tail, copy on write, the read redirection during the
background copy, the jump crossfade, the reverse ramp or the punch fade each make
the suite fail.

## Memory and image

* `Looper-v0.1.0.bin`: 105,168 bytes, SHA-256
  `4b84c249f262bd874c5196462c01fb7f0991c9631c9dc97d2ac100620fc52cff` (local build,
  not a release).
* SDRAM: 66,816,000 bytes for the two buffers (99.56 % of 64 MiB). Delay lines or
  reverb for later effects must use internal SRAM.
* The engine object (4.3 KB, mostly the copy bitmap) is in `.data`; DTCM 58 KB,
  SRAM image 105 KB of 480 KB.
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

## Needs hardware

1. SDRAM: both buffers zeroed at boot (duration), record and play an 87 s loop.
2. Callback deadline at 48 kHz / 48 frames: worst case ×8 overdub with copy on
   write and the background copy (`maximumCycles`, `overruns`, LED D16). The copy
   budget of 32 frames per sample is a guess until measured.
3. Audible checks: seam in both modes on sustained material, reverse fade,
   tape stop and start with full inertia, varispeed range, punch in/out.
4. Key and pot orientation, LED brightness, debounce feel.
5. Input level and soft limit with a line source; mono/stereo jack detection.
