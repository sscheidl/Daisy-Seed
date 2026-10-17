# Daisy Seed — Toy Piano v0.1

Playable C++ firmware for the **Electro-Smith Seed3 Desktop Development Kit**:
16 independent toy-piano voices, button LEDs, and two AD envelope controls.
Audio runs at 48 kHz, with the same mono signal on both stereo outputs and headphones.

The **Daisy Seed3 Hardware Diagnostic Suite** is a separate project:
[sscheidl/Daisy-Seed3-Desktop-Diagnostics](https://github.com/sscheidl/Daisy-Seed3-Desktop-Diagnostics).
Its firmware, Windows application, tests, and documentation are maintained there.

## Playing

| Button | Note | MIDI note | Button | Note | MIDI note |
| --- | --- | --- | --- | --- | --- |
| S1 | C4 | 60 | S9 | C5 | 72 |
| S2 | D4 | 62 | S10 | D5 | 74 |
| S3 | E4 | 64 | S11 | E5 | 76 |
| S4 | F4 | 65 | S12 | F5 | 77 |
| S5 | G4 | 67 | S13 | G5 | 79 |
| S6 | A4 | 69 | S14 | A5 | 81 |
| S7 | B4 | 71 | S15 | B5 | 83 |
| S8 | C5 | 72 | S16 | C6 | 84 |

The first row is S1–S8; the second is S9–S16. S8 and S9 intentionally play the
same note using independent voices. Edit the mapping in [src/config.h](src/config.h).

* **VR1:** attack from 2 to 300 ms; turn left for shorter times.
* **VR2:** decay from 50 to 3000 ms; turn left for shorter times.
* Both mappings are logarithmic. The ADC uses 32× oversampling; PotFilter adds
  20 ms smoothing and hysteresis. Settings apply to the next strike; active
  envelopes retain their times to avoid parameter jumps.
* Each press starts its own AD envelope. Releasing does not stop the sound;
  holding adds neither sustain nor repeated triggers. A button held at startup
  triggers about 7 ms after audio starts.
* D1–D16 show debounced button states independently of the sound's decay.
* VR3–VR8, switches, MIDI, SD, CV/gates, and audio inputs have no musical function.
  **RV1** is the separate analog headphone volume control, not VR1.

The sound is a sine wave with 12% second harmonic. Retriggering preserves
oscillator phase and current envelope level. Fixed gain prevents digital clipping
even with 16 voices in phase; individual notes are relatively quiet. S8 and S9
have independent phases: together they can partially cancel their fundamentals,
depending on when they were triggered.

## Building

Requirements: Git, GNU Make, ARM GCC with Newlib and C++ libraries; g++ for host
tests and Python 3 for the binary check.

```bash
git clone https://github.com/sscheidl/Daisy-Seed.git
cd Daisy-Seed
git switch main
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4
# Linux x86_64: install the pinned toolchain locally, without sudo.
bash scripts/install-toolchain.sh
export PATH="$PWD/.tools/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin:$PATH"
make -j4
make test
python3 scripts/check-firmware.py build/ToyPiano-v0.1.bin
```

Missing libraries are built automatically. Output: **`build/ToyPiano-v0.1.bin`**,
plus ELF, HEX, and a linker map. Official libraries are pinned Git submodules;
avoid `git submodule update --remote` when reproducing a build.
See [Build and dependencies](docs/BUILD.md).

## Flashing

Like the official Desktop template, the application uses **`APP_TYPE=BOOT_SRAM`**
and requires the external Daisy USB DFU bootloader.

1. **Only if the bootloader is missing or incorrect:** install it once through
   the USB-C port **on the Seed3 module**.
2. **Firmware updates and normal operation:** use **J7 on the Desktop carrier**.
   Press RESET, then BOOT within the two-second bootloader window. In the
   [Daisy Web Programmer](https://flash.daisy.audio/), select the `.bin` under **Custom**.

See [FLASHING.md](docs/FLASHING.md) for the separate procedures. A suitable
bootloader does not need reinstalling. Automated builds do not flash hardware.
Flashing Toy Piano replaces the diagnostic application; the bootloader is retained.

## First hardware test

1. Connect headphones to J5 or an amplifier to the line outputs. Start at low
   volume. Power the carrier through J7 and flash the firmware.
2. Turn VR1 and VR2 fully left. Press S1–S16 individually: expect ascending notes
   and corresponding LEDs. S8 and S9 should have the same pitch.
3. Turn VR2 right, strike briefly, and release: the LED turns off after debounce,
   while the sound continues to decay.
4. Turn VR1 right and strike again: expect a slower attack.
5. Play multiple buttons; try S8/S9 together and separately. Voices should decay
   independently. Hold a button for more than 3.3 s: its sound ends, but its LED stays on.
6. Compare both audio outputs, rapid retriggers, and knob changes. Check for
   clicks, noise, wrong LEDs, and dropouts.

## Validation status

ARM cross-compilation, host tests, and BOOT_SRAM vector checks have passed.
The [validation report](docs/VALIDATION.md) separates software checks from pending
hardware tests. Those software tests do not measure actual audio quality, ADC
stability, or callback execution time on the Seed3.

[GitHub Actions](https://github.com/sscheidl/Daisy-Seed/actions/workflows/build.yml)
builds and tests the project and uploads BIN, ELF, and SHA-256 as build artifacts.
A local build does not require GitHub Actions.

## Structure and references

* `src/main.cpp`: official Desktop hardware, edge tracking, audio, and LED mailbox.
* `src/toy_piano.*`: independent voices, AD envelopes, and interpolated sine table.
* `src/config.h`: note mapping, timing, tone, and output gain.
* `tests/`: host tests without an additional test framework.
* [Hardware details and official sources](docs/HARDWARE.md).

Samples, effects, MIDI control, presets, displays, and networking are not implemented.
