# Changelog: Daisy Control Panel (JUCE standalone), Mod on Daisy VST3 and browser editor

Versions come from `CMakeLists.txt` (standalone and VST3) and `web/version.js` (browser).
Hardware results are recorded in the test sheets, never here.

## 0.5.6 / browser 0.4.5 (2026-11-26)

* Daisy Control Panel and VST3 show **BABBAGE** (formerly Acid Seq) for the SQ product `acid-seq` in the
  connection status, the SQ status line, the patch file dialogs, patch error messages and the simulator
  port "BABBAGE (Sim)". The SQ product stays `acid-seq`; the patch format `acid-seq-patch` and the
  `.acidpatch` extension are unchanged.
* The browser editor shows **BABBAGE** in the connection status, the SQ page title, the offline button
  "BABBAGE offline" and its messages.

## 0.5.5 / browser 0.4.4 (2026-11-26)

* Daisy Control Panel shows **KEKULÉ** (formerly Physical Modeling) in the matrix connection status,
  device details, patch compatibility messages and the simulator port "Simulator: KEKULÉ". The Mod on
  Daisy key stays `physical-modeling`; patch files and autosave names keep it.
* The browser editor shows **KEKULÉ** in the connection status and device details.

## 0.5.4 / browser 0.4.3 (2026-11-26)

* Daisy Control Panel shows **MESSIER** in the matrix connection status, device details,
  patch compatibility messages, SD card page and simulator ports. The Mod on Daisy key
  is `nebulae`. Patch files, autosave names and SD share retain their existing identifiers.
* The browser editor shows **MESSIER** in the connection status and device details.

## 0.5.3 / browser 0.4.2 (2026-11-25, branch `claude/ferrum-app-names`, hardware NOT MEASURED)

* **FERRUM display name.** The looper is shown as FERRUM (formerly Looper) on the SD card page, in its messages,
  the update-mode log line and the simulator ports (`Simulator: FERRUM`, `Simulator: FERRUM MOD`). Display text
  only: the SD share profile still matches `app=LooperOnDaisy`, `loopmgmt`, `LOOPER` and SysEx `LP`, the Mod on
  Daisy key stays `looper`, and the SD paths stay `looper/loop.wav` / `looper/loops`.
* The connection status, device info line (native and browser) and the patch-mismatch note show the instrument
  key `looper` as FERRUM; the key itself (IDENTIFY, patch files, autosave names) is unchanged.

## 0.5.2 (2026-11-17, branch `claude/midi-stall-resilience`, hardware NOT MEASURED)

* **A stalled kit no longer takes the app down** (P3 of the Nebulae first-contact test,
  2026-11-08: with the stalling Nebulae 0.2.1 image the standalone and the panel crashed once
  WinMM stopped completing SysEx). Standalone and VST3:
  * The MIDI sender thread moved into the JUCE-free core (`src/core/SendWorker.h`) with a
    watchdog: a message that stays in the driver longer than 2 s, a refused message or a full
    queue marks the link lost. JUCE 9 returns `true` after its own 5 s SysEx timeout, so the old
    port never reported a hang.
  * The editor then closes the port and logs "The Daisy is not answering: … The MIDI port was
    closed; editing continues offline." Connecting again works once the kit answers (the VST3
    retries when the port list changes, e.g. after a power cycle). During an SD-share restart
    the same case is only an info line.
  * JUCE 9 requires MIDI devices to be destroyed on the message thread; the old port let a
    sender stuck in the driver destroy them on its own thread. Now a stuck sender is detached
    without waiting, keeps the devices until the driver returns and then posts their
    destruction to the message thread; after application shutdown began it leaks them instead
    of racing the library's statics. Received SysEx goes to a shared inbox, so a late input
    callback never touches a closed port.
* New host test `tests/test_send_worker.cpp` (fake drivers that block, refuse or throw) and a
  self-test case "stalled kit" (fake driver behind `MidiPort::OpenForTest`, screenshot
  `stalled-kit.png`).
* Browser editor unchanged (`web/version.js` stays 0.4.1).

## 0.5.1 / browser 0.4.1 (2026-11-11, branch `claude/toy-piano-granular-fx`, hardware NOT MEASURED)

* **Device-side changes reach an idle editor** (PR #108 review R5): while connected and nothing is
  open (no edit pending or in flight, no pull running), the standalone, the VST3 (shared core
  `Session::PollCrc`) and the browser editor (`pollCrc`) read the patch CRC every 2 s with `GET_MATRIX`
  and reload the device state once it differs from the editor's. An instrument control that edits the
  patch itself (Toy Piano on Cumulonimbus: the Piano-page source-rate pots) therefore shows up within
  about 2 s. No protocol or firmware change: every firmware answers `GET_MATRIX`. A poll answer is
  ignored if an edit started meanwhile, and a lost poll is silent (the health state reports silence).
  An editor in step does not reload. Traffic: one 10-byte request and its 100-byte reply every 2 s.
* `mod_sim` gets `L <source> <rate>`, an edit on the simulated instrument that sends no message, for
  the browser end-to-end test.

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
* Codex review of the PR (docs/dcp/CODEX_REVIEW_SD_PAGE.md, 15 findings, all addressed): the
  share continuation after the restart belongs to the kit that accepted it and expires with
  the restart window; PERFORM after an eject waits for the confirmed RETURN reply on the same
  connection; the acknowledge question is bound to its interruption; the restored patch goes
  only to the port the share started on; Share is blocked while the matrix is still being
  read and the confirmation re-saves the patch; a kit that power-cycles out of SD management
  is found again on USB-MIDI; other DDP devices (DaisyDiagnostic) are skipped; COM
  enumeration, open and configuration, MIDI sends and the autosave file write left the
  message thread; a failed COM configuration is reported instead of a silent link; READER
  lines with duplicated fields are rejected; the self-test never opens hardware; BYE is
  counted by write completion; expired backoff entries are pruned; the GUI self-test gained
  the no-card, long-text, Looper failed-save, share-gate and acknowledge cases.

## 0.4.0

* Ambient FX reverb LFO rate (control app 0.4.0), Nebulae MOD columns from the device, VST3
  plugin check; see the README sections of the time.
