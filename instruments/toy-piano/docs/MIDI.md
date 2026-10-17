# USB-MIDI

Through **J7 on the Desktop carrier**, the kit enumerates as the class-compliant
USB-MIDI device **Daisy Seed External**. This is the same connector used for power
and firmware updates. The USB connector on the Seed3 module is unused by the app.

## Behavior

| Direction | Behavior |
| --- | --- |
| Input | Note On on any channel, notes 0–127. Frequency follows the note; velocity 1–127 scales amplitude linearly. |
| Input voices | 16 MIDI voices independent of the 16 key voices. A full pool steals the oldest voice; repeating a still-active note retriggers its existing voice. |
| Envelope | VR1/VR2 set attack/decay at the strike. Note Off, sustain, CC, pitch bend and program changes are ignored: these are one-shot AD notes. |
| Output | S1–S16 send Note On on channel 1 with velocity 100 at debounced press, and Note Off at release. |
| Bundling | Up to eight simultaneous key changes per transfer; the remainder follows at the next main-loop service, normally 1 ms later. There is no output event queue. |
| Duplicate C5 | S8/S9 both send note 72: holding both produces two Note Ons, releasing them produces two Note Offs. |
| Connection | Transmission requires a configured USB host. Keys always play locally; received MIDI is not echoed. |

Envelope level and phase are preserved during stealing/retriggering. One documented
exception remains: switching to or from **note 127** changes the second-harmonic
weight immediately, because that harmonic is disabled there. The possible step
is at most about 0.0054, roughly 0.7% of the maximum output level. Smoothing this
transition is deferred; v0.2's DSP remains unchanged.

Key changes are reconciled with the last state reported to the host. A complete
press/release between two services would not be reported, but with normal 1 ms
service this is shorter than the debounce qualification. USB transport failures
are a separate limitation; see [HARDWARE.md](../../../docs/HARDWARE.md).

## Studio One

Basic operation in both directions has been reported on hardware. For setup:

1. Connect J7 and start the kit. Restart Studio One if the kit was connected later.
2. Open Options → External Devices → Add.
   * New Keyboard/controller: receive from **Daisy Seed External** to record keys.
   * New Instrument: send to **Daisy Seed External** to play the board.
3. Audio still comes from J3/J4 or J5, **not over USB**.

If the keyboard exists under External Devices but is absent from a track's input
list, Studio One may not have opened it. In the reported test, deleting and
recreating the keyboard with a new name fixed this. For independent diagnosis,
close Studio One and open a MIDI monitor; applications may compete for a port.

To avoid doubled notes through DAW monitoring, do not route the kit's transmitted
keys straight back to the kit while also hearing its local sound. Disable the
return route or monitoring when only recording key presses.

## Limits and remaining tests

* Notes reach DSP at block boundaries (48 samples / 1 ms), not at sample-accurate timestamps.
* At most 16 incoming notes are started per block; the queue holds 64. Excess input
  can be dropped. This is not a sustained-note MIDI synthesizer.
* The port is MIDI-only; the template's serial Logger is not used concurrently.
* Updates still use RESET then BOOT; the bootloader enumerates as DFU before the app.
* Test semitones, velocity, 16+ simultaneous incoming notes, outgoing chords,
  power-only use, reconnection and full-load callback time on hardware.
