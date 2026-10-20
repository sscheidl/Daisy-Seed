# Nebulae on Daisy v0.1.0

Project `granular`, for the **Seed3 Desktop Development Kit**, using the official
`daisy::desktop_devkit::Hardware`. Stereo granular processing and a phase-locked
2048-point phase vocoder run from the same PCM16 source. Pitch and scan speed are
independent. This adapts Qu-Bit's default Nebulae v2.1.2 Granular Looper, with the
documented differences below. It does not host alternate instruments or claim
bit-identical Csound output. This is the Nebulae granular project; the separate
[Stardust tape looper](../looper/) has its own analysis and implementation path. [Validation](docs/VALIDATION.md) separates software
checks from pending hardware checks. No board was flashed or sound auditioned.

## Build and binary

Use the pinned ARM toolchain and submodules from [BUILD](../../docs/BUILD.md).
From the repository root:

```sh
make PROJECT=granular -j4
make PROJECT=granular test
make PROJECT=granular test-sanitize
python3 scripts/check-firmware.py instruments/granular/build/NebulaeOnDaisy-v0.1.0.bin
python3 scripts/check-granular-elf.py instruments/granular/build/NebulaeOnDaisy-v0.1.0.elf
```

Outputs: `build/NebulaeOnDaisy-v0.1.0.bin`, `.elf`, `.map`, `SHA256SUMS`.
Identity: `NEBULAE-FW-VERSION=0.1.0`. C++17, Cortex-M7 hard float,
`BOOT_SRAM`, 48 kHz, **256 samples/block**. The pinned AudioHandle clamps larger
blocks to 256; firmware checks the actual size. FFT hop is 512 samples.
Buffer duration is 5.33 ms; vocoder windows span 42.67 ms. These are sample-count
durations, not measured end-to-end latency or CPU usage.

Windows PATH needs the ARM toolchain, xPack build tools and Git `usr/bin`.
Host validation used LLVM-MinGW: `HOST_CXX=clang++`. Windows ThreadSanitizer is
unsupported; Linux CI has a `test-thread` target. The workflow uploads BIN, ELF,
MAP and SHA256SUMS after a push. See validation for executed CI evidence.
Generated outputs and toolchains are not tracked.

## Eight fixed pots

| Pot | Primary | Hold SOURCE: secondary |
| --- | --- | --- |
| VR1 | START, normalized start | Grain position spray |
| VR2 | SPEED, -4 to +4, center +1 | Input gain, unity to +20 dB |
| VR3 | SIZE, squared fraction remaining after START | Random stereo panning |
| VR4 | DENSITY, original tanh/cubic law, about 0.125-983 grains/s | Random grain displacement, up to 10 s |
| VR5 | PITCH, -3 to +2 octaves, center unity | Per-grain pitch variation, up to +/-2 octaves |
| VR6 | OVERLAP, original squared size law, capped at 8 s | Independent random grain size |
| VR7 | BLEND, below | Grain FM from stereo input, File only |
| VR8 | WINDOW, seven continuous window positions | Random grain muting, up to 99% |

The original's encoders become fixed pots. SPEED/PITCH center is unity, with small
snap bands. Endpoints snap in the outer 0.3%. Parameters smooth over 20 ms; WINDOW
over 100 ms. Secondaries default to zero. After page changes or incoming CC, a pot
must approach within 1.2% of the stored value or cross it before taking control.
There is no forced takeover jump.

WINDOW sweeps Gaussian, Blackman-Harris, falling ramp, Bartlett, rising ramp,
sinc (called Hamming by the upstream instrument), rectangle. The Pedal port's
exponential replacement is not selected. File BLEND is `sqrt(1-b)` vocoder plus
`sqrt(b)` granular. Live BLEND goes vocoder -> dry input -> granular; center is
dry only. The engines do not overlap across that center. Without a recorded Live
loop, wet endpoints are silent and center monitors the input.

## Three modes

The **three-position switch SW18** selects modes (`tog_sw18`). The two-position
SW17 is reserved. SW18 is the switch called "switch 1" in the product specification,
regardless of PCB component numbering.

| Official position | Mode | S1/D1 through S16/D16 |
| --- | --- | --- |
| `POS_UP` / 1 | NEBULAE | S1 RECORD, S2 FILE, S3 SOURCE, S4 RESET, S5 FREEZE; S6-S16 reserved |
| `POS_CENTER` / 0 | SAMPLE | Direct slots 01-16 |
| `POS_DOWN` / 2 | KEYBOARD | Exact Toy Piano physical note layout |

Switch state must be stable 20 ms. Buttons use unchanged libDaisy DigitalControl
at nominal 1 kHz. The official Hardware class already reverses shift-register bits;
the application does not reverse again. Mode changes keep sample/DSP, release local
notes and suppress already-held buttons until release. MIDI works in every mode.
API polarity is source-verified; mechanical orientation is pending hardware test.

Keyboard notes, rows S1-S8 and S9-S16:

```text
60 62 64 65 67 69 71 72
72 74 76 77 79 81 83 84
```

The two C5 buttons are independent; releasing one does not release the other.
Keyboard LEDs show only debounced physical presses.

## NEBULAE actions and recording

* RECORD starts/stops stereo capture. First Live capture records input at the
  input gain even without an SD sample; File captures processed file playback.
  First capture completion selects Live. START/SIZE/SPEED are held during every
  normal capture, including overdub.
* On an existing Live loop, RECORD punches into its current position. It records
  the processed loop plus blend-weighted input into an inactive bank and stops
  after one pass. Length stays fixed; normal overdub resets speed/pitch to unity.
  Finishing an overdub preserves the latest File/Live source choice.
  An early stop copies untouched frames in bounded 8192-frame batches, with the
  RECORD LED on until complete (at most about 1.37 s at full capacity).
* FILE advances on release to the next occupied numbered slot, wrapping around.
  SOURCE+FILE cycles forward, backward, random order. Directory scanning refreshes
  the occupied mask in main. An invalid WAV still counts as occupied.
* A SOURCE tap under 300 ms changes File/Live. Holding opens secondaries and does
  not switch on release. SOURCE plus another function is immediately a secondary
  chord, without waiting for the hold threshold.
* RESET returns to START. FREEZE latches position while grains and spectral phase
  continue; it never means mute.
* Hold FILE at least 600 ms then press RESET to clear only the Live loop.
  SOURCE+RESET restores secondary pot values. FILE+FREEZE is reserved: no SD save.
* SOURCE+RECORD toggles circular recording. Passes continue until RECORD stops;
  speed/pitch stay unchanged. Live input contribution is `1-dryBlend`, zero at the
  dry center. Immutable banks replace original in-place rolling-buffer updates.
* SOURCE+FREEZE enables grain-trigger mode. Desktop gate input 1 triggers grains;
  at minimum DENSITY only external triggers launch grains. Other gates/CVs are
  reserved. SAMPLE/KEYBOARD have no additional function-button layer.

Two 8 MiB stereo PCM16 Live banks give **43.6907 s** at 48 kHz. DSP reads the
immutable active bank; capture writes the other. A swap retires old grains/FFT
tails with a short output transition. Freeze does not stop recording. File loading
or source changes during File recording finish that recording first. RAM loops
are volatile.

## SD and errors

FAT32 microSD on the Desktop board: `0:/NEBULAE/SAMPLES/01.wav` ... `16.wav`.
FAT case-insensitivity applies. Startup/insertion attempts the first existing
slot. With no card, select Live and record normally.

Only RIFF/WAVE PCM16, mono/stereo, 44100/48000 Hz is accepted. Other encodings,
containers, rates, depths, channels and WAVE_FORMAT_EXTENSIBLE are rejected.
RIFF extent must match the file. Unknown padded chunks and reversed fmt/data order
are supported; duplicate fmt/data, malformed alignment and truncated chunks fail.
Exactly **one** complete file buffer holds PCM16. Mono reads duplicate to stereo;
44.1 kHz uses rate-correct interpolation into the 48 kHz engine.

Capacity comes from linker symbols after DSP/Live allocation and an 8 MiB reserve.
Run `check-granular-elf.py` for exact limits; see [memory](docs/MEMORY_BUDGET.md).
Header/size failures retain the old sample. After destructive loading starts, File
audio stays muted until complete publication. A later I/O failure invalidates the
file and active slot: the overwritten old sample cannot be restored. Live remains
usable. The latest request during loading is retained for a subsequent load.

Main processes one RIFF chunk or at most 4096 payload bytes per iteration. SDMMC
IDMA uses an isolated aligned AXI-SRAM sector with cache maintenance, 20 ms sector
timeouts and USB service between successful sectors. Card initialization and FAT
chain traversal can exceed a single payload service. Audio/controls use interrupts.

SAMPLE: active loaded slot steady; loading target flashes at 5 Hz. Error pulses
repeat per two seconds; an old valid slot remains steady:

| Pulses | Meaning |
| --- | --- |
| 1 | Missing file/path |
| 2 | Invalid WAV/header |
| 3 | File exceeds PCM capacity |
| 4 | SD initialization/read/I/O failure |
| 5 | No card |

NEBULAE: RECORD shows capture/finalization, SOURCE Live, FREEZE frozen; RESET
pulses at reset/wrap. FILE displays loading/errors. During SOURCE hold, RECORD
shows circular, FREEZE trigger mode, FILE 1/2/3 pulses for file order. LEDs update
outside audio, only on changes, at most 100/s. An inherited PCA9685 DMA-start
failure can hang main/LED service; the audio interrupt continues independently.

## USB MIDI on carrier J7

Class-compliant external USB MIDI, Omni input, one shared stereo voice with last
note priority. Note On/Off, velocity-zero Note On, +/-2-semitone bend on the current
note's channel, CC20-27 for pots in table order. C4/60 leaves base PITCH unchanged.
CC120/123 release that channel; CC121 resets its bend; MIDI Reset clears MIDI
notes/bends. Each MIDI channel and keyboard have separate ownership. In all three
modes, notes transpose/retrigger playback with smoothed velocity gain. With no held
note, playback runs freely at the base pitch and full gain, including after note
release, panic or a mode change. Disconnect releases MIDI notes and leaves local
keys/DSP available.

Only physical keyboard changes send channel-1 notes, velocity 100. Received MIDI
is never echoed; function buttons send nothing. Duplicate C5 keys produce one
host note until both release. Main sends at most eight changes/ms. libDaisy Tx
can silently fail a busy transfer. A press/release wholly between main services
may not transmit, though it affects local playback. Avoid DAW echo back to the
same input if duplicate triggering is unwanted.

Receive parsing runs in USB IRQ into a bounded atomic queue; main hands bounded
batches to audio through a second SPSC queue. Overflow triggers a bounded MIDI
panic rather than leaving a dropped Note Off stuck. Audio allocates nothing and
does no USB send, SD read, logging or LED transfer.

## Differences and hardware test

[Upstream analysis](docs/UPSTREAM_ANALYSIS.md) records evidence, pins and decisions.
Ten grains per source channel form a bounded pool; overload drops new grains.
Delayed grains consume that pool. Random scheduling/panning and immutable-bank
overdub adapt partikkel rather than implementing the entire opcode. Pitch random
follows the manual's +/-2 octaves rather than the instrument's 1200-cent expression.
Grain FM is bounded. Vocoder interpolation corrects the reused backward-frame
fraction at fractional pitches. These change random clouds/transients; no original
device null test or listening comparison was performed. No filter, shifter or
reverb was added.

The [hardware plan](docs/HARDWARE_TEST_PLAN.md) gives J7 application flashing and
smoke checks. CPU timing, analog behavior, USB enumeration, SD compatibility and
mechanical switch orientation are **PENDING-HARDWARE**. Debugger-visible
`callback_peak_cycles` and `callback_overruns` are DWT counters, not measurements
until run on a board. See [licenses/relink instructions](LICENSES/README.md).
