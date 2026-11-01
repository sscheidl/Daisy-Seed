# Streichwurst 0.1.0: hardware test sheet

As of 01.11.2026. All items are **NOT MEASURED** until the human carries them out on the kit
and reports the result. The agent does not flash; flashing only when the
human has put the kit into the bootloader and asked for it ([FLASHING.md](../../../docs/FLASHING.md)).

**Order (Modular FX first):** first accept the standalone FX firmware
([FX test sheet](../../../effects/streichwurst-fx/docs/HARDWARE_TEST.md)), then this product.

Image: `instruments/streichwurst/build/Streichwurst-v0.1.0.bin` (built locally, not
published). Beforehand: `python scripts/check-firmware.py <bin>`.

| No. | Check | Expectation (not a measurement) | Result |
| --- | --- | --- | --- |
| H1 | Start-up, headphone/line output | Silence without notes, no click at start-up | NOT MEASURED |
| H2 | Play C4–C5 on S1–S8 | Strings with ensemble mode 1 and some reverb, stereo | NOT MEASURED |
| H3 | USB MIDI (J7) notes, pitch bend, sustain | Sound, bend ±2 semitones, pedal holds | NOT MEASURED |
| H4 | DIN MIDI notes | as H3 (same channel filter) | NOT MEASURED |
| H5 | VR1–VR8 Main page | Registration, Crescendo, Release, Balance, Animate, Phaser, Reverb, Master; takeover only after crossing | NOT MEASURED |
| H6 | S9 page change Main→Solo→FX, LEDs S9–S11 | Page changes on release, no jumps of the values | NOT MEASURED |
| H7 | S10 octaves, S11 mode, S12 ensemble, S13 solo sustain, S14 split | Audible switching, ensemble change without click (20 ms crossfade) | NOT MEASURED |
| H8 | S15 RAM presets 0–3 | Blink sequence on S15, sound changes, channel/master/balance are kept | NOT MEASURED |
| H9 | S16 panic | Sound stops with a short ramp, playable afterwards | NOT MEASURED |
| H10 | S9+S16 pressed briefly, released | neither page change nor panic | NOT MEASURED |
| H11 | Hold S9+S16 for 5 s | all LEDs on, bootloader waits (DFU device appears) | NOT MEASURED |
| H12 | Connect the editor (JUCE 0.3.0 or browser 0.3.0) | "streichwurst 0.1.0 · SQ + Matrix", values from the device, nothing overwritten | NOT MEASURED |
| H13 | Change a parameter in the app, turn a pot | App change audible; pot movement appears in the app | NOT MEASURED |
| H14 | Matrix: LFO 1 → Registration | Moving timbre; Clear matrix → basic sound | NOT MEASURED |
| H15 | Save `.swpatch`, change, load + send | State as saved, matrix included | NOT MEASURED |
| H16 | Dense chord (10 keys, octaves "Both", mode 1, reverb) | no dropouts; counters in the app: callback deadline 0 | NOT MEASURED |
| H17 | CPU: callback peak (app → Read counters) | Target ≤ 60 % of 480 000 cycles per 1 ms block | NOT MEASURED |
| H18 | Stack free (counter 16) | clearly > 0 after H16 (linker reserve 8.6 KiB) | NOT MEASURED |
| H19 | Firmware update from the app | Confirmation prompt, bootloader, all LEDs | NOT MEASURED |
| H20 | Listening comparison with the original plugin/device | Own approximation, deviations see [FIDELITY.md](FIDELITY.md) | NOT MEASURED |

The agent enters reported results with the date as MEASURED in this table and in
[VALIDATION.md](VALIDATION.md).
