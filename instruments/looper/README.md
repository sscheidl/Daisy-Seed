# Looper — Instrument Project

**Status: engine v0.2 with all four loop modes and Slice/Skip; host tests and ARM
build pass; no hardware run yet.** Project ID: `looper`. The goal is an original
stereo tape looper for the Seed3 Desktop Dev Kit, using the Qu-Bit Stardust as the
behavioral reference. Validation details and open hardware checks:
[docs/VALIDATION.md](docs/VALIDATION.md).

```sh
make PROJECT=looper -j4            # firmware: build/Looper-v0.2.0.bin
make PROJECT=looper test           # host engine and production-callback tests
make PROJECT=looper test-sanitize  # the same with ASan and UBSan
```

## Engine

The engine in [src/looper_engine.h](src/looper_engine.h) has no hardware
dependencies. The caller injects two buffers (pointer and capacity); tests use
heap memory, the firmware uses `DSY_SDRAM_BSS`. Every buffer index is checked
against the capacity and the current loop length. Behavior follows sections 4
and 6 to 8 of the [analysis](docs/STARDUST_ANALYSIS.md); the code is written
independently from those descriptions.

* **First take:** written into both buffers with a 960-frame (20 ms) fade-in at
  ×1, automatic stop at the capacity. After the stop the input keeps running for
  another 960 frames into a faded-out post-roll behind the loop end, when the
  buffer has room.
* **Loop modes** (`LoopMode`), latched when a layer starts. All overdubs punch in
  and out with 960-frame fades, and playback always hears the old layer, so the
  dry input is not doubled.
  * *Sound on Sound* mixes in place and clamps to ±1.
  * *Replace* crossfades the old material to the input.
  * *Frippertronics* writes `old × k + input` on each pass. `k` follows the decay
    knob with a one-pole of 0.005 per sample (about 4 ms) and fades in with the
    punch, so the old material never steps. The sum goes through a soft limit
    (transparent below 0.95, asymptote 1.0); the original does not limit here.
    While a Skip offset is active the layer adds like Sound on Sound, as in the
    original.
  * *Resample* captures what is heard (loop playback plus input) into the other
    buffer from frame 0, with the first-take fade-in, its own length, an automatic
    stop at the capacity and a 960-frame post-roll. Then a 10 ms ramp switches to
    the new loop; the resampled loop stays as the undo state.
* **Undo, redo, erase:** one step plus redo, 480-frame (10 ms) output ramps,
  buffer toggle with per-buffer lengths. Erase is undoable; undo of the first take
  returns to the empty state. Undo and redo are locked in Frippertronics mode (see
  below). Erase during a resample abandons the capture.
* **Copy on write:** when a layer starts, the other buffer becomes the snapshot.
  Each 128-frame block is copied before its first write, and a background job
  copies the rest at 32 frames per sample (about 2.7 s for 87 s). Until it
  finishes, reads of uncopied blocks come from the buffer both states share, so
  undo is immediate and never depends on storage (the original reloads the
  previous state from its SD card). A new layer or resample started right after
  such an undo waits until the copy is complete; the record LED blinks meanwhile.
* **Playback head:** semitone target −24…+36, glide (one-pole 0.001 per sample
  below 4 semitones, jump above), exponential inertia on the linear speed with
  τ = (1.998 a² + 0.002) s and Inertial Slope weights, the slice pitch as a
  multiplier, increment smoothing 0.1 per sample, ×8 limit, two-point linear
  interpolation with one frame of latency, and interpolated input when
  overdubbing at another speed.
* **Reverse:** 480-frame fade to silence, direction flip, 480-frame fade in. The
  direction is the user's request XOR the slice reverse.
* **Start/Size:** window start `length × start`, size
  `1152 + size² × (length − 1152)`, dead bands below 0.003 and above 0.997.
* **Slice/Skip** (analysis section 7, free-running): the Start/Size window is
  split into base segments (the whole window below 4 s, otherwise the smallest
  power of two that gives segments of at most about 2 s), each divided by
  `2^⌊4.9 × slice⌋` (1…16), at least 1,152 frames. A counter advances by the
  head's travel; at each slice boundary the previous slice's transformations end
  and new ones are drawn:
  * *Repeat* (slice > 0.02): probability 3/5, depth `j` uniform in
    `0…⌊6.1 f⌋` with `f` the position inside the divisor step; for `j > 0` the
    head loops `max(slice × 2^−j, 1152)` frames from its position until the next
    boundary.
  * *Skip* (> 0.01), manual zone `⌊5.99 skip⌋ + 1` with fraction `f`: offset with
    probability 1/2 (a random slice boundary for `f ≥ 0.5`, otherwise up to
    `2f` slice lengths ahead; skipped while a crossfade runs); from zone 2 on
    reverse with `0.125 + 0.5 f`; zone 3 micro pitch `2^((4f·u − 2)/12)` with a
    linear glide over 0…1 slice, zone 4 octaves ×1/×2/×½/×4, zone 5 `±⌊24 f⌋`
    semitones, zone 6 the same with a glide with probability `f`; each pitch
    change with probability 1/4.
  * Random numbers come from an xorshift32 generator with a fixed default seed
    (`SetRandomSeed`), so a sequence is repeatable.
* **Moves and seams:** every position change, and every change of the window the
  head loops in (Start/Size, a repeat starting or ending) that leaves the head
  outside it or on an edge of the old or new window, starts a 384-frame
  crossfade from a second head that keeps reading the old window.
  `SeamMode::Window` (default) is the original 384-frame linear dip to zero at
  both sides of the window seam for windows of at least 1,152 frames.
  `SeamMode::Crossfade` adds the faded-out post-roll to the faded-in loop start
  (a 960-frame linear crossfade of the first take's seam) and blends window seams
  inside the loop with the material that follows the window end over 384 frames.
  Without a post-roll (a take that filled the buffer) the crossfade mode gates the
  physical seam like the window. Mode changes take effect only while the head is
  away from every seam region, so switching never steps the output.

Not in v0.2: effects, input saturation and mix curves, Freeze, clock-synced
slices, 1V/Oct and CV, the encoder range modes, SD card, MIDI.

**Decided for Frippertronics (2026-10-20),** following manual v1.1.0 rather than
the analyzed code, whose behavior is still OPEN (analysis section 8); both are
implemented and tested:

* **Knob direction:** fully left = no decay, fully right = strongest decay. The
  retention factor is therefore `k = 0.999 × (1 − knob)`, so fully right erases
  old material on each pass. The original's default `k = 0.707` corresponds to
  knob ≈ 0.29, the engine's default.
* **Undo lock:** undo and redo are ignored while the loop mode is Frippertronics,
  and the undo LED shows them as unavailable. They work again once another mode is
  selected; erase stays available. This avoids state changes that are hard to
  follow while the loop keeps decaying.

## Seed3 controls (v0.2)

[src/main.cpp](src/main.cpp) runs the engine in the audio callback (48 kHz,
48-frame blocks) and outputs dry input plus loop through a soft limit above
−0.45 dBFS. A mono source goes into the left input; the right input is used when
its jack is detected. Controls are read in the callback; LEDs are updated in the
main loop only on changes. Pot changes below 0.2 % are ignored for Slope, Start,
Size, Slice, Skip and Decay.

| Control | Function | LED |
| --- | --- | --- |
| S1 | Record: first take start/stop, then overdub, or resample, on/off | on while recording, blinks while a request waits |
| S2 | Play/pause (pause follows the inertia) | on while playing |
| S3 | Undo, or redo when a redo is available (locked in Frippertronics) | bright: undo available, dim: redo available |
| S4 | Reverse | on in reverse |
| S5 | Erase (undoable) | dim while a loop exists |
| S6 | Next loop mode | |
| S7 | Seam: window ↔ crossfade | on for crossfade |
| S9–S12 | Sound on Sound, Replace, Frippertronics, Resample | on for the selected mode |
| VR1 (pot 0) | Varispeed −24…+36 semitones, whole semitones, unity at 40 % | |
| VR2 (pot 1) | Inertia 0…1 | |
| pot 2 | Inertial Slope (centre lags both directions) | |
| pot 3 | Start | |
| pot 4 | Size (fully right = whole loop) | |
| pot 5 | Slice | |
| pot 6 | Skip | |
| pot 7 | Frippertronics decay (left keeps, right erases) | |
| D16 | | callback deadline overrun seen |

The physical orientation of keys and pots still needs a check on the board.

## Reference: Qu-Bit Stardust

Stardust is a Eurorack tape looper built on a Daisy Seed2 DFM. The operator
supplied firmware `Stardust_v1_1_1.bin` and manual v1.1.0 for a private
analysis (SHA-256
`e7556ffd3bbfa578b7061e4b06de6d010d4842115439121eea458e2e6cf1a48a`). The
results are in this directory:

* [Firmware analysis: memory, files, undo, and playback](docs/STARDUST_ANALYSIS.md)
  with evidence classes, function addresses, and open questions.
* [Feasibility study of October 19, 2026](docs/FEASIBILITY_2026-10-19.md)
  (German, unchanged): GO for an independent C++ implementation, HOLD for
  flashing or patching the original binary.
* [Analysis scripts](tools/) to reproduce the static findings from a locally
  supplied binary.

This repository contains neither the original binary nor decompiled code or
coefficients copied from it. The analysis describes behavior and data layout;
the engine here is written independently against libDaisy. **Never
flash the Stardust binary or an analysis ELF onto the Seed3.**

## Key findings for the design

* Stardust keeps **two equal float32 stereo buffers of 4,176,000 frames
  (87.0 s at 48 kHz)** in SDRAM, one active and one for undo. Together they use
  66.8 MB of the 64 MiB SDRAM. The Seed3 has the same SDRAM size, so this layout
  fits but leaves almost no SDRAM for anything else.
* Undo, redo, and erase swap the active buffer index and the two loop lengths
  with 10 ms ramps. Resample writes into the inactive buffer. Overdub writes into
  the active buffer in place.
* After every state change, the original writes the whole loop as a 16-bit WAV to
  its internal card and reloads the previous file into the undo buffer. There is
  one undo step plus redo; from the second overdub on, that step is correct only
  after the card reload, because no RAM copy keeps the undo buffer current.
* Varispeed is a semitone target with a short glide and exponential inertia on
  the linear speed (time constant up to 2 s, separate up and down weights), limited
  to ×8 and played back with two-point linear interpolation. The loop seam is a
  fade to zero, not a crossfade.
* Slice/Skip divide the loop into power-of-two slices and draw offsets, reverse,
  repeats, and pitch jumps from a fixed-seed xorshift generator. The
  Frippertronics factor is the Shift+Start knob value (0…0.999); its direction
  contradicts the manual and needs a hardware check.
* The loop-menu WAV import accepts only 16-bit, 48 kHz, mono or stereo files and
  rejects anything longer than the buffer. Two other load paths read until end
  of file without a length check; the clone must bound-check every path.

## Milestones

The steps follow the gates of the feasibility study, adapted to this repository:

1. **Hardware baseline:** stereo loopback at 48 kHz, SDRAM allocation of two
   87 s buffers, controls, and microSD on the kit, each tested separately.
   *Open; v0.1 is the first image that allocates both buffers.*
2. **Minimal looper:** record, play, overdub, reverse without files; generated
   test signals and block-boundary cases on the host. Pass: click-free loop
   boundaries, no buffer overrun, constant callback deadline.
   *Host part done in v0.1; the callback deadline needs the board.*
3. **Memory and undo:** decided: two float32 buffers with copy on write and a
   background copy. Record, undo, and redo at 42, 44, 86, 88 s and at the exact
   buffer limit pass on the host.
4. **Loop modes and effects:** Replace and varispeed since v0.1; Frippertronics,
   Resample and Slice/Skip in v0.2, each with deterministic host tests. Tape and
   digital effects remain; their DSP in the original is not analyzed yet.
5. **microSD and controls:** file access outside the audio callback, WAV
   validation with bounds checks (the engine's `LoadLoop` already rejects
   oversize loads), power-cycle persistence, and mapping of the Stardust controls
   onto the kit's keys, knobs, switch, and MIDI.
