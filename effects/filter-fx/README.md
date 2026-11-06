# Filter FX

Stereo filter effect for the kit's audio input: Drive into the multimode
[filter core](../../modules/filter-core/), with an LFO and an envelope follower on the cutoff.
Wet-only core under FX contract 1.0, own firmware `filter-fx`.

| Status | |
| --- | --- |
| Version | Core and firmware 0.1.0 (`src/version.h`) |
| Software | Host, sanitizer and ARM checks passed (see CHANGELOG) |
| Hardware | NOT MEASURED, [hardware test](docs/HARDWARE_TEST.md) |
| Control mapping | **Proposal**, not yet confirmed by the owner |

## Controls (firmware `filter-fx`)

| Element | Function |
| --- | --- |
| VR1 | Cutoff, 33 Hz to 19.9 kHz, linear in notes |
| VR2 | Resonance. Self-oscillation from about 91 % |
| VR3 | Drive 0–100 %. At 0 the Drive stage is out of the path |
| VR4 | Mix, dry to wet |
| VR5 | LFO rate 0.05–20 Hz, logarithmic |
| VR6 | LFO depth, up to ±48 semitones |
| VR7 | Envelope amount, −48 to +48 semitones, centre off |
| VR8 | Envelope release 10 ms – 2 s, logarithmic |
| S1–S8 | Mode: LP 6, 12, 18, 24 dB, BP, HP, Notch, Phaser (LED D1–D8) |
| S10–S13 | LFO shape: sine, triangle, saw, sample & hold (LED D10–D13) |
| S14 | Stereo LFO on/off: right channel a quarter cycle later (LED D14) |
| SW17 | Effect on/off, 10 ms crossfade (LED D15). Off = exactly the dry signal |
| SW18 | Bass compensation: up full, middle half, down off |
| S9 + S16 (5 s) | Bootloader |

Input: stereo. A mono signal in the left input feeds both channels. MIDI CC (omni, USB and
DIN) and SQ parameters over USB, per [the contract](contracts/parameters.json). Pots and
switches take effect when they are moved. Until then the value last set via CC or panel
applies. D16 indicates an overrun of the callback deadline.

| Parameter | CC | Parameter | CC |
| --- | --- | --- | --- |
| `enabled` | 20 | `lfo_depth` | 25 |
| `mode` | 21 | `lfo_shape` | 26 |
| `cutoff` | 74 | `lfo_stereo` | 27 |
| `resonance` | 71 | `env_amount` | 28 |
| `drive` | 22 | `env_release` | 29 |
| `mix` | 23 | `bass_comp` | 30 |
| `lfo_rate` | 24 | | |

The control app shows a Filter FX page in the browser panel, JUCE app and VST3 from the same
contract.

## Signal path

```text
input ─┬─ follower ─────────────┐
       │                        ▼ cutoff
       └─ Drive ─ ladder L/R ─ wet ceiling ─ dry/wet ─ on/off fade ─ clamp ─ output
                     ▲ cutoff
                    LFO
```

* **Reused:** Drive is the unchanged `effects/drive` component (Soft curve, tone fully open,
  auto level on), so more drive adds harmonics rather than level. `DriveMixer` fades it in
  and does the on/off fade. LFO shapes come from `mod_on_daisy/sources.h`, `fast-math` does
  the conversions, and the Step Seq services do controls, SQ and queues.
* **Envelope:** peak follower of the dry input, 5 ms attack, linear in dB from −48 to 0 dBFS.
* **Wet ceiling:** the core's limiter curve above 0.75, with a ceiling of exactly 1. Below 0.75
  the signal is untouched. It catches the resonance of a loud input, which the hard clamp
  behind it would turn into clipping.

## Build and test

```bash
make PROJECT=filter-fx -j4                # build/FilterFx-v0.1.0.bin
make PROJECT=filter-fx test               # core and firmware on fakes
make PROJECT=filter-fx test-sanitize
make test-fx                              # shared FX contract test
python3 scripts/check-firmware.py effects/filter-fx/build/FilterFx-v0.1.0.bin
python3 scripts/check-elf-placement.py effects/filter-fx/build/FilterFx-v0.1.0.elf
python3 modules/filter-core/tools/arm_cost.py effects/filter-fx/tools/cost_probe.cpp -Ieffects/filter-fx/include
```

## Callback load

Static estimate from the ARM compiler, **not a kit measurement** (2026-11-06). Budget:
10 000 cycles per sample at 480 MHz and 48 kHz.

| Stage | Cycles per stereo sample |
| --- | --- |
| Two ladders, ramped | 220 to 370 |
| Wet ceiling | 39 to 62 |
| Dry/wet and on/off fade | 48 to 96 |
| Follower and output clamp (estimated by hand) | 30 to 60 |
| **Drive off** | **about 340 to 590: 3.4 to 5.9 %** |
| Drive stage | 259 to 486 |
| **Drive on** | **about 600 to 1080: 6 to 11 %** |

Controls and the block-rate work come on top, as in `drive-fx`. The real figure is
`callback_peak_cycles` from `sq_probe.py stats`, see the [hardware test](docs/HARDWARE_TEST.md).

Static memory from the ELF check (MEASURED 2026-11-06 on the build machine, not a hardware
value): DTCM 108 344 / 131 072 bytes, static reserve 22 728 bytes, SRAM 156 172 bytes.
