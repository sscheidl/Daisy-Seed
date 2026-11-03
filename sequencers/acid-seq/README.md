# Acid Seq

Product made of [Step Seq](../step-seq/), [Acid 303](../../instruments/acid-303/),
[Drive](../../effects/drive/) and [Stereo Delay](../../effects/stereo-delay/).
Signal path: sequencer → voice → Drive → Delay → host mixer → soft limiter →
output. CV/gate and MIDI also run without a connected audio output.

| Status | |
| --- | --- |
| Version | 0.1.0 (`src/composed_version.h`), components with their own versions |
| Phase | 6 implemented, host tests and ARM build green |
| Hardware | NOT MEASURED ([hardware test](docs/HARDWARE_TEST.md)) |
| Release | none. The delivery path for `acid-seq` is still open (work order 4.10) |

Plan and rules: [Acid Seq work order](../../docs/acid-seq/ARBEITSAUFTRAG.md) 4.10.

## Build and test

```bash
make PROJECT=acid-seq -j4
make -C sequencers/acid-seq test
make -C sequencers/acid-seq test-sanitize
python scripts/check-firmware.py sequencers/acid-seq/build/AcidSeq-v0.1.0.bin
python scripts/check-elf-placement.py sequencers/acid-seq/build/AcidSeq-v0.1.0.elf --sdram delay_memory:4608000
make PROJECT=acid-seq manifest
```

Image `sequencers/acid-seq/build/AcidSeq-v<version>.bin`, identifier in the image
`ACID-SEQ-FW-VERSION=`, SQ product `acid-seq`.

## Controls on the kit

As in [Step Seq](../step-seq/README.md) (work order 4.1): S1–S16 steps, VR1–VR4 the
selected step, VR5 tempo, VR6 swing, VR7 length, VR8 transpose, SW17 start/stop,
SW18 mode. The voice and the effects have no knobs of their own on the kit. They are set via
the panel (editor, pages "Voice", "Drive", "Delay") or via MIDI CC.

| Output | Content |
| --- | --- |
| Audio L/R and headphones | Voice → Drive → Delay (ping-pong) → soft limiter |
| CV OUT 1/2 | Pitch and gate of the sequencer, locked until enabled (H14, 4.5) |
| MIDI OUT (USB, DIN) | Sequencer notes on the seq channel (default 1), MIDI clock |

## Voice, MIDI and effects

* **Voice ownership (R16):** The sequencer and external MIDI notes on the voice channel
  (default 2, parameter `voice_channel`) share the monophonic voice. The most recently
  played note wins. Each source only ends its own notes. When the sounding
  note ends, the most recently held note of the other source takes over.
* A sequencer panic (channel change, output switched off) removes only
  sequencer and preview notes. USB loss and a change of the voice channel end the
  MIDI notes. Before the bootloader the voice is fully muted.
* Received notes are not passed on to the MIDI outputs.
* **CCs on the voice channel** (USB and DIN) per [PARAMETERS.md](../step-seq/docs/PARAMETERS.md):
  voice CC 5, 7, 70, 71, 74–79, Drive CC 20–27, Delay CC 102–109.
  Velocity above 100 plays an accent.
* **Tempo hand-over:** In sync mode the delay follows the sequencer's tempo. The
  tempo comes from VR5 or the external clock.
* **Host mixer:** Drive crossfades over 10 ms when switched on and off. The delay has
  Dry/Wet and a tail after switching off. After that the product fades Dry/Wet
  to 0 % and outputs the voice exactly dry. The single firmware `delay-fx`, in contrast, keeps
  `dry · (1 − mix)`.
* **Soft limiter** at the output: unchanged below 0.8, soft towards ±1 above.

## Structure

* `include/acid_seq/chain.h`: signal path, shared by the firmware and the end-to-end test.
* `src/main.cpp`: firmware. The audio callback (1 ms blocks) runs controls,
  sequencer, DAC and audio chain. `main` runs MIDI, SQ, LEDs and the bootloader. The
  hand-over runs via SPSC queues and a triple buffer.
* `delay_memory` (4.6 MB) is in SDRAM and is cleared before audio starts.
* SQ components in IDENTIFY order: `step-seq`, `acid-303`, `drive`, `stereo-delay`.
