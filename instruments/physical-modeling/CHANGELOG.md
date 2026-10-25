# Changelog

## Unreleased

* **Firmware updates without BOOT/RESET** ([common/bootloader](../../common/bootloader/README.md)):
  holding S13+S14 alone for 3 s, or the USB-MIDI SysEx `F0 7D 42 4C 01 01 42 4F 4F 54 F7`
  (answered with `F0 7D 42 4C 01 41 F7`), lights all LEDs and restarts into the Daisy
  bootloader with `DAISY_INFINITE_TIMEOUT`. The audio callback only runs the key timer.
* In **both** builds, so the regular image is no longer byte-identical to v0.1.0; the
  Mod on Daisy code still adds nothing to it. Sound unchanged: golden hashes of both
  builds are the same before and after (compared on one compiler).

## Mod on Daisy build, mod3 - 2026-10-24 (correction order after the review)

* Built with `MOD_ON_DAISY_EXTENSIONS=1`; `InitModulation()` enables the extended
  monitor (per-source range over the monitor window, clock activity, last aftertouch
  kind) and the LFO cross modulation (`fm`, rate of one LFO follows the other, +-2
  octaves, free running only; patch format 2). IDENTIFY version `0.1.0-mod3`,
  capabilities 0x7A.
* Unchanged: the regular image (byte-identical) and the sound of the MOD build with an
  empty matrix (golden hash 37DE0BF9D93B81CE).

## Mod on Daisy build, mod2 - 2026-10-23 (after the first hardware test)

* Column 6 is now **Level** (output gain 0..2, unity with an empty matrix) instead of
  the reverb decay, which had little audible effect. Pot 6 still sets the decay.
* The monitor reports each column's pot/CC position as base value; the editors show
  where a modulation lands. IDENTIFY version `0.1.0-mod2`.
* Unchanged: the regular image (byte-identical) and the sound of the MOD build with an
  empty matrix (golden hash 37DE0BF9D93B81CE).

## Separate Mod on Daisy build - 2026-10-23

* New, separate image `PhysicalModelingMod-v0.1.0.bin` (`make PROJECT=physical-modeling-mod`,
  `MOD=1`) with the Mod on Daisy 8x8 matrix: global offsets on parameters 0-7 and
  Poly AT per Torus voice (Pitch, Structure, Brightness, Damping, Position).
  See [docs/MODULATION.md](docs/MODULATION.md).
* The regular build is unchanged: byte-identical image, version stays 0.1.0.
  With an empty matrix the MOD build is bit-identical in sound (golden test).

## 0.1.0

Initial four-voice Torus keyboard instrument for Seed3 Desktop; all six models,
16 keys/LEDs, eight fixed pots, debounced model switches, global Torus stereo
reverb, velocity and USB MIDI CC20–33/pitch bend, protected .8 FS output,
host/sanitizer/ARM validation and CI artifact integration.

### Review corrections in PR #13

Preserve global reverb tails/polarity while fading only dry/send on model changes.
Keep intentional model-6 reverb parameter independence; preserve its dry stereo
and special string behavior. Add official CpuLoadMeter instrumentation, requested
MIDI diagnostic snapshots, deadline counters and an optional host CSV/stress tool.
Promote the independent review tests into regular host/sanitizer runs, including
spectral polyphony, preservation/steal, sounding bend, tail waveform continuity,
level stress and synthetic pot-noise diagnostics. Add CC28/switch ownership and
telemetry/timer/concurrency tests. Restore original chord-selection hysteresis and
the .625 actual diffusion default; document model-6 mono cancellation.
