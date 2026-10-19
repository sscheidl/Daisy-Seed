# Changelog

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
