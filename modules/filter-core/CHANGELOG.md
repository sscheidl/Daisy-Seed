# Changelog Filter core

## 0.1.0 (2026-11-06)

First version. Hardware NOT MEASURED.

* Zero-delay-feedback ladder: four trapezoidal one-poles, closed-form solve per sample.
* Clean core: one nonlinearity, the rational limiter on the first stage's state.
* Limiter level derived from the loop: self-oscillation within -0.64 to +0.75 dB of 0.5 from
  30 Hz to 19 kHz in LP6/12/18/24, BP, HP and Phaser (MEASURED 2026-11-06 on the host).
* Stage 1 and stage 2 modes: LP 6/12/18/24, BP, HP, Notch, Phaser. Notch does not
  self-oscillate, by design ([DESIGN.md](docs/DESIGN.md) 4).
* Input gain `1 + c k` with selectable c.
* Cutoff in note units through one compile-time table; per-sample ramp, audio-rate input and
  a cheaper path with held coefficients. No square root.
* Drive: saturation inside the loop as a gain from the previous sample, in two models, with
  resonance pushback. Drive 0 is the clean core, bit for bit.
* Age: seeded component tolerance, drift and jitter. Age 0 is the same filter, bit for bit.
* Golden hash `0x150EB967` (clean core).
