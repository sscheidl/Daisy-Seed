# Physical Modeling — Instrument Project

**Status: project outline only; no firmware build or hardware test.**
Project ID: `physical-modeling`. The goal is an original instrument for the
Seed3 Desktop Dev Kit with controlled excitation and a resonant body. Continuous
self-modulation is not yet a requirement.

## References and comparison

* **Rings → Torus:** The original Rings offers several resonator models; Torus
  is an existing Daisy Patch port. Its DSP and board controls must be considered
  separately: Patch controls, encoders, and audio connectors do not map directly
  to the Desktop Kit.
* **Elements:** A second original reference for separating excitation from the
  resonant body. A complete Elements port has not been selected.
* **DaisySP ModalVoice / Resonator:** Already in the pinned library. Source
  comments trace these components to **Plaits**; they are not a full Rings or
  Elements engine.
* **Iridium Resonator:** An additional research reference for resonator and
  control design. No manufacturer DSP is being incorporated here.

Pinned source links, licenses, and comparison limits are in the
[reference survey](../../docs/REFERENCES.md).

## Next milestone

1. Compare the originals, Daisy port, and DaisySP components using the same
   categories: excitation, resonance structure, damping, pitch, memory, and
   block processing.
2. Implement an original minimal monophonic voice with impulse/noise excitation
   and a bounded modal bank. Expose pitch, structure, brightness, and decay.
3. Test finite output, stability, decay, and parameter limits on the host; then
   prepare an ARM build and manual listening test.

Polyphony and modulation follow measured runtime and memory use on Seed3. No
voice count or sound match is promised. Exciting a resonant body from external
audio remains a variant of this project, without a separate resonator effect.

Until implementation, `make PROJECT=physical-modeling` fails explicitly. The
future implementation needs its own `src/`, `tests/`, `docs/`, and Makefile using
`../../common/desktop.mk`, plus entries in `BUILDABLE_PROJECTS` and CI.
