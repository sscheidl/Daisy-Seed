# Hardware test — Toy Piano v0.3 (Mod on Daisy)

Image: `ToyPiano-v0.3.bin` (check the SHA-256 against `SHA256SUMS`). Setup: Seed3
Desktop Kit on J7, Windows PC with `pip install mido python-rtmidi`, Studio One
**closed** or with the "Daisy Seed External" port disabled.
`P` below stands for `python modules/mod-on-daisy/tools/mod_probe.py`.

| Nr. | Step | Expected | Result |
| --- | --- | --- | --- |
| T1 | Flash and play S1–S16 and VR1/VR2 without a PC | exactly as v0.2: notes, LEDs, attack, decay | |
| T2 | `P identify` | `toy-piano`, version `0.3`, `max_active 8`, `tick_hz 1000` | |
| T3 | `P matrix` | columns Pitch, Level, Harmonic, Attack, Decay; 5–7 greyed (░); Poly AT under Attack/Decay greyed | |
| T4 | `P lengths` | `frame lengths OK` (SysEx in both directions, TEST_PLAN H2/H3) | |
| T5 | `P set-cell LFO1 0 30`, hold S1 with a long decay | vibrato of about ±0.6 semitones; `P source LFO1 rate 12000` makes it faster | |
| T6 | `P source LFO1 shape 5` (S&H), then `shape 4` (square) | stepped pitch, then a trill; no clicks | |
| T7 | `P clear` | vibrato stops at once; sound as in T1 | |
| T8 | `P set-cell MODWHEEL 1 -100`, move the mod wheel | volume follows the wheel down to silence without zipper noise | |
| T9 | From a keyboard with polyphonic aftertouch (or Studio One, poly pressure): `P set-cell POLYAT 1 80`, hold two notes and press one | only the pressed note gets louder | |
| T10 | `P set-cell CHAT 3 100`, press a key with channel aftertouch held | the next strike has a long attack; sounding notes unchanged | |
| T11 | `P set-cell CV1 0 50` with a CV source on CV 1 (or a patch cable from a DC source) | pitch follows CV 1; with the jack unplugged there is no audible wobble | |
| T12 | `P source LFO1 sync 1` with Studio One sending MIDI clock (enable it there, then close the probe) | LFO follows the tempo; Start restarts it on the bar | |
| T13 | `P source LFO1 sync 2`, clock pulses on Gate 1 | one LFO cycle per pulse | |
| T14 | `P monitor --seconds 10` with the CV jacks open | CV raw mean and noise (std) for the CV calibration (TEST_PLAN H5) | |
| T15 | Power cycle | matrix empty again (no storage yet), sound as in T1 | |

Please note the results in the table or send the console output; they go into
[VALIDATION.md](VALIDATION.md) and `modules/mod-on-daisy/docs/VALIDATION.md`.
