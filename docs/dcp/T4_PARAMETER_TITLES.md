# T4: renamed automation parameters in Studio One

**Question:** does Studio One follow automation parameters whose display names change
at run time (VST3 `restartComponent(kParamTitlesChanged)`, JUCE
`updateHostDisplay(parameterInfoChanged)`), live and after saving and reopening a
project? The answer decides the DCP automation architecture
([MIDI_ARCHITECTURE.md](MIDI_ARCHITECTURE.md) § 5 D). Status 2026-11-02.

## 1. Probe

`editors/dcp-probes/` builds a throw-away VST3 instrument **"DCP T4 Probe"** (own
plugin code `Dsed`/`DcT4`, never collides with "Mod on Daisy"). No Daisy, no port,
no DSP; output is silence.

* Eight automation slots with fixed IDs `slot01` … `slot08`.
* Three name sets: *Generic* ("Slot 1" …), *Firmware A* ("A Cutoff" …),
  *Firmware B* ("B Crescendo" …).
* **Rename now** announces the names at once; **Rename in 5 s** later (like a
  firmware that connects after the project is open); **Rename silently** changes
  names without telling the host (control case).
* The selected set and the slot values are stored in the project. Option
  **After project load, wait 5 s before announcing names** shows what Studio One
  displays before the plugin speaks.
* Counter "MIDI notes received" (the instrument gets the track's notes) and a log
  with **Copy log**.

Build (MSVC, pinned JUCE 9.0.4):

```
cmake -S editors/dcp-probes -B build/dcp-probes -G "Visual Studio 18 2026" -A x64 -DDCP_PROBE_JUCE_DIR=.tools/juce-9.0.4/src
cmake --build build/dcp-probes --config Release
```

## 2. Pre-check in JUCE's own VST3 host — `MEASURED` 2026-11-02

`DcpT4Check "<…>/DCP T4 Probe.vst3"` loads the probe through `VST3PluginFormat`,
restores a project state with set B, saves it and reopens it in a fresh instance:

| Check | Result |
| --- | --- |
| eight slots first, generic names at load | PASS |
| host receives `parameterInfoChanged` after the restore | PASS |
| host reads the set B names, slot value restored | PASS |
| reopen in a fresh instance: notified again, set B names | PASS |

This shows that the probe emits the change correctly and that JUCE carries it through
VST3. **It says nothing about Studio One.**

Side finding: a JUCE VST3 instrument with MIDI input exposes **2089 parameters**: the
8 slots, `Bypass` and 16 × 130 "MIDI CC" parameters. VST3 has no CC events; JUCE
receives mod wheel, pitch bend and aftertouch from the host through these
(`JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS`, default on). The DCP bridge needs them
to forward these controllers. Whether they clutter Studio One's automation list is
part of T4.

## 3. Procedure in Studio One (owner)

Install: Studio One → Options → Locations → VST Plug-Ins → add the folder
`build\dcp-probes\DcpT4Probe_artefacts\Release\VST3`, rescan. No admin rights,
nothing copied into the system. The kit is not needed.

| Step | Action | Observe |
| --- | --- | --- |
| T4.1 | new song, add **DCP T4 Probe** as an instrument; open the track's automation list (add automation) | are the slots listed as "Slot 1" …? Are 2080 "MIDI CC" entries listed too? |
| T4.2 | choose *Firmware A*, **Rename now** | names in the automation list, in an existing lane header, in the plugin's parameter view |
| T4.3 | create automation on slot 1 (draw it, or move slider 1 while the track writes); choose *Firmware B*, **Rename now**; play | lane header now "B Crescendo"? Slider 1 still follows the curve (binding by ID)? |
| T4.4 | choose *Firmware A*, **Rename in 5 s**, close the plugin window at once | after 5 s, do lane headers change with the window closed? |
| T4.5 | choose *Firmware B*, **Rename silently** | names stay old (expected), until the next announcement |
| T4.6 | set B active, automation on slot 1, save, close, reopen | lane names right after loading; automation still on slot 1 |
| T4.7 | as T4.6 with **wait 5 s** ticked | during the 5 s: stored names (Studio One caches them) or "Slot n"? After: set B names? |
| T4.8 | play notes on the track | "MIDI notes received" counts up |

Afterwards press **Copy log** and paste the log into the report.

## 4. Results

| Step | Result |
| --- | --- |
| T4.1 | NOT MEASURED |
| T4.2 | NOT MEASURED |
| T4.3 | NOT MEASURED |
| T4.4 | NOT MEASURED |
| T4.5 | NOT MEASURED |
| T4.6 | NOT MEASURED |
| T4.7 | NOT MEASURED |
| T4.8 | NOT MEASURED |

## 5. What the results decide

* **T4.2–T4.4 pass:** fixed slot IDs with firmware names are possible.
* **T4.3** shows the core risk either way: the lane stays bound to the slot ID, so
  when another firmware (or another mapping) is connected, the same curve drives a
  different sound parameter. The DCP must store the slot → parameter-key mapping in
  the project and warn instead of silently applying it to a different firmware.
* **T4.6/T4.7** decide whether reopened projects show the right names before the
  kit is connected (cached names) or only after the plugin announces them.
* **Failure of T4.2:** names stay fixed in Studio One; then slot names must be
  neutral and the meaning shown inside the DCP window only.
