# Daisy Control Panel 0.5.0 hardware test sheet: matrix and SD card in one app

All results are **NOT MEASURED** until you run them on the kit. Write what you observe, with
the date, in the Result column: PASS, FAIL or a value. Do not fill in expected values.

| Item | Value |
| --- | --- |
| Tester / date | |
| Package revision (`MANIFEST.json`) | |
| App | `apps/Daisy Control Panel.exe` (from `apps/daisy-control-panel/0.5.0+<commit7>/`) |
| Firmware | `firmware/NebulaeMod-v0.2.1.bin`, `firmware/NebulaeOnDaisy-v0.2.1.bin`, `firmware/LooperMod-v0.6.1.bin`, `firmware/Looper-v0.6.1.bin` |
| SHA-256 checked with `SHA256SUMS` | |
| Kit, USB port, Windows build | |

Background: [SD_PAGE_WORK_ORDER.md](SD_PAGE_WORK_ORDER.md), the port specification
[SD_SHARE_PORT.md](SD_SHARE_PORT.md), the app documentation
[editors/mod-on-daisy/README.md](../../editors/mod-on-daisy/README.md). The app replaces the
editor/panel switcher of the Nebulae MOD package: one program holds J7 in both USB modes.

## 0. Prepare

Use a **test card with a backup**. You need one FAT32 and one exFAT card (or format the same
one between sections 4 and 5), each with WAVs in `NEBULAE/SAMPLES/` and at least one subfolder
as a second bank; for the Looper a `looper/` folder with `loop.wav` and `loops/`. Close any DAW
or browser tab that uses the Daisy MIDI port. Daisy Diagnostic must not be open while the app
runs (both would open the same COM port in SD management).

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 0.1 | Flash `NebulaeMod-v0.2.1.bin` over J7 ([FLASHING.md](../FLASHING.md)); no bootloader install | Kit restarts and plays | NOT MEASURED |
| 0.2 | Connect J7, kit in NEBULAE mode, card in the slot | First sample loads, D16 off | NOT MEASURED |
| 0.3 | Start `apps/Daisy Control Panel.exe` | Window title "Daisy Control Panel 0.5.0"; header shows **Matrix** and **SD card**; SD card page says "No Nebulae or Looper found" | NOT MEASURED |

## 1. J7 connection

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 1.1 | Choose "Daisy Seed External" and click **Connect** | Status pill `nebulae 0.2.1`; matrix columns Start … Window | NOT MEASURED |
| 1.2 | Click **SD card** | Heading "Performance (USB-MIDI)", Instrument "Nebulae on Daisy", Firmware 0.2.1, "card inserted", bank row, Share enabled, Return and Acknowledge disabled | NOT MEASURED |
| 1.3 | Click **Matrix** | The matrix is still connected (the pill stays green) | NOT MEASURED |
| 1.4 | Click **Firmware update …**, then cancel without confirming | Dialog opens; the kit keeps playing | NOT MEASURED |

## 2. Matrix with an audible LFO route

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 2.1 | LFO 1 sine, 1 Hz, to Pitch at depth 30 | Smooth vibrato, no zipper noise; cell confirmed (no yellow dot) | NOT MEASURED |
| 2.2 | Turn VR5 (Pitch) | The white base line of the Pitch column follows the pot; the vibrato stays | NOT MEASURED |
| 2.3 | **Save file …** as `before-share.modpatch` (manual save still works) | File written | NOT MEASURED |

## 3. CONFIRM with a Live loop

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 3.1 | Record a Live loop on the kit (S1), stop | SD card page row "Bank / sample" still shows the bank; the red warning "A Live loop exists only in RAM …" appears | NOT MEASURED |
| 3.2 | While recording, click **Share SD card with Windows** | Refused as busy; kit keeps playing | NOT MEASURED |
| 3.3 | Stop recording, click **Share SD card with Windows** | Heading "Confirmation required"; buttons **Discard Live loop and share** and **Cancel** appear; the log says the patch was saved to `%LOCALAPPDATA%\Daisy Control Panel\autosave\` | NOT MEASURED |
| 3.4 | Click **Cancel** | Heading back to "Performance"; the kit keeps playing; the matrix is still connected (pill green) | NOT MEASURED |

## 4. SHARE, eject, RETURN on FAT32 (the patch survives the share)

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 4.1 | Select a sample in bank 2, note bank and slot | | NOT MEASURED |
| 4.2 | Click **Share SD card with Windows**, then **Discard Live loop and share** | Fade, kit restarts; heading "Restarting into SD management …", then "SD management (USB CDC + mass storage)"; Windows shows a drive; D2 steady; the matrix pill goes offline without an error dialog | NOT MEASURED |
| 4.3 | Time from the click to the drive letter | seconds: | NOT MEASURED |
| 4.4 | Row "Connection" | "USB CDC COMx + mass storage"; row "SD card": "card reader: SHARED" | NOT MEASURED |
| 4.5 | Copy a new WAV into a bank, then eject in Windows (Safely Remove Hardware) | Page shows "Windows ejected the card …", **Return to Nebulae** enabled, D4 steady | NOT MEASURED |
| 4.6 | Click **Return to Nebulae** | Heading "Returning to performance …", kit restarts, then "Performance (USB-MIDI)"; the log says "Back in performance. Banks were rescanned from the card." | NOT MEASURED |
| 4.7 | Click **Matrix** | The matrix reconnected by itself; the log says "Patch restored after the share (…)"; the vibrato of 2.1 is audible again; same bank and slot as 4.1 | NOT MEASURED |
| 4.8 | Play the new WAV | Loads and plays | NOT MEASURED |
| 4.9 | Compare with `before-share.modpatch` (**Open file …**) | Nothing changes (same patch) | NOT MEASURED |

## 5. SHARE, eject, RETURN on exFAT

Repeat with an exFAT card.

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 5.1 | Steps 4.1, 4.2 (without a Live loop: no confirmation), 4.4 to 4.8 | As section 4 | NOT MEASURED |

## 6. Pulling the cable during SHARED (interrupted access)

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 6.1 | Share the card (4.2), wait for the drive, then pull J7 | Page: "SD management connection lost; card ownership is unknown." | NOT MEASURED |
| 6.2 | Reconnect J7 | Page reconnects over USB CDC and asks STATUS only: row "SD card" shows the reader state the firmware reports (SHARED if Windows re-mounted it, INTERRUPTED after a bus reset); nothing is shared or returned by the app | NOT MEASURED |
| 6.3 | If INTERRUPTED: **Return to Nebulae** is disabled; **Acknowledge interrupted access …** asks "Windows may have lost pending changes …"; answer No | State unchanged | NOT MEASURED |
| 6.4 | Acknowledge, answer Yes | Row "SD card": "card reader: LOCAL"; **Return to Nebulae** enabled; the card is not re-shared | NOT MEASURED |
| 6.5 | **Return to Nebulae** | Back in performance; matrix reconnects | NOT MEASURED |

## 7. Plain Nebulae and Looper (no matrix)

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 7.1 | Flash `NebulaeOnDaisy-v0.2.1.bin`, connect in the app | The app switches to the SD card page by itself; the status pill says "Nebulae on Daisy 0.2.1 · no matrix, SD card page"; the matrix page says "Nebulae on Daisy 0.2.1 has no modulation matrix. Use the SD card page." | NOT MEASURED |
| 7.2 | Share, eject, Return (as 4.2, 4.5, 4.6) | Works without a matrix; the log says there was nothing to save | NOT MEASURED |
| 7.3 | Flash `Looper-v0.6.1.bin`, connect | SD card page: Instrument "Looper", row "Loop" with length and "saved in looper/loop.wav"; **Return to the Looper** | NOT MEASURED |
| 7.4 | Record a loop, then Share | Heading "Saving the loop before sharing …" first (SAVING), then the restart; **Cancel** visible while saving | NOT MEASURED |
| 7.5 | Eject, **Return to the Looper** | Back in performance; the loop is reloaded from `looper/loop.wav` | NOT MEASURED |
| 7.6 | Flash `LooperMod-v0.6.1.bin`, connect | Matrix columns Speed … Mix and the SD card page both work; one share/return cycle with a route restored | NOT MEASURED |

## 8. VST3 in Studio One with the D2 note

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 8.1 | Install `apps/Mod on Daisy.vst3` to `C:\Program Files\Common Files\VST3\`, rescan, open a Studio One song that used the 0.4.0 plugin | The plugin loads under the same name "Mod on Daisy" (same plugin codes); the song's patch is sent to the kit | NOT MEASURED |
| 8.2 | Click **SD card** in the plugin window | Only the note "Sharing the SD card needs the Daisy Control Panel app (the kit restarts, and the DAW loses the Daisy MIDI port)"; no buttons | NOT MEASURED |
| 8.3 | Edit a cell in the plugin | Confirmed by the device as before | NOT MEASURED |

## 9. Notes

Write anything unexpected here: timings, error texts, LED states, Windows messages.
