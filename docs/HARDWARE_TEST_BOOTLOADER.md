# Hardware test: bootloader without BOOT/RESET

Checks the software entry into the Daisy bootloader
([common/bootloader](../common/bootloader/README.md), [FLASHING.md](FLASHING.md)).
All firmwares behave the same: hold **S9 + S16 for five seconds**, or
**Firmware update …** in the editor (for diagnostics: **Firmware update mode …** in the
Windows app). The human operates flashing, buttons and cables; the agent only enters
what was reported. All results are **NOT MEASURED** until someone checks them on the
kit (then **MEASURED** with the date).

Prerequisites: external Daisy bootloader v6.x installed (according to the diagnostics of
17.10.2026 `bl=v6_1`), kit connected via J7, firmware built from this state.
The first test still needs RESET/BOOT once, because the old firmware does not know
the entry.

| # | Test | Expectation | Result |
| --- | --- | --- | --- |
| B1 | Toy Piano: hold S9 + S16 alone for 5 s | After 5 s all 16 LEDs light up, the USR LED pulses, Windows shows DFU `0483:DF11`; the Web Programmer connects | NOT MEASURED |
| B2 | Bootloader waits without a time limit | After B1 wait at least 60 s, then flash: works | NOT MEASURED |
| B3 | LEDs after flashing | New firmware starts, all LEDs go off (`hw.Init()`) | NOT MEASURED |
| B4 | Mis-presses | S9 + S16 + a third button for 7 s: nothing; S9 + S16 for 4 s, release: nothing; S9 alone for 7 s: nothing | NOT MEASURED |
| B5 | Editor (HTML, JUCE, VST3) with Toy Piano | **Firmware update …** → confirmation prompt → "update mode" message, kit as in B1 | NOT MEASURED |
| B6 | Editor without a Mod on Daisy connection: Nebulae, Looper, Ambient Reverb | Kit appears as "Daisy Seed External"; **Firmware update …** with the selected port → kit as in B1 | NOT MEASURED |
| B7 | Buttons in Physical Modeling (normal and MOD build), Nebulae, Looper, Reverb, Mod bench, diagnostics | S9 + S16 for 5 s → kit as in B1 | NOT MEASURED |
| B8 | Looper during a card save | Stop recording, immediately S9 + S16 or the editor button: response immediately, reset only after saving; `loop.wav` readable afterwards | NOT MEASURED |
| B9 | Diagnostics firmware | Windows app **Firmware update mode …** → kit as in B1; in the card reader build locked while the card is shared (button and keys) | NOT MEASURED |
| B10 | Older firmware | Editor button with an image without the entry: "No answer from the kit" after 2 s, kit keeps running | NOT MEASURED |
| B11 | Studio One with input monitoring | Kit's response sent back: no second reset, no loop | NOT MEASURED |
| B12 | Recovery | RESET, then BOOT within about 2 s still works | NOT MEASURED |
| B13 | Looper and Reverb with USB MIDI | Normal operation unchanged (recording, playback, effect), no clicks; unplugging and replugging the USB cable without disturbance | NOT MEASURED |
| B14 | Listening test | Sound of the instruments unchanged | NOT MEASURED |
