# Stardust v1.1.1 — Firmware Analysis: Memory, Files, Undo, and Playback

**As of October 20, 2026. Static analysis of a user-supplied binary; no hardware
execution, no audio measurement.** This report continues the
[feasibility study](FEASIBILITY_2026-10-19.md) and answers its proposed next
step: trace the WAV import, buffer layout, and undo paths with a decompiler. A
second pass with Ghidra adds the playback head (varispeed, interpolation,
reverse, inertia), Slice/Skip, the Frippertronics feedback, and the undo
persistence (sections 6–9).

## Scope and evidence

| Item | Value |
| --- | --- |
| Input | `Stardust_v1_1_1.bin`, 221,280 bytes, SHA-256 `e7556ffd3bbfa578b7061e4b06de6d010d4842115439121eea458e2e6cf1a48a` |
| Load address | `0x24000000` (AXI SRAM), reset handler `0x24001894`, `main` at `0x24016008` |
| Tools, first pass (sections 1–5) | angr 10.0.4 (CFG recovery, decompiler), Capstone 5.0.7, radare2 5.5, GNU arm-none-eabi objdump 2.42 |
| Tools, second pass (sections 6–9) | Ghidra 12.1.4 headless, language `ARM:LE:32:v8-m`, Temurin JDK 21.0.12; scripts in [tools/ghidra](../tools/ghidra/) |
| Recovered | angr: 1,388 functions; Ghidra: 872 functions (vector table, auto-analysis, and Thumb pointers in the image and `.data`); cross-references to every file-name string |
| Not done | Execution on hardware or in an emulator, audio comparison, analysis of the effect DSP |

The second pass re-checked the entry points of section 4 in Ghidra: looper object
`0x24047280`, initializer `0x2400f808`, per-block process `0x24010190` (called
from the audio callback `0x24014850`), tape function `0x2400bec4` with the tape
object at `looper + 0x54`, read/write head `0x2400b454`, effect chain
`0x2400dbb0`, state-change callback `0x24005f04`, and `main` `0x24016008` are all
confirmed. Two findings of the first pass are corrected below: the loop seam is a
gain window, not a crossfade (section 4), and Slice/Skip use their own xorshift
generator, not the LCG (sections 5 and 7).

**Language choice:** Ghidra's `ARM:LE:32:Cortex` variant uses the ARMv7 SLEIGH
specification and leaves the FPv5 instructions of this Cortex-M7 build (`VSEL`,
`VMAXNM`, `VMINNM`) as opaque coprocessor operations. Those instructions carry
most clamps in the looper code (44 `VMAXNM`/`VMINNM` between `0x2400b454` and
`0x24011100` alone), so the import uses `ARM:LE:32:v8-m`, which decodes them.

Evidence classes as in the feasibility study:

* **CONFIRMED:** directly visible in decompiled or disassembled code.
* **PLAUSIBLE:** technical interpretation of confirmed code, such as the name of a
  library function or the meaning of a flag.
* **OPEN:** not decided with static analysis.

Addresses are Thumb function entry points (bit 0 cleared) so the findings can be
checked again with the [scripts](../tools/). Function names such as `f_open` are
interpretations from call arguments and are therefore PLAUSIBLE.

## 1. Buffer layout in SDRAM

`main` zeroes all 64 MiB of SDRAM at `0xC0000000` and then calls the looper
initializer `0x2400f808` with the looper object in `.bss` at `0x24047280`, the
SDRAM base, the value `16,704,000`, and the sample rate 48000.0 (**CONFIRMED**).
The initializer derives:

| Looper field | Content | Value |
| --- | --- | --- |
| `+0x5c` | SDRAM base | `0xC0000000` |
| `+0x60` | size argument (in float32 units) | 16,704,000 |
| `+0x64` | maximum frames per buffer (`size / 4`) | **4,176,000 = 87.0 s** |
| `+0x68` | buffer 0 | `0xC0000000` |
| `+0x6c` | buffer 1 (`base + maxFrames × 8`) | `0xC1FDC400` |
| `+0x78` | active buffer index | 0 or 1 |
| `+0x94` | loop length of the active buffer, in frames | |
| `+0x98` | loop length of the undo buffer, in frames | |

Every buffer access in the save, load, import, and record paths uses
`buffer[index] + frame × 8` with interleaved float32 left/right (**CONFIRMED**).
The two buffers occupy 66,816,000 bytes; 292,864 bytes of SDRAM remain outside
them.

**The 43/87 s question:** the maximum of 87.0 s is the manual's import limit of
1:27. The reported ~43 s threshold of the v1.1.1 bug fix is exactly half of one
buffer (2,088,000 frames = 43.5 s). The fix itself cannot be seen without the
v1.1.0 binary; the current layout no longer has a boundary at 43.5 s (**OPEN**
for the old version, **CONFIRMED** for the current one).

## 2. File system and volumes

* FatFs with two volumes, `0:` and `1:` (**CONFIRMED** by paths and two mounts).
* `1:` is the USB flash drive: `options.json`, `StardustFileLog.txt`, the loop
  menu directory scan, and exports use `1:/`. `options.json` is read from the
  USB host's class-active callback (**CONFIRMED** for the paths, **PLAUSIBLE**
  for USB).
* `0:` holds the persistent buffer `/loop.wav` and `/temp.wav`. An SDMMC1 driver
  is present in the image, so `0:` is very likely an internal SD card
  (**PLAUSIBLE**). Startup accesses `0:/test.txt` to check that the volume works;
  persistent buffer recall runs only if that check succeeds.
* Settings (76-byte block, version 2) and a 48-byte calibration block live in
  QSPI flash through the libDaisy persistent-storage pattern; `main` compares and
  writes settings at most every 5 s (**PLAUSIBLE**).

## 3. WAV paths

| Function | Purpose | Format handling | Length bound |
| --- | --- | --- | --- |
| `0x24013bec` | Loop-menu import of slot *n* from `1:/` | Walks RIFF chunks, skips unknown chunks, honors odd-size padding; accepts format tag 1, 3, or 0xFFFE, then requires **16 bit and 48,000 Hz**; mono is duplicated to both channels, more than two channels use the first two | **Rejects `frames > maxFrames − 1`** |
| `0x24005d78` | Recall of `0:/loop.wav` (persistent buffer) | Fixed 44-byte header; 16-bit stereo or 32-bit float stereo | **None found**: reads until end of file |
| `0x24015f18` | Load of `1:/loop.wav` (trigger condition OPEN) | Fixed 44-byte header, assumes 16-bit stereo | **None found**: reads until end of file |
| `0x24006544` | Write active buffer to `0:/temp.wav` | 16-bit stereo 48 kHz, canonical 44-byte header, 1024-frame chunks | Loop length |
| `0x24015a98` | Shared writer: `/temp.wav`, `1:/loop.wav`, overwrite of a menu slot, or new `1:/~zzz-StardustLoop-NNN.wav` (NNN = highest existing + 1) | Same as above; a float32 branch exists but the configured depth is 16 | Loop length |

Details (**CONFIRMED**):

* Conversion on load is `sample / 32768`; on save, values are clamped to
  ±0.999985 and scaled by 32767, so the 16-bit range used is ±32766.
* The loop menu scans `1:/` for `*.wav`, skips directories, hidden and system
  files and names longer than about 127 characters, stops at 32 files, and sorts
  them case-insensitively.
* An import from the loop menu fades the output to below −60 dB, stops audio,
  imports into the **active** buffer, writes it to `0:/temp.wav`, reloads the old
  `0:/loop.wav` into the undo buffer if an undo state exists, replaces
  `loop.wav` with `temp.wav`, and restarts audio. Undo after an import therefore
  returns to the previous loop.

**Consequence for the clone:** the two load paths without a bound would write past
the active buffer for a file longer than 87 s: from buffer 0 into the undo buffer,
or from buffer 1 past the end of the looper area. Whether the original can be
made to load such a file is **OPEN**. The clone must check every load against the
buffer size and validate headers instead of trusting a fixed 44-byte layout.

## 4. Record, overdub, undo

The looper's per-block process function is `0x24010190`. It is called from the
audio callback `0x24014850`, which also reads the panel controls. It hands
playback and recording to a tape object at `looper + 0x54` (`0x2400bec4`) and its
read/write head (`0x2400b454`).

| Operation | Observed behavior | Evidence |
| --- | --- | --- |
| First recording | Writes the same input into **both** buffers, with a 960-frame (20 ms) fade-in; stops automatically at `maxFrames` and sets both lengths | CONFIRMED |
| Overdub (Sound on Sound) | Reads, mixes, and writes back **in place in the active buffer**; the sum is clamped to ±1 | CONFIRMED |
| Replace | Old material is replaced by the new input, with 960-frame fades at punch-in and punch-out | CONFIRMED |
| Frippertronics | Old material is multiplied by a decay factor before the new input is added | CONFIRMED (factor mapping OPEN) |
| Resample | Writes the processed playback into the **inactive** buffer with its own length, then switches buffers; the original stays as the undo state | CONFIRMED |
| Undo / redo | After a 480-frame (10 ms) ramp: toggle the active index; swap active and undo lengths if they differ; call the state-change callback | CONFIRMED |
| Erase | After a 480-frame ramp: active length → 0, old length → undo slot, toggle index; therefore undoable | CONFIRMED |
| Loop seam | **Corrected:** not a crossfade. For loops of at least 1,152 frames the playback signal is multiplied by a gain window that falls linearly to 0 over the last 384 frames (8 ms) before the loop end and rises over the first 384 frames after the loop start. Stored audio is not changed. No window below 1,152 frames, and none in Frippertronics mode while no Skip transformation is active | CONFIRMED |
| Position jumps | A change of the window start (Start knob, Skip offset) always starts a true 384-frame linear crossfade between a second read pointer at the old position and the new position; Size changes take this path only in some position cases (exact conditions not resolved) | CONFIRMED for start changes, PLAUSIBLE for Size |
| Persistence | Each state change enqueues a request in a ring buffer; the main loop writes the active buffer to `0:/temp.wav`, reloads the old `0:/loop.wav` into the undo buffer, and renames | CONFIRMED |

The open point of the first pass on multi-layer undo is resolved in
[section 9](#9-undo-redo-and-persistence).

## 5. Signal path details

| Detail | Observation | Evidence |
| --- | --- | --- |
| Input saturation | `1.6 · t(0.8 · g · x)` with `t(x) = x(27 + x²)/(27 + 9x²)` (rational tanh approximation); gain smoothed per sample | CONFIRMED (gain mapping PLAUSIBLE, constant 63.0957 = +36 dB) |
| Mix curves | Index 0 constant power (sine/cosine), 1 transition (`wet = min(2m, 1)`, `dry = clamp(2 − 2m)`), 2 linear; output scaled by 0.8 and passed through `t(x)` | CONFIRMED |
| Loop modes | Index 0 Sound on Sound, 1 Replace, 2 Frippertronics, 3 Resample | CONFIRMED |
| Master fade | Output gain follows its target with 0.0005 per sample; file operations wait until it is below 0.001 | CONFIRMED |
| CPU guard | Smoothed callback load above 0.98 skips looper processing until it falls below 0.5 | PLAUSIBLE |
| Clock input | Period measured in microseconds and converted to BPM and samples per beat | CONFIRMED |
| Noise source | Linear congruential generator (1103515245, 12345, 31-bit result) with one global state in `.data` at `0x24044008`; the initializer draws per-instance seeds for the effect modulators from it, and the effect chain advances it | CONFIRMED |
| Slice/Skip random numbers | **Not the LCG:** a separate xorshift32 generator (shifts 13, 17, 5) in `looper + 0x138`, see section 7 | CONFIRMED |
| Effects | Tape, digital, reverb, and filter blocks (`0x2400dbb0` and below) | Not analyzed |

## 6. Playback head: varispeed, interpolation, reverse, inertia

The per-block process `0x24010190` calls the tape function `0x2400bec4` **once
per sample**; the tape function calls the head `0x2400b454` once per sample. Field
offsets below are given relative to the looper object and, in parentheses, to the
tape object (`looper + 0x54`).

| Field | Content |
| --- | --- |
| `+0x54` (`+0x00`) | sample rate (48,000) |
| `+0x7c` (`+0x28`) | integer frame index inside the loop window |
| `+0x8c` (`+0x38`) | fractional head phase |
| `+0xc4` (`+0x70`) | Inertial Slope knob, 0…1, default 0.5 |
| `+0xc8` (`+0x74`) | Inertia knob, 0…1 |
| `+0xd4` (`+0x80`) | smoothed varispeed offset in semitones |
| `+0xd8` (`+0x84`) | varispeed target in semitones (encoder) |
| `+0xdc` (`+0x88`) | 1V/Oct offset in semitones |
| `+0xf0` (`+0x9c`) | target speed: ratio while playing, 0 while paused |
| `+0xf4` (`+0xa0`) | current speed after inertia |
| `+0x115` (`+0xc1`) | reverse request, toggled by button or gate |
| `+0x324` (`+0x2d0`) | head increment in frames per sample |

**Pitch and speed chain (CONFIRMED):**

1. **Encoder:** each detent changes the target `+0xd8`, clamped to −24…+36
   semitones (×¼…×8). Step per range mode: 1 semitone; 0.25 semitone; detents
   are accumulated and applied only when the result is a whole octave or an
   octave plus a fifth; ±12 semitones after 6 detents. Shift+turn always uses the
   ±12-semitone step. The order of the four modes matches the manual's list
   Semitones, Unquantized, Octaves & Fifths, Octaves Only (names PLAUSIBLE).
2. **1V/Oct:** the calibrated CV is clamped to ±60 semitones, **rounded to whole
   semitones**, clamped again to −36…+60, and stored in `+0xdc`. The rounding
   adds 1 when the fractional part is at least 0.5; for negative values it
   therefore truncates toward zero (PLAUSIBLE side effect, not tested).
3. **Glide:** the smoothed offset `+0xd4` follows the encoder target with a
   one-pole coefficient of 0.001 per sample (time constant ≈ 1,000 samples ≈
   21 ms) while the distance is below 4 semitones; jumps of 4 semitones or more
   are taken immediately.
4. **Ratio:** `R = 2^((smoothed offset + V/Oct offset) / 12)`; the target speed
   is `R` while playing and 0 while paused.
5. **Inertia:** the current speed `v` is a one-pole low-pass of the target speed:
   `v += (target − v) · c` with `c = clamp(1 − exp(−1 / (τ · fs)), 0, 1)` and
   `τ = (1.998 · a² + 0.002) · w` seconds, `a` = Inertia knob. Below `a = 0.03`
   inertia is off (`c = 1`). The time constant is at most 2.0 s; reaching 1 % of
   the remaining distance takes about 4.6 τ. The curve is exponential in the
   **linear speed** domain, so a tape stop falls faster and faster in semitones.
   Small residues snap to exactly 0 or 1.
6. **Inertial Slope:** `s = 2 · slope − 1` with a dead band of ±0.05. When the
   target is above `v` (speeding up, starting playback) the weight is
   `w = 1 − max(0, −s)`; otherwise (slowing down, pausing) `w = 1 − max(0, s)`.
   Fully right therefore lags only increases and play, fully left only decreases
   and pause, as described in the manual. `w = 0` gives an immediate change.
7. **Head increment:** `+0x324` follows `clamp(v · P, 0, 8)` with a one-pole
   coefficient of 0.1 per sample, where `P` is the Slice/Skip pitch ratio while a
   Skip transformation is active (section 7). Speed is limited to ×8; there is no
   lower limit apart from 0, so 1V/Oct can slow the loop below ×¼.

**Interpolation (CONFIRMED):** the phase accumulates the increment every sample.
For each whole frame crossed in that sample, the head reads (and, when recording,
writes) one frame and keeps the last two frames read. The output is the linear
interpolation between those two frames at the fractional phase, which adds one
frame of latency. Above ×1 the frames in between are skipped without any
anti-alias filter; below ×1 the same pair is interpolated over several samples.
When recording at a speed other than ×1, every frame crossed in a sample is
written with the input linearly interpolated between the previous and the current
input sample at that frame's position. Below ×1 fewer frames than input samples
are written (decimation without filter).

**Reverse (CONFIRMED):** the effective direction is the reverse request XOR the
per-slice reverse of Skip (section 7). A direction change ramps the playback gain
to 0 over 480 frames (10 ms), flips the direction at zero, and ramps back up over
480 frames; it is not a crossfade. The speed magnitude stays the same. In reverse
the index counts down and wraps from the window start to its end; every wrap
returns an end-of-loop event.

**Start and Size (CONFIRMED):** Start and Size are knob plus CV, clamped to 0…1,
with dead bands below 0.003 and above 0.997. The window start is
`length × start`; the window size is `1,152 + size² × (length − 1,152)` frames.
The minimum window is therefore 24 ms, not the 5 ms stated in manual v1.1.0.

## 7. Slice and Skip

Slice (`+0xe4`, tape `+0x90`) and Skip (`+0xe0`, tape `+0x8c`) are knob plus CV,
clamped to 0…1. Both run inside the tape function.

**Segmentation (CONFIRMED):**

* **Free-running:** for loops shorter than 192,000 frames (4 s) the base segment
  is the whole loop. Otherwise the loop is divided by the smallest power of two
  2^m that is at least `length / 96,000` (integer division), so a base segment is
  roughly 1–2 s long.
* **Slice divisor:** `D = 2^⌊4.9 × slice⌋`, that is 1, 2, 4, 8, or 16. Slice
  length = base segment / D, which gives the manual's 62 ms minimum for a 1 s base
  segment. The number of slices is `2^m × D`.
* **Clock mode** (flag `+0x180`, PLAUSIBLE): slice length = four clock periods / D;
  the number of slices is the loop length divided by that.
* A slice counter advances by the current speed every sample (free-running) or by
  1 while moving (clock mode); a slice boundary is reached when it passes the
  slice length.

**At every slice boundary while playing (CONFIRMED):** all slice transformations
are reset (offset 0, no reverse, pitch ratio 1, no repeat). Then:

* **Repeat (Slice > 0.02):** with probability 3/5 a repeat factor `2^−j` is drawn,
  `j` uniform in `0…J`, with `J = ⌊6.1 × f_slice⌋` and `f_slice` the fractional
  part of `4.9 × slice`, that is the position inside the current divisor step.
  For `j > 0` the head loops a window of `max(slice length × 2^−j, 1,152)`
  frames until the next boundary (stutter; that the window starts at the slice
  start is PLAUSIBLE). At the start of
  each divisor step `J = 0`, so no repeats occur; this matches the manual's
  "solid LEDs, no random repeats" (PLAUSIBLE mapping to the LEDs).
* **Skip (Skip > 0.01, Freeze off):** zone `z = ⌊5.99 × skip⌋` (0…5),
  `f = frac(5.99 × skip)`. Position offset and reverse accumulate across zones;
  the pitch transformations belong to one zone each:

| Manual zone | Code zone | Transformation | Probability per slice |
| --- | --- | --- | --- |
| 1 | all | Start offset: for `f ≥ 0.5` jump to a random slice boundary of the whole loop; for `f < 0.5` offset `u × 2f × slice length`, `u` uniform in 0…1 in steps of 0.01. Skipped while a position crossfade is running | 1/2 |
| 2 | ≥ 1 | Reverse this slice | 12.5 % + 50 % × f |
| 3 | 2 | Micro pitch: ratio `2^((4f·u − 2) / 12)`, `u` uniform in 0…1, so always −2 semitones at `f = 0` and uniform in ±2 semitones at `f = 1`; glide over `g × slice length`, `g` drawn from the first `⌊3.99 f⌋ + 1` of the quarter steps 0, ¼, ½, ¾, 1 | 1/4 |
| 4 | 3 | Octave: ratio drawn from the first `⌊3.99 f⌋ + 1` of ×1, ×2, ×½, ×4; immediate | 1/4 |
| 5 | 4 | Semitone jump of `±⌊24 f⌋` semitones; only the sign is random; immediate | 1/4 |
| 6 | 5 | As zone 5, plus with probability `f` a glide as in zone 3 | 1/4 |

* A glide is a **linear ramp of the pitch ratio** over the chosen number of
  frames. Every pitch transformation lasts until the next slice boundary.
* **Random numbers:** all decisions use an xorshift32 generator (shifts 13, 17,
  5) in `looper + 0x138`. The static constructor `0x24017384` sets it to 1 and
  nothing reseeds it, so the decision sequence after every power-up is the same
  (CONFIRMED for the code; audible repeatability PLAUSIBLE because the sequence
  also depends on when boundaries occur).
* An alternative 8-zone decoding (`⌊7.99 × skip⌋`, transformations combined
  differently) is selected by `looper + 0x16c = 0`. The initializer sets this flag
  to 1 and no other code writes it, so that variant is unreachable in v1.1.1
  (PLAUSIBLE dead code).
* **Frippertronics interaction:** while a Skip offset is active in Frippertronics
  mode, the overdub uses the Sound-on-Sound sum with ±1 clamp instead of the decay
  (CONFIRMED).

## 8. Frippertronics feedback

**Factor (CONFIRMED):** the head multiplies old material by `k` at
`looper + 0x364` (tape `+0x310`, the "head field `+0x310`" of the first pass).
Every sample, `k` follows `clamp(D + C, 0, 0.999)` with a one-pole coefficient of
0.005 (≈ 4 ms).

* `D` (`+0x35c`) is the Shift+Start knob value, clamped to 0…0.999. The
  initializer and the control reset set `D = 0.707`, that is −3.0 dB per pass.
* `C` (`+0x360`) is the Start CV, clamped to 0…0.999, when the setting "Start CV
  Destination" selects the Frippertronics decay (`0x24067b30 = 1`); the Start CV
  then no longer moves Start. Otherwise `C = 0`.

**Write rule (CONFIRMED):** while recording, each frame crossed becomes
`old × k + input × gain`; the input is faded in and out over 960 frames at
punch-in and punch-out. There is **no clamp** in this branch (Sound on Sound
clamps to ±1), so with `k` near 0.999 the stored level can exceed full scale.
Passes without recording leave the loop unchanged; the decay is per overdub pass,
not over time.

**Knob direction (OPEN):** the knob value is used directly as the retention
factor: fully left gives `k = 0` (old material erased on each pass), fully right
gives `k = 0.999`. Manual v1.1.0 describes 0 % decay fully left and 100 % decay
fully right. All nine knobs are initialized without flip or invert, and the same
convention places Start fully left at the loop start as the manual says, so either
the manual wording is inverted or the Shift+Start value is inverted outside the
code found. A hardware test decides.

**Undo lock (OPEN):** the manual says Undo does not work in Frippertronics mode.
No loop-mode check was found on the undo path in the UI handlers or in the
process function.

## 9. Undo, redo, and persistence

**Resolution of the open point (CONFIRMED):** Stardust has **exactly one undo
step**, implemented as a toggle between the two buffers. Overdub writes in place
into the active buffer; outside the startup recall there is **no RAM copy** into
the inactive buffer. The only writes to the inactive buffer are the first
recording (both buffers), Resample, and the reload from the card. For the second
and later overdubs the undo buffer is therefore correct only after the card
reload has finished.

Sequence after every state change that calls the callback (for example the end
of a recording, undo, erase, resample):

1. The state-change callback `0x24005f04` enqueues a request.
2. The main loop drains the queue and for each request writes the **entire**
   active buffer (length `+0x94`, the active index read at that moment) as 16-bit
   WAV to `/temp.wav`, while audio keeps running.
3. When the queue is empty and the undo length `+0x98` is not 0, it sets
   `+0x102`, loads the previous `/loop.wav` into the **inactive** buffer with the
   recall function `0x24005d78` (argument 0), sets the undo length to the file
   length, and clears `+0x102`.
4. It deletes `/loop.wav` and renames `temp.wav` to `/loop.wav`; record
   start/stop requests wait while `+0x101` marks this step.

Example with layers A (first recording), B, and C:

| After | Active buffer | Inactive buffer in RAM | Undo correct? |
| --- | --- | --- | --- |
| first recording A | A | A | — |
| overdub B, before reload | B | A | yes |
| overdub C, before reload | C (in place over B) | A | **no:** undo would return to A |
| overdub C, after reload of `loop.wav` (B) | C | B | yes |
| undo, after save and reload | B | C (from the old `loop.wav`) | redo yes |

**Timing and races:**

* Undo requests are ignored while `+0x102` is set (reload running) (CONFIRMED).
  They are **not** blocked during the write of `/temp.wav`. An undo in that phase
  after the second or a later overdub toggles to the state two changes back, and
  the following save and rename persist that state; the newest layer is then lost
  (PLAUSIBLE from the code, not tested).
* Each state change writes 4 bytes per frame (16.7 MB for 87 s) and reads about
  the same amount back. The duration depends on the card and is **OPEN**.
* If `/loop.wav` cannot be opened, an error handler runs instead of the reload,
  and the inactive buffer keeps an older state (CONFIRMED for the branch;
  behavior without a working card OPEN).
* At startup the recall function (argument 1) loads `/loop.wav` into the active
  buffer, copies it into the other buffer, and sets both lengths (CONFIRMED).

**Answer to the question of section 4:** yes. Undo after the second and later
layers depends on the card. There is no multi-level undo history in RAM or on the
card, only the previous state.

## 10. Consequences for the Seed3 looper

1. **Memory:** two 87 s float32 buffers fit the Seed3's 64 MiB SDRAM exactly as
   in the original, but leave about 290 KB. Delay lines and reverb must then live
   in internal SRAM. 16-bit storage would halve the memory per second, which
   allows longer loops or deeper undo; that is a design choice, not a
   requirement.
2. **Undo:** do not inherit the card-reload behavior (section 9): it makes undo
   depend on the card and leaves a window in which undo returns two layers.
   Recommended starting point: two buffers with copy-on-write of each frame into
   the undo buffer before the overdub changes it, so the undo buffer always holds
   the previous layer and undo is immediate and independent of storage. Keep one
   undo step plus redo, as in the original.
3. **Files:** the kit has microSD but no USB host. Map both volumes to the card
   (for example `/stardust/loop.wav` and a `loops/` folder). Validate RIFF chunks,
   format, rate, and length for every load path.
4. **Real time:** keep file I/O out of the audio callback; fading out and pausing
   audio for imports, as the original does, is acceptable.
5. **Controls:** the panel uses an 8-bit shift-register button bank, two direct
   buttons, an encoder, knobs, and CV/gate inputs. Mapping onto the kit's keys,
   knobs, switch, and MIDI remains a separate design step.
6. **Varispeed:** the behavior to match is a semitone target with a short glide,
   exponential inertia on the linear speed with separate up and down time
   constants, a ×8 limit, and a 10 ms fade for direction changes (section 6). The
   original's two-point linear interpolation without anti-alias filtering is a
   floor, not a target; a better interpolator in the clone is a deliberate,
   audible deviation and must be listened to.
7. **Seams:** the original dips the output to zero at the loop point. Decide
   whether the clone copies that dip or uses a real crossfade, and test both on
   sustained material.
8. **Slice/Skip:** implement segmentation, probabilities, and zones as in
   section 7 with an own random generator; seed it from a hardware source or keep
   the fixed seed only if repeatable sequences are wanted.
9. **Frippertronics:** implement `old × k + input` per overdub pass with `k` in
   0…0.999 and a 4 ms smoothing, and clamp or saturate the stored result, which the
   original does not. Fix the knob direction only after checking it on a Stardust.

## 11. Reproduction

See [tools/README.md](../tools/README.md). The binary, the generated CFG, the
Ghidra project, and decompiled output stay local in `work/` and are not
committed.
