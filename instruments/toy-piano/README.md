# Toy Piano v0.2

Project ID: `toy-piano` · [All instruments and effects](../../README.md)

Playable C++ firmware for the **Electro-Smith Seed3 Desktop Development Kit**:
16 independent key voices, button LEDs, two AD envelope controls and **USB-MIDI**.
Audio runs at 48 kHz, with the same mono signal on both outputs and headphones.

The hardware diagnostic suite is maintained separately in
[sscheidl/Daisy-Seed3-Desktop-Diagnostics](https://github.com/sscheidl/Daisy-Seed3-Desktop-Diagnostics).

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

S1–S8 form the first row; S9–S16 form the second. S8 and S9 intentionally play
C5 using independent voices. Edit [src/config.h](src/config.h) to change the mapping.

* **VR1:** attack, 2–300 ms; left is shorter.
* **VR2:** decay, 50–3000 ms; left is shorter.
* Both mappings are logarithmic. The ADC uses 32× oversampling; PotFilter adds
  20 ms smoothing and hysteresis. Changes apply to the next strike. Active
  envelopes retain their times to avoid parameter jumps.
* Each press triggers a one-shot AD envelope. Releasing does not interrupt it;
  holding provides neither sustain nor repeat. A key held at startup triggers
  about 7 ms after audio starts.
* LEDs D1–D16 follow the debounced keys independently of the envelope.
* **USB-MIDI over J7:** incoming notes use 16 additional voices with velocity;
  S1–S16 transmit notes on channel 1. See [USB-MIDI](docs/MIDI.md).
* VR3–VR8, toggle switches, DIN MIDI, SD, CV/gates and audio inputs have no musical
  function. **RV1** is the separate analog headphone volume control, not VR1.

The sound is a sine with a quiet second harmonic (12%). Retriggering preserves
phase and envelope level. Fixed scaling and a soft limiter below 0.8 protect
against digital clipping; single notes are relatively quiet. S8/S9 have separate
phases, so their fundamentals can partially cancel when played together.

## Building

Set up the libraries and toolchain as described in [BUILD.md](../../docs/BUILD.md).
From the repository root:

```bash
make PROJECT=toy-piano -j4
make PROJECT=toy-piano test
make PROJECT=toy-piano test-sanitize
make PROJECT=toy-piano test-thread
make test-release
python3 scripts/check-firmware.py
```

Output: **`instruments/toy-piano/build/ToyPiano-v0.2.bin`**, plus ELF, HEX and map.
Direct builds also work with `make -C instruments/toy-piano`.
The version lives only in [src/version.h](src/version.h); it determines the file
name and embedded `TOYPIANO-FW-VERSION=…` marker. See the [changelog](CHANGELOG.md).
Keep the pinned submodules; do not use `git submodule update --remote` for a
reproducible build. CI provides BIN, ELF and SHA-256 as a downloadable artifact.

## Flashing

Like the official Desktop template, the application uses **`APP_TYPE=BOOT_SRAM`**
and the external Daisy DFU bootloader:

1. Only if the appropriate bootloader is missing: install it once through the
   USB-C connector **on the Seed3 module**.
2. For normal use and firmware updates: use **J7 on the Desktop carrier**. Press
   RESET, then BOOT during the approximately two-second bootloader window. Select
   the BIN under Custom in the [Daisy Web Programmer](https://flash.daisy.audio/).

See [FLASHING.md](../../docs/FLASHING.md) for the separate procedures. An installed,
appropriate bootloader does not need reinstalling. No hardware was flashed from cloud.

## Manual hardware checks

1. Connect headphones to J5 or a line-level amplifier/interface to the outputs.
   Start at low volume, power through J7 and flash the firmware.
2. Turn VR1/VR2 left; test each key and matching LED. S8/S9 should have equal pitch.
3. Turn VR2 right, strike briefly, then release: the LED goes off while sound decays.
4. Turn VR1 right and strike again: the onset should be noticeably slower.
5. Play chords and S8/S9 separately. Hold a key longer than 3.3 seconds: sound ends,
   but its LED remains on until release.
6. Compare outputs; test quick repetitions and knob movement for clicks/dropouts.
7. Test USB-MIDI in both directions, simultaneous key changes, semitones, velocity
   and more than 16 incoming notes. Also test power-only operation without a USB
   host and reconnection. Details: [MIDI.md](docs/MIDI.md).

## Validation status

ARM cross-compilation and host tests including ASan, UBSan and TSan passed.
The [validation report](docs/VALIDATION.md) separates software checks from reported
hardware tests. The operator reports successful keys, AD, polyphony, fast playing
and basic bidirectional USB-MIDI with Studio One. ADC endpoints and callback time
remain unmeasured. The final release BIN still needs its manual acceptance test.

Sources: `src/main.cpp` handles hardware, MIDI and LEDs; `src/toy_piano.*` implements
key/MIDI voices, envelopes, limiting and the sine table; `src/spsc_queue.h` provides
the lock-free queue. See [hardware details](../../docs/HARDWARE.md).
No samples, effects, presets, display or networking; MIDI carries notes over USB only.
