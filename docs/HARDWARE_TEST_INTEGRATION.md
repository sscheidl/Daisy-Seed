# Hardware test: integration of bootloader + Toy Piano Ambient + Shimmer

Integration branch `integration/shimmer-bootloader` (PR #40, #38, #41). The human takes care of flashing, listening,
buttons, switches and cables; the agent only enters what was reported.
All results are **NOT MEASURED** until they are checked on the kit (then
**MEASURED** with the date). The previous BINs (ToyPianoAmbient v0.1.1, AmbientReverb
v0.3) are kept unchanged.

| Image | Bytes | SHA-256 |
| --- | --- | --- |
| `ToyPianoAmbient-v0.1.2.bin` | 184220 | `0ecfb4f9f44a7a8b51fafe1a7b1ec9d81cb1dce420b7ce46cbeecfc00f9753e0` |
| `AmbientReverb-v0.3.1.bin` | 145748 | `3a19183d3474b00979ae2578ee8744865ff2aa0559f4df4b1de658503568ea62` |

Check beforehand: SHA-256 of the BIN to be flashed against the table. The first flash
still needs RESET + BOOT, because the running firmware does not know the entry.
Start with a low listening level.

## Toy Piano + Ambient v0.1.2

| # | Test | Expectation | Result |
| --- | --- | --- | --- |
| P1 | VR6 fully left, S1–S16 one at a time | 16 notes C4…C6 as in Toy Piano, LED follows the key | NOT MEASURED |
| P2 | VR1 transposition, VR2 decay | ±12 semitones, centre = original pitch; decay 50–3000 ms | NOT MEASURED |
| P3 | Six switch positions (SW17 A/B × SW18 up/centre/down) | Cloud, Halo, Scatter, Abyss, Bloom, Shimmer; note the switch direction | NOT MEASURED |
| P4 | Shimmer (B/down), VR7 high, VR3 long | Rising octaves in the tail, no runaway build-up | NOT MEASURED |
| P5 | VR8 Stretch at both ends | Audible difference, no clicks | NOT MEASURED |
| P6 | Fast strikes and MIDI chords | No dropouts; `mod_probe.py stats`: note load, overruns | NOT MEASURED |
| P7 | Connect the editor | `toy-piano-ambient 0.1.2`, 8 columns, mode display | NOT MEASURED |

## Ambient Reverb v0.3.1 (pure FX firmware)

| # | Test | Expectation | Result |
| --- | --- | --- | --- |
| R1 | S1–S6 | Cloud, Halo, Scatter, Abyss, Bloom, **Shimmer (S6, LED D6)** | NOT MEASURED |
| R2 | S15 bypass, S16 sustain | as before | NOT MEASURED |
| R3 | USB on J7 | Appears as "Daisy Seed External" (USB MIDI); sound unchanged, also when unplugging/replugging | NOT MEASURED |

## MIDI

| # | Test | Expectation | Result |
| --- | --- | --- | --- |
| M1 | Notes from a DAW to Toy Piano Ambient | Plays with velocity; keys send notes | NOT MEASURED |
| M2 | Studio One with input monitoring | No reply feedback loop (Mod on Daisy and bootloader) | NOT MEASURED |

## Bootloader (both images)

| # | Test | Expectation | Result |
| --- | --- | --- | --- |
| B1 | S9 + S16 alone for 5 s | All 16 LEDs on, DFU device `0483:DF11` appears, Web Programmer connects | NOT MEASURED |
| B2 | S9 + S16 + third button for 7 s; S9 + S16 for 4 s, release | No entry | NOT MEASURED |
| B3 | **Firmware update …** in the editor | "Request accepted", then DFU device; bootloader waits without a time limit (wait 60 s, then flash) | NOT MEASURED |
| B4 | After flashing | New firmware starts, LEDs off | NOT MEASURED |
| B5 | RESET + BOOT | Rescue path still works | NOT MEASURED |

Explicitly unchecked until the manual test: hardware sound, CPU load,
stack high-water mark (AmbientReverb v0.3.1: only 10488 B static DTCM reserve left),
switch direction and actual DFU behaviour.
