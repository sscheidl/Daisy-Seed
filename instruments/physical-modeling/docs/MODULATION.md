# Physical Modeling + Mod on Daisy (separate build)

**Image:** `build/mod/PhysicalModelingMod-v0.1.0.bin`
(`make PROJECT=physical-modeling-mod`). Firmware marker: `PHYSICAL-MODELING-MOD-VERSION`.
**Status:** software tested, hardware test pending.

This is an **additional, separate build** of the Rings/Torus instrument with the
[Mod on Daisy](../../../modules/mod-on-daisy/) matrix. The regular firmware stays
as it is:

* `make PROJECT=physical-modeling` still compiles exactly the v0.1.0 sources. Its
  image is **byte-identical** to the one built from `main` before this change
  (137,784 bytes, compared after a clean rebuild).
* All matrix code sits behind `PHYSICAL_MODELING_MOD` and `TORUS_VOICE_MODULATION`.
  Only `MOD=1` sets them, and that build uses its own `build/mod/` directory.
* With an empty matrix the MOD build sounds bit-identical to v0.1.0. A golden test
  runs 4,000 blocks of keys, pots, model switches, USB notes, CCs and pitch bend and
  gets the same output hash `37DE0BF9D93B81CE` for both builds
  (`tests/test_golden.cpp`).

## Columns

Columns 0–5 and 7 are the instrument parameters of the same index (same as CC20–25
and CC27). Column 6 is the output level since the first hardware test (23.10.2026):
the reverb decay it replaced had little audible effect.

| Column | Target | Path | Depth 100 means |
| --- | --- | --- | --- |
| 0 | Pitch (Frequency) | global **and per voice** | ±12 semitones |
| 1 | Structure | global and per voice | full range |
| 2 | Brightness | global and per voice | full range |
| 3 | Damping | global and per voice | full range |
| 4 | Position | global and per voice | full range |
| 5 | Excitation | global, latched at the strike | full range; Poly AT greyed out |
| 6 | **Level** (output gain, smoothed 5 ms) | global | ±100 % of unity: gain 0 (silence) … 2 (+6 dB, into the soft limiter); exactly 1 with an empty matrix |
| 7 | Dry/Wet | global | full range |

* **Global offsets** go through the existing 5 ms parameter smoothing. The pot or CC
  value remains the base, so a pot can be moved while it is modulated.
* **Poly AT per voice** works on all four Torus voices:
  * Each voice gets its own patch (Structure, Brightness, Damping, Position) and
    its own pitch offset.
  * The resonators recompute their filters for every 12-sample quantum anyway, so
    this costs no extra filter work.
  * Pressure is tracked per MIDI note. A voice takes the pressure of the note it
    plays. After Note Off the voice keeps ringing and glides back within 10 ms.
* Poly AT on Level and Dry/Wet uses the most recent held note (last note).
* **Base values for the editor**: this build reports each column's pot/CC position
  before modulation in the monitor. Level reports its fixed unity point 0.5 (gain 1);
  it is not a pot reading, pot 7 still sets the reverb decay. The editors show a tick
  at the base value and the range the modulation covers, so it is visible when a
  modulation runs into 0 or 1. Requested in the hardware test of 23.10.2026.
* **Extensions (mod3, correction order of 24.10.2026)**: built with
  `MOD_ON_DAISY_EXTENSIONS=1` and switched on in `InitModulation()` with
  `EnableExtensions({true, true})`:
  * extended monitor (89 bytes): per-source min/max over the monitor window, clock
    activity and the last received aftertouch kind/note/value/age;
  * LFO cross modulation: each LFO's rate can follow the other LFO (`fm`, ±2 octaves,
    free running only); patches are format 2 (111 bytes).
  See PROTOCOL.md § 3.6. IDENTIFY capabilities `0x7A`. Nothing changes in the audio path
  with `fm = 0`; the golden hash of the MOD build with an empty matrix is unchanged.
* **Active model (mod4, hardware test of 24.10.2026)**: the build reports which of the six
  models sounds and whether the last change came from SW17/SW18 or from CC28, and names the
  models with their switch positions (PROTOCOL.md § 3.6, `Extensions::modes`). The editors
  show it in the connection area. Nothing changes in the audio path.
* IDENTIFY reports the version `0.1.0-mod4` (mod2: Level column and base values; mod3:
  extensions; mod4: active model). The image name stays `PhysicalModelingMod-v0.1.0.bin`; the regular build
  is unchanged.
* `OFFEN`: In the chord models, Structure selects the chord for the whole part.
  A per-voice Structure there changes only the resonator settings, not the chord.
  Whether that is musically useful needs listening.

## Changed sources (all guarded)

| File | MOD=1 only |
| --- | --- |
| `torus/dsp/part.h/.cpp` | `set_voice_modulation()`: per-voice `Patch` and pitch offset in the voice loop; the `#else` branch is the original code |
| `src/parameter_manager.h` | `SetModOffset()`: offset added before the 5 ms smoothing |
| `src/physical_modeling.*` | `SetVoiceModulation()`: builds the four voice patches per quantum |
| `src/main.cpp` | module, `Modulate()` in the callback before the pots, SysEx/CC1/aftertouch in `ServiceMidi()`, `Service()` in the main loop |
| `src/modulation.h` | column table (new file) |

MIDI: CC1 and channel and polyphonic aftertouch are consumed by the matrix.
CC20–33, pitch bend, notes and the CPU telemetry on channel 16 CC119 are unchanged.

## Resources (measured, ARM build)

| | Default | MOD=1 |
| --- | ---: | ---: |
| BIN | 137,784 B | 170,312 B (mod3, 24.10.2026) |
| DTCM static | 107,792 B (reserve 23,280) | 110,352 B (reserve 20,720, mod3) |

`scripts/check-physical-elf.py` passes for both (≥ 8 KiB DTCM reserve). Use the
existing CPU telemetry (`tools/cpu_probe.py`, channel 16 CC119) to measure the
callback with and without modulation.

## Hardware test

`P` = `python modules/mod-on-daisy/tools/mod_probe.py`. Studio One closed or with
the port disabled.

| Nr. | Step | Expected | Result |
| --- | --- | --- | --- |
| R1 | Flash, play keys, pots and model switches without a PC | exactly as v0.1.0 | |
| R2 | `P identify`, `P matrix` | `physical-modeling`, `0.1.0-mod3`, caps `0x7A`; Poly AT under Excitation greyed | |
| R3 | `P lengths` | `frame lengths OK` | |
| R4 | `P set-cell LFO1 2 40`, `P source LFO1 rate 9000` | brightness sweeps at about 0.9 Hz without zipper noise | |
| R5 | `P set-cell MODWHEEL 7 100` | mod wheel opens Dry/Wet | |
| R6 | Poly AT keyboard: `P set-cell POLYAT 3 -80`, hold a chord, press one note | only that voice rings longer or shorter | |
| R7 | `P set-cell POLYAT 0 20` | pressed note bends up to 2.4 semitones; others stay | |
| R8 | `P source LFO1 sync 1` with MIDI clock from Studio One | LFO in tempo | |
| R9 | CPU: `python instruments/physical-modeling/tools/cpu_probe.py` with an empty matrix and with 8 connections | difference is the matrix share; no overruns | |
| R10 | `P clear` | sound exactly as before | |
