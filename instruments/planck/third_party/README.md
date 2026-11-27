# Third-party code of the PLANCK study

## `eurorack/`

The DSP of Mutable Instruments Beads by Emilie Gillet, MIT. The licence notice is in the head of every file;
`eurorack/stmlib/LICENSE` holds the stmlib notice.

| Part | Source | Commit |
| --- | --- | --- |
| `eurorack/beads/` | [pichenettes/eurorack](https://github.com/pichenettes/eurorack), folder `beads` | `f4cfa7697095a28d292bff758beb51deb49b5a86` |
| `eurorack/stmlib/` | [pichenettes/stmlib](https://github.com/pichenettes/stmlib), the gitlink of that commit | `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` |

Only the files the processor and the synth need: the include closure of `beads/dsp/*.cc` and `beads/resources.cc`, plus
`stmlib/dsp/units.cc` and `stmlib/utils/random.cc`, as in upstream `beads/test/makefile`. Not copied: the module's
drivers, UI, settings, calibration, factory test, bootloader and the hardware files. Copied from the L2A reference copy of
case `beads`. `../SHA256SUMS.upstream` holds the hashes of the upstream files (`sha256sum -c` from `eurorack/`).

**Changes (one):**

| File | Change | Why |
| --- | --- | --- |
| `beads/dsp/processor.h` | `#define ENABLE_HARDWARE_24K` is wrapped in `#ifndef TEXTURE_NO_HARDWARE_24K` | The kit has no 24 kHz codec rate. With the macro the processor uses the source's own alternative for the µ-law mode: 48 kHz with 2:1 conversion and the wow-and-flutter low-pass at 0.6. The shipped module firmware compiles this branch out |

`sha256sum -c ../SHA256SUMS.upstream` therefore reports exactly one mismatch, `beads/dsp/processor.h`.

**Host builds** add `-include cstdio`: upstream `wavetable_granular_player.h` calls `printf` under `TEST` without including
`<cstdio>`. No file is changed for that.

**Naming.** The upstream README, section *Guidelines for derivative works*: "Mutable Instruments" is a registered trademark
and must not be used on derivative works, and keeping the module's name is not recommended. So the folder used the working
name `texture`; the owner chose PLANCK on 2026-11-27 (port concept D1).
