# Toy Piano + FV-1 interpreter v0.8.0 (research / feasibility study)

Own test product `toy-piano-fv1`: Toy Piano → [fv1-vm](../../../effects/fv1-vm/) (48 kHz adapter, program at
32 kHz) → stereo output. Composition by build flag `FV1=1`; the regular Toy Piano, `toy-piano-tape` and
`toy-piano-ambient` stay unchanged (ARM images byte-identical, 2026-11-05 for 0.1.0 to 0.8.0). Board results:
[fv1-vm README](../../../effects/fv1-vm/README.md#hardware-results).

```bash
make PROJECT=toy-piano-fv1 test
make PROJECT=toy-piano-fv1 -j4
make PROJECT=toy-piano-fv1 manifest
python3 scripts/check-firmware.py instruments/toy-piano/build/fv1/ToyPianoFv1-v0.8.0.bin
# Local catalogue (never committed or released): FV1_PROGRAMS=local, see effects/fv1-vm/README.md
make -C instruments/toy-piano FV1=1 FV1_PROGRAMS=local -j4
```

## Controls

Piano first (owner decisions 2026-11-05): VR1 transposition, VR2/VR3 the piano times of the regular Toy Piano, the
effect follows from VR4, VR8 the internal rate. History: before 0.3.0 VR1..VR3 were the FV-1 pots and VR4 dry/wet;
0.3.0/0.4.0 had VR1/VR2 attack/decay, VR3..VR5 FV-1 pots and VR6 dry/wet.


| Control | Function |
| --- | --- |
| Keys S1..S16, MIDI notes | Toy Piano as usual |
| VR1 | piano transposition, continuous −24 to +24 semitones, centre detent = original pitch |
| VR2 | piano attack, 2–300 ms (as in the regular Toy Piano) |
| VR3 | piano decay, 50–3000 ms (as in the regular Toy Piano) |
| VR4 · VR5 · VR6 | parameter 1 · 2 · 3 of the FV-1 program (POT0 · POT1 · POT2) |
| VR7 | dry/wet (0 = exact dry piano) |
| VR8 | rate: the program's internal sample rate, 32 kHz at the left stop, 16 kHz centre, 8 kHz right (exponential, glides like tape speed; longer delays, lower generator pitch, less CPU) |
| SW18 | unused |
| SW17 | FX bypass: exact dry piano; the effect keeps running in the background |
| MIDI Program Change, any channel | Bank Select (CC0 or CC32) = group, Program Change = position in the group (local grouped table, since 0.4.0); without groups bank × 128 + n |
| MIDI channel 16 CC22/CC23 | program index (high/low 7 bits, 0-based, wraps); the change mutes one block |
| DIN MIDI in (5-pin) | as USB: notes, Program Change/Bank Select, CC1, aftertouch, CC22/CC23/CC119 (since 0.6.0); SysEx only over USB |
| MIDI channel 16 CC119 | CPU telemetry, reply CC100..119 (protocol id 2, `effects/fv1-vm/tools/fv1vm_probe.py`), followed by the status packet CC80..89: rate, VR1 transposition, dry/wet, bypass (since 0.7.0) |

Mod on Daisy identifies the product as `toy-piano-fv1` with the five Toy Piano columns. Program list, selection
and CPU display: the local control page `effects/fv1-vm/tools/control/fv1vm_control.html`.

Generator programs: the piano is the only FV-1 input, the same mono signal on both channels. Free-running
generators sound regardless of the keys (mix with VR7); programs that read the input as a trigger or expect a
separate clock or gate on one channel do not get one (not measured in this product).

## Notes

* The whole `Fv1Host` (interpreter, resamplers, 128 KiB delay RAM) lives in AXI SRAM behind the image (since 0.8.0); DTCM is 91.4 % used.
* CPU load of all 240 local programs with 16 voices sounding: MEASURED 2026-11-05 with 0.1.0, no overrun, peak
  ≤ 92.7 %. Sound and listening: NOT MEASURED ([test plan](../../../effects/fv1-vm/docs/HARDWARE_TEST.md)).
