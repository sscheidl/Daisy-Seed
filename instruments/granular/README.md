# Granular — Instrument Project

**Status: project outline only; no firmware build or hardware test.**
Project ID: `granular`. The goal is an original, bounded granular engine for the
Seed3 Desktop Dev Kit, with GR-1 as an important reference for musical control.

## References and comparison

* **GR-1:** Existing hardware and separate research reports on grain scheduling,
  sample position, and controls. These findings are references, not an engine.
* **Iridium Particle:** A second research reference for granular organization
  and parameter roles. No manufacturer functions or coefficients will be copied.
* **DaisySP GranularPlayer:** Available for controlled experiments with
  independent speed and pitch; not a complete GR-1 model.
* **Clouds → Nimbus:** Public original and existing Daisy Patch/Field ports.
  Their audio input and freeze workflow is especially relevant to the later
  effect variant.
* **Beads:** A possible interpretation of the earlier name “Beats.” Its source
  is public, but the author notes missing validation on the module. No Daisy
  port is assumed.

Pinned source links and limitations are in the
[reference survey](../../docs/REFERENCES.md).

## Next milestone

1. Create a small synthetic sample buffer and one testable grain reader with
   its own window, position, and pitch.
2. Add a scheduler with a fixed, preallocated pool and defined overload
   behavior. Set pool size and maximum density only after measurement.
3. Track base position, effective scan position, and individual grain read
   positions separately. A later waveform viewport must not replace musical
   sample position.
4. Test finite output, buffer bounds, sample changes, and CPU/memory limits;
   then prepare a manual hardware test.

File access and allocation do not belong in the audio callback. The Cortex-M7
has no NEON; performance figures from reference devices do not apply. Start
with generated test data, then add sample management, visualization, and higher
polyphony in separate steps. Tulip integration has not been implemented here.

The later [granular effect](../../effects/granular/) should reuse the same
proven DSP core with live input and freeze. Until implementation,
`make PROJECT=granular` fails explicitly.
