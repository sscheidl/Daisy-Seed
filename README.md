# Daisy Seed — Instruments and Effects

A shared workspace for independent firmware projects on the **Electro-Smith Seed3
Desktop Development Kit**. Each instrument or effect has its own directory and
documentation, and each implemented project has its own firmware build. Projects
share the pinned libraries, toolchain, and board build configuration.

## Projects

| Project | Directory | Status |
| --- | --- | --- |
| [Toy Piano v0.4](instruments/toy-piano/) | `instruments/toy-piano/` | USB MIDI and the Mod on Daisy matrix (per-voice pitch/level/harmonic, Poly AT); with an empty matrix bit-identical to v0.2 (golden test); v0.4 has the full Mod on Daisy panel like Physical Modeling (pot positions, extended monitor, LFO cross modulation) and ignores echoed replies (Studio One input monitoring); ARM and host tests pass; v0.2 keys, AD, polyphony and MIDI with Studio One reported working on hardware, v0.3/v0.4 hardware test pending |
| [Toy Piano + Ambient Reverb v0.1.2](instruments/toy-piano/docs/AMBIENT.md) | `instruments/toy-piano/` (`AMBIENT=1`) | Separate `toy-piano-ambient` build: six reverb modes including Shimmer, Stretch, physical transpose/decay, existing matrix/editor contract and callback diagnostics; host/sanitizer/ARM checks pass, hardware untested |
| [Toy Piano + Tape FX v0.1.0](instruments/toy-piano/docs/TAPE.md) | `instruments/toy-piano/` (`TAPE=1`) | Separate `toy-piano-tape` test build: Toy Piano into the TapeVibe port, all eight pots for the tape, bypass/character switches, existing matrix/editor contract and callback diagnostics; host/sanitizer/ARM checks pass, hardware untested |
| [Physical Modeling](instruments/physical-modeling/) | `instruments/physical-modeling/` | Four-voice Torus/Rings, six models and global reverb; user-confirmed hardware/MIDI behavior and measured CPU headroom; model-switch spikes accepted as a known limitation ([results](instruments/physical-modeling/docs/HARDWARE_PLAYING_2026-10-22.md)) |
| [Physical Modeling + Mod on Daisy](instruments/physical-modeling/docs/MODULATION.md) | `instruments/physical-modeling/` (`MOD=1`) | Separate build `make PROJECT=physical-modeling-mod`: Mod on Daisy matrix with Poly AT per Torus voice; regular build without Mod on Daisy code, MOD build with empty matrix bit-identical in sound (golden test); hardware test pending |
| [Composed Physical Modeling FX](docs/MODULAR_COMPONENTS.md) | `instruments/physical-modeling/` (`COMPOSED=1`) | Separate v0.2.0 pilot builds with Torus or Ambient Reverb adapters and Mod on Daisy; explicit component manifests; hardware validation pending |
| [Nebulae on Daisy v0.1.1](instruments/granular/) | `instruments/granular/` | Stereo granular/vocoder, SD PCM16 samples, Live recording, three UI modes and J7 USB MIDI; local software validation, hardware pending |
| [Looper v0.4.0](instruments/looper/) | `instruments/looper/` | Original tape looper modeled on the Qu-Bit Stardust (firmware analysis published); Sound on Sound, Replace, Frippertronics, Resample, Slice/Skip, copy-on-write undo/redo, varispeed, inertia, reverse, loop persistence and WAV import (48 or 44.1 kHz) on microSD, MIDI CC control over USB; host tests and ARM build pass; hardware test pending |
| [Looper + Mod on Daisy](instruments/looper/docs/MODULATION.md) | `instruments/looper/` (`MOD=1`) | Separate build `make PROJECT=looper-mod`: Mod on Daisy matrix on Speed, Start, Size, Slice, Skip, Inertia, Frippertronics decay and Mix; regular image byte-identical, MOD build with empty matrix bit-identical in sound (golden test); hardware test pending |
| [Ambient Reverb v0.3.1](effects/reverb/) | `effects/reverb/` | Original stereo reverb with Desktop controls and variable-rate wet Stretch; host tests and ARM build pass; hardware test pending |
| [Tape FX v0.1.0](effects/tape-fx/) | `effects/tape-fx/` | Reusable stereo tape component ported from the drumlogue TapeVibe (bit-identical reference path) with optional MixForge stages, plus the standalone `tape-fx` stereo effect firmware; host/sanitizer/ARM checks pass, hardware untested ([report](effects/tape-fx/docs/REPORT.md)) |
| [Step Seq v0.1.0](sequencers/step-seq/) | `sequencers/step-seq/` | KORG SQ-1-style 2 × 8-step sequencer, firmware `step-seq`: CV/Gate (own DAC register path, calibration, enable), MIDI USB+DIN with note tracking, three play modes, internal/Gate/MIDI/Auto clock, SQ protocol and `sq_probe.py`; host/sanitizer/ARM checks pass, hardware untested |
| [Acid 303 v0.1.0](instruments/acid-303/) | `instruments/acid-303/` | Monophonic TB-303-style voice (DaisySP PolyBLEP and ladder filter with self-oscillation, accent, slide, two-source note stack) and standalone firmware `acid-303` with MIDI USB+DIN and CC plan; golden tests; host/sanitizer/ARM checks pass, hardware untested |
| [Drive v0.1.0](effects/drive/) | `effects/drive/` | Soft/Hard/Tape/Fold saturation shared with Tape FX (`common/dsp/saturation.h`, Tape FX images byte-identical after the move), tone, auto level within 1 dB, wet-only FX contract; standalone `drive-fx` firmware; host/sanitizer/ARM checks pass, hardware untested |
| [Stereo Delay v0.1.0](effects/stereo-delay/) | `effects/stereo-delay/` | Ping-pong and stereo L/R delay, free or synced up to 12 s (dotted whole at 30 BPM, 4.6 MB in SDRAM), feedback 95 % bounded, damping, host tail handling; standalone `delay-fx` firmware with MIDI clock; host/sanitizer/ARM checks pass, hardware untested |
| [Acid Seq v0.1.0](sequencers/acid-seq/) | `sequencers/acid-seq/` | Product firmware `acid-seq` composed of the four components above: sequencer → voice → Drive → Delay → soft limiter, voice shared with MIDI channel 2, delay synced to the sequencer, all components over SQ; end-to-end golden; host/sanitizer/ARM checks pass, hardware untested, no release ([work order](docs/acid-seq/ARBEITSAUFTRAG.md)) |
| [Hardware diagnostics v0.3.2](diagnostics/) | `diagnostics/` | Implemented SD diagnostics, Windows SD reader and CV/gate tests. **GEMESSEN 31 October 2026:** SD reader 0.3.2 normal I/O and write-interruption recovery; both CV and gate channels 0.3.1 functional loopbacks. [Scope, remaining checks and binary downloads](diagnostics/README.md#hardware-validation) |
| [Granular Effect](effects/granular/) | `effects/granular/` | Planned live/freeze application of the granular work |
| [Mod on Daisy](modules/mod-on-daisy/) | `modules/mod-on-daisy/`, editor in `editors/mod-on-daisy/` | Reusable 8×8 modulation matrix (header-only module, no instrument firmware). Phase 2 software done: core, host tests (ASan/UBSan/TSan, fuzzing), bench firmware `make PROJECT=mod-bench`, `tools/mod_probe.py`; hardware measurements pending ([validation](modules/mod-on-daisy/docs/VALIDATION.md)). Spec: [decisions](modules/mod-on-daisy/docs/DECISIONS.md), [architecture](modules/mod-on-daisy/docs/ARCHITECTURE.md), [MIDI protocol](modules/mod-on-daisy/docs/PROTOCOL.md), [storage](modules/mod-on-daisy/docs/STORAGE.md) |
| [Mod on Daisy Editor](editors/mod-on-daisy/) | `editors/mod-on-daisy/` (`src/` JUCE, `web/` browser) | Matrix/source panel as JUCE standalone, browser and VST3 device editor. Simulators are opt-in. Core/protocol tests cover lost/corrupted frames; VST3 passes PC audio through and stores the modulation patch, not the complete instrument/FX state |

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
├── sequencers/             # step-seq core and contracts, acid-seq product
├── effects/
│   ├── reverb/             # buildable original Ambient Reverb
│   ├── tape-fx/            # Tape FX component (TapeVibe/MixForge port) and standalone firmware
│   ├── drive/              # planned Drive effect (Acid Seq)
│   ├── stereo-delay/       # planned Stereo Delay effect (Acid Seq)
│   └── granular/           # effect plan
├── modules/
│   └── mod-on-daisy/       # reusable modulation matrix: include, tests, bench firmware, tools
├── editors/
│   └── mod-on-daisy/       # grid editor: JUCE standalone (src/), browser variant (web/)
├── diagnostics/           # Seed3 hardware diagnostics: firmware, Windows GUI, tests
├── common/desktop.mk      # shared official Seed3 Desktop build settings
├── common/dsp/            # shared header-only DSP (saturation curves, deterministic exp2/log2)
├── common/bootloader/     # firmware updates without BOOT/RESET (keys, SysEx)
├── docs/                  # hardware, build, flashing, and references
├── scripts/               # toolchain and BOOT_SRAM image checks
├── projects.mk            # project IDs, paths, and implementation status
├── libDaisy/              # pinned official submodule
└── DaisySP/               # pinned official submodule
```

The same project layout is used in local checkouts and cloud workspaces.
The Nebulae implementation was built and validated in the local Windows checkout.

Hardware diagnostics (firmware, Windows GUI, protocol, tests) live in
[diagnostics/](diagnostics/README.md). SD card reader and both CV/gate channels
are implemented with recorded basic hardware tests; exact versions and limits
are listed in the [validation table](diagnostics/README.md#hardware-validation).

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
**`instruments/toy-piano/build/ToyPiano-v0.4.bin`**. The version is defined in
`instruments/toy-piano/src/version.h`; `make -s version` prints it. Builds for
projects that are still plans fail explicitly instead of building a substitute
firmware. `make all-projects` and `make test-projects` run all implemented projects.
The reverb image is `effects/reverb/build/AmbientReverb-v0.3.1.bin`.
Nebulae builds with `make PROJECT=granular` to
`instruments/granular/build/NebulaeOnDaisy-v0.1.1.bin`; its controls and hardware
test are in the [project README](instruments/granular/README.md).

Each implemented project is currently a separate firmware image. Switching
instruments requires flashing the corresponding BIN. The composed Physical Modeling
pilot selects a downstream Torus or Ambient FX at build time; runtime instrument/FX
switching and arbitrary effect chains have not been implemented.

All implemented instruments, effects and control panels are listed in the
[component registry](components/README.md). `make components` validates owned
version sources; `make PROJECT=<id> manifest` checks the built image and records
the product/component versions, Git revision and checksum in `COMPONENTS.json`.

## Documentation and next steps

* [Desktop case and Paper on Daisy](hardware/desktop-case/): closed V2 construction
  study with metal standoffs, Knurlies, two acrylic layers and key guides;
  physical fit untested. The first open tray/frame prototype remains available.
* [Future effect programs](docs/FUTURE_EFFECT_PROGRAMS.md): stereo delay and the
  existing original ambient reverb; integration and combined performance unmeasured.

* **[Handover](HANDOVER.md)** (state, local setup, hardware test run sheet, open decisions)
  and the [agent rules](AGENTS.md).
* [Build and toolchain](docs/BUILD.md), [hardware](docs/HARDWARE.md), and [flashing](docs/FLASHING.md);
  updates without BOOT/RESET: hold S9+S16 for 5 s or use the editor ([common/bootloader](common/bootloader/README.md)).
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

## License

Open source, decided on October 24, 2026:

| Part | License |
| --- | --- |
| Everything of our own unless stated otherwise: firmware, the Mod on Daisy module, the browser editor (`editors/mod-on-daisy/web/`), `mod_probe.py`, scripts, docs | **MIT**, see [LICENSE](LICENSE) |
| JUCE editor and VST3 plugin (`editors/mod-on-daisy/src/`, `tests/`, `CMakeLists.txt`) | **AGPLv3**, see [COPYING.AGPL-3.0.txt](editors/mod-on-daisy/COPYING.AGPL-3.0.txt). JUCE 9 is used under its AGPLv3 option; the MIT parts it includes stay MIT |
| Third-party code | its own license, kept next to it: libDaisy and DaisySP (MIT, submodules), Mutable Instruments stmlib/Torus (MIT, `instruments/physical-modeling/third_party/stmlib/LICENSE`, `instruments/physical-modeling/torus/UPSTREAM.md`), a patched copy of libDaisy's `midi_parser.cpp` (MIT, notice in the file, `instruments/physical-modeling/src/`), the VST3 SDK bundled with JUCE (MIT), FatFs (`common/sd/fatfs/LICENSE.txt`), Ambient Reverb (`effects/reverb/LICENSE-MIT.txt`), the granular instrument's sources incl. LGPL-2.1 parts (`instruments/granular/LICENSES/`) |

The repository is public on GitHub; until this license was added it had none. Material
that may not be redistributed (manufacturer documents, photos, analysis inputs) does not
belong here. The content and full history were audited on October 25, 2026: no secrets,
firmware images, samples or manufacturer documents were found in the published history.
Local analysis outputs and exports that bundle third-party 3D models are excluded in
`.gitignore`.
