# Portable instrument core

Streichwurst instrument component (`streichwurst-instrument`), C++17, no libDaisy/HAL.

* `instrument.h`: contract (Prepare/Reset/SetParameters/ApplyEvent/RenderSections),
  separate mono string and solo buses, sample-accurate events, pending events for
  zero-frame blocks, host-owned key tables (`InstrumentStorage`).
* `string_section.h`: twelve pitch-class clocks, divide-down, own band-limited event
  kernel (compile-time table), leaky integration, envelopes, registration, octaves.
* `solo_section.h`: eight own solo voices (replacement section, see docs/FIDELITY.md).
* `params.h`, `events.h`, `version.h`; `param_table.h` is generated from
  `contracts/parameters.json` by `sequencers/step-seq/tools/gen_params.py`.
