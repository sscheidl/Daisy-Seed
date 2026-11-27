# PLANCK: Beads port study

Until 2026-11-27 the working name was *Texture* (folder `instruments/texture`); the rename changed no code, and the
measured spike images are byte-identical. The load spike keeps its identifiers: the marker `TEXTURE-SPIKE-VERSION=`,
the macros `TEXTURE_SPIKE_*` and `TEXTURE_NO_HARDWARE_24K`, the key `texture-spike`, the image names
`TextureSpike*.bin` and the store `firmware/texture-load-spike/`.

A possible port of the Mutable Instruments Beads DSP to the Seed3 Desktop Kit. **The port is not ordered.** This
folder holds only what the study needs so far:

* [`load-spike/`](load-spike/README.md): measurement firmware S1, the CPU load of the unchanged processor with the buffer
  in SDRAM (A) or in internal SRAM as on the module (B). Decides the memory layout before any port code is written.
* [`third_party/`](third_party/README.md): the upstream DSP (MIT), with provenance and the one change.

The analysis and the port concept live in the L2A case `beads` (local, outside this repository). The name is
PLANCK (owner decision 2026-11-27); upstream asks derivative works not to use the Mutable Instruments name.
