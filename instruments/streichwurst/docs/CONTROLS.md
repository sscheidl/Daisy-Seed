# Control design: Streichwurst

2026-11-01. Own adaptation for the Seed3 Desktop Development Kit:
eight pots, 16 buttons/LEDs according to the [repository hardware baseline](../../../docs/HARDWARE.md).
**Implemented in firmware 0.1.0** ([src/controls.h](../src/controls.h), host tests in
`tests/test_product.cpp`); on the device **NOT MEASURED**. No display assumed.
No claim of identical Waldorf labelling or parameter ranges.

The mapping belongs to the firmware/control adapter according to the
[module contract](MODULAR_ARCHITECTURE.md). The string and solo controls operate the
instrument; Animate/Ensemble/Phaser/Reverb operate the separate FX component.
Kit, MIDI and the Mod on Daisy control app use the same parameter register.
The app offers all parameters independently of the selected pot page.

## Pot pages

| Pot | Main page | Solo page | FX page |
| --- | --- | --- | --- |
| VR1 | String Registration | Solo Tone | Animate Amount |
| VR2 | String Crescendo | Solo Attack | own Animate Rate |
| VR3 | String Release | Solo Release | Phaser Amount |
| VR4 | Balance String/Solo | Solo Tremolo Depth | own Phaser Rate |
| VR5 | Animate Amount | Solo Tremolo Rate | Reverb Wet |
| VR6 | Phaser Amount | Solo Sustain Level | own Reverb Decay |
| VR7 | Reverb Wet | Split point | Master Level |
| VR8 | Master Level | Master Level | Balance String/Solo |

Documented original relationships determine the internal scaling where known.
The additional separate rate/wet/decay controls are own parameters, not
claimed original Waldorf knobs. Define default values, ranges and MIDI CCs in a
versioned parameter table before implementation.

## Buttons and LEDs

* S1..S8: local play keys, initially an own C major octave from MIDI 60;
  an external MIDI keyboard remains intended for the full note range.
* S9: cycles the pot page Main/Solo/FX; S9..S11 show the active page.
* S10: cycles the string octave setting Base/Both/Upper.
* S11: cycles ensemble mode 0/1/2. Native values instead of unverified product labels.
* S12: ensemble on/off.
* S13: solo sustain mode; document the exact own state semantics.
* S14: cycles Layer/Split; note-exact boundary in an own parameter table.
* S15: cycles four own RAM presets; do not import manufacturer presets.
* S16: Panic/AllSoundOff.

S9+S16 for 5 s remains reserved for the shared bootloader entry.
Delay the single actions of these two buttons until the chord has been evaluated,
or suppress them when a chord begins. Test the hold, release and abort paths;
no unintended page change/panic after the bootloader chord.

LED feedback for page and selection must be distinguishable, e.g. a
short, time-limited selection blink sequence. LED I/O in the main loop,
at most at the existing limited service rate. No LED timing in the audio.

On a page change, parameters do not jump to the positions of other pots:
soft takeover until the stored value is crossed, with hysteresis;
display/latch per parameter. Take MIDI changes into account. Document smoothing and
MIDI/pot priority. This design may be improved, with reasons, if real
control conflicts arise.

## Implementation in 0.1.0 (own decisions)

* Parameters per page and button as above; ranges, default values and CCs in
  [PARAMETERS.md](PARAMETERS.md). Integer/enum parameters on a pot use zones of equal width.
* **Soft takeover:** after start-up, a page change, a RAM preset or a change via MIDI
  or the app, a pot is locked until it crosses the stored value or comes within
  2 % of it. After that it only writes from 0.4 % movement onwards (noise threshold). The last
  change wins; the change appears in the SQ monitor and therefore in the app.
* **S9/S16:** act on release and only if the respective other button was not pressed
  in the meantime. A started S9+S16 chord suppresses both single actions, also
  after the 5 s bootloader entry or an abort by a third button.
* **S13 Solo sustain mode:** 0 "Decay to level" (decay 80 ms to VR6 Solo),
  1 "Hold" (stays at full level), 2 "Percussive" (decays even while held,
  decay time 0.05–3.2 s via VR6 Solo).
* **S14:** Layer (all keys to strings and solos) / Split (strings below the split point,
  solos from the split point upwards). Assignment and transposition apply per key until release.
* **S15:** four own RAM presets (0 contract default, 1 dark strings, 2 bright strings
  with both octaves and phaser, 3 split with solo); MIDI channel, master and balance are kept.
  No flash/QSPI writes.
* **LEDs:** S1–S8 pressed play keys; S9–S11 active page (short blink after the
  change); S12 ensemble on; S13 off/dim/bright per sustain mode; S14 split;
  S15 blinks preset number + 1 times; S16 lights during the panic ramp. LED I²C only in the
  main loop, at most every 10 ms.
* Smoothing: parameters act through the stages of the DSP (20 ms amounts, 25.4 ms balance,
  51 ms master, 5 ms pitch); pots are not filtered additionally.
