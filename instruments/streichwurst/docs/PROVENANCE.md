# Research status, provenance and limits

2026-11-01. Hand-over of self-written results from the separate private
research project Custom-Firmware/Streichwurst. The user explicitly commissioned the preparation
of the new instrument in the existing Daisy-Seed repository and its storage on
GitHub. This repository is public.

## What is included here

Six own research/design texts and an own mathematical
Python model; no manufacturer programs, presets, tables, samples,
decompilations or disassemblies. A snapshot header and relative links
were adjusted for this hand-over. Original and target hashes are listed in
[RESEARCH_MANIFEST.json](RESEARCH_MANIFEST.json). In the Python model, the
import of the image checker was replaced by its own small output path check;
DSP and nine tests remain unchanged. Its functional limits are documented.

Source checkpoint: `a7b81b61092b499eb4384905f1ebc4b9f62f27a2` in the private
research repository. The resource report additionally contains the later
user clarification about the desktop board. Individual file hashes are therefore authoritative for the
exact attribution. Further research files are not imported automatically;
the complete private research status remains separate.

Examined local source: Streichfett VST3, 4,180,992 bytes,
SHA-256 `ba3d175e63e8eae5a48a8c0d9382ad17dfd6f0a340eedf8fc69fba0555c89c2a`.
PE32+ AMD64; resource version 0.8.1, no independently confirmed official
release attribution. The user's source was only examined statically and remained
unchanged. No manufacturer authentication or redistribution licence is claimed.
Addresses in the texts are RVAs, not runtime addresses and not firmware code.

## Reading order and evidence grades

1. [Audio/voice findings](research/audio-and-voices.md).
2. [Focused deep-dive SF-F01..F08](research/focused-findings.md), which refines older
   OPEN notes on envelope, octave transformation, balance and bypass.
3. [Ensemble](research/ensemble.md) and [Parameters](research/parameters.md).
4. [Own draft 0.2](research/daisy-dsp-draft-0.2.md) and
   [resource assumptions](research/daisy-budget.md).

CONFIRMED (BESTÄTIGT) denotes the static examination scope named in each case. PLAUSIBLE (PLAUSIBEL)
contains remaining interpretations; OPEN (OFFEN) is not reconstructed. Own
design decisions are not an original finding. A local numerical comparison
does not replace a complete control-flow/register/edge-case proof.

In particular, 128 logical note levels are no promise of 128 independent
oscillators or of a particular Daisy polyphony. Mode gain words are not a
finished reconstruction of the overall loudness. The 128 examined envelope table
indices do not prove the complete UI domain. The complete event amplitude,
solo wave generation, Animate/shaping and parts of phaser/reverb remain open.

Historical check: 36 bounded image anchors as well as 25 synthetic tests over
two checkpoints; image/model metadata reproduced locally. These checks
are not re-run by the document import. Only nine own
model tests become re-runnable here as an included reference.

## Existing hardware/toolchain baseline

[HARDWARE.md](../../../docs/HARDWARE.md), [BUILD.md](../../../docs/BUILD.md) and
[common/desktop.mk](../../../common/desktop.mk) are the concrete Daisy baseline:

* libDaisy `f715e4875b2d28445d0c11a0a17f064a248f69d0`.
* DaisySP `2c72eaf9eac5fc0dca1919d65d606da907832618`.
* xPack GCC 13.2.1-1.1, GNU++17/M7/hard float, BOOT_SRAM.
* Start configuration 48 kHz/48 frames, eight pots, 16 buttons/LEDs,
  the official `desktop_devkit::Hardware` abstraction and J7 USB MIDI.

These are the pinned project foundations. The concrete board revision and the
Streichwurst runtime/memory limits have not yet been measured on the device.
The hardware connection, earlier described only as study assumptions, is checked in the
implementation project against these already existing local sources.

Preserve the code provenance and notices of existing libraries. No manufacturer
source code is published under the MIT licence of the own project.
Record future findings and deviations in `FIDELITY.md`; justify unknown values
instead of presenting them as documented based on names or generic synthesizer recipes.
