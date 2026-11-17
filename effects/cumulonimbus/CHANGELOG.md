# Changelog — Cumulonimbus

## 0.1.1 — 2026-11-11 (component, standalone firmware; meter 0.2.0) — Codex review of PR #108

* **R3** The grain cap bounds the total number of active grains: after lowering the cap no grain starts
  (not even a trigger into a free low slot) until enough running grains have finished; quality and gain
  normalization count every audible grain. A refused trigger or random birth counts once, an overdue
  regular grain once per overdue period.
* **R4** Unfreeze crossfades over the samples actually captured: a Freeze shorter than 256 samples no
  longer reads zeros or samples from an earlier Freeze; a new Freeze during a fade captures afresh.
  Full-length Freezes keep Clouds' gains.
* **R8** Unsupported blocks (32 kHz build: not a multiple of 3 frames) and an unprepared effect are
  rejected without touching any buffer, as FX contract 1.0 requires.
* **R2** Diagnostics: the snapshot request goes from the main loop straight to the meter, the reset
  through its own flag, and the audio thread resets in the same block, so neither request can swallow
  or overwrite the other.
* **R7** Meter 0.2.0: each snapshot carries its own window (audio-thread ms) and the product metadata
  of the same block. New metrics (set 2, 13 records): control-queue drops, stale rate reports, the
  meter's own accounting cost and the untouched DTCM stack (painted at boot). The probe fails a point on
  a block-count mismatch (invalid window, also ends `--stop-on-fail`) and on any MIDI or control drop.
* **R6** Run sheet: B0 timing on the D48/D32 dry route (P0 has no callback meter), sound comparison
  with P0 at the same logical settings; long-tail Bypass case added.
* Hardware: NOT MEASURED.

## 0.1.0 — 2026-11-09 (component, chain, meter, standalone firmware)

* First version: the Clouds granular path (eurorack f4cfa76) as a stereo wet-only effect under FX
  contract 1.0, cross-checked against Nimbus and Cumuloid ([UPSTREAM.md](UPSTREAM.md)).
* Build-time internal rate: 48 kHz grain kernel (2 × 24-frame calls) or 32 kHz kernel behind a fixed
  48↔32 kHz polyphase converter; grain durations and smoother times in rate-correct units.
* Shared renderer (`grain.h`) separate from the live-ring scheduler (`live_scheduler.h`).
* Host chain with Torus reverb, Freeze/Run/Bypass policy, stage isolation and output guard.
* Profiling meter: per-stage DWT maxima/means, 1 % histogram, missed-period detection, channel-16
  telemetry records (protocol id 3) and `tools/cumulonimbus_probe.py`.
* Standalone diagnostic firmware F48/F32 with a deterministic stereo test source.
* Hardware: NOT MEASURED.
