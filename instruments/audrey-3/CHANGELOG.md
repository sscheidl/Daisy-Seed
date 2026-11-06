# Changelog Audrey 3

## 0.1.0 (2026-11-06)

First port of the Audrey II to the Seed3 Desktop Dev Kit. Hardware NOT MEASURED.

* **Engine:** the Audrey II feedback engine at block size 4, 48 kHz, with its output limiter. A 60 s host render with moving
  controls is bit-identical to the upstream code built with its own DaisySP version (MEASURED 2026-11-06, host only, same
  compiler for both).
* **DC blocker:** keeps the 0.99 of the DaisySP version Audrey II was built with; the current DaisySP value would change the
  string loop.
* **Controls:** eight pots with a second page on SW17 and pick-up, play modes on SW18, echo warp on S1.
* **MIDI:** notes with last-note priority, CC 20–31 for all eleven parameters and the warp, over USB and DIN.
* **Note gate:** new in the engine; at 1 the output is exactly unchanged.
* **Licence:** links ReverbSc and Tone from DaisySP-LGPL (LGPL-2.1).
