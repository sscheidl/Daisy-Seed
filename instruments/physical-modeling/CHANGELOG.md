# Changelog

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
