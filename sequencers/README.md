# Sequencer Projects

Separate project directories for sequencers on the Seed3 Desktop Dev Kit: they drive
CV/Gate and MIDI and may compose an internal voice and effects. The layout matches
`instruments/` and `effects/` (`DAISY_ROOT ?= ../..` in `common/desktop.mk`).

| Sequencer | Project ID | Status |
| --- | --- | --- |
| [Step Seq](step-seq/) | `step-seq` | v0.1.0: 2 × 8-step sequencer modeled on the KORG SQ-1, CV/Gate and MIDI (USB, DIN), [SQ protocol](step-seq/docs/PROTOCOL.md), [parameter](step-seq/docs/PARAMETERS.md) and [transport](step-seq/docs/TRANSPORT.md) contracts; software checks pass, [hardware test](step-seq/docs/HARDWARE_TEST.md) pending |
| [BABBAGE](babbage/) | `babbage` | v0.1.0: product Step Seq → [Acid 303](../instruments/acid-303/) → [Drive](../effects/drive/) → [Stereo Delay](../effects/stereo-delay/) → soft limiter; software checks pass, [hardware test](babbage/docs/HARDWARE_TEST.md) pending, no release |

The plan, decisions and hardware rules are in the
[BABBAGE work order](../docs/acid-seq/ARBEITSAUFTRAG.md) (German, written under the former name Acid Seq).
