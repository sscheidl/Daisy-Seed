# Changelog SC Reverb

## 0.1.1 (2026-11-12)

PR #108 review finding R13. Hardware NOT MEASURED. Host tests and the firmware build were NOT RUN locally (the pinned
DaisySP-LGPL checkout was absent); only a syntax check against a stand-in `ReverbSc` declaration was run.

* **Core:** `Process` returns `bool`: false when it rejects a block (not prepared, or an invalid block) without touching any
  buffer. Accepted blocks are processed exactly as before.
* **Firmware `sc-reverb-fx`:** built with WetHost 0.2.0, which returns the exact dry input for a rejected block instead of
  mixing unwritten wet buffers. The normal 48-frame path is unchanged; the image changes, so the firmware is 0.1.1.

## 0.1.0 (2026-11-06)

Extracted from the Audrey 3 port as a component of its own. Hardware NOT MEASURED.

* **Core:** ReverbSc from DaisySP-LGPL under FX contract 1.0, wet-only, memory from the host. Output bit-identical to ReverbSc
  called directly (host test, MEASURED 2026-11-06).
* **Firmware `sc-reverb-fx`:** stereo input, pots for decay, damping and dry/wet, SW17 feed, bootloader.
