# Looper — Instrument Project

**Status: v0.5 with all four loop modes, Slice/Skip, loop persistence and WAV
import on the microSD card, MIDI CC control over USB, and Windows access to the card
through the control panel (SD management, new in v0.5); host tests and ARM build
pass; no hardware run yet.** Project ID: `looper`. The goal is an original
stereo tape looper for the Seed3 Desktop Dev Kit, using the Qu-Bit Stardust as the
behavioral reference. Validation details: [docs/VALIDATION.md](docs/VALIDATION.md); checklist for the
first run on the kit: [docs/HARDWARE_TEST.md](docs/HARDWARE_TEST.md). A separate
build with the Mod on Daisy modulation matrix: [docs/MODULATION.md](docs/MODULATION.md).
Windows access to the card: [docs/SD_MANAGEMENT.md](docs/SD_MANAGEMENT.md).

```sh
make PROJECT=looper -j4            # firmware: build/Looper-v0.5.0.bin
make PROJECT=looper MOD=1 -j4      # Mod on Daisy build: build/mod/LooperMod-v0.5.0.bin
make PROJECT=looper test           # host tests of both builds, incl. production callback
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
    (transparent below 0.95, asymptote 1.0) that fades in with the punch as well,
    so material above the knee does not step either; the original does not
    limit here.
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
  copies the rest at 32 frames per sample (about 2.7 s for 87 s). Copies on write
  count against the same budget, so a 48-frame callback copies at most 1,536
  frames unless its writes alone need more. Until it
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
  physical seam like the window. Loops shorter than 1,152 frames dip to zero over
  a third of their length at each end of the seam in both modes, unless they are
  at least 960 frames long and the crossfade mode has their post-roll. Mode
  changes take effect only while the head is away from every seam region, so
  switching never steps the output; where a loop has no such region (up to 1,344
  frames), the post-roll crossfade switches in under a 384-frame jump crossfade.

Not in v0.5: effects, input saturation and mix curves, Freeze, clock-synced
slices, 1V/Oct and CV, the encoder range modes, WAV export, MIDI notes, clock and
feedback to the controller. J7 is a USB-MIDI device for CC control, the
firmware update request and the control panel's SD SysEx; in SD management it is a
USB drive plus a serial port for the control panel.

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

## Seed3 controls (v0.4)

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
| S8 | Import the next WAV file from `looper/loops/` (undoable) | card: off = none, dim = ready, blinking = saving or loading, fast = error |
| S9–S12 | Sound on Sound, Replace, Frippertronics, Resample | on for the selected mode |
| S9 + S16, 5 s | Firmware update mode: after a running card write, all LEDs on, Daisy bootloader ([details](../../common/bootloader/README.md)); S9 selects Sound on Sound first | all on |
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

## microSD (v0.3)

A FAT32 card holds a folder `looper/` (created when missing). Since v0.5 the card can
stay in the kit: the [control panel](../../diagnostics/docs/LOOPER_PROFILE.md) hands it
to Windows as a USB drive and back ([SD management](#sd-management-v05)).

* **`loop.wav`, persistence.** Two seconds after the loop last changed and once
  the engine is idle (no recording, fade, undo copy or import running), the main
  loop writes the current loop as 32-bit float stereo WAV at 48 kHz, with a
  small `lprs` chunk for the loop length and the crossfade post-roll. Audio keeps
  running. The file is written as `loop.tmp` in 32 KB pieces and replaces
  `loop.wav` only when complete; any change to the loop during the write
  discards it, and the save starts again later. After erase, `loop.wav` is
  deleted. At power-up the firmware loads `loop.wav` (or a complete `loop.tmp`
  left by a power loss between delete and rename) before audio starts. Only the
  current loop is kept: undo starts empty after a power cycle. 87 s take about
  33 MB.
* **`loops/`, import.** S8 loads the next `*.wav` file, sorted by name ignoring
  case (at most 32 files, names shorter than 96 characters, hidden files
  skipped). Accepted: 16- or 24-bit PCM or 32-bit float, also
  WAVE_FORMAT_EXTENSIBLE, mono (duplicated) or up to 8 channels (the first two
  are used), 48 kHz or 44.1 kHz, at most 87 s. Every chunk is checked against
  the file size and the length (after conversion) against the buffer before
  anything is written.
  The file is read into the other buffer while the loop keeps playing, then the
  engine switches with a 10 ms fade; undo returns the previous loop. While a
  file loads, record, undo and erase are ignored. Rejected files and a busy
  engine (recording, resampling) are shown with a fast-blinking S8 LED.
* **44.1 kHz files** (v0.3.2) are converted to 48 kHz while they load: a
  64-tap polyphase windowed-sinc filter (ratio 160/147), flat to 17 kHz within
  0.001 dB, −0.1 dB at 19 kHz, −3.3 dB at 20 kHz, images below −90 dB. The file
  is treated as one loop period, so the seam stays as clean as in the file. The
  looper itself always runs at 48 kHz (libDaisy's audio clock has no 44.1 kHz
  setting). The conversion runs in steps of 4,096 input frames per main-loop
  pass, so a long file loads more slowly than a 48 kHz one (load time on the
  board: NICHT GEMESSEN). The filter table (40 KB) lives in SDRAM. Other rates
  (32, 88.2, 96 kHz …) are still rejected.

All card access runs in the main loop. The audio callback only publishes a
snapshot of the engine state (sequence lock) and answers import requests, so
file I/O never runs in the callback. The FatFS objects and the 32 KB transfer
buffer live in SDRAM, because the SDMMC1 IDMA cannot reach DTCM. The SD
interrupt runs below the audio DMA. The original fades audio out and pauses it
for imports and reloads its undo state from the card; this firmware does neither
(analysis sections 3 and 9).

## SD management (v0.5)

The Windows control panel 0.6.0 (`diagnostics/windows`, tab **SD share**) can open the
looper's card as a Windows drive, for example to copy WAV files into `looper/loops/` or
back up `looper/loop.wav`:

1. **Share SD card with Windows** in the panel. If the loop changed since the last
   save, the looper writes `loop.wav` at once (S8 LED blinking) and the panel waits.
   While recording, overdubbing, resampling or importing, sharing is refused.
2. The looper fades out, stops audio and restarts into its **SD management** mode:
   no audio, the card appears as a USB drive (`Looper SD`). D2 shows SHARED, D16 and
   the Seed LED blink.
3. Eject the drive in Windows. **Return to the Looper** in the panel, or S4 held alone
   for 2 s on the kit, restarts the looper, which reloads `loop.wav`.

Undo/redo, loop and seam mode and play/reverse start fresh after the return, as after a
power cycle. A power cycle in SD management always starts the looper. If the loop
cannot be saved (card write error), the panel asks before the unsaved changes are given
up. The card must be FAT32 (exFAT for the looper itself is a later step). Protocol,
ownership rules, LEDs and memory: [docs/SD_MANAGEMENT.md](docs/SD_MANAGEMENT.md).
Hardware behaviour: NICHT GEMESSEN.

## MIDI control (v0.4)

The kit appears as a USB-MIDI device on **J7**. The looper listens to control
changes on **every channel**; a DAW or a MIDI controller can play it without an
editor. Notes, pitch bend, aftertouch, clock and all other controllers are
ignored, and the looper sends nothing back except the bootloader reply.

| CC | Function | Values |
| --- | --- | --- |
| 20 | Speed (varispeed) | value − 64 semitones: **64 = ×1**, 76 = +12, 40 = −24; below 40 −24, above 100 +36 |
| 21 | Inertia | 0…127 = 0…1, like pot 1 |
| 22 | Inertial Slope | like pot 2 (64 ≈ centre) |
| 23 | Start | like pot 3 |
| 24 | Size | like pot 4 (127 = whole loop) |
| 25 | Slice | like pot 5 |
| 26 | Skip | like pot 6 |
| 27 | Frippertronics decay | like pot 7 (0 keeps, 127 erases) |
| 102 | Record (S1) | a press is a step from below 64 to 64 or more |
| 103 | Play/pause (S2) | |
| 104 | Undo, or redo when available (S3) | |
| 105 | Reverse (S4) | |
| 106 | Erase (S5) | |
| 107 | Next loop mode (S6) | |
| 108 | Seam: window ↔ crossfade (S7) | |
| 109 | Import the next WAV file (S8) | |
| 110–113 | Sound on Sound, Replace, Frippertronics, Resample (S9–S12) | |

* **Keys:** use momentary buttons (127 on press, 0 on release), as most
  controllers and DAW button lanes send by default. A held value (127 again, or
  any value of 64 or more) is not a new press, so a DAW that repeats the last
  value on playback start does not toggle anything twice. A controller in
  "toggle" mode sends 127 and 0 on alternate presses and therefore triggers on
  every second press only.
* **Pots and CCs together:** the control moved last wins. A CC takes its knob
  over at once; moving the pot by more than 2 % of its travel hands the knob
  back. Without MIDI the pots behave exactly as before.
* SysEx `F0 7D 4C 50 …` is the control panel's STATUS/SHARE request
  ([SD management](docs/SD_MANAGEMENT.md#performance-control-sysex-f0-7d-4c-50)); the
  bootloader request `F0 7D 42 4C …` works as before. Other SysEx is ignored (or goes to
  Mod on Daisy in the MOD build).
* CC1 (mod wheel), aftertouch and CC120/123 stay free in this build. The
  separate [Mod on Daisy build](#mod-on-daisy-build-mod1) consumes them; the CCs above
  work there unchanged.
* The CCs reach the engine at the next audio callback (1 ms blocks). USB
  messages are read in the main loop; a card write or import may delay them by a
  few milliseconds (NICHT GEMESSEN on the board).

## Mod on Daisy build (MOD=1)

`make PROJECT=looper MOD=1` (or `PROJECT=looper-mod`) builds a second image,
`build/mod/LooperMod-v0.5.0.bin`, with the
[Mod on Daisy](../../modules/mod-on-daisy/README.md) 8×8 modulation matrix. The
editors (browser, JUCE, VST3) connect over J7 and read its eight looper columns from
the device: **Speed** (±12 semitones at depth 100), **Start**, **Size**, **Slice**,
**Skip**, **Inertia**, **Fripp Decay** and **Mix** (dry/loop balance, new in this
build). Each value is the pot or CC value plus the modulation, clamped to the
control's range; with an empty matrix the build sounds bit-identical to the regular
one, and the regular image stays byte-identical. Columns, MIDI routing, tests and
resources: [docs/MODULATION.md](docs/MODULATION.md).

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
   validation with bounds checks and power-cycle persistence in v0.3 (host-tested
   with an in-memory card; the card itself is untested). Windows access through the
   shared card reader in v0.5 (host- and simulation-tested). Open: exFAT and the
   patched FatFs R0.16 for the looper's own writes (path 1 in
   [SD_MANAGEMENT.md](docs/SD_MANAGEMENT.md#plan)), WAV export, the mapping of the
   remaining Stardust controls onto the kit's switches and MIDI.
