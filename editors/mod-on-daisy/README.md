# Mod on Daisy Editor

Graphical editor for the [Mod on Daisy](../../modules/mod-on-daisy/) modulation matrix.
The central control is the fully visible **8×8 grid matrix** modeled on the
MatrixBrute/MiniFreak. In addition there are the source parameters. The
connection to the Daisy runs over USB MIDI.

| Version | Status |
| --- | --- |
| **JUCE editor** ([`src/`](src/)) | **usable**: standalone for Windows (`.exe` from CI), Linux and macOS via CMake. Simulator only for development and self-test (`--simulators`) |
| **Browser editor** ([`web/`](web/)) | **usable**: quick variant for Chrome/Edge (Web MIDI), no installation |
| **VST3 plugin** ([`src/plugin/`](src/plugin/)) | **new, not yet tested on the device**: the same editor in Studio One, patch stored in the song ([below](#vst3-plugin-for-studio-one)) |

Both versions read and write the same `.modpatch` files.

## Acid Seq (SQ protocol)

Since Acid Seq phase 5, all three versions also operate the Acid Seq firmwares (`acid-seq`,
`step-seq`, `acid-303`, `drive-fx`, `delay-fx`) via the
[SQ protocol](../../sequencers/step-seq/docs/PROTOCOL.md) (`F0 7D 53 51`).

* **Detection:** When connecting, the editor queries SQ first. If the device answers,
  the Acid Seq pages appear and no matrix is read. Without an answer (about 0.4 s) the
  existing Mod on Daisy connection runs; Mod on Daisy v1 stays unchanged.
* **The device takes precedence:** Connecting only reads (components, parameters,
  pattern, state, calibration). Writing only happens on a change or with
  "Send to device".
* **Pages:**
  * Status with transport state (STOP/WAITING/RUNNING/PAUSE), SW17, SW18, clock source,
    tempo and applied or accepted revision.
  * Start/Stop (only takes effect with SW17 on Run).
  * Pattern grid with 16 steps, note names and running light, plus a step editor.
  * Parameter pages Sequencer, Voice, Drive, Delay and MIDI. They are built from the
    device's descriptors; the labels come from the parameter contract.
  * Calibration of both CV outputs with separate enabling. The enable is never
    stored. The browser remembers calibration data per kit UID.
  * Error counters.
* **Patch** (`.acidpatch`, JSON): pattern and all parameters of all components, each with
  component and schema version. It can be loaded without a device (browser: "Acid Seq
  offline"). Missing or incompatible components are shown and skipped.
  Calibration belongs to the kit and is not in the patch. `.modpatch` stays
  readable unchanged.
* **VST3:** With an Acid Seq device, the song state is the `.acidpatch`. When a
  song is loaded, the plugin asks whether the patch should be sent: "Send", "Always
  send" (for this plugin instance) or "Don't send".
* **Simulator:** With `--simulators` there is the port "Acid Seq (Sim)", the
  firmware's SQ device side running on the PC.
* **Fixes after the Codex bug hunt** (2026-10-27,
  [report](../../docs/acid-seq/CODEX_FEHLERSUCHE.md)):
  * Browser: the timers now also run in Chromium (F16). Before, every
    connection to an SQ device failed.
  * JUCE: "Connected" only once all descriptors, values and labels have been read (F05).
  * Both versions:
    * `.acidpatch` with invalid numbers is rejected (F07).
    * Values are clamped to the device's range before sending.
  * JUCE: calibration shows and sends fractions exactly (F13).
  * Browser: calibration shows "valid" after sending.

## Streichwurst (SQ 1.1 + Mod on Daisy, from editor 0.3.0)

All three versions operate the [Streichwurst firmware](../../instruments/streichwurst/README.md)
over one port: sound parameters via SQ 1.1, the modulation matrix via Mod on Daisy
([SQ § 8](../../sequencers/step-seq/docs/PROTOCOL.md)).

* **Detection:** If an SQ device reports capability bit 6, the editor additionally starts
  its Mod on Daisy session on the same port after reading the SQ state. Both read
  first; writing only happens on a change or an explicit send. The
  status line shows "streichwurst 0.1.0 · SQ + Matrix". Acid Seq devices (SQ 1.0) and
  pure Mod on Daisy devices behave as before.
* **Pages:** Strings, Solo, Keyboard, FX and Global, built from the device's descriptors
  and the parameter contract's pages; status line with product, instrument and
  FX version as well as applied/accepted revision. "Pot page on the kit" and "RAM preset"
  are read-only. Pot movements on the kit appear in the app via the monitor.
* **Matrix:** The "Matrix" button (top) shows the familiar grid view with the eight
  Streichwurst targets; "Sound parameters" goes back.
* **Patch** (`.swpatch`, JSON, format `streichwurst-patch`): all writable parameters
  of instrument, FX and product with component and schema version, plus the matrix as an
  embedded `.modpatch` object. When sending, missing or incompatible
  parts are skipped; the matrix goes through the familiar column check. In the VST3 the
  `.swpatch` is the song state, with the same prompt as for Acid Seq. `.modpatch` and
  `.acidpatch` stay unchanged.
* **Offline:** Browser "Streichwurst offline" shows the pages from the contract.
* **Standalone FX** (`StreichwurstFx-v0.1.0`, SQ 1.0, one component): only the FX page, no
  matrix.
* **Simulator:** With `--simulators` there is "Streichwurst (Sim)": the firmware's SQ device, register and
  Mod on Daisy core on the PC (no audio DSP in the editor).

## JUCE editor

### Getting the Windows program

CI builds `ModOnDaisyEditor.exe` and the VST3 plugin `Mod on Daisy.vst3`:

1. On GitHub under **Actions → Editor**, open the latest green run. The run
   only starts manually ("Run workflow", choose the branch).
2. At the bottom under **Artifacts**, download and unpack `ModOnDaisyEditor-windows-x64-…`.
3. Start `ModOnDaisyEditor.exe`. No installation is needed; the
   Visual C++ runtime is built in. The same package contains the folder
   `Mod on Daisy.vst3` for Studio One ([below](#vst3-plugin-for-studio-one)).
   `SHA256SUMS` lists the checksums of both files.

Windows SmartScreen may warn on the first start because the file is not signed.
Then choose "More info" and "Run anyway".

### Operation

1. Connect the Daisy with Mod on Daisy firmware to J7, for example `ToyPiano-v0.3.1.bin`,
   `PhysicalModelingMod-v0.1.1.bin` or `LooperMod-v0.4.0.bin`.
2. **Close Studio One** or deactivate the port "Daisy Seed External" there.
   On Windows only one application can open the port. If it is in use, the
   editor says so.
3. Choose the device at the top (a Daisy port is suggested, even if the kit only appears after
   the editor has started) and click **Connect**. The editor reads the instrument,
   columns and current state from the device.

**Firmware update …** (top right) restarts the kit into the Daisy bootloader without the
BOOT and RESET buttons on the Seed3 – even with the case closed. After a confirmation
prompt the editor sends its own SysEx message to the selected port
([common/bootloader](../../common/bootloader/README.md)); this works with every firmware in this
repository (all instruments and effects have USB MIDI on J7), even without a
Mod on Daisy connection, e.g. with Nebulae, Looper or Ambient Reverb. The kit
replies as soon as it has accepted the request; the reply is not yet proof of
DFU entry. Then all 16 LEDs light up and the bootloader waits without a time limit
(the Looper finishes running card work first). Then load the
firmware with the [Daisy Web Programmer](https://flash.daisy.audio/) or `make program-dfu`
([docs/FLASHING.md](../../docs/FLASHING.md)). If there is no answer after 2 s, the
firmware does not know the command yet: then hold S9 and S16 for five seconds or use RESET and
BOOT. With a simulator the button is disabled.

**Without a Daisy:** The list contains only MIDI ports; without a device you can edit offline.
The simulators ("Simulator: Toy Piano", "Simulator: Physical Modeling") only appear with
`"Mod on Daisy Editor" --simulators` and in the self-test. They are the firmware's controller and
engine code, compiled on the PC, and identify themselves like the device
(e.g. `physical-modeling 0.1.0-mod3`), but have no pots. In the hardware test of
2026-10-24 it was therefore impossible to tell that the simulator was connected instead of the kit;
since then they no longer appear in the normal device list.

* **Grid**:
  * Rows are the 8 sources, columns are the instrument's targets. Under each
    column name it says what depth 100 means; "note-on" means the target only takes effect
    from the next note.
  * Hatched cells are not available; the tooltip gives the reason.
  * A yellow dot in the cell means: not yet confirmed by the device.
* **Changing the depth**:
  * Drag the cell vertically (fine with `Shift`) or use the mouse wheel (±1, with
    `Shift` ±10).
  * Or the knob or number field on the right.
  * Keyboard: arrow keys select, `+`/`−` ±1, `Page Up`/`Page Down` ±10, `0` to zero,
    `Space` on/off.
  * Right-click: Set to 0, Invert, on/off.
* **Double-click** switches a connection off and on again; the depth is kept
  (shown in brackets, dashed border).
* **Limit**: At most 8 active connections. A further cell receives its value
  but stays switched off; the editor reports this.
* **Sources**:
  * LFO 1/2: Shape (Sine, Triangle, Saw ↑/↓, Square, S&H).
  * Sync: Free (then Rate and Retrigger), MIDI clock (then Division) or Gate (then
    Input and cycles per pulse).
  * Random: mode Stepped or Smooth, Sync as for the LFOs.
  * **FM ← L2 / FM ← L1** (only devices with LFO cross modulation, e.g. Physical Modeling
    `0.1.0-mod3`): the other LFO changes this LFO's rate, ±100 = ±2 octaves.
    Only effective with Sync = Free; with Clock or Gate the knob is disabled, the value
    is kept.
  * With Sync = MIDI clock or Gate, the **Clock** row shows whether the clock is arriving
    ("No MIDI clock – LFO stopped"). Without a clock the LFO stops.
* **Live display** (20 frames/s):
  * **Sources**: With the extended monitor (Physical Modeling `0.1.0-mod3`) the
    device reports which range each source has covered in the last 50 ms. The bright
    band is that range, the line is the current value. Slow sources show a
    narrow band, fast ones what they really sweep – also with MIDI clock
    or Gate. Older firmware (Toy Piano v0.3) only reports the instantaneous value; then
    a full band indicates a free LFO above 5 Hz.
  * Dimmed: the source has no active connection; the firmware then does not compute
    it.
  * **Model** (at the top next to the status): for Physical Modeling from `0.1.0-mod4` the model that
    is currently sounding, e.g. "Model: Western Chords"; "(MIDI)" if it was last selected via CC28.
    The tooltip names the switch position (bank A = SW17 not pressed, B = pressed;
    SW18 up/middle/down) and all models.
  * **Column headers**: If the instrument reports base values, the **current
    value** of the pot or CC position is shown under the name (e.g. "63 %", Pitch in semitones around the center, Level
    "+0 %" = gain 1). What depth 100 does is shown in the tooltip and in the cell card.
  * **Targets**: The white line is the column's **base value** before modulation:
    the pot or CC position; for Physical Modeling "Level" a fixed value of 0.5
    (= gain 1, no pot; pot 7 still controls the reverb time). The band shows where
    the modulation drives the **target value**, the accent-colored line where it currently
    is. Red ends: the modulation hits 0 or 1. The target value is the value
    before smoothing in the instrument and only the **global part**: Poly AT on a
    "per voice" column acts additionally per voice and is not included. Columns with
    "note-on" take the value at note start; sounding notes no longer
    change. If an instrument reports no base value, only the offset is shown there.
  * **Aftertouch** (top left): one shared display for the most recently received kind,
    with the addition "Channel" or "Poly · Note". A message with value 0 also counts.
    For Poly AT it shows the most recently pressed held note; 0 after release.
    In the matrix, Channel AT and Poly AT remain separate rows; a dot marks
    the one displayed. Older firmware does not report the kind; then it shows "Channel/Poly
    separate" and both rows apply.
  * **Connection state** in the status at the top: green = everything arrives; yellow "no
    live data" = no monitor data for 1.5 s, replies may still work; red
    "no answer" = nothing from the device for 5 s, changes stay unconfirmed. As soon as
    the device responds again, the editor reloads its state. Usually another
    application is holding the input (see below).
* **Files**:
  * "Save file …" creates a `.modpatch` file (JSON).
  * "Open file …" checks the file first and then sends the whole grid in one
    step (including zeros and switched-off cells), the source parameters and the name.
    If the device rejects something along the way or does not answer, the editor reloads the
    state from the device and reports the transfer as incomplete.
  * **If the file does not exactly match** the connected device, the editor asks before
    sending. This is the case for a different instrument, for a column that had a different
    name in the file (example: old file with "Reverb", device with "Level"), for
    files without column information and for LFO cross modulation that the device does not
    know. The list names every affected column. Choice:
    * **Load without affected** (default): these connections are switched off,
      their depth is kept; everything else is loaded.
    * **Load everything anyway**: everything as in the file.
    * **Cancel**: the device stays unchanged.
  * Files opened offline keep the source file's instrument, schema and columns when saved,
    so that the check is still possible later.
  * **Clear matrix** sets all depths to 0 and switches all connections off. LFOs,
    Random and the name remain. Depth 0 means: no modulation via this connection,
    not "parameter at 0".
  * The device has no storage slots yet; after power-off the matrix is
    empty.
* **Verification**: The device answers every change with the checksum of its patch.
  The editor compares it with its own state and reloads on a mismatch.
  Without an answer it repeats a request three times.
* **Connection loss**: If the Daisy is unplugged (input or output disappears),
  the editor reports this and continues offline. On reconnecting, the
  device's state applies; edits made offline are not sent. If a
  connection fails (no answer to the identification, sending refused), the editor releases the
  port immediately so that another program or a new attempt can open it.

### Studio One and the editor at the same time

**Status 2026-10-24: works** (run sheet E7b, Studio One first, then editor). With
Windows MIDI Services both programs open "Daisy Seed External" at the same time.

* Physical Modeling from `0.1.0-mod4` is required. Studio One sends everything that comes from the Daisy
  back to the Daisy via input monitoring. Older firmware answered
  these returned replies with NAKs; the endless NAK loops
  crowded out the replies to the editor (status red). Since mod4 the firmware ignores
  reply frames (PROTOCOL.md § 2, step 2a).
* **Toy Piano v0.3 does not have this protection yet.** There, when in doubt, switch off the Daisy input in
  Studio One or turn off input monitoring on the track.
* Not yet tested: reverse order (editor first, then Studio One).

The previously specified "pass-through" via a virtual port is therefore no longer
needed.

### Building it yourself

Requirements: CMake ≥ 3.22 and a C++17 compiler. On Windows Visual Studio 2022
with "Desktop development with C++", on Linux the JUCE packages from
`.github/workflows/editor.yml`. CMake downloads JUCE 9.0.4 itself.

```bash
cmake -S editors/mod-on-daisy -B build/editor -DCMAKE_BUILD_TYPE=Release
cmake --build build/editor --config Release
```

The program is then located under
`build/editor/ModOnDaisyEditor_artefacts/Release/`.

### Tests

```bash
make -C editors/mod-on-daisy test             # core against the firmware controller, without JUCE
make -C editors/mod-on-daisy test-sanitize    # the same with ASan/UBSan
"…/Mod on Daisy Editor" --selftest out        # window against the simulator, screenshots in out/
```

* **Core test** (`tests/test_core.cpp`): It runs against the firmware's controller and
  engine. It checks connecting, columns, editing, coalescing while
  dragging, the limit of 8, sources, name, clearing, files and disconnecting. In addition there is a run
  with 400 random changes at 5 % lost and corrupted frames. Since the
  correction order of 2026-10-24 also: compatibility check (old Reverb file,
  same schema, file without information, different instrument, origin of files saved
  offline, LFO FM), connection state (no live data, no answer, recovery),
  setting to zero, clearing and loading as per order § 7, extended monitor (range, clock,
  aftertouch cases), target range 0.1/0.5/0.9 ± 0.5 and LFO FM.
* **Self-test** (`--selftest`): It operates the real window with keyboard, dragging,
  mouse wheel, knob, double-click, sources, limit, name and files, plus FM knob,
  range display, clock state, connection state, compatibility prompt (Cancel,
  Load without affected) and the port release after a failed connect. After each
  step it compares the patch checksum of the window and the simulated device. It writes the result
  and screenshots to `out/`; on Windows the result is in
  `out/selftest.txt`.

CI (`.github/workflows/editor.yml`) runs both on Linux (Xvfb) and Windows
and provides the `.exe`. It only starts manually: GitHub → Actions → **Editor** →
"Run workflow".

### License

**Decided on 2026-10-24: open source.**

* **JUCE editor and VST3 plugin** (`src/`, `tests/`, `CMakeLists.txt`): **AGPLv3**, text in
  [COPYING.AGPL-3.0.txt](COPYING.AGPL-3.0.txt). JUCE 9 is used under its AGPLv3 option;
  the commercial JUCE license is therefore not needed. Anyone who distributes the `.exe` or the
  plugin must include the complete source code under the AGPLv3 or make it
  available.
* **Browser editor** (`web/`): **MIT** like the rest of the repo ([LICENSE](../../LICENSE)).
* Included MIT parts (Mod on Daisy headers, the instruments' column tables) stay
  MIT; MIT code may go into an AGPL program.
* The VST3 interface (included in JUCE) has been under **MIT** (Steinberg) since 2025.
  The VST logo is not used; Steinberg's own rules would apply to it.

## VST3 plugin for Studio One

The same editor as the plugin "Mod on Daisy" (vendor "Daisy-Seed", category
Fx/Tools). It is an **audio effect that passes the sound through unchanged**; you put it
on any track, e.g. the Daisy track.

* **Connects by itself** to "Daisy Seed External" as soon as the plugin is on a track,
  even without an open window. After "Disconnect" it stays disconnected until you
  click "Connect". If you replug the kit, it connects again.
* **Closing the window does not disconnect**: connection, live display and model display keep
  running; when reopened everything is as before.
* **Patch in the project**: Studio One saves the patch with the song. When the song is loaded
  the plugin shows it immediately and **sends it to the device** as soon as it is connected. The
  patch in the device is replaced. Columns that mean something different on the device
  stay switched off (like "Load without affected").
* The plugin shares the port with Studio One (Windows MIDI Services, see above).
  Two plugin instances in the same song would both send; use only one.
* No simulators in the plugin.

**Installing** (Windows): Copy the folder `Mod on Daisy.vst3` (from the CI package
`ModOnDaisyEditor-windows-x64-…` or from your own build) to
`C:\Program Files\Common Files\VST3\` (administrator rights), then rescan in Studio One
under *Options → Locations → VST Plug-Ins*.

**Building:**

```bash
cmake -S editors/mod-on-daisy -B build/editor
cmake --build build/editor --config Release --target ModOnDaisyPlugin_VST3
```

The plugin is located under `build/editor/ModOnDaisyPlugin_artefacts/Release/VST3/`. JUCE brings
the VST3 interface; a separate VST3 SDK is not needed.

**Checking without Studio One** (`tests/plugin_check.cpp`): loads the `.vst3` like a host,
checks an effect with stereo input/output, unchanged sound, the project state round trip
between two instances and the window. `MOD_ON_DAISY_NO_AUTOCONNECT=1` keeps the
plugin away from a connected kit:

```bash
cmake --build build/editor --config Release --target ModOnDaisyPluginCheck
MOD_ON_DAISY_NO_AUTOCONNECT=1 build/editor/ModOnDaisyPluginCheck_artefacts/Release/ModOnDaisyPluginCheck "build/editor/ModOnDaisyPlugin_artefacts/Release/VST3/Mod on Daisy.vst3" editors/mod-on-daisy/web/tests/legacy-reverb.modpatch
```

The standalone editor's self-test additionally checks the path "patch from the project,
loaded offline, sent to the device on connect" against the simulator.

## Browser editor

1. Connect the Daisy and close Studio One, as above.
2. Open `web/index.html` in **Chrome or Edge**. Both files (`index.html` and
   `mod_protocol.js`) must be in the same folder.
   * If the browser does not offer MIDI access for a locally opened file,
     open it via a local server: `python -m http.server 8000` in the `web/` folder,
     then `http://localhost:8000`.
3. Allow the request for **MIDI access with SysEx**.
4. Choose input and output "Daisy Seed External" (suggested) and
   click **Connect**.

Firefox and Safari are not supported. Operation matches the JUCE editor,
without the right-click menu; **Firmware update …** uses the selected input and output, even
without a connection. **Demo (offline)** (or `index.html#demo`) shows the grid with the
Toy Piano columns and an example patch.

```bash
node editors/mod-on-daisy/web/tests/protocol.test.cjs   # codec against the C++ reference (also in CI)
make -C modules/mod-on-daisy test-editor                  # editor in headless Chromium against mod_sim
```

`mod_sim` (`modules/mod-on-daisy/tools/mod_sim.cpp`) is the firmware's real controller and
engine code on the PC, with the columns of Toy Piano or Physical
Modeling. The end-to-end test operates the editor like a human: keyboard, dragging,
mouse wheel, double-click, sources, limit, files, plus port release (input in use,
timeout while connecting, disconnecting – with counted `close()` calls),
compatibility dialog, connection state, range and aftertouch display, FM,
setting to zero, clearing and default patch. After each step it checks that the editor and
simulated device have the same patch checksum.
