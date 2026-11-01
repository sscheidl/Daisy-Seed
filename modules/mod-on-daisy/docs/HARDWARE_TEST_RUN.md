# Hardware test run sheet — Mod on Daisy, Toy Piano v0.3, Physical Modeling, editors

**As of:** 24 October 2026 (correction request: E7b in two orders, E11–E17
new). Executed locally under Windows on the Seed3 Desktop Kit.
The firmware images and tools are built and tested on the host. Nothing has been
measured on the device yet.

This run sheet combines the individual checklists into one sequence. It is also
included as `README.md` in the hardware test package.

**Roles:**
* **[A] Agent** (Codex or Claude Code locally): command line, USB MIDI, scripts,
  entering results.
* **[M] Human**: listening, keys and knobs, keyboard, cables, BOOT/RESET. In listening
  tests the human describes the result and the agent enters it.

`P` stands for `python modules/mod-on-daisy/tools/mod_probe.py`, in the package for
`python mod_probe.py`.

## 0. Preparation

| No. | Role | Step |
| --- | --- | --- |
| V1 | A | Update the repo and build the package: `python scripts/hardware_test_package.py`. Result in `build/hardware-test/`. Without a local ARM toolchain, use the CI artifacts (Actions → Firmware or Editor) |
| V2 | A | JUCE editor: get `ModOnDaisyEditor.exe` from Actions → Editor or build it yourself ([editor README](../../../editors/mod-on-daisy/README.md)). With `--editor-exe <path>` it is added to the package |
| V3 | A | `pip install mido python-rtmidi`, then `P selftest` (without device): `PASS` |
| V4 | A | Self-test of the JUCE editor under Windows, result in `out/selftest.txt` (ends with `PASS`). With the CI artifact in the unpacked folder: `.\ModOnDaisyEditor.exe --selftest out`. With your own build from the repo root: `& "build/editor/ModOnDaisyEditor_artefacts/Release/Mod on Daisy Editor.exe" --selftest out` |
| V5 | M | Close Studio One or disable "Daisy Seed External" there. Windows gives the port to only one application |
| V6 | M | Flash according to [docs/FLASHING.md](../../../docs/FLASHING.md): J7, RESET, then BOOT within the 2 s window, Web Programmer or `make PROJECT=<id> program-dfu`. Install the external bootloader v6.x only if it is missing |

Agents flash only after the human has put the kit into bootloader mode and approved
the flashing.

## 1. Measurement firmware ModBench (basis)

Image `ModBench-v0.1.0.bin`, instructions in [bench/README.md](../bench/README.md), plan in
[TEST_PLAN_PHASE2.md](TEST_PLAN_PHASE2.md) § 3–4. Results in
[VALIDATION.md](VALIDATION.md) § 3.

| No. | Role | Step | Expected |
| --- | --- | --- | --- |
| H1 | A | `P ports` | "Daisy Seed External" as input and output |
| H2/H3 | A | `P identify`, then `P lengths` | `mod-bench`; `frame lengths OK` in both directions |
| H4 | M+A | Open Studio One with the port active, then `P identify` | Note the result: can the port be opened at the same time? |
| S0–S8 | A | `P bench --seconds 60 --csv s48.csv` | Table § 3.1: cycles, load, overruns 0, MIDI lost 0 |
| S6 | M+A | Flash `ModBench-v0.1.0-max64.bin`, `P bench --seconds 60 --csv s48-max64.csv` | Scenario S6 valid, no overruns |
| B256 | M+A | Flash `ModBench-v0.1.0-block256.bin`, `P bench --seconds 60 --csv s256.csv` | second table for block 256 |
| H5 | A | `P monitor --seconds 20` with open CV jacks, then with 0 V | mean value and noise of the CV raw values |
| H6 | M+A | Send CC1, Channel AT and Poly AT from the keyboard, with `P monitor` running | values visible; Poly AT assigned to the correct note |
| H7 | M | Listening test: LFO 0.02–20 Hz, modwheel sweep, Poly AT | no zipper noise at block 48 |
| H8 | M+A | LFO on MIDI clock (Studio One) and on gate 1/2 with pulses of 1, 5 and 20 ms | in tempo; start on the start of the bar; pulses counted |

**Abort:** if `P lengths` fails (H2/H3), stop here. Everything else depends on SysEx
arriving cleanly under Windows. Please record the console output.

## 2. Toy Piano v0.3

Image `ToyPiano-v0.3.bin`, checklist **T1–T15** in
[instruments/toy-piano/docs/HARDWARE_TEST_V0.3.md](../../../instruments/toy-piano/docs/HARDWARE_TEST_V0.3.md).

* [M]: T1 (without PC, like v0.2), T5–T13 (listening, keys, keyboard, CV, clock, gate), T15
  (power off and on again).
* [A]: T2–T4, T14 and the `P` commands in T5–T13.
* Results into the table of the checklist and into
  [instruments/toy-piano/docs/VALIDATION.md](../../../instruments/toy-piano/docs/VALIDATION.md).
* **Release:** a draft v0.3 is under GitHub → Releases. Publish only when T1–T15 have
  passed. The human decides.

## 3. Physical Modeling with matrix

Image `PhysicalModelingMod-v0.1.0.bin`, checklist **R1–R10** in
[instruments/physical-modeling/docs/MODULATION.md](../../../instruments/physical-modeling/docs/MODULATION.md).
For comparison in R1, briefly flash `PhysicalModeling-v0.1.0.bin`. This regular
build is byte-identical to the previous state.

* [A]: R2, R3, R9 (`cpu_probe.py`) and the `P` commands.
* [M]: R1 and R4–R8, R10 (listening, poly AT keyboard, clock).

## 4. Editors on the device

With `ToyPiano-v0.3.bin`, for E9 with `PhysicalModelingMod-v0.1.0.bin`. Close `P` and
other MIDI programs beforehand, because the port belongs to only one application.

| No. | Role | Step | Expected |
| --- | --- | --- | --- |
| E1 | M | Start `ModOnDaisyEditor.exe`, select the Daisy port, **Connect** | Status "toy-piano 0.3 · ID 0"; columns as in T3; Poly AT under Attack/Decay and columns 6–8 hatched |
| E2 | M | Hold a note with long decay, drag LFO 1 → Pitch to +30 | vibrato audible; yellow dot disappears; at the bottom "Device confirmed (patch CRC …)" |
| E3 | M | Watch the live display, move modwheel and aftertouch | LFO bars move; Modwheel, Channel AT and Poly AT deflect; the pitch bar under the column shows the offset |
| E4 | M | LFO 1: shape S&H, change the rate; Random: smooth | audible, without clicks |
| E5 | M | Create 8 connections, try a 9th | the 9th stays off (dashed), message "At most 8 …" |
| E6 | M | Save file, Clear matrix, Open file | sound as before clearing; the same file also loads in the browser editor |
| E7 | M | Open Studio One with the Daisy port active, then **Connect** in the editor | clear message "… in use (e.g. Studio One) …", no crash |
| E8 | M | While connected, pull the USB cable at J7, plug it back in, **Connect** | "Device disconnected", still editable offline; afterwards state loaded from the device |
| E9 | M | Physical Modeling: connect, Poly AT → Brightness +70, hold a chord, press one key | Poly AT × Excitation hatched; only the pressed voice changes |
| E9b | M | Physical Modeling (`0.1.0-mod4`), **"Daisy Seed External"** selected in the editor (no simulator): turn the pots, Modwheel → Level −100 | Column headers show a white line at the base value that follows the pot, and above it the current value (e.g. "63 %", Pitch in semitones); for Level it stays fixed at 0.5 ("+0 %"). Column 7 is called "Level", the modwheel fades out to silence |
| E7b | M | Check several programs per port (Windows MIDI Services), see [editor README](../../../editors/mod-on-daisy/README.md#studio-one-und-der-editor-gleichzeitig). **Both orders**: (a) Studio One first, then the editor; (b) the editor first, then Studio One | Note the result for each order: do both work at the same time? For (b): does Studio One get notes, does the editor stay green? |
| E11 | M | Physical Modeling, LFO 1 → Structure +50, LFO 1 free at 20 Hz, then MIDI clock 150 BPM 1/32 from Studio One | LFO 1 bar shows a wide band (not a stationary value); column header Structure shows the range around the base value, red ends when it hits 0 or 1. Stop the clock: the "Clock" row reports "No MIDI clock – LFO stopped", the band shrinks |
| E12 | M | Structure pots at approx. 10 %, 50 %, 90 %, each with LFO ±50 | range roughly 0…0.6 / 0…1 / 0.4…1, clamped ends red; sound matches the display |
| E13 | M | Keyboard with Channel AT only, then with Poly AT only; with Poly AT hold two keys and press only on the **first**; release | Display "Aftertouch · Channel" or "Poly · Note", dot at the matching row; for the first of two keys it shows that key's pressure; 0 after release. Sound effect as before |
| E14 | M | LFO 2 slow (0.3 Hz) → nothing; LFO 1 → Pitch +20, "FM ← L2" at +60 | LFO 1 periodically gets faster and slower (audible in the vibrato). With LFO 1 on MIDI clock the FM control is locked |
| E15 | M | Open the file `legacy-reverb.modpatch` (from `editors/mod-on-daisy/web/tests/`) in the connected editor | Question "Patch does not exactly match …" names "Reverb → Level"; "Cancel" changes nothing; "Load without affected" leaves Modwheel → Level switched off |
| E16 | M | Studio One holds the port, then **Connect** in the editor; then close Studio One and **Connect** again (both editors) | First attempt: clear message, port is released; second attempt connects without restarting the editor |
| E17 | M | Connected, then enable the Daisy input in Studio One (if possible) or pull the cable | After approx. 1.5 s the status turns yellow "no live data", after 5 s red "no reply", one message in the log; after recovery green and "reloading state" |
| E18 | M | Physical Modeling (`0.1.0-mod4`): SW17/SW18 through all six positions, then CC28 from Studio One or from the keyboard | At the top next to the status, "Model: …" matching the switch position (tooltip: bank/SW18); after CC28 "… (MIDI)". Both editors the same |
| E19 | M | Connect Toy Piano v0.3 | No model display; column headers keep showing the excursion (Toy Piano reports no base values) |
| E10 | M | `editor/ModOnDaisyEditor.html` in Chrome/Edge, allow SysEx, connect, change a cell | Confirmation with patch CRC as in the JUCE editor |
| P1 | M | VST3: copy `Mod on Daisy.vst3` to `C:\Program Files\Common Files\VST3\`, rescan in Studio One, put "Mod on Daisy" as an effect on a track (kit with mod4 on J7, Daisy active in Studio One) | Plug-in appears under Daisy-Seed; it connects without opening the window (open the window: status green, model shown); sound of the track unchanged |
| P2 | M | In the plug-in set LFO 1 → Pitch +20, close the window, play notes | vibrato audible; reopen the window: everything as before, status green, live display running |
| P3 | M | Save the song, close Studio One, power the kit off and on (empty matrix), open Studio One and the song | Plug-in connects and sends the song patch: vibrato back, grid shows LFO 1 → Pitch +20 |
| P4 | M | In the plug-in "Disconnect", then unplug the kit and plug it back in | Stays disconnected until "Connect"; normal afterwards |
| P5 | M | Open and connect the standalone editor in addition to the plug-in | Both green; a change in one appears in the other after "Load from device" |

Results into [VALIDATION.md](VALIDATION.md) § 1b (JUCE editor) or § 1a
(browser editor) under a new item "On the device".

## 5. Recording results

* Enter only what was observed or measured. Mark every line:
  `MEASURED` with date or `NOT MEASURED`.
* Console outputs and CSV files (`s48.csv`, `s256.csv`, …) belong in the report
  or as an attachment in the PR.
* On a failure, first record the exact output and the steps, then fix it in the same
  branch. The rule is: host tests and CI green before going back to the device.
* Commit results on a separate branch and merge them via PR.
