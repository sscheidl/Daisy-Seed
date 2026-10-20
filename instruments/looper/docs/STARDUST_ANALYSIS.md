# Stardust v1.1.1 — Firmware Analysis: Memory, WAV Paths, and Undo

**As of October 20, 2026. Static analysis of a user-supplied binary; no hardware
execution, no audio measurement.** This report continues the
[feasibility study](FEASIBILITY_2026-10-19.md) and answers its proposed next
step: trace the WAV import, buffer layout, and undo paths with a decompiler.

## Scope and evidence

| Item | Value |
| --- | --- |
| Input | `Stardust_v1_1_1.bin`, 221,280 bytes, SHA-256 `e7556ffd3bbfa578b7061e4b06de6d010d4842115439121eea458e2e6cf1a48a` |
| Load address | `0x24000000` (AXI SRAM), reset handler `0x24001894`, `main` at `0x24016008` |
| Tools | angr 10.0.4 (CFG recovery, decompiler), Capstone 5.0.7, radare2 5.5, GNU arm-none-eabi objdump 2.42 |
| Recovered | 1,388 functions; cross-references to every file-name string |
| Not done | Execution on hardware or in an emulator, audio comparison, analysis of the effect DSP |

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
| Undo / redo | After a 480-frame (10 ms) ramp: toggle the active index, swap active and undo lengths | CONFIRMED |
| Erase | After a 480-frame ramp: active length → 0, old length → undo slot, toggle index; therefore undoable | CONFIRMED |
| Loop and splice seams | 384-frame (8 ms) linear crossfade; no crossfade for loops shorter than 1,152 frames | CONFIRMED |
| Persistence | Each state change enqueues a request in a ring buffer; the main loop writes the active buffer to `0:/temp.wav`, reloads the old `0:/loop.wav` into the undo buffer, and renames | CONFIRMED |

**Open point — multi-layer undo:** overdub changes the active buffer in place,
and the RAM code found so far does not copy the active buffer into the undo
buffer when a layer is committed. The reload of the previous `loop.wav` from the
card after every change is the only synchronization found. Whether undo after the
second and later layers restores the immediately previous layer, and how long that
takes for a full 87 s loop, needs a hardware test on a Stardust or an explicit
decision in the clone (**OPEN**).

## 5. Signal path details

| Detail | Observation | Evidence |
| --- | --- | --- |
| Input saturation | `1.6 · t(0.8 · g · x)` with `t(x) = x(27 + x²)/(27 + 9x²)` (rational tanh approximation); gain smoothed per sample | CONFIRMED (gain mapping PLAUSIBLE, constant 63.0957 = +36 dB) |
| Mix curves | Index 0 constant power (sine/cosine), 1 transition (`wet = min(2m, 1)`, `dry = clamp(2 − 2m)`), 2 linear; output scaled by 0.8 and passed through `t(x)` | CONFIRMED |
| Loop modes | Index 0 Sound on Sound, 1 Replace, 2 Frippertronics, 3 Resample | CONFIRMED |
| Master fade | Output gain follows its target with 0.0005 per sample; file operations wait until it is below 0.001 | CONFIRMED |
| CPU guard | Smoothed callback load above 0.98 skips looper processing until it falls below 0.5 | PLAUSIBLE |
| Clock input | Period measured in microseconds and converted to BPM and samples per beat | CONFIRMED |
| Noise source | Linear congruential generator (1103515245, 12345) seeded per effect | CONFIRMED |
| Effects | Tape, digital, reverb, and filter blocks (`0x2400dbb0` and below) | Not analyzed |

## 6. Consequences for the Seed3 looper

1. **Memory:** two 87 s float32 buffers fit the Seed3's 64 MiB SDRAM exactly as
   in the original, but leave about 290 KB. Delay lines and reverb must then live
   in internal SRAM. 16-bit storage would halve the memory per second, which
   allows longer loops or deeper undo; that is a design choice, not a
   requirement.
2. **Undo:** choose and test an explicit strategy instead of inheriting the
   card-reload behavior. Recommended starting point: two buffers with copy-on-write
   of the active buffer into the undo buffer during an overdub pass, so undo is
   immediate and independent of storage.
3. **Files:** the kit has microSD but no USB host. Map both volumes to the card
   (for example `/stardust/loop.wav` and a `loops/` folder). Validate RIFF chunks,
   format, rate, and length for every load path.
4. **Real time:** keep file I/O out of the audio callback; fading out and pausing
   audio for imports, as the original does, is acceptable.
5. **Controls:** the panel uses an 8-bit shift-register button bank, two direct
   buttons, an encoder, knobs, and CV/gate inputs. Mapping onto the kit's keys,
   knobs, switch, and MIDI remains a separate design step.

## 7. Reproduction

See [tools/README.md](../tools/README.md). The binary, the generated CFG, and
decompiled output stay local and are not committed.
