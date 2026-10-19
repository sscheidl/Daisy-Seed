# Sources for Future Instruments and Effects

As of October 17, 2026, this is an initial survey of public original and Daisy
sources alongside existing research reports. It is neither a measured sound
comparison nor proof that the ports run on our Desktop Kit. The additional
original engines and DaisyExamples ports have not been imported, built, or run
here. DaisySP remains the pinned library already included in this repository.

## Pinned source revisions

| Source | Reviewed commit |
| --- | --- |
| [Mutable Instruments / pichenettes/eurorack](https://github.com/pichenettes/eurorack/tree/f4cfa7697095a28d292bff758beb51deb49b5a86) | `f4cfa7697095a28d292bff758beb51deb49b5a86` |
| [DaisyExamples](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27) | `259ed82c7d4c0d7699f695945e6e111b7dc7cb27` |
| [DaisySP, already included as a submodule](https://github.com/daisyaudio/DaisySP/tree/2c72eaf9eac5fc0dca1919d65d606da907832618) | `2c72eaf9eac5fc0dca1919d65d606da907832618` |
| [Our research reports, Custom-Firmware](https://github.com/sscheidl/Custom-Firmware/tree/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9) | `3e041b5b9ce98eb687316ffb3c4ba19492ec74e9` |
| [FV-1 research and independent Ambient Reverb](https://github.com/sscheidl/fv-1-DSP-xtractor/tree/eb7fe38e165b2a997e8128ebc2b7a7a228bfcb65) | `eb7fe38e165b2a997e8128ebc2b7a7a228bfcb65` |

The DaisySP review covered headers, attribution comments, and license. For the
original projects, the linked DSP interfaces and project README were read. The
Torus/Nimbus review also covered their READMEs, the Torus Makefile, and a
resonator header. Research reports were consulted only as text references.

## Physical modeling / resonator

| Source | Documented approach | Relevance here |
| --- | --- | --- |
| [Rings Part](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/rings/dsp/part.h) | Model selection includes modal, string, and other resonator models; processes input, output, and auxiliary audio in blocks | Original reference for fuller model and voice organization |
| [Elements Part](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/elements/dsp/part.h) | Separate performance and patch data, blow and strike inputs, and main/aux outputs | Second reference for excitation and resonant body |
| [Torus](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Torus) | Its README calls it a Rings port for **Daisy Patch**; its build includes its own DSP and stmlib | Comparable port structure, not a Desktop Kit firmware |
| [DaisySP ModalVoice](https://github.com/daisyaudio/DaisySP/blob/2c72eaf9eac5fc0dca1919d65d606da907832618/Source/PhysicalModeling/modalvoice.h) / [Resonator](https://github.com/daisyaudio/DaisySP/blob/2c72eaf9eac5fc0dca1919d65d606da907832618/Source/PhysicalModeling/resonator.h) | Source comments trace components to **Plaits**; ModalVoice describes click → low-pass → resonator | Small, available experiment path, not complete Rings/Elements |
| [Official Seed resonator example](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/seed/DSP/resonator) | Demonstrates the resonator with random parameters | API and integration example, not a complete instrument for this kit |
| [Our Iridium Resonator study](https://github.com/sscheidl/Custom-Firmware/blob/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9/analysis/synthesizers/waldorf-iridium/04_resonator/README.md) | Research report on resonator organization and parameters; no open manufacturer source | Third conceptual reference, not a manufacturer library |

**Implemented decision (v0.1.0):** The physical modeling project now vendors the
pinned Torus Part DSP and matching stmlib to provide six models and four voices.
See [project provenance](../instruments/physical-modeling/torus/UPSTREAM.md) and
[validation](../instruments/physical-modeling/docs/VALIDATION.md). Elements and
DaisySP remain references; no additional engine is incorporated.

## Granular / freeze

| Source | Documented approach | Relevance here |
| --- | --- | --- |
| [GR-1 research](https://github.com/sscheidl/Custom-Firmware/blob/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9/analysis/synthesizers/gr-1/README.md) | Our reports on GR-1 DSP and controls; no licensed manufacturer DSP component | Musical reference for an original scheduler and sample path |
| [GR-1 / Iridium comparison](https://github.com/sscheidl/Custom-Firmware/blob/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9/analysis/comparative/gr1-iridium-granular/README.md) | Comparison of grain organization, resources, and UI roles | Bounded pool; keep base, scan, and grain read positions separate |
| [Iridium Particle](https://github.com/sscheidl/Custom-Firmware/blob/3e041b5b9ce98eb687316ffb3c4ba19492ec74e9/analysis/synthesizers/waldorf-iridium/05_particle/README.md) | Research reference, not a hardware-tested engine of our own | Additional organizational and parameter concepts |
| [DaisySP GranularPlayer](https://github.com/daisyaudio/DaisySP/blob/2c72eaf9eac5fc0dca1919d65d606da907832618/Source/Sampling/granularplayer.h) | Init takes a sample buffer and size; Process takes speed, pitch in cents, and grain time in ms | Available component for small comparison experiments |
| [Clouds GranularProcessor](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/clouds/dsp/granular_processor.h) | Supplied memory buffers, block-based audio input/output, and several modes | Public original for audio effect and freeze comparison |
| [Nimbus Patch](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Nimbus) / [Field](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/field/Nimbus) | READMEs identify Clouds ports with Patch/Field-specific controls | Existing Daisy references requiring Desktop Kit adaptation |
| [Beads GranularProcessor](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/beads/dsp/granular_processor.h) | Public original with delay and granular processing | Additional reference; no Daisy port verified here |

“Beats” **may have meant Beads**; this identification is an assumption. The
original [README](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/README.md)
indicates that a desktop build is possible but the build and operation on the
module were unverified. Public availability is not hardware validation.

The historical research PRs are [GR-1 #17](https://github.com/sscheidl/Custom-Firmware/pull/17),
[comparison #18](https://github.com/sscheidl/Custom-Firmware/pull/18), and
[Iridium GUI #19](https://github.com/sscheidl/Custom-Firmware/pull/19).
This plan uses the pinned current `main` revision above. Earlier statements
about then-open PRs are not current integration claims.

## Reverb

**Clarified by the user:** The [reverb area](../effects/reverb/) covers the
reverb section of **Clouds/Nimbus** and the **Walrus Audio Slöer Stereo Reverb**
as a second development and possible porting target.

| Source | Reviewed status | Use and limitations |
| --- | --- | --- |
| [Clouds Reverb](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/clouds/dsp/fx/reverb.h) | Public original path in the pinned Mutable revision | Study the reverb separately from the granular application |
| [Nimbus Reverb, Patch](https://github.com/daisyaudio/DaisyExamples/blob/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Nimbus/dsp/fx/reverb.h) / [Field](https://github.com/daisyaudio/DaisyExamples/blob/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/field/Nimbus/dsp/fx/reverb.h) | Reverb header and MIT notices reviewed; Daisy ports exist | Starting point for Desktop Kit experiments; this section has not been built here |
| Walrus Audio Slöer | On October 17, 2026, the user identified a Spin Semiconductor FV-1 marked “2211” in an opened pedal; no independent visual inspection here | FV-1 observed in that device. Meaning of the marking, effect program, clocking, full stereo path, and portable algorithm source remain unknown |
| [FV-1 porting strategy](https://github.com/sscheidl/fv-1-DSP-xtractor/blob/d1ef0525d072091d9d4da46f1821d45e930a2745/docs/FV1_DAISY_PORTING.md) and [source catalog](https://github.com/sscheidl/fv-1-DSP-xtractor/blob/d1ef0525d072091d9d4da46f1821d45e930a2745/docs/FV1_SOURCE_CATALOG.md) | Existing C++17 Plate/Hall references and reports reviewed; no port performed here | FV-1 research, explicitly not a Slöer implementation. The historical source did not confirm the chip; the new observation does not identify the algorithm |
| [Independent Ambient Reverb](https://github.com/sscheidl/fv-1-DSP-xtractor/tree/eb7fe38e165b2a997e8128ebc2b7a7a228bfcb65/ambient) | Original MIT-licensed stereo DSP and [Seed3 example](https://github.com/sscheidl/fv-1-DSP-xtractor/tree/eb7fe38e165b2a997e8128ebc2b7a7a228bfcb65/examples/seed3-ambient), imported into [reverb-fx](../effects/reverb/) | Host tests and ARM build pass here; no board run or Slöer sound match has been established |

Reverb is tracked separately from the granular effect. CloudSeed is not the
user's selected Clouds/Nimbus reference. The imported Ambient Reverb is an
original design inspired by Slöer features, not a direct Slöer port. The
specific Slöer algorithm and any actual sound match remain unverified.

## Reuse and open checks

The reviewed Mutable STM32 DSP headers carry MIT license text; DaisySP documents
its MIT licensing and attribution. Any later code reuse must account for the
specific files, dependencies, copyright notices, and naming rules. No
manufacturer engines or assets have been copied here. The independent
MIT-licensed Ambient Reverb code and its license have been imported.
Manufacturer binaries,
disassemblies, and reconstructed manufacturer functions will not be imported.

**Open:** detailed code comparison, sample-rate and block-size adaptation for
Seed3, CPU/memory measurements, sound comparison with identical test signals,
and final algorithm selection. A Cortex-M7 has no NEON; other devices' voice
and grain counts are not Daisy performance guarantees.
