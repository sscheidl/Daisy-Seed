# Looper — Instrument Project

**Status: research and project outline; no firmware build or hardware test.**
Project ID: `looper`. The goal is an original stereo tape looper for the Seed3
Desktop Dev Kit, using the Qu-Bit Stardust as the behavioral reference.

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
the engine here will be written independently against libDaisy/DaisySP. **Never
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

## Next milestone

The steps follow the gates of the feasibility study, adapted to this repository:

1. **Hardware baseline:** stereo loopback at 48 kHz, SDRAM allocation of two
   87 s buffers, controls, and microSD on the kit, each tested separately.
2. **Minimal looper:** record, play, overdub, reverse without files; generated
   test signals and block-boundary cases on the host. Pass: click-free loop
   boundaries, no buffer overrun, constant callback deadline.
3. **Memory and undo:** decide the undo strategy explicitly (copy on commit,
   copy on write, or a reduced sample format) and test record, undo, and redo at
   42, 44, 86, 88 s and at the exact buffer limit.
4. **Loop modes and effects:** Replace, Frippertronics, Resample, slice/skip,
   varispeed, then tape and digital effects, each with deterministic host tests.
5. **microSD and controls:** file access outside the audio callback, WAV
   validation with bounds checks, power-cycle persistence, and mapping of the
   Stardust controls onto the kit's keys, knobs, switch, and MIDI.

Until implementation, `make PROJECT=looper` fails explicitly.
