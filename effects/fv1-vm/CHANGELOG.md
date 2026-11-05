# Changelog: fv1-vm

## 0.1.0 (2026-11-05, unreleased, phase 1)

* Header-only FV-1 interpreter `seed::fx::fv1::Vm<T>` behind the FX interface 1.0, decoder, host runner
  `tools/fv1vm_run.cpp` and 16 host tests with hand-encoded programs.
* Validated on the host against the L2A reference model (see README). No firmware, no hardware results.
