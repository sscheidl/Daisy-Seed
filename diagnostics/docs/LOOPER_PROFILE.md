# Control panel 0.6.0: Looper profile (SD share tab)

App 0.6.0 renames the **Nebulae** tab to **SD share** and adds the Looper (firmware
0.5.0) next to Nebulae on Daisy 0.2.0. Both instruments hand their microSD to Windows
the same way: a restart into an SD management mode with the shared card reader
(USB mass storage) and DDP over USB CDC. Everything in the
[Nebulae profile](NEBULAE_PROFILE.md) applies (buttons, ownership rules, timeouts,
interruption quarantine); this page lists what is different for the looper.
Simulation-tested only; real Windows enumeration, drive letters, transfer and save speed
are **NICHT GEMESSEN**.

## How the instrument is found

One workflow owns the MIDI port, because Windows MIDI ports are usually opened by one
program part at a time. It probes each port whose name contains "Daisy" with the STATUS
SysEx of both instruments; each firmware ignores the other's request. The instrument that
answers within 1 s decides the texts and commands. In SD management the HELLO `app`
decides (`NebulaeOnDaisy` or `LooperOnDaisy`).

| Looper personality | Transport | How the panel finds it |
| --- | --- | --- |
| Performance | USB-MIDI only | STATUS SysEx `F0 7D 4C 50 01 01 F7` answered within 1 s |
| SD management | USB CDC (DDP v1) + mass storage, `Looper SD` | HELLO `app=LooperOnDaisy`, capability `loopmgmt`, status message `LOOPER` |

Protocol: [instruments/looper/docs/SD_MANAGEMENT.md](../../instruments/looper/docs/SD_MANAGEMENT.md).

## Differences from Nebulae

| | Nebulae on Daisy | Looper |
| --- | --- | --- |
| Data in RAM at share | Live loop is lost: CONFIRM_REQUIRED unless confirmed | the loop is **saved to `looper/loop.wav` first**: the firmware answers SAVING, the panel shows "Saving the loop before sharing …" and asks again every 0.5 s (up to 180 s; Cancel stops waiting) |
| Confirmation | Live loop exists | only when the loop cannot be saved (the last card write failed): "Share without the unsaved changes" |
| Refused while | recording, sample loading | recording, overdub, resample, WAV import |
| Info rows | bank / sample | loop length and save state ("saved in looper/loop.wav", "not saved yet", "saving …", last card write failure) |
| Return | `NEB a=PERFORM`, banks rescanned | `LOOPER a=PERFORM`, `loop.wav` reloaded |
| Card format | FAT32 or exFAT | FAT32 (exFAT for the looper's own writes is a later firmware step) |

The saved report contains `looper` (LOOPER status line in SD management) and
`nebulae_workflow` with `"profile": "Looper"` (the key keeps its 0.5.0 name).

## Simulation and tests

* `DaisyDiagnostic.exe --sim-looper` simulates both looper personalities, including the
  save before the share; `--sim-nebulae` is unchanged.
* `diagnostics/tests/python/test_looper.py`: codec, LOOPER status line, probe (looper and
  Nebulae through the same two-profile workflow), save-then-share, failed save with
  confirmation, cancel, save timeout, refusals, management start, NEB rejected.
* `diagnostics/tests/python/test_gui_looper.py`: tab, info rows, buttons, warning,
  report.
