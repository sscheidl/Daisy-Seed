# Streichwurst 0.1.0: hardware test sheet

As of 01.11.2026. Items are **NOT MEASURED** until the human carries them out on the kit
and reports the result. The agent does not flash; flashing only when the
human has put the kit into the bootloader and asked for it ([FLASHING.md](../../../docs/FLASHING.md)).

**Order (Modular FX first):** first accept the standalone FX firmware
([FX test sheet](../../../effects/streichwurst-fx/docs/HARDWARE_TEST.md)), then this product.

Image: `instruments/streichwurst/build/Streichwurst-v0.1.0.bin` (built locally, not
published). Beforehand: `python scripts/check-firmware.py <bin>`.

| No. | Check | Expectation (not a measurement) | Result |
| --- | --- | --- | --- |
| H1 | Start-up, headphone/line output | Silence without notes, no click at start-up | NOT MEASURED |
| H2 | Play C4–C5 on S1–S8 | Strings with ensemble mode 1 and some reverb, stereo | MEASURED 01.11.2026: played, sound OK, no audio problems |
| H3 | USB MIDI (J7) notes, pitch bend, sustain | Sound, bend ±2 semitones, pedal holds | MEASURED 01.11.2026: USB MIDI keyboard notes OK, pitch bend ±2 semitones OK; sustain NOT MEASURED (no pedal) |
| H4 | DIN MIDI notes | as H3 (same channel filter) | NOT MEASURED |
| H5 | VR1–VR8 Main page | Registration, Crescendo, Release, Balance, Animate, Phaser, Reverb, Master; takeover only after crossing | MEASURED 01.11.2026: pots OK |
| H6 | S9 page change Main→Solo→FX, LEDs S9–S11 | Page changes on release, no jumps of the values | NOT MEASURED |
| H7 | S10 octaves, S11 mode, S12 ensemble, S13 solo sustain, S14 split | Audible switching, ensemble change without click (20 ms crossfade) | MEASURED 01.11.2026 (partly): ensemble modes (S11) heard and convincing; Layer and Split (S14) work; S10, S12, S13 not reported |
| H8 | S15 RAM presets 0–3 | Blink sequence on S15, sound changes, channel/master/balance are kept | NOT MEASURED |
| H9 | S16 panic | Sound stops with a short ramp, playable afterwards | NOT MEASURED |
| H10 | S9+S16 pressed briefly, released | neither page change nor panic | NOT MEASURED |
| H11 | Hold S9+S16 for 5 s | all LEDs on, bootloader waits (DFU device appears) | MEASURED 01.11.2026: S9+S16 starts the firmware update (bootloader) |
| H12 | Connect the editor (JUCE 0.3.0 or browser 0.3.0) | "streichwurst 0.1.0 · SQ + Matrix", values from the device, nothing overwritten | MEASURED 01.11.2026: JUCE standalone EXE and VST3 work with the instrument. Under Studio One the EXE handles input noticeably delayed; the VST3 feels better there (see session notes). Browser NOT MEASURED |
| H13 | Change a parameter in the app, turn a pot | App change audible; pot movement appears in the app | NOT MEASURED |
| H14 | Matrix: LFO 1 → Registration | Moving timbre; Clear matrix → basic sound | MEASURED 01.11.2026: LFO → target modulation works |
| H15 | Save `.swpatch`, change, load + send | State as saved, matrix included | NOT MEASURED |
| H16 | Dense chord (10 keys, octaves "Both", mode 1, reverb) | no dropouts; counters in the app: callback deadline 0 | MEASURED 01.11.2026: 10+ keys, octaves "Both", ensemble, reverb: no crackles, stable; counter "Callback deadline" 0. Counter "MIDI queue overflow" 64 (see session notes) |
| H17 | CPU: callback peak (app → Read counters) | Target ≤ 60 % of 480 000 cycles per 1 ms block | NOT MEASURED |
| H18 | Stack free (counter 16) | clearly > 0 after H16 (linker reserve 8.6 KiB) | NOT MEASURED |
| H19 | Firmware update from the app | Confirmation prompt, bootloader, all LEDs | NOT MEASURED |
| H20 | Listening comparison with the original plugin/device | Own approximation, deviations see [FIDELITY.md](FIDELITY.md) | NOT MEASURED |

The agent enters reported results with the date as MEASURED in this table and in
[VALIDATION.md](VALIDATION.md).

## Reported session 01.11.2026 (MEASURED, human report)

Image: instrument with integrated FX, `Streichwurst-v0.1.0.bin` (test build 1). The standalone
FX image was not tested separately.

* Played with the kit keys and a MIDI keyboard over USB, Layer and Split. Pitch bend ±2
  semitones OK; no sustain pedal available.
* Pots OK. No audio problems. Stress test with 10+ keys, octaves "Both", ensemble and reverb:
  no crackles, stable.
* Counters (app → Read counters, screenshot): Callback deadline 0, Note-on dropped 0,
  Monitor dropped 0, Busy 0, all SQ frame errors 0, Echo 3404, **MIDI queue overflow 64**.
  Callback peak, queue high water and stack free were not in the screenshot (NOT MEASURED).
  The overflow counter adds MIDI Note-Ons, pitch bends and CCs that the full 64-entry command
  queue dropped, plus releases that became All Notes Off. No audible effect was reported; the
  cause (e.g. a burst of pitch-bend messages) is NOT MEASURED and open.
* Control app: the JUCE standalone EXE and the VST3 work. With Studio One running, the EXE
  handles input noticeably delayed ("as usual"); the VST3 feels better there. Intended use
  (human decision): VST3 inside Studio One, the EXE only standalone without a DAW.
* Modulation matrix: LFO → target works. S9+S16 starts the firmware update.
* Listening impression (own sound, no comparison with the original): the ensemble modes and
  the phaser are particularly impressive, "very musical and convincing". The reverb is
  somewhat weak and metallic; to be improved later (see [FIDELITY.md](FIDELITY.md), Reverb).
