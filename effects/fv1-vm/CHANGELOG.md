# Changelog: fv1-vm

## 0.1.0 (2026-11-05, unreleased)

* Header-only FV-1 interpreter `seed::fx::fv1::Vm<T>` behind the FX interface 1.0, decoder, host runner
  `tools/fv1vm_run.cpp` and 16 host tests with hand-encoded programs.
* Validated on the host against the L2A reference model (see README).
* `fv1vm/fv1_asm.h` encoders (shared by tests and demo programs) and `fv1vm/fv1_fx48.h`: 48 kHz adapter with the
  program at 32 kHz between two 3:2 polyphase FIR resamplers.
* Standalone firmware `fv1-vm-fx` 0.1.0 (`src/`): 32 kHz, two own demo programs or a local generated table,
  DWT cycle telemetry over USB-MIDI CC, build variants for precision, delay-RAM placement and program source.
* Tools: `gen_programs.py` (local program table), `fv1vm_probe.py` (telemetry decoder and program sweep).
* Composed product `toy-piano-fv1` (Toy Piano `FV1=1`).
* Host tests and ARM builds only; hardware NOT MEASURED.
