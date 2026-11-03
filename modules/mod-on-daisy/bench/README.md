# ModBench — measurement firmware for Mod on Daisy

This firmware is **not an instrument**. It measures the matrix on the Seed3 Desktop Kit
(TEST_PLAN_PHASE2 § 3) and is at the same time the first hardware test of the MIDI protocol.
A sine voice bank with 8 targets runs through the matrix:

| Column | Target | Path |
| --- | --- | --- |
| 0 | Pitch (`+-1 st`) | voice |
| 1 | Level | voice |
| 2 | Harmonic | voice |
| 3 | Tone | voice |
| 4 | Pan (`+-50%`) | voice |
| 5 | Master | global |
| 6 | Width | global |
| 7 | Drive | global |

## Build and flash

```bash
make PROJECT=mod-bench -j4                          # build/ModBench-v0.1.1.bin (block 48)
make -C modules/mod-on-daisy/bench BLOCK=256 -j4     # build/b256-max8/
make -C modules/mod-on-daisy/bench MAX_ACTIVE=64 -j4 # build/b48-max64/ (scenario S6)
make PROJECT=mod-bench program-dfu                   # as for the instruments (docs/FLASHING.md)
```

## Operation on the device

* **Keys S1–S9** select scenarios S0–S8. The LED of the respective key lights up.
  After power-on S1 is active.
* Audio on J3/J4 (J5 headphones). Without notes, 4 or 32 voices play a fixed
  chord.
* **USB MIDI** (J7, "Daisy Seed External"):
  * Note On assigns a note to voices one after another; Poly AT on that note modulates
    the voice.
  * CC1 and Channel AT are sources.
  * CC3 is the numbered test stream for loss detection.
* **CV 1/2** and **Gate 1/2** are the matrix's sources and sync inputs respectively.

## Measurement procedure on Windows

```bash
pip install mido python-rtmidi
python modules/mod-on-daisy/tools/mod_probe.py identify
python modules/mod-on-daisy/tools/mod_probe.py lengths          # H2/H3
python modules/mod-on-daisy/tools/mod_probe.py bench --seconds 60 --csv s48.csv   # S0..S8
python modules/mod-on-daisy/tools/mod_probe.py monitor --seconds 20             # H5/H6, raw CV values
python modules/mod-on-daisy/tools/mod_probe.py cc-stats         # if SysEx replies do not arrive
```

* Studio One should not have the port open during this (H4).
* The results belong in [docs/VALIDATION.md](../docs/VALIDATION.md).
* For S6 flash the `MAX_ACTIVE=64` variant, for the measurement at block 256 the
  `BLOCK=256` variant.

## Measured quantities

| Quantity | Meaning |
| --- | --- |
| Matrix cycles | DWT cycles for `ReadHardware` + `Tick` + all `Apply`/`ApplyVoice` of one block (480 cycles = 1 µs at 480 MHz) |
| Callback load | `daisy::CpuLoadMeter`, mean and peak, in 0.01 % |
| Overruns | Callbacks that take longer than the block budget |
| MIDI lost | Gaps in the CC3 test stream |
| Main max | Maximum duration of one `HandleMidi` pass or one `Service()` in cycles; interruptions by the audio IRQ are included |

## CC fallback

If SysEx sent by the pinned libDaisy does not arrive at the computer (H3):

* **Request**: CC119 = 1 on channel 16.
* **Reply**: 20 CCs CC100–119 on channel 16. Multi-byte values are split into 7 bits each,
  least significant first.

| CC | Content |
| --- | --- |
| 100 | Format 1 |
| 101–103 | Matrix cycles max |
| 104–106 | Matrix cycles mean |
| 107–109 | Callback load max (0.01 %) |
| 110–112 | Overruns |
| 113 | Scenario |
| 114–116 | Callback load mean (0.01 %) |
| 117 | MIDI lost (max. 127) |
| 119 | 64 = end |
