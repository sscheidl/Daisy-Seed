# Daisy Physical Modeling v0.1.0

Four-voice Mutable Instruments Rings / Torus instrument for the **Electro-Smith
Seed3 Desktop Development Kit**. Native C++17, pinned libDaisy, 48 kHz stereo.
No external audio input or extra synthesis/reverb engine is used.
The [performance implementation and A/B procedure](docs/PERFORMANCE_IMPLEMENTATION.md)
describe equivalent DSP optimizations, optional AXI placement and diagnostic builds.
Hardware CPU gains and ARM render comparisons are not measured yet.
Status: **software validated; functional hardware acceptance reported;
four-voice CPU measured** (2026-10-22). See
[executed validation and artifact](docs/VALIDATION.md) and
[hardware results with raw measurements](docs/HARDWARE_RESULTS_2026-10-22.md).
The [practical follow-up](docs/HARDWARE_PLAYING_2026-10-22.md) found short deadline
overruns during model changes, accepted by the user as a known limitation for
the intended use. Running synthesis has headroom.

**Mod on Daisy:** a separate build with the modulation matrix and Poly AT per
voice is described in [docs/MODULATION.md](docs/MODULATION.md)
(`make PROJECT=physical-modeling-mod`). The regular build is unchanged.

## Additional composed FX products

The [modular component pilot](../../docs/MODULAR_COMPONENTS.md) adds separate
`physical-modeling-fx-torus` and `physical-modeling-fx-ambient` builds. Both keep
the existing keyboard/MIDI/Mod-on-Daisy host and choose the downstream wet effect
at build time. They have product version 0.2.0 and explicit component manifests;
the regular 0.1.0 and accepted Mod4 targets retain their prior identities.
These combinations still need hardware CPU and listening tests.

## Models and controls

| SW17 bank | SW18 UP | SW18 CENTER | SW18 DOWN |
| --- | --- | --- | --- |
| A: not Pressed | Modal Resonator | Sympathetic Strings | Inharmonic Strings |
| B: Pressed | FM Voice | Western Chords | String & Reverb |

UP=1, CENTER=0, DOWN=2 are the pinned driver's states, not numeric model order.
SW18 UP grounds the first driver pin (D28); DOWN grounds D27. SW17 uses the
active-low official Switch driver. Confirm the physical lever orientation on your
board. A changed combination must remain stable for 20 ms.

| Pot | Function | Range |
| --- | --- | --- |
| VR1 | Frequency / transpose | linear -24 to +24 semitones; center=0 |
| VR2 | Structure | original model-dependent 0..1 |
| VR3 | Brightness | original model-dependent 0..1 |
| VR4 | Damping | original model-dependent 0..1 |
| VR5 | Position | original model-dependent 0..1 |
| VR6 | Excitation | 0..1, multiplied by note velocity before resonance |
| VR7 | Reverb decay | feedback coefficient 0.35..0.98, not seconds |
| VR8 | Dry/wet | 0=dry only, 1=wet only; linear crossfade |

S1..S16 play 60,62,64,65,67,69,71,72,72,74,76,77,79,81,83,84.
The intentional duplicate C5 can retrigger two separate voices. Edit the central
`src/config.h` table to change this mapping. Each LED follows its held key;
releasing a key leaves its resonator decaying. The fifth strike steals the oldest
round-robin slot. All sixteen simultaneous strikes are rendered within one
48-sample callback, in groups of four at offsets 0,12,24,36.

USB MIDI uses **J7**, all channels, Notes 0..127, velocity, CC20..33 and smoothed
14-bit pitch bend +/-2 semitones. Hardware and MIDI share one parameter manager.
An unmoved knob does not overwrite MIDI; moving it takes control. See
[the complete MIDI map](docs/MIDI.md). Standalone play needs no USB host.

## Build and artifacts

Use the pinned dependencies and toolchain from [BUILD](../../docs/BUILD.md):

```sh
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
make PROJECT=physical-modeling -j4
make PROJECT=physical-modeling test
make PROJECT=physical-modeling test-sanitize
make PROJECT=physical-modeling test-thread
python3 scripts/check-firmware.py instruments/physical-modeling/build/PhysicalModeling-v0.1.1.bin
python3 scripts/check-physical-elf.py instruments/physical-modeling/build/PhysicalModeling-v0.1.1.elf
```

The Firmware CI matrix provides `PhysicalModeling-v0.1.0.bin`, `.elf`, `.map` and
`SHA256SUMS` as an Actions artifact. Generated outputs are ignored by Git.
`APP_TYPE=BOOT_SRAM`; application load address 0x24000000, maximum 480 KiB.

## Flash and hardware validation

Use the **existing external Daisy bootloader**, following
[FLASHING](../../docs/FLASHING.md) and [HARDWARE_TEST](docs/HARDWARE_TEST.md).
Flash only the application BIN. A working compatible bootloader needs no
reinstallation. For an update, hold **S9 + S16** for 5 s or use **Firmware-Update …**
in the editor (both builds): all LEDs light up and the bootloader waits
([details](../../common/bootloader/README.md)). The user reports successful flashing and functional listening
tests with S1 (Studio One). The authorized six-model hardware CPU capture recorded
zero deadline overruns in 174084 blocks, with a highest peak of 56.71% at four
voices and muted final master in the settled-model run. A subsequent practical
run with knob sweeps found switch-time peaks up to 141.58% and deadline overruns;
see [settled results](docs/HARDWARE_RESULTS_2026-10-22.md) and
[playing/automation results](docs/HARDWARE_PLAYING_2026-10-22.md).
CPU average/peak and deadline misses can be requested over USB MIDI; the
[hardware test guide](docs/HARDWARE_TEST.md) includes a CSV measurement helper.

## Provenance and limits

Torus is the Daisy Patch port of Rings by Emilie Gillet / Mutable Instruments.
The DSP is vendored at DaisyExamples
`259ed82c7d4c0d7699f695945e6e111b7dc7cb27`; stmlib at its corresponding submodule
`5dc8b89b9d166de5f47ba91ee9d25c8805c2bd86`. All original MIT notices remain.
See [UPSTREAM](torus/UPSTREAM.md), [architecture](docs/ARCHITECTURE.md), and
[validation](docs/VALIDATION.md).

Fixed four voices, no NRPN, MPE, DIN MIDI, or aftertouch assignment. This is a
keyboard adaptation: CV note settling and automatic strumming are replaced by
explicit per-voice events; the original strummer helper is retained for reference.
At extreme damping the FM model intentionally approaches an oscillator, as in
Torus. The original string model's approximate tuning is retained (the host probe
measured about 887 Hz for an 880 Hz target with structure .25/damping .7).
Output gain is fixed at 0.8 times MIDI master volume, followed by a soft peak
protection below +/-0.8 FS. The recorded four-voice CPU margin applies to its
documented settings; six/eight voices and a modulation matrix are not yet measured.
