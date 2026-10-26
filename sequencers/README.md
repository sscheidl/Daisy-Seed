# Sequencer Projects

Separate project directories for sequencers on the Seed3 Desktop Dev Kit: they drive
CV/Gate and MIDI and may compose an internal voice and effects. The layout matches
`instruments/` and `effects/` (`DAISY_ROOT ?= ../..` in `common/desktop.mk`).

| Sequencer | Project ID | Status |
| --- | --- | --- |
| [Step Seq](step-seq/) | `step-seq` | Planned: 2 × 8-step sequencer core modeled on the KORG SQ-1, CV/Gate and MIDI (USB, DIN). Phase 0 holds the scaffold and the [SQ protocol](step-seq/docs/PROTOCOL.md), [parameter](step-seq/docs/PARAMETERS.md) and [transport](step-seq/docs/TRANSPORT.md) contracts |
| [Acid Seq](acid-seq/) | `acid-seq` | Planned product: Step Seq → [Acid 303](../instruments/acid-303/) → [Drive](../effects/drive/) → [Stereo Delay](../effects/stereo-delay/) |

The plan, decisions and hardware rules are in the
[Acid Seq work order](../docs/acid-seq/ARBEITSAUFTRAG.md) (German).
