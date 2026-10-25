# Ambient Reverb v0.3 — Seed3 Desktop Dev Kit

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
filtered octave-up/down paths, stereo width, predelay, sustain, and bypass with
trails. Six direct mode buttons select Cloud, Halo, Scatter, Abyss, Bloom and
Shimmer. S7–S14 are reserved for future original modes. Shimmer adds a stronger
filtered octave-up injection and recirculates the late tail through the shifter;
its feedback return scales with unused tank gain. X controls its octave amount.
No additional large audio buffers are required.
The modulation shape is Sine in this control revision; mode buttons do not
recall knob presets. The DSP allocates no memory in the audio callback.

The board runs at **48 kHz, 32 frames per callback** with
`desktop_devkit::Hardware`. Plug a mono input into the left input and leave the
right input empty; the firmware duplicates left when the right jack is not
detected. With both inputs connected, it processes stereo. The two-way toggle
can select Legacy Mono, which downmixes both connected inputs at half gain
and sends identical audio to both outputs; wet Pan is ignored in full mono.
This is our own mono routing mode,
not a recreation of the original Slö firmware.
The exact physical button and pot orientation still needs a board check.

| Control | Function |
| --- | --- |
| S1–S5 | Cloud / Halo / Scatter / Abyss / Bloom (Dream / Light / Rain / Dark / Rise inspired) |
| S6 | Shimmer: original rising-octave ambient reverb |
| S7–S14 | Reserved for future modes; no action |
| S9 + S16, 5 s | Firmware update mode: all LEDs on, Daisy bootloader ([details](../../common/bootloader/README.md)); S16 switches sustain first |
| S15 | Bypass. Tap to toggle; hold from bypass for temporary effect (250 ms threshold) |
| S16 | Sustain. Latching in Cloud; momentary in the other modes |
| Three-way `tog_sw18` | Up = Wide, center = Medium, down = Narrow |
| Two-way `tog_sw17` | Stereo / Legacy Mono; polarity to confirm on the board |
| VR1 | Nominal decay, 1–45 s, exponential mapping |
| VR2 | Tone, 400–14,000 Hz, exponential mapping |
| VR3 | Modulation depth, 0–3 ms |
| VR4 | Dry/wet mix, 0–100%, equal-power mapping |
| VR5 | Wet Pan, left–center–right |
| VR6 | Mode-dependent X: sustained Cloud vibrato, Halo/Abyss/Shimmer octave amount, Scatter tap diffusion, Bloom rise |
| VR7 | Base LFO rate, 0.03–1.5 Hz |
| VR8 | Stretch: continuously varies the wet processing clock from 48 to 12 kHz |

Exactly one of LEDs D1–D6 indicates the selected mode. D15 lights when bypass
is active; D16 lights when Sustain is active. D7–D14 remain off. The main loop
updates LEDs on state changes, polling every 10 ms. Callback overruns remain
available through the `overruns` debugger variable; D16 is now a Sustain LED.
Pot changes update DSP coefficients at most about 50 times per second; button
presses are handled on debounced edges. A brief contact dropout while a button
remains held cannot trigger a second action.

Stretch clocks only the **wet** DSP more slowly; the codec and dry signal stay
at 48 kHz. A 64-tap input low-pass filter limits aliasing before the reduced
wet rate, and the wet output is interpolated back to 48 kHz. The full signal
is not recorded or time-stretched as an audio file. The FV-1 datasheet confirms
that its sampling rate follows an external clock on X1; the actual Slöer circuit
and its clock range have not been verified.

The [Desktop control plan](docs/CONTROL_PLAN.md) records the source checks,
mode expansion path, and hardware acceptance steps. The table above describes
the v0.3 firmware; the older plan predates the sixth algorithm.

## Reuse inside an instrument

`src/fx_adapter.h` exposes the existing DSP through the shared wet-only audio
contract. Its integration adapter has its own version in `adapter_version.h`;
the shared core is versioned 0.3. The first composed firmware is
`make PROJECT=physical-modeling-fx-ambient`; see
[modular components](../../docs/MODULAR_COMPONENTS.md) for its limited pilot
parameter mapping and hardware-test boundary.

## Build and test

From the repository root, with the pinned submodules and ARM toolchain installed
as in [BUILD.md](../../docs/BUILD.md):

```bash
make PROJECT=reverb-fx test
make PROJECT=reverb-fx test-sanitize
make PROJECT=reverb-fx -j4
python3 scripts/check-firmware.py effects/reverb/build/AmbientReverb-v0.3.bin
```

The firmware is `effects/reverb/build/AmbientReverb-v0.3.bin`, built with the
same `BOOT_SRAM` setup and external DFU bootloader as Toy Piano. Follow the
[Desktop Kit flashing guide](../../docs/FLASHING.md); no bootloader change is
required. Do not flash the research project's original image if you want the
integrated build and version marker described here.

The host suite checks filters and delays, octave shifting, dry/wet endpoints,
predelay, hold/release, bypass trails, mode behavior, long decay, stretched
wet-clock timing, mono routing, control gestures, LED mapping, finite output,
and absence of audio-path allocation. It also runs under ASan and UBSan.
The firmware checker validates size, BOOT_SRAM vectors, filename, and the
embedded version. These checks cannot establish board CPU load, analog level,
or audible behavior.

On October 20, 2026, `make PROJECT=reverb-fx test` and `test-sanitize` passed,
and xPack ARM GCC 13.2.1-1.1 built a 130,428-byte v0.2 BIN. The firmware
checker passed; SHA-256 is
`b7642865581c7c12e3c76f8fd4c089b55a1ae9f40d124ad52b050871d01c4114`.
In the host impulse test, the first wet response moved from sample 2,361 at
48 kHz internal rate to sample 9,417 at 12 kHz. This confirms the intended
clock-ratio effect in simulation, not the result on the device.

`ambient::Memory` reserves **1,114,112 bytes** in external SDRAM and is cleared
after board initialization. The ARM link report for v0.2 uses
69,108 bytes of DTCM, 130,428 bytes of SRAM, and 17,096 bytes of DMA RAM.
The callback records its maximum DWT cycle count and overruns for debugger
inspection. No measured device CPU result exists yet. The source branch's
[architecture and measurement report](https://github.com/sscheidl/fv-1-DSP-xtractor/blob/eb7fe38e165b2a997e8128ebc2b7a7a228bfcb65/docs/AMBIENT_REVERB.md)
and [synthetic demos](https://github.com/sscheidl/fv-1-DSP-xtractor/tree/eb7fe38e165b2a997e8128ebc2b7a7a228bfcb65/results)
remain available there. Those numbers and audio files are host renders, not
Seed3 recordings or a listening comparison with Slöer.

## First hardware check

1. Keep monitoring level low. Test bypass and dry signal first, including the
   left-only input, stereo input, and Legacy Mono switch positions.
2. Test S1–S6, S15/S16, their LEDs, both toggles, and all eight pots. Confirm
   Sustain and bypass transitions without loud jumps; verify the physical
   positions of `tog_sw17` and `tog_sw18`.
3. Turn VR8 slowly and compare the wet tail timing and tone at both endpoints.
   Try Halo/Abyss/Shimmer with maximum decay, modulation, and texture. Read
   `maximumCycles`, `overruns`, and `budgetCycles` in a debugger; record the
   board setup and signal level. D16 now indicates Sustain, not an overrun.
4. Check output level, noise, and stereo image through the actual codec. The
   codec output has a soft knee above 0.95; the DSP core itself has no final
   ±1 limiter.

The FV-1 marking on the user's Slöer board is documented in the
[reference survey](../../docs/REFERENCES.md). Its effect program and complete
stereo path remain unknown.

## v0.3 software validation (October 25, 2026)

Host and fatal ASan/UBSan suites pass, including six-mode control/LED mapping,
Shimmer octave spectrum, 90-second maximum-control tail stability and four
samplerates. The shared wet-only adapter and Torus/Ambient host tests pass.
A deterministic 5-second comparison with `44b08ca` preserves float output for
the original five modes. These are software results; device sound and CPU are
**NICHT GEMESSEN**.

xPack GCC 13.2.1-1.1 builds a 130828-byte `AmbientReverb-v0.3.bin`; BOOT_SRAM
vectors and ARM ELF/memory checks pass. DTCM: 69124 B; SDRAM: 1114112 B. SHA-256:
`576e9d81132f6b511913c477930c7073a7c0641f0d2ed8dc3eb071fb698bcc90`.
The earlier v0.2 measurements above are retained as historical results.
