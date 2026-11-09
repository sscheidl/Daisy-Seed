# Changelog — Cumulonimbus

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
