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

Omni, over USB and DIN. Notes use last-note priority across the full MIDI range, which reaches beyond VR1. All Notes Off
releases all notes, so it leaves a drone sounding. All Sound Off also closes a drone, until the next note or a move of SW18.

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
| Feedback engine | `third_party/audrey-ii/` | `Engine<Stages>`: `Reverb`, `Echo` and `Damper` are template parameters; the engine routes and mixes them |
| Reverb | [`effects/sc-reverb/`](../../effects/sc-reverb/), component `sc-reverb` 0.1.0 | FX contract 1.0, own firmware `sc-reverb-fx` |
| Echo | [`effects/echo-delay/`](../../effects/echo-delay/), component `echo-delay` 0.1.0 | FX contract 1.0, own firmware `echo-delay-fx` |
| Shared filters | [`common/dsp/audrey-ii/`](../../common/dsp/audrey-ii/), component `audrey-dsp` 0.1.0 | Used by the engine and the echo |
| Parameters and controls | `include/audrey/params.h`, `control.h` | One table for pots and CCs; no hardware access |
| Product composition | `include/audrey/composition.h`, `src/main.cpp` | Selects the stages, maps parameters, owns memory, audio, MIDI and LEDs |

**Adapter.** The reverb sits inside the feedback loop, so the engine cannot work in blocks. `FrameStage` in `composition.h`
hands the FX contract a block of one frame, so any effect under FX contract 1.0 fits unchanged. The echo uses the same path.
The feedback overdrive stays in the engine: it is part of the voice, not an effect of its own.

To replace a stage, change `Stages` in `composition.h`. `tests/test_audrey.cpp` does this with a pass-through, and with the
echo in the reverb slot. The sink maps Reverb Decay to the stage's `feedback` parameter, so a stage without one needs a line
there too.

Effects are accepted on their own first: their hardware tests are in their folders, and all are NOT MEASURED yet. The CPU load
and sound of Audrey 3 are a separate acceptance.

Not included yet: a control panel page, the Mod on Daisy matrix, and the CV and gate inputs.

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
