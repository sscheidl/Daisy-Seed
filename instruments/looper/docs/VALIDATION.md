# Validation — Looper v0.3

Date: 2026-10-20, updated 2026-10-25 for v0.3.2 (44.1 kHz import) and v0.4.0
(MIDI CC control). Hardware validation: **PENDING — no run on the Seed3 yet.**

| Gate | Status | Evidence |
| --- | --- | --- |
| Host engine tests | PASS | `tests/test_engine.cpp`, 3.9 s at `-O2`, 15.5 s with ASan/UBSan |
| WAV parser and writer | PASS | `tests/test_wav.cpp`: valid variants, every rejection, round trip, fuzzing |
| MIDI CC mapping | PASS | `tests/test_midi.cpp` and the MIDI part of `tests/test_callback.cpp` |
| 44.1 → 48 kHz converter | PASS | `tests/test_resample.cpp`: accuracy against an ideal 48 kHz sine, seam, images, short loops, chunking |
| Card persistence and import | PASS | `tests/test_store.cpp` with an in-memory card and fault injection |
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
| Short loops | Crossfade, every length from 1,152 to 1,400 frames with DC and post-roll: tail of 960 frames, the level holds within 1e-3 after the switch, the switch stays within the gate ramp plus its own ramp (`2 × 0.5 / 384`); 200 seam-mode switches on a 1,200-frame tone loop stay within the seam bound. Loops of 3, 100, 600, 959, 960, 1,100 and 1,151 frames in both modes: largest step within the third-of-loop gate ramp plus the take fade-in and switch ramps, the seam dips to zero, except in crossfade mode from 960 frames, which holds the level |
| Start/Size | Window size `1152 + size² × (len − 1152)` and start `len × start` exact, dead bands, 40 random Start/Size changes with jump crossfades stay click-free |
| Layers | Sound on Sound clamps to ±1; Replace leaves material before the punch untouched, reaches the input after the fade, has a monotonic 960-frame punch-in ramp with the expected midpoint |
| Undo/redo | Bit-exact previous state after the 1st, 2nd and 3rd overdub and exact redo, both after the background copy finished and immediately (layers placed far from frame 0 so undo reads uncopied blocks); a new layer waits while the undone state still borrows blocks; erase and undo of erase |
| Copy budget | Second layer of a 10 s loop at +36 semitones with Slice 0.6 and Skip 0.4, in Sound on Sound, Replace and Frippertronics: no 48-frame callback copies more than 12 blocks (1,536 frames) including copies on write (before: up to 16), the copy finishes and undo is exact |
| Long loops | Full 87 s buffers (2 × 4,176,000 frames): record 42, 44, 86, 87 and 88 s (88 s stops at 87.0 s), overdub a full pass across the loop seam and the 43.5 s point, undo and redo bit-exact; loads of capacity + 1, 0 frames and a null source are rejected with data, flags and position unchanged |
| Exact capacity | `LoadLoop` of exactly 4,176,000 frames accepted, rejected while recording; overdub across the end of a full buffer and undo restore it exactly |
| Reverse | DC loop: linear fade to 0 within 480 frames, flip at zero, fade back in, head runs backwards afterwards; reverse wrap on a tone is click-free |
| Inertia | Time constant 2.0 s at a = 1 and 0.5015 s at a = 0.5 for start and tape stop (1 − e⁻¹, 1 − e⁻⁵, e⁻¹ within 0.002); slope fully right lags only increases, fully left only decreases; dead band and inertia off below 0.03 |
| Varispeed | +12, +7, −24, +36 semitones and clamped +50/−40: head increment, head travel over 6,000 samples (±1 frame) and the measured pitch of a 441 Hz loop by zero crossings; glide to +3 semitones follows `3 (1 − 0.999¹⁰⁰⁰)` after 1,000 samples |
| Bounds | Overdubs at ×8, ×¼, reverse, small windows, both loop and seam modes on a full buffer, block sizes 1, 7 and 129; stored data finite and within ±1 |
| Frippertronics | Decay knob 0 → k = 0.999, 0.25 → 0.749, 1 → 0, default 0.707; k smoothing 1 − 0.995²⁰⁰ after 200 samples; two silent passes give `old × k²` exactly (1e-5), the factor fades in with the punch (no step), and so does the soft limit: a silent punch into material of 0.951 to 1.0 at decay 0, default, 0.5 and 1 steps the stored loop by at most `1.05 / 960` (before: 0.0178 at 0.99), the post-roll decays with the loop start; input adds as `old × k + input`; loud layers stay between 0.95 and 1 (soft limit); undo/redo ignored and reported unavailable, available again in Sound on Sound and then exact; erase works |
| Resample | Capture at +12 semitones equals the heard output sample for sample (with the 960-frame fade-in), new length and 960-frame post-roll, the new loop plays an octave up (882 Hz from 441 Hz); undo returns the original exactly, redo the capture; erase during a capture abandons it and undo restores the loop; automatic stop at the capacity without post-roll |
| Slice segmentation | 8 s window: 96,000 / 48,000 / 24,000 / 12,000 / 6,000 frames for slice 0, 0.25, 0.5, 0.7, 1; 5 s and 6 s windows (segments rounded to a power of two), 3 s window as one segment, 1,152-frame minimum |
| Slice/Skip statistics | 2,000 boundaries each with a fixed seed: repeat rate 0.6 × 5/6 at f = 0.895, none at the start of a divisor step, repeat window 1,152…1,500 frames; offsets 1/2 in zone 1 (both offset kinds), reverse 0.125 + 0.5 f in zone 2 with real reverse playback; pitch changes 1/4 in zones 3–6 with values in the expected sets (micro pitch range, ×1/×2/×½ with the head really at ×2, ±11 semitones, ±16 semitones with glides); same seed → same sequence, another seed → a different one |
| Slice/Skip audio | 100 Hz tone, slice 0.9 and skip in manual zones 3, 4 and 6, window and crossfade seams, whole loop and a Start/Size window: largest sample step within the slope of a 400 Hz tone plus the fade slope; overdubs at ×8, ×1 and ×¼ while slicing stay finite and within ±1, and undo after each is exact; Frippertronics with Skip offsets keeps the frames written during offset slices (Sound on Sound rule) |
| WAV | PCM 16/24 bit and float 32, mono to 8 channels, WAVE_FORMAT_EXTENSIBLE (PCM and float GUID), odd-sized and unknown chunks with pad bytes, fmt after data, RIFF size beyond the file, clamping of out-of-range and non-finite floats. Rejected: short or non-RIFF/WAVE files, missing or short fmt, missing data, 8/32-bit PCM, 16-bit float, ADPCM, 0 or 9 channels, wrong block align, wrong GUID, 32, 88.2 and 96 kHz, 0 frames, capacity + 1 frames (exactly the capacity passes). 44.1 kHz is accepted; the length limit applies after conversion (919 frames → 1,000 accepted at a limit of 1,000, 920 frames → 1,001 rejected), and a loop chunk at 44.1 kHz is ignored, truncated data, a data size of 0xFFFFFFFF; an inconsistent loop chunk is ignored. Float32 stereo with loop chunk round-trips bit for bit. Every truncation of two valid files and 40,000 random header mutations: no read past the end, every accepted header lies inside the file, decoded samples finite and within ±1 |
| Card | Nothing saved while empty; save 2 s after the last change and only while idle; file equals the stored frames and tail exactly; no save without a change; undo is saved; power-up recall restores the loop exactly, not undoable, without re-saving; erase deletes the file. A layer started during a save, a version change between copying and writing a chunk, and the audio thread writing while the last chunk is copied all discard the temporary file and keep the last good `loop.wav`; write failure (retry after 10 s), failed rename (the complete `loop.tmp` is recalled), truncated `loop.tmp` (not recalled), card pulled during a save (abort, no-card status, save after reinsertion). Import: case-insensitive order, wrap-around, hidden and non-WAV files skipped, mono/stereo values, exactly the capacity accepted (also a 44.1 kHz file that converts to exactly the capacity), wrong rate (96 kHz), capacity + 1 and a 44.1 kHz file one frame too long after conversion rejected without change, undoable, saved and recalled, refused while overdubbing, replaces a running save, no files reported; with a 3-callback reply delay. Engine: while importing, record, undo and erase are ignored; invalid commits change nothing |
| 44.1 kHz | Converter (`tests/test_resample.cpp`): output length `⌊n × 160 / 147⌋`; DC gain 1 within 2.1e-7; one second at 44.1 kHz holding whole cycles of 100 Hz … 15 kHz matches the same sine sampled at 48 kHz within −83.8 dB (18 kHz: −79.7 dB), and the 64 frames at each end of the loop (the seam) are no worse; level at 12/15/17 kHz within 0.001 dB; images of 12/15/17 kHz tones (at 15.9/18.9/20.9 kHz after folding) below −93 dB; roll-off −0.10 dB at 19 kHz, −3.29 dB at 20 kHz, −17.96 dB at 21 kHz; loops of 1 … 1,000 frames give identical output for input pieces of 1, 7 and 4,096 frames; a ±1 square wave stays within ±1; wrong history and missing input are rejected. Card (`tests/test_store.cpp`): a 441 Hz sine at 44.1 kHz imports as 48,000 frames within 4.0e-5 of the ideal 48 kHz sine (16-bit quantization), spread over at least 10 main-loop passes; saved and recalled bit for bit; a 44.1 kHz `loop.wav` is converted on recall; without the conversion table, import reports the sample rate and recall fails without loading anything |
| MIDI | Mapping (`tests/test_midi.cpp`): each of CC102–113 gives exactly its key bit on the step to ≥ 64 and nothing on release; 64 counts as pressed, held values (64, 100, 127) give no second press, two presses before one callback give one bit; CC20–27 deliver the latest value with a sequence, so the same value again is news; all 128 controllers checked, only CC20–27 and CC102–113 are taken; speed 64 → 0, 76 → +12, 40 → −24, 0 → −24, 100 and 127 → +36; unit 0 → 0, 127 → 1. Takeover: a CC takes the knob, MIDI keeps it for pot moves up to 1.9 % and hands it back beyond 2 %. One writer and one reader thread (20,000 rounds): every knob value read was sent, the last value and the last press arrive. Callback (`tests/test_callback.cpp`, production `main.cpp`): reverse, play/pause, overdub start/stop, undo and redo, the four mode CCs (one on channel 16), next mode and seam over CC; a held CC does not retrigger; CC20 sets 12, −24, 36 semitones, a 1 % pot move keeps MIDI, a 3 % move hands the knob back (pot value 2 semitones); inertia, slice divisor 16/1, size and decay over CC, the decay pot taking over again; CC1, 7, 10, 64, 101, 114, 120, 123 and a note change nothing and get no reply |
| Callback | Keys S1–S7 through the debouncer: take start/stop, play/pause, undo/redo toggle, reverse, erase and undo of erase, loop and seam mode; knob mapping −24/+6/0/+36 semitones and inertia; S6 cycling and S9–S12 selecting the four loop modes with the mode LEDs; the undo key locked in Frippertronics; decay, Slice, Start and Size pots; a resample with the record key and its undo; with an in-memory card: the full loop saved, S8 importing `loops/one.wav`, and a power-up recalling the last save; held key with 1–6 ms dropouts does not retrigger; automatic stop of a 4 s buffer; output finite and within ±1 |

Mutation check (done by hand for v0.1 and again for v0.2, not part of
`make test`): disabling the seam window, the post-roll tail, copy on write, the
read redirection during the background copy, the jump crossfade, the reverse ramp
or the punch fade; inverting the decay knob, dropping the Frippertronics punch
ramp, its Sound-on-Sound rule during Skip offsets or the post-roll decay; the
resample fade-in; halving the repeat chance; not applying the repeat window, the
slice reverse or the slice pitch; rounding segments to non-powers of two; or not
crossfading when the head leaves an edge region each make the suite fail. For
v0.3 also: removing the truncation or sample-rate check, the version check, the
save delay, the abort on change, the check after copying a chunk, the recall of
`loop.tmp`, the deletion after erase, the name sort, or the import lock in the
engine.

## Memory and image

* `Looper-v0.4.0.bin`: 166,072 bytes, SHA-256
  `7477080fb2b21e2d79393dfc3db4a93749f5695d5db97df8fe11449447727a67` (local build,
  not a release). MIDI CC control adds 960 bytes of code and 272 bytes in DTCM
  (88.5 %).
* `Looper-v0.3.2.bin`: 165,112 bytes, SHA-256
  `d4a4405c7a9c9442db2455f425e5cb461bb071e7631e3d16a314da20c07b564d` (local build,
  not a release; v0.3.1 was 159,048 bytes from the same toolchain). The 44.1 kHz
  converter adds the 40 KB filter table in SDRAM (built at boot), 1.3 KB of
  converter state in DTCM (now 88.3 %) and about 6 KB of code.
* `Looper-v0.3.0.bin`: 146,892 bytes, SHA-256
  `c7545199b93c9129e2f030c1a58b697f4bf7717b2ca144134e078a73ae5bb18e` (local build,
  not a release; v0.2.0 was 109,916 bytes, `785c557e…d068c`). The slice counter uses `double`, which the `fpv5-d16` FPU runs
  in hardware (no soft-float helpers are linked).
* SDRAM: 66,816,000 bytes for the two buffers plus 32 KB transfer buffer and the
  FatFS objects (99.62 % of 64 MiB). Delay lines or reverb for later effects must
  use internal SRAM.
* The engine object (4.3 KB, mostly the copy bitmap) is in `.data`; DTCM 58 KB,
  SRAM image 147 KB of 480 KB, DTCM 64 KB.
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
* Card: one `loop.wav` in 32-bit float instead of the original's 16-bit
  `loop.wav`/`temp.wav` pair that also carries the undo state; imports load
  into the other buffer while audio plays instead of pausing audio; only 48 kHz
  files are accepted (no resampling); no WAV export yet.

## Needs hardware

Step-by-step checklist: [HARDWARE_TEST.md](HARDWARE_TEST.md).

1. SDRAM: both buffers zeroed at boot (duration), record and play an 87 s loop.
2. Callback deadline at 48 kHz / 48 frames: worst case ×8 overdub with copy on
   write and the background copy, with Slice/Skip moving the head and the jump
   crossfade running (`maximumCycles`, `overruns`, LED D16). The copy budget of
   32 frames per sample is a guess until measured; copies on write share it, so
   a callback copies at most 1,536 frames (12 KiB) on the host test.
3. Audible checks: seam in both modes on sustained material, reverse fade,
   tape stop and start with full inertia, varispeed range, punch in/out,
   Frippertronics decay feel, resample, repeats and Skip zones (the dip when a
   repeat starts in window mode).
4. Key and pot orientation, LED brightness, debounce feel.
5. Input level and soft limit with a line source; mono/stereo jack detection.
6. microSD: card detect, mount, FAT32 cards of different sizes and speeds, the
   time a full 87 s save takes, no audio dropouts or D16 events while saving or
   importing, recall after a power cycle, removal during a save. The SDMMC/FatFS
   path, the cache handling and the SD interrupt priority are untested on the
   board.
