# Hardware test — Looper v0.3 — PENDING — USER TEST

No hardware run or flash has been done by the agent. Work through the sections in
order: later checks only make sense once the earlier ones pass. Tick each box and
write down what you hear or see; a failed box with a note is more useful than a
skipped one.

## 0. Prepare

- [ ] Build or download `Looper-v0.3.0.bin` and note its SHA-256. The PR CI
      artifact and a local build of the same commit must match.
- [ ] Flash it through the external Daisy bootloader as in
      [docs/FLASHING.md](../../../docs/FLASHING.md) (`PROJECT=looper`). Do not
      reinstall a working bootloader. **Never flash the Stardust binary.**
- [ ] Use a line-level source (synth, phone, or an audio interface) into the
      **left** input, headphones or J3/J4 to monitor. Start at low volume.
- [ ] Starting positions: pot 0 (Varispeed) at about **40 %** (unity), pot 1
      (Inertia) left, pot 2 (Slope) centre, pot 3 (Start) left, **pot 4 (Size)
      fully right**, pot 5 (Slice) and pot 6 (Skip) left, pot 7 (Decay) at about
      30 %.

Record: board revision, binary SHA-256, source, input level, monitoring.

## 1. Boot, memory and audio path

- [ ] Power on standalone (no computer). Time until the input is audible at the
      output: the bootloader waits about 2 s, then both 87 s buffers (66.8 MB
      SDRAM) are zeroed. Expected: the bootloader wait plus well under 1 s. Note
      the time.
- [ ] Input is audible at the output (dry monitoring), no hum, no distortion at
      normal levels. Loud peaks are limited softly, not clipped hard.
- [ ] Plug a second cable into the right input: the right channel follows it.
      Unplug it: the left input feeds both channels again.
- [ ] LED D16 stays off for the whole session. If it ever lights, the audio
      callback missed its deadline at least once: note what you were doing.

## 2. Keys, pots and LEDs

- [ ] Each key S1–S12 does what the README table says, and the LED
      above it matches. Note any key or LED that is swapped or mirrored.
- [ ] Each pot 0–7 controls the listed parameter; turning right increases it.
      Note the physical position of each pot number on the board.
- [ ] Holding a key with a slightly unsteady finger does not trigger it twice.

## 3. Record, play, undo

- [ ] S1 starts the first take (LED S1 on), S1 again stops it. The loop starts
      playing at once, without a click at the start.
- [ ] Listen to the loop seam for several passes with a sustained sound (pad or
      drone): window mode (S7 LED off) dips briefly to silence at the seam;
      crossfade mode (S7 LED on) passes the seam without a dip. Neither clicks.
- [ ] S2 pauses with a short tape-style stop and resumes.
- [ ] Overdub (S1 on/off) a second layer: punch in and out without clicks.
- [ ] S3 undoes the layer instantly (10 ms fade); S3 again redoes it. Repeat
      after a 2nd and a 3rd overdub: undo always returns exactly the previous
      layer, never an older one.
- [ ] S5 erases (silence); S3 brings the loop back.
- [ ] Record for longer than 87 s: recording stops on its own at 87 s and the
      loop plays. Overdub, undo and redo still work on this full-length loop.

## 4. Speed, inertia, reverse

- [ ] Pot 0: whole-semitone steps from two octaves down to three octaves up;
      unity at about 40 %. Small changes glide; big jumps are immediate.
- [ ] Pot 1 right: pause and play now slow down and speed up like a tape over
      about 2 s. Pot 2 fully right lags only speeding up, fully left only
      slowing down.
- [ ] S4 reverses with a short fade, no click; S4 again plays forwards.
- [ ] Overdub at a high speed (pot 0 fully right) for a few seconds: no
      dropouts, D16 stays off. Repeat with Slice and Skip turned up (section 6).

## 5. Loop modes

- [ ] S10 Replace: an overdub replaces the old material, with soft punch fades.
- [ ] S11 Frippertronics: overdubbing with silent input lets the loop decay on
      each pass. Pot 7 left keeps the loop almost unchanged, fully right clears it
      in one pass. Playing into it builds layers that never distort hard.
- [ ] In Frippertronics the S3 LED is off and S3 does nothing; after switching
      to S9 (Sound on Sound) undo works again.
- [ ] S12 Resample: S1 on/off captures what you hear (try it with pot 0 changed
      or with Slice/Skip active) as a new loop; the switch has a short fade. S3
      returns the original loop.

## 6. Start/Size and Slice/Skip

- [ ] Pot 4 left plays a very short window (24 ms); pot 3 moves it through the
      loop. Moving the pots does not click.
- [ ] Pot 5 (Slice) up: short random stutters (repeats) appear; at the start of
      each fifth of the pot range there are none.
- [ ] Pot 6 (Skip) through its range: random jumps, then reversed slices, small
      pitch shifts, octaves, larger semitone jumps, and jumps with glides.
- [ ] All of these stay free of clicks in both seam modes. Note any click with
      the pot positions.

## 7. microSD

Use a FAT32 card. Put a few WAV files (48 kHz; 16/24-bit or float; mono or
stereo; one longer than 87 s; one at 44.1 kHz) into a folder `looper/loops/`.

- [ ] Without a card the S8 LED is off and everything else works.
- [ ] With a card the S8 LED is dim after boot.
- [ ] Record a loop and wait: about 2 s after the last change the S8 LED blinks
      while `loop.wav` is written. Note how long a short loop and a full 87 s
      loop take. Audio must not drop out and D16 stays off.
- [ ] Overdub while it is saving: the save stops, and starts again after the
      overdub.
- [ ] Power-cycle: the last saved loop plays again after boot. Undo is not
      available right after boot.
- [ ] Erase, wait 3 s, power-cycle: the looper starts empty.
- [ ] S8 loads the files of `looper/loops/` in name order, one per press; the
      loop switches with a short fade. S3 returns the previous loop. The 44.1 kHz
      file and the over-long file make the S8 LED blink fast and change nothing.
- [ ] Pull the card while it is saving: no crash, LED off; insert it again: the
      next change is saved.
- [ ] Check `looper/loop.wav` on a computer: it plays as a 32-bit float stereo
      WAV at 48 kHz.

## 8. Report

Write down for each box: pass, fail, or not tested, plus notes. Especially:
boot time, any D16 event, control orientation, clicks with the settings that
caused them, and anything that sounds different from what the README describes.
Software tests do not replace this listening test; CPU load on the board is
still **not measured**. `maximumCycles` and `overruns` in `src/main.cpp` hold
the measured peak and overrun count and can be read with a debugger (ST-Link)
if D16 lights.
