# Texture (working name): Beads port study

A possible port of the Mutable Instruments Beads DSP to the Seed3 Desktop Kit. **The port is not ordered.** This
folder holds only what the study needs so far:

* [`load-spike/`](load-spike/README.md): measurement firmware S1, the CPU load of the unchanged processor with the buffer
  in SDRAM (A) or in internal SRAM as on the module (B). Decides the memory layout before any port code is written.
* [`third_party/`](third_party/README.md): the upstream DSP (MIT), with provenance and the one change.

The analysis and the port concept live in the L2A case `beads` (local, outside this repository). "Texture" is a working
name: upstream asks derivative works not to use the Mutable Instruments name, and the final name is the owner's choice.
