# Parameter contract and CC plan (Acid Seq)

**As of:** 26 October 2026 · **Status:** contract from phase 0. Machine-readable
source: [`contracts/parameters.json`](../contracts/parameters.json). These tables
are generated from it; the contract test checks that both match.

## 1. Rules

* **Stable IDs:** each component numbers its parameters from 1 (`u14`). An ID
  keeps its meaning forever. New parameters get new IDs; dropped IDs
  are not reassigned. An incompatible change increases the schema version
  of the component.
* **Values** travel as `i32` in thousandths of the unit (PROTOCOL.md § 2). Enum and
  bool values are integers (`2000` = index 2).
* **Descriptors:** each firmware reports exactly these entries for its components
  via `GET_PARAM_DESC` (type, unit, flags, min, max, default, CC, key). The
  component tests compare their C++ tables with `parameters.json`.
* **CC plan:** CCs for the voice and the effects apply on the **voice channel**
  (`step-seq.voice_channel`, default 2). The sequencer itself has no CCs; its
  pots and switches belong to the hardware.
  * Mapping: continuous linear or logarithmic (flag) over min…max with
    CC 0…127. Enum: index = ⌊CC × count / 128⌋. Bool: CC ≥ 64 is on.
  * Assigned numbers: 5, 7, 70–71, 74–79 (voice, following the
    sound controller convention), 20–27 (Drive), 102–109 (Delay). No CC is
    assigned twice. CC 120, 121 and 123 remain reserved for their standard meanings
    (All Sound Off, Reset All Controllers, All Notes Off).
* **Pots versus panel:** the last change wins. A pot only writes once
  it moves beyond its hysteresis (4.1 of the work order).
* **Read-only:** `play_mode` follows SW18. The panel shows it but cannot
  set it (D3).

## 2. Parameters

### `step-seq` (schema 1)

| ID | Key | Label | Type | Unit | Range | Default | CC | Note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `tempo` | Tempo | float | BPM | 30 … 300 | 120 | — | VR5 |
| 2 | `swing` | Swing | float | % | 50 … 75 | 50 | — | VR6 |
| 3 | `length` | Length | int | — | 1 … 16 | 16 | — | VR7 |
| 4 | `transpose` | Transpose | int | semitones | −12 … 12 | 0 | — | VR8 |
| 5 | `play_mode` | Mode | enum | — | 0 … 2 | 0 | — | SW18; read-only; 0 Forward, 1 Reverse, 2 Random |
| 6 | `scale` | Scale | enum | — | 0 … 2 | 0 | — | 0 Chromatic, 1 Major, 2 Minor |
| 7 | `clock_source` | Clock source | enum | — | 0 … 3 | 0 | — | 0 Internal, 1 Gate IN 1, 2 MIDI, 3 Auto |
| 8 | `clock_divider` | Clock divider | enum | — | 0 … 3 | 0 | — | 0 1 per step, 1 2 per quarter, 2 4 per quarter, 3 24 per quarter |
| 9 | `gate_in_2_mode` | Gate IN 2 | enum | — | 0 … 1 | 0 | — | 0 Reset, 1 Run |
| 10 | `cv_in_1_mode` | CV IN 1 | enum | — | 0 … 1 | 0 | — | 0 Transpose, 1 Off |
| 11 | `cv_in_2_mode` | CV IN 2 | enum | — | 0 … 2 | 0 | — | 0 Off, 1 Gate length, 2 Swing |
| 12 | `seq_channel` | Seq channel | int | channel | 1 … 16 | 1 | — |  |
| 13 | `voice_channel` | Voice channel | int | channel | 1 … 16 | 2 | — |  |
| 14 | `usb_out` | USB out | bool | — | 0 … 1 | 1 | — |  |
| 15 | `din_out` | DIN out | bool | — | 0 … 1 | 1 | — |  |
| 16 | `accent_velocity` | Accent velocity | int | velocity | 1 … 127 | 127 | — |  |
| 17 | `velocity` | Velocity | int | velocity | 1 … 127 | 100 | — |  |
| 18 | `glide_time` | Glide time | float | ms | 10 … 500 | 60 | — |  |
| 19 | `clock_out` | Send clock | bool | — | 0 … 1 | 1 | — |  |

### `acid-303` (schema 1)

| ID | Key | Label | Type | Unit | Range | Default | CC | Note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `waveform` | Waveform | enum | — | 0 … 1 | 0 | 70 | 0 Saw, 1 Square |
| 2 | `tune` | Tuning | int | semitones | −12 … 12 | 0 | 78 |  |
| 3 | `fine` | Fine | float | cent | −100 … 100 | 0 | 79 |  |
| 4 | `cutoff` | Cutoff | float | Hz | 40 … 12000 | 800 | 74 | logarithmic |
| 5 | `resonance` | Resonance | float | % | 0 … 100 | 50 | 71 |  |
| 6 | `env_mod` | Env Mod | float | % | 0 … 100 | 50 | 76 |  |
| 7 | `decay` | Decay | float | ms | 30 … 3000 | 300 | 75 | logarithmic |
| 8 | `accent` | Accent | float | % | 0 … 100 | 50 | 77 |  |
| 9 | `volume` | Volume | float | % | 0 … 100 | 80 | 7 |  |
| 10 | `slide_time` | Slide time | float | ms | 10 … 500 | 60 | 5 |  |
| 11 | `slide_mode` | Slide mode | enum | — | 0 … 2 | 1 | — | 0 Off, 1 Legato, 2 Always |

### `drive` (schema 1)

| ID | Key | Label | Type | Unit | Range | Default | CC | Note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `enabled` | Drive on | bool | — | 0 … 1 | 1 | 20 |  |
| 2 | `curve` | Curve | enum | — | 0 … 3 | 0 | 21 | 0 Soft, 1 Hard, 2 Tape, 3 Fold |
| 3 | `drive` | Drive | float | % | 0 … 100 | 25 | 22 |  |
| 4 | `tone` | Tone | float | % | 0 … 100 | 70 | 23 |  |
| 5 | `level` | Level | float | dB | −24 … 12 | 0 | 24 |  |
| 6 | `auto_level` | Auto-Level | bool | — | 0 … 1 | 1 | 25 |  |
| 7 | `compensation` | Compensation | float | % | 0 … 100 | 100 | 26 |  |
| 8 | `target_offset` | Target offset | float | dB | −3 … 0 | −1.5 | 27 |  |

### `stereo-delay` (schema 1)

| ID | Key | Label | Type | Unit | Range | Default | CC | Note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `enabled` | Delay on | bool | — | 0 … 1 | 1 | 102 |  |
| 2 | `mode` | Mode | enum | — | 0 … 1 | 0 | 103 | 0 Ping-Pong, 1 Stereo L/R |
| 3 | `time` | Time | float | ms | 10 … 2000 | 375 | 104 | logarithmic |
| 4 | `sync` | Sync | bool | — | 0 … 1 | 1 | 105 |  |
| 5 | `note_value` | Note value | enum | — | 0 … 17 | 8 | 106 | 0 1/32, 1 1/32 T, 2 1/32 d, 3 1/16, 4 1/16 T, 5 1/16 d, 6 1/8, 7 1/8 T, 8 1/8 d, 9 1/4, 10 1/4 T, 11 1/4 d, 12 1/2, 13 1/2 T, 14 1/2 d, 15 1/1, 16 1/1 T, 17 1/1 d |
| 6 | `feedback` | Feedback | float | % | 0 … 95 | 40 | 107 |  |
| 7 | `mix` | Dry/Wet | float | % | 0 … 100 | 25 | 108 |  |
| 8 | `damping` | Damping | float | Hz | 1000 … 20000 | 6000 | 109 | logarithmic |
