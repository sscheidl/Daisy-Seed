# Hardware acceptance — PENDING — USER TEST

1. Download the Physical Modeling Actions artifact; verify SHA256SUMS.
   Use the already working external Daisy bootloader and the application BIN
   following the repository FLASHING guide. Do not reinstall a working bootloader.
2. Connect J3/J4 or headphones and start at low monitor volume. Confirm stereo
   audio at 48 kHz, and startup/standalone operation without a computer.
3. Press/release all S1–S16: matching D1–D16 follows held keys; sound decays after
   release. Check duplicate C5 on S8/S9, rapid repeats and all sixteen keys.
4. Check VR1 centered transpose, both transpose extremes, Structure, Brightness,
   Damping, Position, and genuine excitation response down to zero.
5. Cycle SW17 banks and SW18 UP/CENTER/DOWN: Modal, Sympathetic, Inharmonic,
   FM, Western Chords, String & Reverb. Confirm physical orientation against README
   and rapid model changes with notes ringing. Check fades for audible clicks.
6. Play four-note chords and a fifth note; confirm deterministic voice stealing.
7. Check Decay and exact dry-only/wet-only endpoints in all six models, especially
   model 6 with Position at center. Test long tails and maximum brightness/damping.
   Use stereo monitoring: model 6 at Position=.5 has opposite-polarity dry
   channels, which cancel when summed to mono. With Wet=100% and long Decay,
   switch models including 6: the existing global hall tail must continue.
8. Connect J7 to Studio One, select the USB MIDI device and send chords, repeated
   notes, velocities, Note Off and velocity-zero Note On. Check local + MIDI use.
9. Check pitch bend center and +/-2 semitone extremes, including already ringing
   notes. Send CC20–33; verify stationary knobs retain MIDI values, moving knobs
   take control, and a moved switch takes over model selection.
10. Measure actual callback average/peak and 1 ms deadline misses using the
    telemetry instructions below. Also check analog output peaks, IRQ/stack
    behavior and stationary-pot noise/MIDI ownership. CPU load: NOT MEASURED.

Record board revision, codec, USB host/DAW, switch orientation and observations.
Software tests do not replace listening, electrical measurements or hardware
real-time deadline validation. No hardware access/flash was performed by the agent.


## Record actual CPU measurements

After flashing this build and connecting J7, close Studio One's MIDI device if
it prevents another application from opening the same Windows MIDI ports.
Use a MIDI monitor to send channel-16 CC119=2 (reset), then CC119=1 (read), or use
the included command-line helper. On the measurement PC only:

```sh
python -m pip install mido python-rtmidi
python instruments/physical-modeling/tools/cpu_probe.py --list
python instruments/physical-modeling/tools/cpu_probe.py --input "EXACT INPUT NAME" --output "EXACT OUTPUT NAME" --seconds 60 --csv cpu-manual.csv
python instruments/physical-modeling/tools/cpu_probe.py --input "EXACT INPUT NAME" --output "EXACT OUTPUT NAME" --benchmark --seconds 30 --csv cpu-six-models.csv
```

Normal mode observes your playing: try all keys, four-note chords, local+MIDI,
parameter extremes, long reverb tails and rapid model switches. Reset beforehand;
record the board revision, binary SHA256, model/parameters, sample rate, block size,
smoothed average, maximum and deadline misses. 100% equals the 1 ms callback
budget. Values above 100% and any deadline misses must be retained in the report.
The benchmark changes synth controls, sends repeated chords for 30 seconds per
model and ends at Master=0; send CC33=127 to play again. It measures steady-model
stress after settling, so keep a separate manual run covering model transitions.

Host tests use synthetic clock values and **are not hardware performance data**.
No measurements have yet been taken on a real board. Keep the CSV files as user
measurements outside Git. Four-voice readings establish headroom; they alone do
not prove that eight voices are possible, since the global reverb has fixed cost
and the original Part still supports a maximum of four. No memory placement or
voice-count expansion has been made without measurements.
