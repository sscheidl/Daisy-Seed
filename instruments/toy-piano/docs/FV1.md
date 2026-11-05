# Toy Piano + FV-1 interpreter v0.1.0 (research / feasibility study)

Own test product `toy-piano-fv1`: Toy Piano → [fv1-vm](../../../effects/fv1-vm/) (48 kHz adapter, program at
32 kHz) → stereo output. Composition by build flag `FV1=1`; the regular Toy Piano, `toy-piano-tape` and
`toy-piano-ambient` stay unchanged (ARM images byte-identical, 2026-11-05). No device was connected or flashed.

```bash
make PROJECT=toy-piano-fv1 test
make PROJECT=toy-piano-fv1 -j4
make PROJECT=toy-piano-fv1 manifest
python3 scripts/check-firmware.py instruments/toy-piano/build/fv1/ToyPianoFv1-v0.1.0.bin
# Local catalogue (never committed or released): FV1_PROGRAMS=local, see effects/fv1-vm/README.md
make -C instruments/toy-piano FV1=1 FV1_PROGRAMS=local -j4
```

## Controls

| Control | Function |
| --- | --- |
| Keys S1..S16, MIDI notes | Toy Piano as usual; attack 2 ms and decay 1.5 s are fixed bases (the matrix can modulate them) |
| VR1..VR3 | POT0..POT2 of the FV-1 program |
| VR4 | dry/wet (0 = exact dry piano) |
| MIDI channel 16 CC22/CC23 | program index (high/low 7 bits, 0-based, wraps); the change mutes one block |
| MIDI channel 16 CC119 | CPU telemetry, reply CC100..119 (protocol id 2, `effects/fv1-vm/tools/fv1vm_probe.py`) |

Mod on Daisy identifies the product as `toy-piano-fv1` with the five Toy Piano columns.

## Notes

* The whole `Fv1Host` (interpreter, resamplers, 128 KiB delay RAM) lives in SDRAM; DTCM is 91.3 % used.
* Hardware, CPU load and sound: **NOT MEASURED** ([test plan](../../../effects/fv1-vm/docs/HARDWARE_TEST.md)).
