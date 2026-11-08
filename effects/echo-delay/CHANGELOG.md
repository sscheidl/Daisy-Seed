# Changelog Echo Delay

## 0.1.0 (2026-11-06)

Extracted from the Audrey 3 port as a component of its own. Hardware NOT MEASURED.

* **Core:** two upstream `EchoDelay` lines under FX contract 1.0, wet-only, memory from the host. Output bit-identical to the
  upstream lines (host test, MEASURED 2026-11-06).
* **Firmware `echo-delay-fx`:** stereo input, pots for time, feedback, dry/wet and lag, SW17 feed, bootloader.
