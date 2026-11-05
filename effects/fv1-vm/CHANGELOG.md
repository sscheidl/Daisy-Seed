# Changelog: fv1-vm

## Firmware 0.3.0, toy-piano-fv1 0.4.0 (2026-11-05, unreleased)

* Program groups: `gen_programs.py --groups` writes a group table (offset and size per group) into the local header
  and the program list; MIDI Bank Select = group, Program Change = position; out-of-range requests are ignored.
  Without groups the flat bank × 128 + program mapping stays.
* Standalone: S3/S4 jump to the previous/next group; LEDs S1..S8 show the position, S9..S12 the group.
* Control page: groups as chips and list headers, codes such as `REV 32`, search by code or `#` catalogue number.

## toy-piano-fv1 0.3.0 (2026-11-05, unreleased)

* Piano first (owner decision): VR1 attack and VR2 decay as in the regular Toy Piano, VR3..VR5 = FV-1
  POT0..POT2, VR6 dry/wet, SW17 FX bypass (exact dry piano). Firmware fv1-vm-fx stays 0.2.0.

## Firmware 0.2.0, toy-piano-fv1 0.2.0 (2026-11-05, unreleased; component stays 0.1.0)

* Standard MIDI program selection in both firmwares: Program Change on any channel with Bank Select CC0/CC32
  (bank × 128 + program).
* Local Web-MIDI control page `tools/control/fv1vm_control.html` (program list, selection, CPU telemetry,
  standalone remote controls); `gen_programs.py --names` writes its local, git-ignored program list.
* `fv1vm_probe.py --notes N` for instrument products.
* Board CPU sweeps of 0.1.0 recorded in the README (no overrun in either product).

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
