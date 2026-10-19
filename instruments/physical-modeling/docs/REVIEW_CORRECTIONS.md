# Independent review corrections — PR #13

Review source: Claude's independent review of commit 5829b86, supplied by the
user on 2026-10-19. The temporary review files remain untouched under `.work/`.
Applicable tests were adapted into tracked `tests/test_review.cpp` and run by
both `test` and `test-sanitize`. Corrections stay on the existing feature branch.

| Finding | Resolution |
| --- | --- |
| F1 global reverb tail | Fixed: fade dry and reverb send only; never fade the stored wet return. Model-6 polarity affects only dry, so the wet tail never flips on model changes. |
| F2 model-6 reverb coupling | Deliberate design, retained per user instruction: Decay, Dry/Wet and Reverb Damping/Diffusion/Input Gain stay independent from synthesis Damping/Brightness. Special string damping/dispersion/stereo routing remain. Architecture now states this explicitly. |
| F3 CPU measurement | Official CpuLoadMeter in the callback, smoothed average/peak and deadline-overrun counters, coherent audio-to-main snapshots on request, channel-16 MIDI diagnostic replies only in main. Optional real-device CSV/benchmark helper. Actual board readings remain NOT MEASURED. No eight-voice mode or memory migration is inferred from host timing. |
| F4 test gaps | Regular spectral four-note tests in every model, earlier-voice preservation and fifth-note steal, bend on all sounding voices, extreme levels, wet-tail waveform continuity including model 6, CC28 versus unchanged/moved switches. |
| F5 chord thresholds | Original Structure*10 nearest-chord rule with +/-0.1 chord hysteresis restored; unit tests cover thresholds, jitter and endpoints. |
| F6 diffusion default | Normalized default .625/.9 gives actual .625 diffusion; units and mapping documented and tested. |
| F7 mono monitoring | Hardware guide documents model-6 dry cancellation when opposite-polarity channels are summed at Position=.5. |
| F8 all-sound-off | Optional suggestion not added: established note-release/ringing behavior and CC mapping remain. FM at high synthesis damping can sustain, as documented. The optional benchmark silences itself through existing CC33. |
| F9 pot noise | Synthetic noise/ownership test included; .5 prefilter is an explicit simulation, not a claim about the physical ADC. No guessed threshold increase. Actual stationary-pot noise remains a hardware test. |
| F10 limiter under extremes | Protected level bound retained and exercised by the adopted stress tests. No unmeasured change to musical gain staging. |

## Demonstrated regression

The enhanced T5 test was also compiled against the original `physical_modeling.cpp`
from 5829b86: all six tested transitions produced an exactly-zero minimum tail
quantum and failed continuity assertions (12 failures). With the fix, all pass:
0->1/4/5 relative waveform error 0; 5->0/1/4 below 3e-14 in the local deterministic
run. The test compares a decayed dry source's wet tail against an uninterrupted
reference waveform, rather than merely checking that the DSP/model index runs.
It therefore detects both the old silence hole and a right-channel wet inversion.

T2 runs at Excitation=.05 to isolate state preservation from the original shared
Torus limiter. The review itself measured 5–7 dB interaction at full excitation;
asserting identical full-level gain would incorrectly reject upstream behavior.
Fifth-note stealing is separately asserted as spectral attenuation of the first
voice. Full-level output stress remains covered by T4.

## Measurement scope

Fake-timer tests exercise the actual pinned CpuLoadMeter and production callback
at controlled synthetic durations: averaging, peaks above 100%, deadline counting,
reset, timer wrap, request filtering and offline guard. They are **not real CPU
measurements**. The telemetry SPSC payload also has its own concurrent TSan test.
The Python decoder is tested offline for framing, malformed/partial packets,
resynchronization, overload values and uint32 counters. No measurement tool was
run against a board; no hardware access or flash was performed.

See MIDI.md for the diagnostic protocol and HARDWARE_TEST.md for real CSV capture.
