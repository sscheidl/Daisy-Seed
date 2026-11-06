# Audrey 3

The [Synthux Academy Audrey II](https://github.com/Synthux-Academy/Audrey-II) feedback synth on the Seed3 Desktop Dev Kit:
a Karplus string, overdrive, filters and reverb in one feedback loop, with an echo after it. This is a faithful port with two
additions: a second pot page and MIDI.

**Status 0.1.0:** builds, and host tests pass. **Nothing was run on the kit: sound, CPU load and controls are NOT MEASURED.**

## Controls

| Control | SW17 off | SW17 on |
| --- | --- | --- |
| VR1 | Frequency | same |
| VR2 | Feedback Gain | same |
| VR3 | Feedback Body | same |
| VR4 | Feedback LPF | same |
| VR5 | Feedback HPF | same |
| VR6 | Reverb Mix | Reverb Decay |
| VR7 | Echo Send | Echo Time |
| VR8 | Output Volume | Echo Feedback |

After SW17 moves, VR6–VR8 do nothing until they reach the stored value of their new parameter, so nothing jumps.
Pots write only when they move, so a MIDI CC stays until then.

| Control | Function |
| --- | --- |
| SW18 up | **Drone:** always sounding, as the Audrey II. Notes only set the pitch |
| SW18 middle | **Play:** notes open the output, pitch glides |
| SW18 down | **Play, no glide:** the same, pitch jumps |
| S1 | Echo warp on/off (the Audrey II delay switch), shown on D1 |
| S9 + S16 | Bootloader (shared entry, `common/bootloader`) |
| D2 | Output open |
| D16 | At least one audio callback overran its time budget |

The sound comes from the loop itself, so raise Feedback Gain until it sings. In Play mode the loop keeps running; a note opens
the output in 5 ms and closes it in 150 ms, before the echo send, so echoes ring on. Velocity is ignored.

## MIDI

Omni, over USB and DIN. Notes use last-note priority across the full MIDI range, which reaches beyond VR1. All Notes Off and
All Sound Off close the output.

| CC | Parameter | CC | Parameter |
| --- | --- | --- | --- |
| 20 | Frequency | 26 | Reverb Decay |
| 21 | Feedback Gain | 27 | Echo Send |
| 22 | Feedback Body | 28 | Echo Time |
| 23 | Feedback LPF | 29 | Echo Feedback |
| 24 | Feedback HPF | 30 | Output Volume |
| 25 | Reverb Mix | 31 | Echo warp (64 and above: on) |

## Modules

| Part | Path | Contract |
| --- | --- | --- |
| Feedback engine | `third_party/audrey-ii/` | `Engine<Stages>`: `Reverb`, `Echo` and `Damper` are template parameters; the host gives the reverb and echoes their memory |
| Parameters and controls | `include/audrey/params.h`, `control.h` | One table for pots and CCs; no hardware access |
| Product composition | `include/audrey/composition.h`, `src/main.cpp` | Selects the stages, owns memory, audio, MIDI and LEDs |

To replace the reverb, change `Stages::Reverb` in `composition.h`; `tests/test_audrey.cpp` does this with a pass-through.
The reverb sits inside the feedback loop, so it does not use the FX interface of `common/audio/fx_contract.h`.

Not included yet: a control panel page, the Mod on Daisy matrix, the CV and gate inputs, and `effects/` components for the
echo and reverb.

## Memory

The engine is about 158 KiB, too much for the 128 KB DTCM, so it lives in AXI SRAM (`src/memory.ld`). The reverb and both echoes
are in SDRAM, as on the Audrey II. The engine and everything in SDRAM are constructed after `hw.Init()`.

## Differences from the Audrey II

* Kit controls instead of raw ADC reads: pots run at 1 kHz through the shared filter, not at 12 kHz.
* Pots are read as they are; the Audrey II reads `1 - adc`. Check the direction on the kit.
* A parameter is applied only when its value changes. The result is the same and saves filter recalculation.
* Different codec: the input's noise floor may change how the loop starts.

## Build and test

```bash
git -C DaisySP submodule update --init DaisySP-LGPL
make PROJECT=audrey-3 -j4
make PROJECT=audrey-3 test
```

**Licence.** Our code is MIT and the port is MIT (`third_party/audrey-ii/LICENSE`). A build also links ReverbSc and Tone from
DaisySP-LGPL, under LGPL-2.1: see [third_party/README.md](third_party/README.md) before passing a binary on.
