# Changelog: Daisy Control Panel (JUCE standalone), Mod on Daisy VST3 and browser editor

Versions come from `CMakeLists.txt` (standalone and VST3) and `web/version.js` (browser).
Hardware results are recorded in the test sheets, never here.

## 0.5.0 (2026-11-08, branch `claude/dcp-sd-page`, hardware NOT MEASURED)

* **Daisy Control Panel**: the standalone app is renamed (window title, EXE
  `Daisy Control Panel.exe`, about line on the SD card page). The CMake target names, the
  module name "Mod on Daisy" and the SysEx `0x7D` stay. The VST3 keeps its name, plugin and
  manufacturer codes and bundle name `Mod on Daisy.vst3` (D3).
* **SD card page** (standalone only, D2): shares the microSD of Nebulae on Daisy 0.2.x and
  the Looper 0.6.x with Windows and brings it back, with the texts, buttons and card-safety
  rules of Daisy Diagnostic 0.6.0's "SD share" tab ([docs/dcp/SD_SHARE_PORT.md](../../docs/dcp/SD_SHARE_PORT.md)).
  The app follows the kit from USB-MIDI to its USB CDC port (Win32 overlapped I/O, SetupAPI
  enumeration by VID/PID, own I/O thread) and back. Instruments without a matrix (plain
  Nebulae, Looper) open on the SD card page (D4).
* **Patch around a share** (D5): before SHARE the current patch is written to
  `%LOCALAPPDATA%\Daisy Control Panel\autosave\<instrument>-<date>.modpatch` and sent back
  after the return. Manual Save/Open is unchanged.
* **Core** (no JUCE): `SdShareProtocol.h` (NB/LP SysEx), `Ddp.h` and `DdpSession.h`
  (DDP v1 over CDC), `SdShare.h` (workflow), `SdShareSim.h` (both instruments, both USB
  personalities), `SerialTransport.h`; tests `tests/test_sd_share.cpp` (byte vectors from the
  Python reference in `tests/sd_share_vectors.txt`) and `tests/test_serial_transport.cpp`.
* **Self-test**: complete share/eject/return cycles for Nebulae MOD (with CONFIRM), Looper
  MOD (with SAVING), plain Nebulae and Looper, an interruption case, screenshots of both pages.
* **VST3**: the SD card page shows only the note "Sharing the SD card needs the Daisy Control
  Panel app (the kit restarts, and the DAW loses the Daisy MIDI port)". Window minimum width
  1200 px (the header gained the page buttons).
* Browser editor unchanged (`web/version.js` stays 0.4.0).
* Independent review before the PR (docs/dcp/SD_PAGE_LOG.md, phase 7): the saved patch is
  restored only to the kit that comes back from the share; timers no longer depend on the
  millisecond counter starting near zero; the Disconnect button releases a port held only by
  the SD workflow; the Win32 serial backend honours Cancel in reads and writes and Close()
  is bounded; the simulator sends its periodic READER line.

## 0.4.0

* Ambient FX reverb LFO rate (control app 0.4.0), Nebulae MOD columns from the device, VST3
  plugin check; see the README sections of the time.
