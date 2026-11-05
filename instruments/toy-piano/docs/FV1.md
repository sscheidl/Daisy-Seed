# Toy Piano + FV-1 interpreter v0.4.0 (research / feasibility study)

Own test product `toy-piano-fv1`: Toy Piano → [fv1-vm](../../../effects/fv1-vm/) (48 kHz adapter, program at
32 kHz) → stereo output. Composition by build flag `FV1=1`; the regular Toy Piano, `toy-piano-tape` and
`toy-piano-ambient` stay unchanged (ARM images byte-identical, 2026-11-05 for 0.1.0 to 0.4.0). Board results:
[fv1-vm README](../../../effects/fv1-vm/README.md#hardware-results).

```bash
make PROJECT=toy-piano-fv1 test
make PROJECT=toy-piano-fv1 -j4
make PROJECT=toy-piano-fv1 manifest
python3 scripts/check-firmware.py instruments/toy-piano/build/fv1/ToyPianoFv1-v0.4.0.bin
# Local catalogue (never committed or released): FV1_PROGRAMS=local, see effects/fv1-vm/README.md
make -C instruments/toy-piano FV1=1 FV1_PROGRAMS=local -j4
```

## Controls

Piano first (0.3.0, owner decision 2026-11-05): VR1/VR2 keep the piano times of the regular Toy Piano, the
effect follows from VR3. Before 0.3.0, VR1..VR3 were the FV-1 pots, VR4 dry/wet, and the piano times were fixed.


| Control | Function |
| --- | --- |
| Keys S1..S16, MIDI notes | Toy Piano as usual |
| VR1 | piano attack, 2–300 ms (as in the regular Toy Piano) |
| VR2 | piano decay, 50–3000 ms (as in the regular Toy Piano) |
| VR3 · VR4 · VR5 | parameter 1 · 2 · 3 of the FV-1 program (POT0 · POT1 · POT2) |
| VR6 | dry/wet (0 = exact dry piano) |
| VR7, VR8, SW18 | unused |
| SW17 | FX bypass: exact dry piano; the effect keeps running in the background |
| MIDI Program Change, any channel | Bank Select (CC0 or CC32) = group, Program Change = position in the group (local grouped table, since 0.4.0); without groups bank × 128 + n |
| MIDI channel 16 CC22/CC23 | program index (high/low 7 bits, 0-based, wraps); the change mutes one block |
| MIDI channel 16 CC119 | CPU telemetry, reply CC100..119 (protocol id 2, `effects/fv1-vm/tools/fv1vm_probe.py`) |

Mod on Daisy identifies the product as `toy-piano-fv1` with the five Toy Piano columns. Program list, selection
and CPU display: the local control page `effects/fv1-vm/tools/control/fv1vm_control.html`.

Generator programs: the piano is the only FV-1 input, the same mono signal on both channels. Free-running
generators sound regardless of the keys (mix with VR6); programs that read the input as a trigger or expect a
separate clock or gate on one channel do not get one (not measured in this product).

## Notes

* The whole `Fv1Host` (interpreter, resamplers, 128 KiB delay RAM) lives in SDRAM; DTCM is 91.3 % used.
* CPU load of all 240 local programs with 16 voices sounding: MEASURED 2026-11-05 with 0.1.0, no overrun, peak
  ≤ 92.7 %. Sound and listening: NOT MEASURED ([test plan](../../../effects/fv1-vm/docs/HARDWARE_TEST.md)).
