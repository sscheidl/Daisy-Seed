# Changelog Filter FX

## 0.1.0 (2026-11-06)

First core and first firmware `filter-fx`. Hardware NOT MEASURED. The control mapping is a
proposal until the owner confirms it.

* Stereo effect under FX contract 1.0: Drive, two [filter core](../../modules/filter-core/)
  instances, LFO, envelope follower, wet ceiling. Dry/wet and on/off fade belong to the host.
* Parameter contract `contracts/parameters.json`, component `filter-fx`, with a page in the
  browser panel, JUCE app and VST3, and in the SQ simulator.
* Golden hash `0xBDFBAE6A` (all modes, drive 40 %, resonance 70 %, LFO and envelope).
