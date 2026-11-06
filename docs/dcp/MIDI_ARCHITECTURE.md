# DCP MIDI architecture: technical assessment

**Status:** assessment for the architecture decision, 2026-11-02. Nothing implemented.
Branch `dcp`. Results marked `MEASURED` (with date) or `NOT MEASURED`.

## 1. Recommendation

Adopt the **instrument plugin as MIDI bridge**, the model SoundTower calls an
"instrument plugin" and Aura uses for the Virus editor:

```
Studio One ── notes, bend, AT, automation ──> DCP VST3 (instrument)
                                                │  owns the Daisy USB-MIDI port alone
                                                │  notes + SQ/MD SysEx, one I/O thread
                                                v
                                              Daisy (synthesis, mod engine, pots)
                                                │
Studio One <── audio interface <── analog outputs
```

* Studio One does **not** open the Daisy port (disabled in its MIDI devices).
* One regular bidirectional USB-MIDI port. No own driver, no second USB port,
  no dependency on Windows MIDI Services multi-client.
* **Firmware: no change expected.** Notes from the plugin arrive like notes from
  Studio One today; SQ and MD keep their echo protection.
* The work is in the plugin shell and one new port layer; `core/` (Session,
  SqSession, patch files) stays as it is.

## 2. What exists today (code facts)

| Item | Today | Source |
| --- | --- | --- |
| Plugin type | audio effect, pass-through, `VST3_CATEGORIES Fx Tools` | `editors/mod-on-daisy/CMakeLists.txt:77` |
| MIDI flags | `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT/OUTPUT FALSE`, `acceptsMidi() false` | `CMakeLists.txt`, `src/plugin/Plugin.cpp` |
| Port model | the plugin opens the Daisy port and **relies on Windows MIDI Services to share it with Studio One** (E7b) | `Plugin.cpp` header comment |
| Port class | `MidiPort`: JUCE `MidiInput`/`MidiOutput` (WinMM API), opens both or neither, **drops every non-SysEx input**, `sendMessageNow` | `src/ui/MidiPort.h` |
| Threading | all protocol work on the host's **message thread**: 10 ms timer polls `Take()`, sends from there | `src/ui/EditorComponent.cpp:150, 296, 391` |
| Transport abstraction | `Transport::Send()` interface, the session does not know JUCE | `src/core/Session.h` |
| Project state | `.modpatch` text in `get/setStateInformation`, sent to the device on load | `Plugin.cpp` |
| Device → editor | SQ monitor event 5 "parameter (pot or CC change on the device)", ≤ 50 frames/s, coalesced; MD monitor | `sequencers/step-seq/docs/PROTOCOL.md` § 3.6 |
| Echo protection | SQ/MD responses carry the response bit and are never answered | PROTOCOL § 2; MD `0.1.0-mod4` |
| History | E10: WinMM single client. E7b `MEASURED` 2026-10-24 by the owner: sharing via Windows MIDI Services works (Studio One first, then editor); Studio One input monitoring echoed device frames, causing a NAK loop, fixed in `0.1.0-mod4` | `modules/mod-on-daisy/docs/DECISIONS.md`, `VALIDATION.md` |
| This PC | service `midisrv` (Windows MIDI Services) running, automatic. `MEASURED` 2026-11-02 | `Get-Service midisrv` |

Owner observation 2026-11-01/02: EXE and VST3 work under Studio One, but react
with delay; disconnecting ports causes problems.

### Likely causes of the lag (hypotheses, NOT MEASURED)

1. **Host message thread.** Every SysEx send and receive and the 10 ms poll share
   the thread with Studio One's whole GUI. When Studio One draws, the editor waits.
2. **Shared port.** With Studio One also holding the port, everything the Daisy
   sends (monitor frames, 50/s) also reaches Studio One; with input monitoring it
   comes back to the Daisy. The echo protection discards it, but the traffic and
   the work remain in both directions.
3. **WinMM through the service translation layer** adds a hop per message.

The bridge removes 2 and 3 by design; 1 is fixed by the I/O thread (§ 4.2).

## 3. Comparison

| Aspect | SoundTower (TEO-5 PlugSE) | Aura (Virus editor) | DCP today | DCP proposed |
| --- | --- | --- | --- | --- |
| Plugin roles | control plugin **or** instrument plugin | instrument plugin, standalone | control (effect) plugin | instrument plugin only |
| Who owns the port | instrument plugin; DAW port disabled | plugin; DAW port disabled | plugin **and** Studio One (shared) | plugin alone |
| Notes | through the plugin | through the plugin | Studio One direct | through the plugin |
| Parameter channel | NRPN + SysEx | SysEx/CC | SQ/MD SysEx | SQ/MD SysEx (unchanged) |
| Device → editor | bidirectional | bidirectional | monitor frames | monitor frames (unchanged) |
| Total recall | yes | yes | `.modpatch` in project | by stable parameter keys |
| Device set | one fixed synth per product | one fixed synth | several firmwares | several firmwares (data-driven) |

The last row is our difference from both models: their automation lists are fixed
because they know one synth. Ours must cope with any firmware (§ 5 D).

A control-plugin mode like SoundTower's is not worth keeping: it is exactly the
shared-port setup that shows the lag. If ever needed, it remains possible with
Windows MIDI Services, which E7b showed working.

## 4. Proposed structure

### 4.1 Components

| Component | Role | New / changed |
| --- | --- | --- |
| `PortHub` | one per process and Daisy port (`juce::SharedResourcePointer`); owns `MidiInput`/`MidiOutput` and **one I/O thread** | new, `src/io/` |
| Note queue | lock-free SPSC, audio thread → I/O thread; short MIDI messages with a sample offset | new |
| Control queue | session → I/O thread; whole SysEx frames | new |
| Receive queue | I/O thread → message thread; SysEx frames (as `MidiPort::Take` today) and, optionally, kit notes | adapted from `MidiPort` |
| `MidiPort` | becomes a thin `Transport` that talks to the hub | changed |
| `core/` Session, SqSession | unchanged; still single-threaded on the message thread | none |
| Plugin shell | instrument, MIDI in, silent stereo out, feeds the note queue in `processBlock` | changed |
| Standalone EXE | uses the same hub; no audio thread, so no note queue | small change |

### 4.2 Rules of the I/O thread

* Never open, close or send on the audio thread; `processBlock` only pushes into
  the note queue (no allocation, no lock).
* Priority: pending notes are sent first; SysEx goes out **between** whole frames,
  never split. SQ frames are ≤ 120 bytes, so a note waits at most one frame
  (expected in the range of 1–2 USB frames of 1 ms, NOT MEASURED).
* SysEx rate limit as today (the session already paces requests; monitor ≤ 50/s).
* Open, reconnect after unplugging, and release run only on this thread; the GUI
  sees a state (`Connected`, `Busy`, `Lost`), never a blocking call.

### 4.3 Timing

The plugin sends the notes of a block when `processBlock` runs. Studio One renders
blocks ahead of playback, so the Daisy hears a note roughly the output latency
earlier or later than a software instrument on the same timeline, plus up to one
block of jitter (256 samples at 48 kHz ≈ 5.3 ms). Sending at the sample offset
(I/O thread waits until the offset's time) can reduce the jitter later. For a hobby
setup the first version sends per block; offset correction is a test result, not
a precondition. NOT MEASURED.

## 5. Answers to the open questions

**A. Can the VST3 receive notes as an instrument and forward them?**
Yes, with JUCE this is standard: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`,
`VST3_CATEGORIES Instrument`, `acceptsMidi() true`, read the `MidiBuffer` in
`processBlock`. Needs a stereo output bus (silent) and no input bus. Practical
test T1, T2.

*VST3 ID when switching from effect to instrument* (checked in JUCE 9.0.4 source,
2026-11-02):

* The VST3 class ID depends **only** on `PLUGIN_MANUFACTURER_CODE` and
  `PLUGIN_CODE` (`juce_VST3ModuleInfo.h:61`, `VST3Interface::jucePluginId`), not on
  `IS_SYNTH` or the category. Keeping `Dsed`/`MoDa` would therefore keep the ID.
* Keeping it is **not** recommended. A Studio One song stores the plugin by this ID
  in its slot. Old test songs hold "Mod on Daisy" as an **insert effect** that
  passes audio through. Reopened with an instrument under the same ID, that slot
  would get a plugin without an input bus that outputs silence: the track could go
  quiet without any message. Studio One also caches the category per ID.
* JUCE's `JUCE_VST3_COMPATIBLE_CLASSES` could declare the new plugin as a
  replacement for the old ID. That asks hosts to perform exactly the risky
  substitution above, so it is not used.
* **Recommendation:** the DCP instrument gets a **new plugin code** (for example
  `DCPi`, product name "Daisy Control Panel"); manufacturer code `Dsed` stays.
  The old "Mod on Daisy" effect can stay installed next to it, so old test songs
  open unchanged; once it is removed, Studio One reports it as missing, which is
  visible instead of silent. Affected are only the owner's test songs.
* Automation parameter IDs: JUCE hashes the string IDs (`slot01` …) into VST3
  parameter IDs. They stay stable as long as the strings never change.
* A JUCE instrument with MIDI input also exposes `Bypass` and 16 × 130 "MIDI CC"
  parameters; this is how VST3 delivers mod wheel, pitch bend and aftertouch, so
  the bridge keeps them ([T4_PARAMETER_TITLES.md](T4_PARAMETER_TITLES.md) § 2).

**B. Notes, parameters, SysEx and status without conflicts?**
One owner of the port, one I/O thread, two queues with priority (§ 4.2). MIDI 1.0
lets real-time and short messages pass between SysEx frames, but we do not rely on
that: frames stay whole. The firmware already separates SysEx (SQ/MD) from notes.

**C. Kit keys as MIDI notes back to Studio One?**
Possible. Studio One shows a plugin's MIDI output as an input on **another** track
("Instrument Input"); several users report this for VST3 note generators. This
needs `NEEDS_MIDI_OUTPUT TRUE` / `producesMidi() true` and the hub forwarding kit
notes (today `MidiPort` drops them). Caveat: if the recorded notes play back into
the plugin while the kit also plays its keys locally, notes double, the classic
local-control problem. Recommendation: optional, later; not part of the first
bridge. Practical test T5.

**D. Hardware pots visible in the VST3 and automatable?**
The VST3 must declare its parameters at load time, before it knows the firmware.
Proposal:

* A **fixed pool** of automation parameters, named after the kit's physical
  controls ("Pot 1 page 1" … plus switches) and a pool of extra slots. The kit is
  the same for all firmwares, so these names stay stable.
* After `IDENTIFY` the plugin maps each pool entry to a firmware parameter by its
  stable key and may update the displayed names (VST3 `kParamTitlesChanged`,
  JUCE `updateHostDisplay`). Whether Studio One shows renamed parameters is
  unknown: test T4.
* Device → host: monitor event 5 → `setValueNotifyingHost` on the message thread,
  marked "from device" so the listener does **not** send it back (no loop).
* Host → device: parameter change → `SET_PARAM`, coalesced to ≤ 50 per second.
* Automation in read mode wins over the pot; touching a pot while writing records
  it. Soft takeover stays on the kit.
* The project state stores values by parameter key (total recall), as planned for
  the parameter register.

**E. Several plugin instances?**
Inside one process (Studio One hosts plugins in-process by default) the
`PortHub` singleton owns the port; the first instance controls the device, further
instances show "Daisy controlled by another instance" and stay passive. Across
processes (EXE next to the plugin) a named Windows mutex per port lets the second
program report "busy" instead of fighting over the port. No driver needed.
Several Daisys on several ports work naturally: one hub per port. Test T6.

**F. One communication layer for standalone and VST3?**
Yes: both use `PortHub` + `MidiPort` as `Transport`; the session code is already
shared. Only the plugin has an audio thread and feeds the note queue.

## 6. Changes and their size (estimates)

| Area | Change | Size |
| --- | --- | --- |
| Firmware (all) | none expected | — |
| `mod_probe.py`, `sq_probe.py`, browser editor | none | — |
| `src/io/PortHub` + queues | new | medium |
| `src/ui/MidiPort.h` | thin transport over the hub; optional note pass-through | small |
| `src/plugin/Plugin.cpp`, `CMakeLists.txt` | instrument shell, MIDI in, buses | small |
| Automation pool, key mapping, echo guard | new | medium |
| `EditorComponent` | connection state from the hub instead of direct open | small |
| Docs | E10/E7b superseded by this decision; editor README setup (disable Daisy port in Studio One) | small |
| Tests | hub queue tests with a fake device (host test); plugin self-test | small |

## 7. Practical tests (owner, with Studio One and the kit)

| ID | Test | Pass |
| --- | --- | --- |
| T1 | instrument plugin on a track, keyboard → Studio One → plugin → Daisy | notes play, no hangs |
| T2 | Daisy port disabled in Studio One, plugin opens it alone | `Connected`; editor reacts without lag |
| T3 | save project, close, reopen with the kit on | device state restored |
| T4 | plugin renames automation parameters after connecting | Studio One shows the new names (or not: then fixed names only) |
| T5 | plugin MIDI output selected as input of a second track | kit keys recorded (optional feature) |
| T6 | second plugin instance; EXE started while the plugin runs | clear "busy" message, first instance unaffected |
| T7 | unplug and replug the kit during playback | plugin reconnects, Studio One stays responsive |
| T8 | 30 min playing with pot moves and automation | no stuck notes, MIDI overflow counters 0 |

Host-side tests stay hobby-sized: queue and echo-guard unit tests, the existing
editor self-test extended by the plugin shell.

## 8. Owner decisions (2026-11-02)

The bridge architecture is adopted as the basis of the DCP.

1. **One plugin type, the VST3 instrument: yes.** It owns the bidirectional USB-MIDI
   link; Studio One sends its MIDI to the plugin, which forwards it. One `PortHub`
   for standalone and VST3. The VST3 ID question is answered in § 5 A (new plugin
   code recommended; to be confirmed).
2. **Kit keys to Studio One: later.** The hub's receive path is designed so kit
   notes can be forwarded later (§ 5 C); not part of the first bridge.
3. **Automation: not decided yet.** Three levels stay separate: physical controls
   (VR1–VR8 …), the firmware's sound parameters, and mod sources/destinations. A
   sound parameter without a pot must still be automatable, so automation is not
   bound to the pots alone. Candidate: a fixed set of slots with stable IDs whose
   names and mapping follow the connected firmware, provided existing automation and
   projects do not silently lose their mapping. Decided after
   [T4](T4_PARAMETER_TITLES.md).

Further constraints: no second USB-MIDI interface; no dependency on Windows MIDI
Services; no own MIDI drivers; one communication core for standalone and VST3; no
blocking MIDI operations on the audio thread; existing firmware protocols stay for
now; no new DSP in this work. The bridge itself is not implemented yet.

## Sources

* SoundTower PlugSE: [manual (Scribd)](https://www.scribd.com/document/456163654/Artis-PlugSE-Manual-pdf),
  [Image-Line forum, DAW port disabled](https://forum.image-line.com/viewtopic.php?p=1055737)
* [MIDI Quest: plugins with single-client Windows MIDI](https://soundquest.org/Help/MidiQuest13/using_plugins_with_single_clie.htm)
* Studio One plugin MIDI output to another track:
  [JUCE forum](https://forum.juce.com/t/vst3-midi-audio/62624/14),
  [Scaler forum](https://forum.scalermusic.com/t/cannot-send-individual-scaler-3-tracks-to-instrument-tracks-in-studio-one/20079),
  [UJAM Virtual Pianist](https://www.ujam.com/blog/virtual-pianist-110-update/)
* Aura Virus editor and Moog editors: owner's research summary of 2026-11-02, not
  re-checked here.
