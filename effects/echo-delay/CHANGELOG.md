# Changelog Echo Delay

## 0.1.1 (2026-11-12)

PR #108 review finding R13. Hardware NOT MEASURED.

* **Core:** `Process` returns `bool`: false when it rejects a block (not prepared, or an invalid block such as one longer than
  the prepared `max_block`) without touching any buffer. Accepted blocks are processed exactly as before.
* **Firmware `echo-delay-fx`:** built with WetHost 0.2.0, which returns the exact dry input for a rejected block instead of
  mixing unwritten wet buffers. The normal 48-frame path is unchanged; the image changes, so the firmware is 0.1.1.

## 0.1.0 (2026-11-06)

Extracted from the Audrey 3 port as a component of its own. Hardware NOT MEASURED.

* **Core:** two upstream `EchoDelay` lines under FX contract 1.0, wet-only, memory from the host. Output bit-identical to the
  upstream lines (host test, MEASURED 2026-11-06).
* **Firmware `echo-delay-fx`:** stereo input, pots for time, feedback, dry/wet and lag, SW17 feed, bootloader.
