# Hardware results — 2026-10-22

**FUNCTIONAL HARDWARE ACCEPTANCE REPORTED — FOUR-VOICE CPU MEASURED.**

The user successfully flashed the instrument and confirmed its behavior by
playing/listening with S1 (Studio One). With the user's explicit authorization,
the agent then collected real callback telemetry over USB MIDI. The agent did
not flash the board.

## User-reported functional checks

The user reports all functional tests green:

- The global reverb tail continues decaying across model changes.
- Model changes are free of audible clicks.
- Four-note polyphony and the voices' stereo distribution work as expected.
- USB MIDI and pitch bend work.
- All knobs have a function and behave as expected from the user's recollection
  of playing Rings in a modular system.
- No overload was perceptible during normal playing.

This confirms the Rings/Torus keyboard adaptation in practical use. The listening
report is not a controlled comparison against a reference Rings module.

## Actual CPU measurements

The completed six-model run recorded **180 snapshots and 174084 callback blocks,
with zero callbacks exceeding the 1 ms deadline in the captured intervals**.

| Model | Average (%) | Peak (%) | Deadline overruns | Captured blocks |
| --- | ---: | ---: | ---: | ---: |
| Modal Resonator | 33.07 | 34.65 | 0 | 29010 |
| Sympathetic Strings | 42.69 | 55.78 | 0 | 29006 |
| Inharmonic Strings | 39.23 | 48.12 | 0 | 29054 |
| FM Voice | 36.24 | 38.66 | 0 | 29004 |
| Western Chords | 42.38 | 56.71 | 0 | 29006 |
| String & Reverb | 39.65 | 49.16 | 0 | 29004 |

[Unmodified raw CSV](measurements/2026-10-22-four-voices.csv), SHA-256:

```text
1edc3b19d767af451a18779e47bcd4810e1e0a6c54137c724edbe01c3d03cf58
```

The table's average is the arithmetic mean of the firmware's smoothed average
readings with `blocks >= 10000` in each phase, rounded to two decimal places.
It is not a mean over every callback. Peak and overrun count are the largest
reported cumulative values since that phase's reset. Captured blocks are the
largest reported block count per phase; the total is their sum.

### Measurement conditions

- Actual Daisy Seed External MIDI input/output on the measurement PC, using
  `tools/cpu_probe.py --benchmark --seconds 30` and a session-local wrapper.
- 48 kHz, 48 samples per callback: 100% corresponds to the 1 ms callback budget.
  Four voice slots are processed in every model.
- Four velocity-127 Note On messages approximately every 10 ms; pitches vary
  across MIDI notes 36–95. Host scheduling can reduce the actual strike rate.
- CC22/23/25/26/27/31/32 = 127: Brightness, synthesis Damping, Excitation,
  reverb Decay, Dry/Wet, Diffusion and reverb Input Gain.
- CC20/21/24/30 = 64: transpose, Structure, Position and Reverb Damping.
  CC20=64 produces approximately +0.189 semitones, rather than exact zero.
  Pitch bend was explicitly centered.
- The wrapper initialized CC33=0 and replaced every benchmark CC33 message with
  value 0. Final master output stayed muted. Voices and the global reverb still
  ran; the final output limiter's nonlinear knee was not exercised.
- The hardware controls were to remain stationary. Settings above are commanded
  MIDI targets, not independently sampled analog-knob positions.
- Each model settled for 50 ms before reset. Telemetry was requested once per
  second. Each phase ran for 30 seconds, with the last captured snapshot around
  29 seconds. The remaining interval before the next reset/end is not included
  in the reported counters.

The earlier audible run was interrupted after the user reported an unpleasant
feedback-like sound. It used repeated full-velocity strikes and maximum reverb
feedback/input; its partial data is excluded from this table. The completed run
remained muted, including after it ended.

### Interpretation and remaining checks

The worst captured peak was **56.71%**, leaving **43.29 percentage points of the
callback budget** at these settings. This is measured four-voice headroom.
Six voices are a plausible follow-up experiment; neither six nor eight voices,
nor a modulation matrix, have been implemented or performance-validated here.

This run does not establish the worst possible parameter combination. A separate
capture during manual playing, knob sweeps and model transitions is still open.
The settled-model reset deliberately excludes transition peaks. Muting also
excludes the final output limiter's nonlinear path from this run.

Telemetry measures the user callback interval, including controls, MIDI
consumption and DSP. USB serialization, snapshot publication and work outside
that interval are excluded; it is not whole-system CPU utilization. See
[architecture](ARCHITECTURE.md#cpu-measurement).

Board revision, codec identity and the flashed binary hash were not independently
verified during this session. Analog output levels, stack high-water, full IRQ
budget and stationary-pot noise have not been instrumentally characterized.
The functional report does not separately certify every detailed item in the
[hardware checklist](HARDWARE_TEST.md).

The software baseline is v0.1.0 from merged [PR #13](https://github.com/sscheidl/Daisy-Seed/pull/13).
The validated build's checksum remains recorded in [VALIDATION](VALIDATION.md);
the MIDI telemetry protocol does not attest the identity of the flashed image.
