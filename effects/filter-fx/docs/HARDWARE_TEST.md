# Hardware test Filter FX 0.1.0

**Status:** 6 November 2026 · Firmware `effects/filter-fx/build/FilterFx-v0.1.0.bin`
(`make PROJECT=filter-fx`). The owner runs this test on the kit. All results are
**NOT MEASURED** until an entry with a date is here.

Source: line signal into the stereo input, ideally a synth with a saw or square wave, then a
full mix or drum loop. Start quietly on headphones: resonance can be loud.

## 1. Listening test

| Check | Result |
| --- | --- |
| VR1 sweeps smoothly with no stepping or zipper noise, in every mode | NOT MEASURED |
| S1–S8 change the mode without a click, LEDs follow | NOT MEASURED |
| LP 6/12/18/24: steeper with each step, low end stays with SW18 up | NOT MEASURED |
| SW18 up, middle, down: bass falls off at high resonance | NOT MEASURED |
| Is the sound of the filter better than DaisySP's `LadderFilter` in `acid-303`? | NOT MEASURED |
| BP, HP, Notch and Phaser sound as named, Notch narrows with resonance | NOT MEASURED |
| VR2 at 100 %, no input: a sine appears, in every mode except Notch | NOT MEASURED |
| That sine keeps about the same loudness across VR1 and across modes | NOT MEASURED |
| That sine is in tune against a keyboard, up to about 8 kHz | NOT MEASURED |
| Below about 60 Hz it takes seconds to start from silence (known limit) | NOT MEASURED |
| VR3 Drive adds harmonics with little change of loudness | NOT MEASURED |
| Loud input on the resonance: no ugly clipping (wet ceiling) | NOT MEASURED |
| Aliasing at high resonance and high drive, judged by ear | NOT MEASURED |
| LFO shapes (S10–S13), rate and depth; S14 moves the sound across the stereo image | NOT MEASURED |
| VR7 up and down from the centre: filter follows the playing; VR8 sets the fall | NOT MEASURED |
| VR4 mix without steps, SW17 on/off without a click. Off = dry signal | NOT MEASURED |
| Mono cable in the left input only: both outputs | NOT MEASURED |

## 2. MIDI and panel

| Check | Result |
| --- | --- |
| CCs from the README over USB and DIN | NOT MEASURED |
| `sq_probe.py identify` reports `filter-fx`, `params filter-fx` reads the values | NOT MEASURED |
| The Filter FX page in the browser panel, JUCE app and VST3 reads and sets values | NOT MEASURED |

## 3. Resource pass

Stress case: Drive 100 %, resonance 100 %, Phaser, LFO 20 Hz at full depth, envelope at full
amount, fast pot movements, CCs over DIN, `sq_probe.py monitor`. Then `sq_probe.py stats`.
The estimate is 6 to 11 % of the callback plus controls, which is about 29 000 to 52 000
cycles per block. That estimate is not a result.

| Value | Limit | Result |
| --- | --- | --- |
| `callback_overruns` (LED D16) | 0 | NOT MEASURED |
| `callback_peak_cycles` | < 480 000 | NOT MEASURED |
| `queue_high_water` | < 32 | NOT MEASURED |

Please also note `callback_peak_cycles` with Drive at 0. The difference to a run without
the two ladders is the first real figure for the cost per voice of a later polysynth.
