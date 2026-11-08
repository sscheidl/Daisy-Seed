# Changelog Grain Cloud

## 0.1.0 (2026-11-06)

First offline host core of the new independent granular engine. Host tests only. Hardware NOT MEASURED.

* **Block order:** control tick before rendering in one context, 1 kHz by default, configurable to 250 Hz.
* **Voices:** eight notes over sixteen slots, generations, four note-start modes, sustain, panic.
* **Grains:** global pool up to 256, sample-accurate births, direction as a probability, triangular
  spray, stealing by ending grains sooner, a fair share with a reserve.
* **Window:** one shared table with a slow control-rate morph; grains hold only phase and step.
* **Envelope:** linear attack, exponential decay and release at control rate, plus a fast release.
* **Level:** gain follows the smoothed grain count; ramps for all running values; one clip point.
* **Modulation:** Mod on Daisy as the single matrix; one-sided spray, size and density.
* **Effects:** none in the core; one stereo point after the voice sum for FX contract 1.0.
