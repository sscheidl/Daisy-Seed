# Ambient Reverb v0.1 — Seed3 Desktop Dev Kit

**Buildable prototype; no hardware run has been reported.** Project ID: `reverb-fx`.
This is an original stereo ambient reverb imported from the
[`feature/seed3-ambient-reverb` branch at `eb7fe38`](https://github.com/sscheidl/fv-1-DSP-xtractor/tree/eb7fe38e165b2a997e8128ebc2b7a7a228bfcb65).
The native DSP, Seed3 example, and host verification were written for that
research project and are MIT licensed; the [license](LICENSE-MIT.txt) is retained
here. The old FV-1 reconstructions, third-party sources, user photographs,
manufacturer material, and rendered audio were not copied. This engine uses
Slöer as a design reference; it is **not** a port of the Slöer algorithm, which
has not been identified. The separate Clouds/Nimbus reverb study is still
planned; its code is not included in this firmware.

## What it does

The engine has eight modulated feedback branches, input allpass diffusion,
filtered octave-up/down paths, stereo width, predelay, hold, and bypass with
trails. Five modes select Cloud, Bloom, Abyss, Halo, or Scatter. Five modulation
shapes are available. The mode buttons change the mode without recalling a
separate knob preset. The DSP allocates no memory in the audio callback.

The board runs at **48 kHz, 32 frames per callback** with
`desktop_devkit::Hardware`. Plug a mono input into the left input and leave the
right input empty; the firmware duplicates left when the right jack is not
detected. With both inputs connected, it processes stereo. Outputs are stereo.
The exact physical button and pot orientation still needs a board check.

| Control | Function |
| --- | --- |
| S1–S5 | Cloud / Bloom / Abyss / Halo / Scatter |
| S6 | Toggle Hold, with a 300 ms transition |
| S7 | Toggle bypass with reverb trails |
| S8 | Cycle Sine / Warp / Sink / Step / Random modulation |
| VR1 | Nominal decay, 1–45 s, exponential mapping |
| VR2 | Tone, 400–14,000 Hz, exponential mapping |
| VR3 | Modulation depth, 0–3 ms |
| VR4 | Dry/wet mix, 0–100%, equal-power mapping |
| VR5 | Stereo width, 0–100% |
| VR6 | Texture; also Bloom rise, 0.05–4 s |
| VR7 | Base LFO rate, 0.03–1.5 Hz |
| VR8 | Predelay, 0–150 ms |

LEDs 1–5 indicate the mode, LED 6 Hold, LED 7 bypass, and LED 8 modulation
shape with five brightness levels. LED 16 stays lit after a callback exceeds
its cycle budget. The main loop updates LEDs every 50 ms. Pot changes update
DSP coefficients at most about 50 times per second; button presses are handled
on debounced edges. As with Toy Piano, a brief contact dropout while a button
remains held cannot trigger a second action.

The [Slöer-inspired Desktop control plan](docs/CONTROL_PLAN.md) proposes a closer
pedal-style layout. It is a design document; the table above describes the
current v0.1 firmware.

## Build and test

From the repository root, with the pinned submodules and ARM toolchain installed
as in [BUILD.md](../../docs/BUILD.md):

```bash
make PROJECT=reverb-fx test
make PROJECT=reverb-fx test-sanitize
make PROJECT=reverb-fx -j4
python3 scripts/check-firmware.py effects/reverb/build/AmbientReverb-v0.1.bin
```

The firmware is `effects/reverb/build/AmbientReverb-v0.1.bin`, built with the
same `BOOT_SRAM` setup and external DFU bootloader as Toy Piano. Follow the
[Desktop Kit flashing guide](../../docs/FLASHING.md); no bootloader change is
required. Do not flash the research project's original image if you want the
integrated build and version marker described here.

The imported host suite checks filters and delays, octave shifting, dry/wet
endpoints, predelay, hold/release, bypass trails, mode behavior, long decay,
finite output, and absence of audio-path allocation. It also runs under ASan
and UBSan. The firmware checker validates size, BOOT_SRAM vectors, filename,
and the embedded version. These checks cannot establish board CPU load, analog
level, or audible behavior.

`ambient::Memory` reserves **1,114,112 bytes** in external SDRAM and is cleared
after board initialization. The ARM link report for this integration uses
63,732 bytes of DTCM, 121,324 bytes of SRAM, and 17,096 bytes of DMA RAM.
The callback records its maximum DWT cycle count and overruns for debugger
inspection. No measured device CPU result exists yet. The source branch's
[architecture and measurement report](https://github.com/sscheidl/fv-1-DSP-xtractor/blob/eb7fe38e165b2a997e8128ebc2b7a7a228bfcb65/docs/AMBIENT_REVERB.md)
and [synthetic demos](https://github.com/sscheidl/fv-1-DSP-xtractor/tree/eb7fe38e165b2a997e8128ebc2b7a7a228bfcb65/results)
remain available there. Those numbers and audio files are host renders, not
Seed3 recordings or a listening comparison with Slöer.

## First hardware check

1. Keep monitoring level low. Test bypass and dry signal first, including the
   left-only mono input and two-input stereo behavior.
2. Test S1–S8, LEDs, and all eight pots. Confirm Hold transitions and trails
   without loud jumps.
3. Try Halo/Abyss with maximum decay, modulation, and texture. Read
   `maximumCycles`, `overruns`, and `budgetCycles` in a debugger; record the
   board setup and signal level. LED 16 should remain off in normal use.
4. Check output level, noise, and stereo image through the actual codec. The
   codec output has a soft knee above 0.95; the DSP core itself has no final
   ±1 limiter.

The FV-1 marking on the user's Slöer board is documented in the
[reference survey](../../docs/REFERENCES.md). Its effect program and complete
stereo path remain unknown.
