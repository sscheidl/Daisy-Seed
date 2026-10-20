# Daisy Seed — Instruments and Effects

A shared workspace for independent firmware projects on the **Electro-Smith Seed3
Desktop Development Kit**. Each instrument or effect has its own directory and
documentation, and each implemented project has its own firmware build. Projects
share the pinned libraries, toolchain, and board build configuration.

## Projects

| Project | Directory | Status |
| --- | --- | --- |
| [Toy Piano v0.2](instruments/toy-piano/) | `instruments/toy-piano/` | USB MIDI; ARM and host tests pass; keys, AD envelope, polyphony, fast repeats, and MIDI with Studio One reported working on hardware |
| [Physical Modeling](instruments/physical-modeling/) | `instruments/physical-modeling/` | Four-voice Torus/Rings instrument, six models, global stereo reverb and USB MIDI; software validation in project docs |
| [Nebulae on Daisy v0.1.0](instruments/granular/) | `instruments/granular/` | Stereo granular/vocoder, SD PCM16 samples, Live recording, three UI modes and J7 USB MIDI; local software validation, hardware pending |
| [Looper v0.3](instruments/looper/) | `instruments/looper/` | Original tape looper modeled on the Qu-Bit Stardust (firmware analysis published); Sound on Sound, Replace, Frippertronics, Resample, Slice/Skip, copy-on-write undo/redo, varispeed, inertia, reverse, loop persistence and WAV import on microSD; host tests and ARM build pass; hardware test pending |
| [Ambient Reverb v0.1](effects/reverb/) | `effects/reverb/` | Original stereo reverb imported from the FV-1 research project; host tests and ARM build pass; hardware test pending |
| [Granular Effect](effects/granular/) | `effects/granular/` | Planned live/freeze application of the granular work |

The repository remains **`sscheidl/Daisy-Seed`**. Project directories share one Git
history while keeping their source, tests, validation notes, and build output
separate. Branches, PRs, and issues can track each project independently; separate
or nested Git repositories are unnecessary.

## Local layout

```text
Daisy-Seed/
├── instruments/
│   ├── toy-piano/          # Makefile, README, src, tests, docs, generated build
│   ├── physical-modeling/  # Torus DSP, hardware/MIDI, tests and firmware
│   ├── granular/           # Nebulae on Daisy DSP, SD, UI, MIDI and tests
│   └── looper/             # Stardust analysis and buildable engine core
├── effects/
│   ├── reverb/             # buildable original Ambient Reverb
│   └── granular/           # effect plan
├── common/desktop.mk      # shared official Seed3 Desktop build settings
├── docs/                  # hardware, build, flashing, and references
├── scripts/               # toolchain and BOOT_SRAM image checks
├── projects.mk            # project IDs, paths, and implementation status
├── libDaisy/              # pinned official submodule
└── DaisySP/               # pinned official submodule
```

The same project layout is used in local checkouts and cloud workspaces.
The Nebulae implementation was built and validated in the local Windows checkout.

Hardware diagnostics live in the separate
[Daisy-Seed3-Desktop-Diagnostics](https://github.com/sscheidl/Daisy-Seed3-Desktop-Diagnostics)
repository.

## Build and test

```bash
git clone https://github.com/sscheidl/Daisy-Seed.git
cd Daisy-Seed
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
bash scripts/install-toolchain.sh  # Linux x86_64, no sudo
export PATH="$PWD/.tools/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin:$PATH"

make list
make PROJECT=toy-piano -j4
make PROJECT=toy-piano test
make PROJECT=toy-piano test-sanitize
make PROJECT=toy-piano test-thread
make PROJECT=reverb-fx -j4
make PROJECT=reverb-fx test
make PROJECT=reverb-fx test-sanitize
make test-release
```

Toy Piano is the default when `PROJECT=` is omitted. You can also build it from
its directory with `make -C instruments/toy-piano`. Its image is
**`instruments/toy-piano/build/ToyPiano-v0.2.bin`**. The version is defined in
`instruments/toy-piano/src/version.h`; `make -s version` prints it. Builds for
projects that are still plans fail explicitly instead of building a substitute
firmware. `make all-projects` and `make test-projects` run all implemented projects.
The reverb image is `effects/reverb/build/AmbientReverb-v0.1.bin`.
Nebulae builds with `make PROJECT=granular` to
`instruments/granular/build/NebulaeOnDaisy-v0.1.0.bin`; its controls and hardware
test are in the [project README](instruments/granular/README.md).

Each implemented project is currently a separate firmware image. Switching
instruments requires flashing the corresponding BIN; runtime instrument switching
and a shared effect chain have not been implemented.

## Documentation and next steps

* [Build and toolchain](docs/BUILD.md), [hardware](docs/HARDWARE.md), and [flashing](docs/FLASHING.md).
* [Toy Piano controls and hardware test](instruments/toy-piano/README.md),
  [USB MIDI](instruments/toy-piano/docs/MIDI.md),
  [changelog](instruments/toy-piano/CHANGELOG.md), and [validation report](instruments/toy-piano/docs/VALIDATION.md).
* [Sources and comparison limits](docs/REFERENCES.md): Rings/Elements, Daisy
  ports, Iridium Resonator, GR-1, Iridium Particle, Clouds, Beads, and reverb references.

The operator accepted the Toy Piano v0.2 release BIN after reporting “all tests
green” on October 17, 2026. The [validation report](instruments/toy-piano/docs/VALIDATION.md)
still lists hardware measurements and detailed MIDI cases without recorded
results. The next steps are hardware tests of Ambient Reverb, the Torus physical
modeling instrument and Nebulae on Daisy.
Effect variants can share DSP components once those components actually exist.
Resonator models belong to the physical modeling project. The original Ambient
Reverb is separate from the planned Clouds/Nimbus study and a possible direct
Slöer port.
The user observed an FV-1 marked “2211” in an opened Slöer pedal; the specific
Slöer algorithm is still unknown. Reverb work can proceed independently of the
instrument projects.
