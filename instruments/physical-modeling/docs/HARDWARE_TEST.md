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
8. Connect J7 to Studio One, select the USB MIDI device and send chords, repeated
   notes, velocities, Note Off and velocity-zero Note On. Check local + MIDI use.
9. Check pitch bend center and +/-2 semitone extremes, including already ringing
   notes. Send CC20–33; verify stationary knobs retain MIDI values, moving knobs
   take control, and a moved switch takes over model selection.
10. Measure actual CPU callback time/deadline reserve (48 samples = 1 ms), analog
    output peaks and IRQ/stack behavior when possible. CPU load: NOT MEASURED.

Record board revision, codec, USB host/DAW, switch orientation and observations.
Software tests do not replace listening, electrical measurements or hardware
real-time deadline validation. No hardware access/flash was performed by the agent.
