# Acid 303

Monophonic voice modeled on the TB-303 for the Seed3 Desktop Dev Kit: saw or
square (DaisySP PolyBLEP), DaisySP `LadderFilter` (LP24, internally 4× oversampled)
with self-oscillation (D4), filter envelope with Env Mod and Decay, accent and slide.
Header-only core for the product [Acid Seq](../../sequencers/acid-seq/) and its own
firmware `acid-303` for testing and playing.

| Status | |
| --- | --- |
| Version | Core and firmware 0.1.0 (`src/version.h`) |
| Software | Host, sanitizer and ARM checks passed (see CHANGELOG) |
| Hardware | NOT MEASURED, [hardware test](docs/HARDWARE_TEST.md) |

The LadderFilter is a Moog-style model. It approximates the 303 sound but does not
model a diode ladder. A diode ladder remains a later, separately versioned
sound change with a new golden hash.

## Controls (firmware `acid-303`)

| Element | Function |
| --- | --- |
| S1–S16 | chromatic from C2 (MIDI 36–51) |
| VR1 / VR2 | Cutoff 40 Hz – 12 kHz (logarithmic) / resonance (100 % self-oscillates) |
| VR3 / VR4 | Env Mod / Decay 30 ms – 3 s (logarithmic) |
| VR5 / VR6 | Accent amount / tuning ±12 semitones (stepped) |
| VR7 / VR8 | Volume / waveform (left saw, right square) |
| SW17 | Accent for the keys |
| SW18 | up slide off, middle slide on held key (legato), down slide always. Applies at power-on and when the switch is moved; a value from the panel or via CC stays until then |
| S9 + S16 (5 s) | Bootloader. The voice is muted first |

Pots only take effect once they move. Until then the value last set via MIDI CC or
panel applies. Output: mono on both channels and the headphones. LEDs show
held keys and the sounding note.

MIDI over USB (J7) and DIN, omni in this firmware: notes (velocity above 100 = accent),
CCs per [MIDI.md](docs/MIDI.md), CC 120/123 end all notes. SQ parameters over USB only.

## Build and test

```bash
make PROJECT=acid-303 -j4                 # build/Acid303-v0.1.0.bin
make PROJECT=acid-303 test                # voice and firmware on fakes
make PROJECT=acid-303 test-sanitize
make -C instruments/acid-303 bench        # relative host cost, not a Seed3 measurement
make -C instruments/acid-303 fixture      # rewrite the 303 fixture (only on purpose)
python3 scripts/check-firmware.py instruments/acid-303/build/Acid303-v0.1.0.bin
python3 scripts/check-elf-placement.py instruments/acid-303/build/Acid303-v0.1.0.elf
```

## Core

`include/acid303/voice.h` contains the voice, `params.h` the parameters per the
[contract](../../sequencers/step-seq/docs/PARAMETERS.md) and the CC plan.

* **Notes:** shared stack for sequencer and MIDI. The most recently played note sounds;
  a note-off only affects the note of its source and ID, panic only one
  source (R16). When the sounding note ends, the most recent note still held takes over legato.
* **Envelopes:** amplitude 3 ms attack, slow decay while held, 8 ms release.
  Filter to 10 % within the decay time. Accent: up to +6 dB, up to 2 octaves more envelope,
  decay at most 200 ms.
* **Slide:** constant time (default 60 ms), without a new envelope.
* **Smoothing:** parameters 20 ms, filter coefficients every 8 samples. The output has a
  DC blocker (~4 Hz).
* **Determinism:** no libm calls in the audio path (our own `Exp2`), so that the
  golden hashes are the same on every host.
