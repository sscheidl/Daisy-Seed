# Nebulae MOD hardware test sheet

All results are **NOT MEASURED** until you run them on the kit. Write what you observe,
with the date, in the Result column: PASS, FAIL or a value. Do not fill in expected values.

| Item | Value |
| --- | --- |
| Tester / date | |
| Package revision (`MANIFEST.json`) | |
| Firmware | `firmware/NebulaeMod-v0.2.1.bin` |
| SHA-256 checked with `SHA256SUMS` | |
| Kit, USB port, Windows build | |

Background: [MODULATION.md](MODULATION.md). In the package, start
**`Start-Nebulae-Mod-Test.cmd`** and switch with its two buttons: **Modulation** opens the
editor, **SD card** opens the panel. Only one can use J7.

## 0. Prepare

Use a **test card with a backup**. You need one FAT32 and one exFAT card (or format the
same one between sections 6 and 7), each with WAVs in `NEBULAE/SAMPLES/` and at least one
subfolder as a second bank. Close any DAW or browser tab that uses the Daisy MIDI port.

The editor holds J7 alone, so no other program can send MIDI to the kit while it is open.
This sheet therefore uses LFOs, CV and the kit's own keys. Steps marked *VST3* need the
editor inside a DAW, which can send MIDI on the same port; skip them if you do not use one.

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 0.1 | Flash the application over J7 ([FLASHING.md](../../../docs/FLASHING.md)); no bootloader install | Kit restarts and plays | NOT MEASURED |
| 0.2 | Connect J7, kit in NEBULAE mode, card in the slot | First sample loads, D16 off | NOT MEASURED |

## 1. J7 connection

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 1.1 | Click **Modulation** | Editor connects by itself; pill shows `nebulae 0.2.1`; tick 1000 Hz | NOT MEASURED |
| 1.2 | Click **Firmware update ...**, then cancel without confirming | Dialog opens; the kit keeps playing | NOT MEASURED |

## 2. Eight targets

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 2.1 | Read the column headers | Start, Speed, Size, Density, Pitch, Overlap, Blend, Window; no grey cell | NOT MEASURED |
| 2.2 | Turn VR1...VR8 in turn | The white base line of the matching column follows the pot | NOT MEASURED |
| 2.3 | *VST3:* send CC20...CC27, then move each pot back through its value | Base follows the CC; the pot takes over only after reaching it (pickup) | NOT MEASURED |
| 2.4 | *VST3:* send CC1 with Modwheel to Start at depth 50 | The column moves with the wheel; the base line stays | NOT MEASURED |

For each target, route LFO 1 (sine, 1 Hz) to that column at depth 50, listen, then click
**Clear matrix**:

| Target | Audible change with the LFO | Returns when cleared | Result |
| --- | --- | --- | --- |
| Start | read position moves | yes | NOT MEASURED |
| Speed | scan speed changes | yes | NOT MEASURED |
| Size | loop window changes | yes | NOT MEASURED |
| Density | grain rate changes | yes | NOT MEASURED |
| Pitch | pitch changes | yes | NOT MEASURED |
| Overlap | grain length changes | yes | NOT MEASURED |
| Blend | vocoder/granular balance changes | yes | NOT MEASURED |
| Window | grain envelope changes | yes | NOT MEASURED |

## 3. Audible LFO route

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 3.1 | LFO 1 sine, 1 Hz, to Pitch at depth 30 | Smooth vibrato, no zipper noise | NOT MEASURED |
| 3.2 | Time ten cycles | About 10 s; compare with the audio load in section 4 | NOT MEASURED |
| 3.3 | Raise the rate to 20 Hz | Depth falls (20 ms smoother); no clicks | NOT MEASURED |
| 3.4 | LFO 1 to Start and Size, depth 40, then record (S1) | Start, Size and Speed hold until the capture ends | NOT MEASURED |
| 3.5 | During 3.1, SW18 down (KEYBOARD), hold one key, then a second | The key transposes on top of the vibrato; the last key wins | NOT MEASURED |
| 3.6 | Gate 1 into trigger mode (SOURCE+FREEZE), matrix running | Every pulse still launches grains | NOT MEASURED |
| 3.7 | CV 1 to Density, depth 50 | Density follows the CV | NOT MEASURED |

## 4. Callback peaks and overruns

STATUS shows `peak ... % of the block, ... overruns` on the panel's **SD share** tab. These
are firmware counters since the last restart. Reading them does not share the card, so use
**SD card**, read, and do not click Share. Restart the kit before each row.

| # | Matrix during 2 minutes of playing | Peak % | Overruns | D16 | Result |
| --- | --- | --- | --- | --- | --- |
| 4.1 | Empty | | | | NOT MEASURED |
| 4.2 | One LFO route | | | | NOT MEASURED |
| 4.3 | Eight routes, all columns, monitor open | | | | NOT MEASURED |
| 4.4 | As 4.3, while loading samples in SAMPLE mode | | | | NOT MEASURED |

## 5. Patch save and reload

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 5.1 | Build a patch with four routes, **Save file ...** | A `.modpatch` is written | NOT MEASURED |
| 5.2 | Power-cycle the kit, editor reconnects | Matrix on the device is empty | NOT MEASURED |
| 5.3 | **Open file ...** with the saved patch | Grid, sources and sound return; log shows `Device confirmed` | NOT MEASURED |

## 6. SD share on FAT32

Save the patch first. Create a Live loop so the confirmation appears.

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 6.1 | Select a sample in bank 2, note bank and slot | | NOT MEASURED |
| 6.2 | Click **SD card**, answer the save question with Yes | Editor closes first, then the panel opens; nothing is shared | NOT MEASURED |
| 6.3 | Tab **SD share**: panel finds the kit in performance | STATUS line appears, firmware 0.2.1 | NOT MEASURED |
| 6.4 | While recording, click **Share SD card with Windows** | Refused as busy; kit keeps playing | NOT MEASURED |
| 6.5 | Stop recording, Share again | Warning: the Live loop will be lost | NOT MEASURED |
| 6.6 | Confirm | Fade, kit restarts, Windows shows a drive, D2 steady | NOT MEASURED |
| 6.7 | Copy a new WAV into a bank, then eject in Windows | Panel shows ejected, D4 steady | NOT MEASURED |
| 6.8 | **Return to Nebulae** | Panel shows Performance; same bank and slot as 6.1 sound | NOT MEASURED |
| 6.9 | Play the new WAV | Loads and plays | NOT MEASURED |
| 6.10 | Click **Modulation**, answer Yes | Panel closes first, editor reconnects, matrix empty | NOT MEASURED |
| 6.11 | **Open file ...** with the saved patch | Routes work again | NOT MEASURED |
| 6.12 | Interrupted access: Share, then pull J7 before ejecting; reconnect | Card stays quarantined until you acknowledge it in the panel | NOT MEASURED |

## 7. SD share on exFAT

Repeat with an exFAT card.

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 7.1 | Steps 6.1 to 6.3, 6.5 to 6.11 | As section 6 | NOT MEASURED |

## 8. Notes

Anything unexpected: what you did, what you heard or saw, LEDs, panel text.
