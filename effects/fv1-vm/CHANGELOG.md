# Changelog: fv1-vm

## Interpreter fix (2026-11-05, unreleased)

* `SKP 0,n` with n > 0 is an unconditional jump; it was treated as a no-op. `SKP 0,0` stays the NOP. Found by review on
  the PR; regression test added. Programs without such a skip are unaffected.

## toy-piano-fv1 0.8.0 (2026-11-05, unreleased)

* `FxVar`: 8 zero crossings, cutoff 0.87, Kaiser beta 7; precomputed unity-gain rows on the up side, a full symmetric
  table on the down side, no per-tap range check, absolute value or normalisation. Reason (MEASURED 2026-11-05, 0.7.1):
  at 32 kHz about 30 % of the block budget was fixed cost against about 15 % with `Fx48`, and the heaviest programs
  overran with 16 voices (peak up to 108.8 %).
* `Fv1Host` including the delay RAM moved from SDRAM to AXI SRAM (`ld/toy_piano_axi.lds`).
* Correction: the 0.7.1 entry below blames VR1. The later status-packet measurements refute that; the difference
  was the VR8 rate. The 0.7.1 change itself is harmless and stays.

## toy-piano-fv1 0.7.1 (2026-11-05, unreleased)

* Transposition without an active matrix no longer runs the per-voice matrix path. With VR1 off centre that path
  cost about 28 % of the block budget without a sounding voice (MEASURED 2026-11-05 on 0.7.0: REV 1 about 81 % with
  VR1 off centre, 53 % centred) and overloaded the heaviest programs: avg 100 %, peak 109 %, about 1760 overruns with
  16 voices. Also in the regular Toy Piano 0.5 (not on the kit yet).

## toy-piano-fv1 0.7.0 (2026-11-05, unreleased)

* Status packet after each telemetry reply (channel-16 CC80..89): internal rate, VR1 transposition, dry/wet, bypass
  (`fv1_fw::Status`, `EncodeStatus`, host-tested). The control page shows these live values under "Now".

## toy-piano-fv1 0.6.1 (2026-11-05, unreleased)

* `FxVar` hot loops in float with 32-bit ring indices: per-tap double arithmetic and 64-bit conversions (library
  calls on the Cortex-M7) moved to once per sample. Same response (tests unchanged). Correction: the board drop from
  81 % to 54 % first credited to this change came from VR1 being centred in the second reading (see 0.7.1); the
  effect of this change alone on the board is NOT MEASURED.

## toy-piano-fv1 0.6.0 (2026-11-05, unreleased)

* DIN MIDI in, handled like USB (notes, Program Change/Bank Select, CC1, aftertouch, channel-16 CCs); SysEx only
  over USB; separate Bank Select/CC22 state per input. Shared with the regular Toy Piano 0.5.

## Component addition, toy-piano-fv1 0.5.0 (2026-11-05, unreleased)

* `fv1vm/fv1_fxvar.h`: `FxVar<T>`, variable-rate 48 kHz adapter (8–32 kHz internal rate, band-limited fractional
  resamplers with a fixed down lead so that rate glides keep the up side in step); `tests/test_fxvar.cpp`.
* toy-piano-fv1 0.5.0 (owner decision): VR1 piano transposition ±24 semitones with centre detent, VR2 attack,
  VR3 decay, VR4..VR6 FV-1 POT0..POT2, VR7 dry/wet, VR8 rate (32 kHz left, 8 kHz right), SW17 bypass.

## Firmware 0.3.0, toy-piano-fv1 0.4.0 (2026-11-05, unreleased)

* Program groups: `gen_programs.py --groups` writes a group table (offset and size per group) into the local header
  and the program list; MIDI Bank Select = group, Program Change = position; out-of-range requests are ignored.
  Without groups the flat bank × 128 + program mapping stays.
* Standalone: S3/S4 jump to the previous/next group; LEDs S1..S8 show the position, S9..S12 the group.
* Control page: groups as chips and list headers, codes such as `REV 32`, search by code or `#` catalogue number.

## toy-piano-fv1 0.3.0 (2026-11-05, unreleased)

* Piano first (owner decision): VR1 attack and VR2 decay as in the regular Toy Piano, VR3..VR5 = FV-1
  POT0..POT2, VR6 dry/wet, SW17 FX bypass (exact dry piano). Firmware fv1-vm-fx stays 0.2.0.

## Firmware 0.2.0, toy-piano-fv1 0.2.0 (2026-11-05, unreleased; component stays 0.1.0)

* Standard MIDI program selection in both firmwares: Program Change on any channel with Bank Select CC0/CC32
  (bank × 128 + program).
* Local Web-MIDI control page `tools/control/fv1vm_control.html` (program list, selection, CPU telemetry,
  standalone remote controls); `gen_programs.py --names` writes its local, git-ignored program list.
* `fv1vm_probe.py --notes N` for instrument products.
* Board CPU sweeps of 0.1.0 recorded in the README (no overrun in either product).

## 0.1.0 (2026-11-05, unreleased)

* Header-only FV-1 interpreter `seed::fx::fv1::Vm<T>` behind the FX interface 1.0, decoder, host runner
  `tools/fv1vm_run.cpp` and 16 host tests with hand-encoded programs.
* Validated on the host against the L2A reference model (see README).
* `fv1vm/fv1_asm.h` encoders (shared by tests and demo programs) and `fv1vm/fv1_fx48.h`: 48 kHz adapter with the
  program at 32 kHz between two 3:2 polyphase FIR resamplers.
* Standalone firmware `fv1-vm-fx` 0.1.0 (`src/`): 32 kHz, two own demo programs or a local generated table,
  DWT cycle telemetry over USB-MIDI CC, build variants for precision, delay-RAM placement and program source.
* Tools: `gen_programs.py` (local program table), `fv1vm_probe.py` (telemetry decoder and program sweep).
* Composed product `toy-piano-fv1` (Toy Piano `FV1=1`).
* Host tests and ARM builds only; hardware NOT MEASURED.
