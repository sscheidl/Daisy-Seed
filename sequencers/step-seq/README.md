# Step Seq

16-step sequencer (2 × 8) modeled on the KORG SQ-1 for the Seed3 Desktop Dev Kit:
CV/gate, MIDI over USB (J7) and DIN, three play modes on SW18, start/stop on SW17.
Its own firmware `step-seq` without audio, core of the product [Acid Seq](../acid-seq/).

| Status | |
| --- | --- |
| Version | Core and firmware 0.1.0 (`src/version.h`) |
| Software | Host, sanitizer and ARM checks passed (see CHANGELOG) |
| Hardware | NOT MEASURED, [hardware test](docs/HARDWARE_TEST.md) |

## Controls

| Element | Function |
| --- | --- |
| S1–S8, S9–S16 | Select step A1–A8, B1–B8. A long press (≥ 400 ms) toggles the gate on release. Chords do not trigger a toggle |
| LEDs | Gate on bright, off dark. The running light briefly dims or brightens. The selected step blinks fast |
| VR1 / VR2 | Octave (0–4) / semitone (C–B) of the selected step. With the sequencer stopped the note sounds for 150 ms |
| VR3 / VR4 | Gate length 5–100 % / nothing, accent, slide, accent + slide |
| VR5 / VR6 | Tempo 30–300 BPM / swing 50–75 % |
| VR7 / VR8 | Length 1–16 / transpose ±12 |
| SW17 | Start/stop. If set to Run at power-on, the sequencer starts after initialization |
| SW18 | up forward, middle backward, down random |
| S9 + S16 (5 s) | Bootloader. Gates and notes are switched off first |

After a step change, VR1–VR4 only take effect once they have moved by more than 2 %.

| Jack | Assignment |
| --- | --- |
| CV OUT 1 | Pitch 1V/Oct (0 V = C1), slide as glide |
| CV OUT 2 | Gate (codes from the calibration) |
| Gate IN 1 | Clock input (source "Gate IN 1" or "Auto") |
| Gate IN 2 | Reset on the rising edge, alternatively run enable |
| CV IN 1 | Transpose 1V/Oct (±24 semitones) |
| CV IN 2 | off, gate length or swing |

**CV is locked after every start.** It is only enabled after diagnostic test H14
and calibration with [`tools/sq_probe.py`](tools/sq_probe.py) (hardware test steps 1–2).

MIDI: notes on channel 1 (adjustable), accent with velocity 127, slide as legato.
MIDI clock (24 PPQN), Start, Stop and Continue are only sent with internal clock or
Gate IN 1. Received clock is never sent back. USB and DIN send
simultaneously; each output can be switched off.

## Build and test

```bash
make PROJECT=step-seq -j4                 # build/StepSeq-v0.1.0.bin
make PROJECT=step-seq test                # core, firmware on fakes, threads, contract, probe
make PROJECT=step-seq test-sanitize
make PROJECT=step-seq test-thread         # Linux only (TSan)
python3 scripts/check-firmware.py sequencers/step-seq/build/StepSeq-v0.1.0.bin
python3 scripts/check-elf-placement.py sequencers/step-seq/build/StepSeq-v0.1.0.elf
make PROJECT=step-seq manifest
```

## Structure

* `include/step_seq/`: header-only core without libDaisy. Contains the pattern and play rules
  (`pattern.h`), time base, transport and clock sources (`engine.h`), controls
  (`controls.h`), CV calibration, glide and the DAC register adapter (`cv.h`),
  MIDI output with note tracking and send budget (`midi_out.h`), SQ codec and
  device side (`sq_protocol.h`, `device.h`), parameters (`params.h`, `param_table.h`
  from `contracts/parameters.json`) as well as the callback diagnostics (`diagnostics.h`).
* `src/main.cpp`: hardware adapter. The callback (1 kHz) owns controls, core and DAC.
  `main` owns MIDI, SQ, LEDs and the bootloader. The hand-over runs only via
  SPSC queues and snapshots.
* **DAC (R01):** `hw.SetDacOut` only once at startup. After that the callback writes
  `DAC1->DHR12R1/DHR12R2` and `SWTRIGR`, without HAL and without a lock.

## Contracts

* [SQ protocol](docs/PROTOCOL.md) (`F0 7D 53 51`), reference codec
  [`tools/sq_codec.py`](tools/sq_codec.py), test vectors
  [`contracts/sq_vectors.json`](contracts/sq_vectors.json)
* [Parameter contract and CC plan](docs/PARAMETERS.md), machine-readable in
  [`contracts/parameters.json`](contracts/parameters.json)
* [Transport and event contract](docs/TRANSPORT.md) with expected sequences in
  [`contracts/transport_vectors.json`](contracts/transport_vectors.json)

Plan and rules: [Acid Seq work order](../../docs/acid-seq/ARBEITSAUFTRAG.md).
