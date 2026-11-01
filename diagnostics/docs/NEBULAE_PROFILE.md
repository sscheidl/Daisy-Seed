# Control panel 0.5.0: Nebulae on Daisy profile

App 0.5.0 adds a **Nebulae** tab for Nebulae on Daisy firmware 0.2.0. It moves the
microSD that stays in the kit between Nebulae and Windows. All diagnostic functions of
0.4.0 are unchanged. Simulation-tested only; real Windows enumeration, drive letters,
transfer speed and USB reconnect are **NICHT GEMESSEN**.

## How the device is found

| Nebulae personality | Transport | How the panel finds it |
| --- | --- | --- |
| Performance | USB-MIDI only (no COM port) | MIDI output ports whose name contains "Daisy"; a port counts only after it answers the STATUS SysEx within 1 s (python-rtmidi) |
| SD management | USB CDC (DDP v1) + mass storage | the normal COM-port session; HELLO `app=NebulaeOnDaisy`, capability `nebmgmt` |

Without python-rtmidi the performance personality is not found; SD management still
works. Protocols: [instruments/granular/docs/CONTROL_PROTOCOL.md](../../instruments/granular/docs/CONTROL_PROTOCOL.md).

## Workflow

| Button | Enabled when | Action |
| --- | --- | --- |
| Share SD card with Windows | performance, card present, not recording/loading; or SD management LOCAL | SHARE SysEx (restart), then `READER a=SHARE` once after the restart |
| Discard Live loop and share / Cancel | the firmware answered CONFIRM_REQUIRED | resend with confirm / keep playing |
| How to eject safely in Windows … | SHARED | explanation only; ejects nothing |
| Return to Nebulae | firmware reports EJECTED (or LOCAL) | `READER a=RETURN` then `NEB a=PERFORM`; waits for USB-MIDI STATUS |
| Acknowledge interrupted access … | INTERRUPTED or ERROR | confirmation dialog, then `READER a=RETURN` |

Rules: the panel shows "ejected" only for the firmware state EJECTED; a timeout means
"ownership unknown"; closing the app, BYE or reconnecting never returns or reshares
the card. Timeouts: STATUS 3 s, SHARE reply 2 s, restart 20 s. If the kit restarts back
into performance instead of SD management, the panel reports it (risk R1).

In SD management the LED, read-test, CV and audio controls are disabled because the
firmware does not advertise them. The saved report contains `nebulae` (NEB status) and
`nebulae_workflow`.

## Simulation and tests

* `DaisyDiagnostic.exe --sim-nebulae` simulates both personalities (no drive is created);
  the Simulation tab's "Simulate Windows eject" / "USB bus reset" act on the card.
* `diagnostics/tests/python/test_nebulae.py` (codec, workflow, failure paths) and
  `test_gui_nebulae.py` (buttons, warning, long texts, report).
* Packaging: `build_exe.cmd` adds `--hidden-import rtmidi`; requirements pin
  `python-rtmidi==1.5.8`.
