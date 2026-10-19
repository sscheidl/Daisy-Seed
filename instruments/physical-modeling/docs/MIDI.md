# USB MIDI

J7 external USB device, omni input (channels 1–16). No echo or local-key MIDI
transmission. No PC required for standalone sound.

* Note On: notes 0..127, velocity 1..127, shared round-robin voice pool.
* Velocity scales internal excitation before the resonator, and FM envelopes.
* Note Off / Note On velocity 0: leave the resonator ringing; no ADSR added.
* Pitch bend: full 14-bit input, center 8192=0, 0=-2, 16383=+2 semitones;
  smoothed 5 ms, including sounding voices, without retriggering.
* The MIDI note is counted once. VR1 adds -24..+24 semitones to all voice notes.

| CC | Parameter | Mapping |
| --- | --- | --- |
| 20 | Frequency | -24 + 48*v/127 semitones |
| 21 | Structure | v/127 |
| 22 | Brightness | v/127 |
| 23 | Damping | v/127 |
| 24 | Position | v/127 |
| 25 | Excitation | v/127, latched per strike |
| 26 | Reverb Decay | feedback .35+.63*v/127 |
| 27 | Dry/Wet | v/127 |
| 28 | Model | min(5,floor(6*v/127)); order 1..6 in README |
| 29 | Polyphony | reserved; all values ignored, fixed four voices |
| 30 | Reverb Damping | lp=.05+.9*(1-v/127) |
| 31 | Reverb Diffusion | coefficient .9*v/127 |
| 32 | Reverb Input Gain | gain .5*v/127 |
| 33 | Master Volume | v/127 multiplied by fixed output gain .8 |

For models 1–6, CC28 ranges are 0–21, 22–42, 43–63, 64–84, 85–105,
106–127. MIDI is 7-bit for CCs; internal targets and smoothing are float.
CC20 has no exact zero-transpose value: 63=-.188976 and 64=+.188976 semitones.
The physical pot center .5 is exactly zero. Pitch bend center is exactly zero.

Hardware and MIDI share targets. An unmoved pot or switch leaves MIDI values
in effect. Physical pot movement takes over once filtered change exceeds .2%.
No NRPN or aftertouch assignment, MPE, DIN MIDI, program-change or sustain pedal
mapping is implemented. Incomplete NRPN messages are ignored as unsupported CCs.
Malformed data bytes/statuses are rejected after the libDaisy MIDI parser.
Notes at pitch extremes retain Torus' original frequency/delay and antialiasing
limits; the MIDI note range is accepted without extending engine buffers.


## CPU diagnostics (channel 16 only)

These diagnostics leave CC20–33 unchanged. Send **CC119=1** on MIDI channel 16
for a snapshot, or **CC119=2** to reset the meter and obtain the first new measured
block. Other values/channels do not request diagnostics. Rapid overlapping
requests are coalesced while a reply is pending; request at most once per second.
The reply contains twenty ordinary channel-16 CC messages, ordered CC100..119:

| CC | Value |
| --- | --- |
| 100 | Protocol version 1 / frame start |
| 101,102,103 | Smoothed average callback load, three base-128 digits, low digit first |
| 104,105,106 | Peak callback load, same encoding |
| 107..111 | Callback count since reset, five base-128 digits |
| 112..116 | Number of callbacks exceeding 1 ms, five base-128 digits |
| 117 | Current model, 0..5 |
| 118 | Voice count, fixed 4 |
| 119 | Value 64 / frame complete (never itself a request) |

For every multi-byte field: integer = sum(value[i] << (7*i)). Load integers are
in .01 percentage-point units: divide by 100 for percent (10000 = 100% = 1 ms).
Average/peak fields support up to 20971.51%; they do not clamp overloads to 100%.
Counters are uint32 values. A complete frame is 60 raw MIDI bytes; partial or
out-of-order frames must be discarded. Musical messages are never echoed.
Only a requested diagnostic reply transmits; no USB transmit happens in audio.
See ARCHITECTURE for measurement scope, smoothing and reset semantics.

`tools/cpu_probe.py` decodes real device replies and writes UTC CSV records.
The optional `--benchmark` sends worst-parameter four-note chord bursts to all
six models, resetting statistics per model after 50 ms settling. It does not
measure the model-transition peak; use normal monitoring while switching for
that separate test. The benchmark finishes with CC33=0 to silence sustained FM;
restore CC33=127 afterwards. It does not enable eight voices. See HARDWARE_TEST.
