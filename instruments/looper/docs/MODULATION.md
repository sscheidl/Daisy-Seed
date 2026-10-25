# Looper + Mod on Daisy (separate build)

**Image:** `build/mod/LooperMod-v0.4.0.bin` (`make PROJECT=looper MOD=1`, or
`make PROJECT=looper-mod`). Firmware marker: `LOOPER-MOD-VERSION=0.4.0`; IDENTIFY
reports the key `looper` and the version `0.4.0`, the same version as image and marker.
**Status:** software tested, hardware test pending (NICHT GEMESSEN).

This is an **additional, separate build** of the looper with the
[Mod on Daisy](../../../modules/mod-on-daisy/) modulation matrix, following the
[Physical Modeling](../../physical-modeling/docs/MODULATION.md) pattern. The regular
firmware stays as it is:

* `make PROJECT=looper` still compiles exactly the v0.4.0 sources. Its image is
  **byte-identical** to the v0.4.0 image of `feat/looper-midi-cc` (166,072 bytes,
  SHA-256 `7477080fb2b21e2d79393dfc3db4a93749f5695d5db97df8fe11449447727a67`,
  compared after a clean rebuild on 2026-10-26).
* All matrix code in `src/main.cpp` sits behind `LOOPER_MOD`. Only `MOD=1` sets it
  (together with `MOD_ON_DAISY_EXTENSIONS`), and that build uses its own `build/mod/`
  directory.
* With an empty matrix the MOD build sounds bit-identical to the regular build. A
  golden test (`tests/test_golden.cpp`) runs 9,000 blocks of the production callback:
  a first take, overdubs, reverse, undo, all four loop modes, every pot, CC20/26 and
  CC104/105, and everything only the matrix consumes (CC1, aftertouch, clock, start,
  notes, CV and gate inputs). Both builds must print the same FNV-1a hash of the
  output; the Makefile compares them byte for byte.

The editors (browser, JUCE standalone, VST3) read the columns from the device
(`GET_TARGET`, `GET_MODE`), so they work with this build without a change.

## Columns

The looper has one loop and no voices: every column is global, none is latched, and
all 64 cells of the grid are allowed. Poly AT therefore acts on every column through
the most recent held note (module rule for global targets). Speed, Start, Size, Slice,
Skip, Inertia and Decay modulate the value the pot or its CC sets; Mix is new in this
build and has no pot.

| Column | Target | Base value (before modulation) | Depth 100 means | Range after modulation |
| --- | --- | --- | --- | --- |
| 0 | **Speed** (varispeed) | pot 0 or CC20, whole semitones | ±12 semitones (`+-12 st`, span 0.2 of the 60 semitones) | −24…+36 semitones |
| 1 | **Start** | pot 3 or CC23 | full range (`100%`) | 0…1 of the loop length |
| 2 | **Size** | pot 4 or CC24 | full range | 0…1 (window `1152 + size² × (length − 1152)`) |
| 3 | **Slice** | pot 5 or CC25 | full range | 0…1 (divisor 1…16, repeats) |
| 4 | **Skip** | pot 6 or CC26 | full range | 0…1 (the six Skip zones) |
| 5 | **Inertia** | pot 1 or CC21 | full range | 0…1 (off below 0.03, τ up to 2 s) |
| 6 | **Fripp Decay** | pot 7 or CC27 | full range | 0…1 (0 keeps, 1 erases; acts in Frippertronics only) |
| 7 | **Mix** | fixed 0.5 = dry and loop at unity | ±50 % (`+-50%`, span 0.5) | 0 = dry input only … 1 = loop only |

Inertial Slope (pot 2, CC22) is not a column; it keeps working as before.

### How modulation combines with pots and MIDI CCs

* **Base:** the control moved last, as in the regular build: the pot, or the CC
  after it took the knob over (a 2 % pot move hands the knob back,
  [README "MIDI control"](../README.md#midi-control-v04)). The pot noise filter
  (changes below 0.2 % ignored for Start, Size, Slice, Skip and Decay) applies to the
  base.
* **Value = base + offset, clamped.** The offset is the sum of `source × depth/100 ×
  span` over the column's active connections (module `Engine::Apply`). The sum is
  clamped to the column range, so a pot can be turned while it is modulated and a
  modulation runs into the end of the range instead of wrapping. With no active
  connection the value is the base **bit for bit**.
* **Speed** keeps whole semitones for the base (pot and CC20, as before) and adds the
  modulation **unrounded**, so an LFO bends the pitch smoothly instead of stepping.
  The engine's glide and inertia then act on the result as on a pot move.
* **Mix** sets two gains for the output stage: dry `min(1, 2 × (1 − x))` and loop
  `min(1, 2 × x)`. At the base 0.5 both are exactly 1. Mix changes only what is heard:
  recording, overdubs and Resample take the input and the loop playback before it.
* **Start, Size, Slice, Skip** go to the engine like pot moves. Window changes that
  move the head use the engine's 384-frame jump crossfade; fast modulation of Start or
  Size therefore produces many crossfades. Whether that sounds good is OPEN until the
  listening test.
* **Decay** acts only in Frippertronics mode, like pot 7.
* **Rate:** the matrix ticks once per audio block (48 frames = 1 kHz control rate)
  in `Controls()`, right after `hw.UpdateAllControls()` and before the engine reads
  its controls. MIDI sources are smoothed by the module (10 ms), CV by 2 ms.
* **Editor monitor:** the build reports each column's base (Speed as its position
  `(semitones + 24) / 60`, Mix as 0.5), so the editors show where the pot sits and the
  range the modulation covers. It also reports the **loop mode** (Sound on Sound,
  Replace, Frippertronics, Resample, with the keys S9–S12 / CC110–113 as hints) and
  whether the last change came from a key (S6, S9–S12) or a CC (107, 110–113).
* **Extensions:** built with `MOD_ON_DAISY_EXTENSIONS=1` and switched on in
  `InitModulation()` with `EnableExtensions({true, true, true})`: extended monitor,
  LFO cross modulation (patch format 2) and instrument modes. IDENTIFY capabilities
  `0x7A`, capabilities 2 `0x01`, as Physical Modeling MOD.
* Patches are not stored: after a power cycle the matrix is empty (no flash writes
  until the human approves, module `STORAGE.md`, D8).

## MIDI over J7

| Message | Regular build | MOD build |
| --- | --- | --- |
| CC20–27, CC102–113 (every channel) | looper knobs and keys | unchanged; they never reach the matrix |
| Bootloader request SysEx | reply, then reset into the bootloader | unchanged; checked before the matrix |
| CC1 (mod wheel) | ignored | consumed by the matrix (source ModWheel) |
| Channel and polyphonic aftertouch | ignored | consumed (sources Channel AT, Poly AT) |
| Mod on Daisy SysEx (`F0 7D 4D 44 …`, PROTOCOL.md) | ignored | consumed and answered |
| Notes, CC120/123, MIDI clock, start | ignored | observed by the matrix (Poly AT last note, all notes off, LFO sync); the looper still ignores them |
| Other controllers, pitch bend | ignored | ignored |

The matrix's replies and monitor frames leave from the main loop
(`controller.Service()`, at most one message per millisecond). A card save or import
also runs in the main loop and can delay them by its chunk time (NICHT GEMESSEN).

## Changed sources

| File | Change |
| --- | --- |
| `src/modulation.h` | new: column table, `Modulate()` (base + offset, clamped), `ReportBases()`, loop-mode names |
| `src/main.cpp` | `LOOPER_MOD` only: module, `InitModulation()`, the tick and the column values in `Controls()`, the Mix gains in the output stage, `ForwardToModulation()` in `ServiceUsbMidi()`, `Service()` in the main loop, the marker `LOOPER-MOD-VERSION`; the `#else` branches are the v0.4.0 code |
| `Makefile` | `MOD=1`: target `LooperMod-v…`, `build/mod/`, defines and include paths; `make test` always runs both builds |
| `tests/fakes/desktop_devkit.h` | CV and gate inputs for the Mod on Daisy adapter |

Real time: the module is one static object (no allocation); the audio callback only
ticks the engine and reads its offsets, and the main loop handles the SysEx and the
replies (single producer, single consumer, as in the other integrations).

## Tests (host)

| Test | Checks |
| --- | --- |
| `tests/test_columns.cpp` | the table (names, span texts, global, all 64 cells, four modes); Speed mapping and Mix gains; with an empty matrix every value equals its base bit for bit for all 61 semitones and six pot positions; base + depth × span with clamping at both ends; unrounded speed bends; reported bases |
| `tests/test_modulation.cpp` | production `main.cpp` in the MOD build: IDENTIFY, all eight `GET_TARGET` replies, `GET_ALLOWED`, `GET_MODE` over SysEx; mod wheel on Speed with pot and CC20 as base and the clamp at +36; LFO 1 on Speed ±6 semitones unrounded; CV 1 on Start, mod wheel on Size and Decay with CC27 as base; Slice and Inertia; Mix dry only / loop only / unity; the looper CCs with an active matrix; loop mode and origin in the monitor; CC1, aftertouch, clock and notes leave the looper unchanged; the bootloader SysEx is answered and not seen by the matrix; clearing the matrix returns every control exactly to its base |
| `tests/test_callback.cpp` | the whole production-callback test also in the MOD build (empty matrix) |
| `tests/test_golden.cpp` | the golden performance above, regular build against MOD build |
| `modules/mod-on-daisy`: `mod_probe.py --sim … --instrument looper targets` | `tools/mod_sim.cpp` plays the looper's column table; `mod_probe.py` reads it with its own decoder (no hardware) |

## Resources (measured 2026-10-26, local ARM build, GCC 13.2.1)

| | Default | MOD=1 |
| --- | ---: | ---: |
| BIN | 166,072 B | 200,024 B |
| DTCM static | 115,976 B (88.5 %) | 118,560 B (90.5 %, reserve 12,512 B) |

CPU load of the matrix on the board: NICHT GEMESSEN. `maximumCycles` and `overruns`
in `src/main.cpp` (LED D16) show the callback peak with and without connections.

## Hardware test

Checklist: [HARDWARE_TEST.md § 9](HARDWARE_TEST.md#9-mod-on-daisy-build-loopermod-image).
