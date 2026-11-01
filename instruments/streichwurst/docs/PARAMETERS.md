# Streichwurst: parameter contract and CC plan

As of 01.11.2026, schema 1 of all three components. Machine-readable source:
[contracts/parameters.json](../contracts/parameters.json). From it,
`sequencers/step-seq/tools/gen_params.py` generates the firmware table
`include/streichwurst/param_table.h` as well as the labels of both editors
(`--check` verifies that everything matches).

## Rules

* **One register for everything:** pots and buttons on the kit, MIDI CC, SQ protocol (control app) and
  the Mod on Daisy matrix access the same values (`src/register.h`). The matrix
  only adds offsets to eight targets; the register value itself is never modulated.
* **Stable IDs** per component from 1, values as `i32` in thousandths of the unit (SQ § 2).
  Dropped IDs are not reassigned; incompatible changes increase the schema.
* **CCs** apply on the configured MIDI channel (`midi_channel`, 0 = omni). Mapping as
  in Acid Seq: continuous linear or logarithmic over min…max, enum/int = ⌊CC·count/128⌋,
  bool = CC ≥ 64. Not assigned: CC1 (Mod on Daisy), CC64 (sustain), CC120/123 (All Sound/
  Notes Off). Used are the numbers 20–31, 85–87 and 102–109, which are undefined in the MIDI
  specification, as well as CC7 (Master) and CC8 (Balance). **Own plan**, not
  a copy of a manufacturer assignment.
* **The last change wins** (pot, CC or app). A pot only takes over once it crosses the
  stored value or comes within 2 % of it (soft takeover, [CONTROLS.md](CONTROLS.md)).
* **Read-only:** `pot_page` and `preset` are state on the kit, not part of a patch.
* Additional controls (Animate/Phaser rate, Reverb decay, velocity, tuning) are
  **own parameters**, not claimed original knobs.

## Instrument `streichwurst-instrument` (schema 1)

| ID | Key | Label | Type | Unit | Range | Default | CC | Page / control |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `registration` | Registration | float | % | 0 … 100 | 50 | 20 | strings · VR1 Main |
| 2 | `octave` | Octaves | enum | — | 0 … 2 (0 Base, 1 Both, 2 Upper) | 0 | 21 | strings · S10 |
| 3 | `crescendo` | Crescendo | float | % | 0 … 100 | 20 | 22 | strings · VR2 Main |
| 4 | `release` | Release (strings) | float | % | 0 … 100 | 45 | 23 | strings · VR3 Main |
| 5 | `solo_tone` | Solo Tone | float | % | 0 … 100 | 50 | 24 | solo · VR1 Solo |
| 6 | `solo_attack` | Solo Attack | float | % | 0 … 100 | 10 | 25 | solo · VR2 Solo |
| 7 | `solo_release` | Solo Release | float | % | 0 … 100 | 30 | 26 | solo · VR3 Solo |
| 8 | `solo_sus_mode` | Solo sustain mode | enum | — | 0 … 2 (0 Decay to level, 1 Hold, 2 Percussive) | 0 | 27 | solo · S13 |
| 9 | `solo_sustain` | Solo Sustain | float | % | 0 … 100 | 70 | 28 | solo · VR6 Solo |
| 10 | `solo_trem_depth` | Tremolo depth | float | % | 0 … 100 | 0 | 29 | solo · VR4 Solo |
| 11 | `solo_trem_rate` | Tremolo rate | float | Hz | 0.5 … 12 | 5 | 30 | solo · VR5 Solo · logarithmic |
| 12 | `split_mode` | Layer / Split | enum | — | 0 … 1 (0 Layer, 1 Split) | 0 | 31 | keyboard · S14 |
| 13 | `split_key` | Split key (MIDI note) | int | — | 0 … 127 | 60 | 85 | keyboard · VR7 Solo |
| 14 | `string_velocity` | Velocity (strings) | float | % | 0 … 100 | 0 | 86 | strings |
| 15 | `solo_velocity` | Velocity (solo) | float | % | 0 … 100 | 100 | 87 | solo |
| 16 | `bend_range` | Bend range | int | semitones | 0 … 12 | 2 | — | keyboard |
| 17 | `transpose` | Transpose | int | semitones | -24 … 24 | 0 | — | keyboard |
| 18 | `tune` | Tune | float | cent | -50 … 50 | 0 | — | keyboard |

## FX `streichwurst-fx` (schema 1)

| ID | Key | Label | Type | Unit | Range | Default | CC | Page / control |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `animate` | Animate | float | % | 0 … 100 | 0 | 102 | fx · VR5 Main, VR1 FX |
| 2 | `animate_rate` | Animate rate (own) | float | Hz | 0.05 … 5 | 0.5 | 103 | fx · VR2 FX · logarithmic |
| 3 | `ensemble_on` | Ensemble on/off | bool | — | 0 … 1 | 1 | 104 | fx · S12 |
| 4 | `ensemble_mode` | Ensemble mode | enum | — | 0 … 2 (0 Mode 0 (3 taps), 1 Mode 1 (dry + 5), 2 Mode 2 (dry + 3)) | 1 | 105 | fx · S11 |
| 5 | `phaser` | Phaser | float | % | 0 … 100 | 0 | 106 | fx · VR6 Main, VR3 FX |
| 6 | `phaser_rate` | Phaser rate (own) | float | Hz | 0.05 … 5 | 0.4 | 107 | fx · VR4 FX · logarithmic |
| 7 | `reverb` | Reverb | float | % | 0 … 100 | 20 | 108 | fx · VR7 Main, VR5 FX |
| 8 | `reverb_decay` | Reverb decay (own) | float | % | 0 … 100 | 50 | 109 | fx · VR6 FX |

## Product `streichwurst` (SQ component kind 4) (schema 1)

| ID | Key | Label | Type | Unit | Range | Default | CC | Page / control |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `balance` | Balance strings/solo | float | % | 0 … 100 | 50 | 8 | global · VR4 Main, VR8 FX |
| 2 | `master` | Master | float | % | 0 … 100 | 80 | 7 | global · VR8 Main/Solo, VR7 FX |
| 3 | `midi_channel` | MIDI channel (0 = omni) | int | channel | 0 … 16 | 0 | — | global |
| 4 | `pot_page` | Pot page on the kit | enum | — | 0 … 2 (0 Main, 1 Solo, 2 FX) | 0 | — | global · S9 · read-only |
| 5 | `preset` | RAM preset (last recalled) | int | — | 0 … 3 | 0 | — | global · S15 · read-only |

## Modulation matrix (Mod on Daisy, own selection)

Eight columns, 1 kHz control rate (`src/mod_targets.h`). Each column adds an offset to the
normalised base setting of the register (0…1). An empty matrix leaves the sound bit-exactly
unchanged (product and firmware test).

| Column | Target | Depth 100 |
| --- | --- | --- |
| 1 | Registration | ±100 % |
| 2 | Animate | ±100 % |
| 3 | Phaser | ±100 % |
| 4 | Reverb | ±100 % |
| 5 | Balance | ±100 % |
| 6 | Solo Tone | ±100 % |
| 7 | Tremolo depth | ±100 % |
| 8 | Pitch | ±1 semitone |

Sources: LFO 1/2, Random, CV 1/2, mod wheel (CC1), channel and poly aftertouch, as in all
Mod on Daisy firmwares. A general runtime loader or new sources are not part of it.
