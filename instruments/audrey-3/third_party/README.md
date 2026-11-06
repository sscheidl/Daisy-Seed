# Third-party code of Audrey 3

## `audrey-ii/`

From [Synthux-Academy/Audrey-II](https://github.com/Synthux-Academy/Audrey-II) at `e5ab799`, MIT, see `audrey-ii/LICENSE`.
`KarplusString.h` is itself derived from DaisySP (MIT, Electrosmith). The other upstream files used here moved out, since
effects must not depend on an instrument: `EchoDelay.h` to `effects/echo-delay/third_party/audrey-ii/`, and `BiquadFilters.*`
and `DSPUtils.h` to `common/dsp/audrey-ii/`, each with the licence and its own notes.

| File | Change |
| --- | --- |
| `FeedbackSynthEngine.h` | Header only, from `.h` and `.cpp`. `Engine<Stages>` takes its reverb, echo and damper types as template parameters. The host prepares the stages, passes them in instead of the SDRAM allocator and sets their parameters, so the engine's setters for them and their start values moved to the components. New note gate (`SetGate`), which leaves the output exactly unchanged at 1 |
| `KarplusString.h` | Header only, from `.h` and `.cpp`. Damper as template parameter. `StringDcBlock` replaces `daisysp::DcBlock` |

**Why `StringDcBlock`:** Audrey II pins DaisySP `7f67768`, where `DcBlock` uses a fixed 0.99. Our pinned DaisySP uses
`1 - 10/sample_rate`, which moves the corner from about 76 Hz to about 1.6 Hz. The block sits inside the string loop.

Not used: `ParameterRegistry.h`, `SmoothedValue.h` (allocates, replaced by `include/audrey/params.h`), `Source/memory/`, and
`FeedbackSynthControls` (replaced by `include/audrey/control.h`).

## DaisySP-LGPL: ReverbSc and Tone

Both moved out of DaisySP into its LGPL submodule, under **LGPL-2.1**. Nothing is copied into this repository: the
`Makefile` compiles `reverbsc.cpp` and `tone.cpp` from `DaisySP/DaisySP-LGPL`, pinned by the DaisySP commit. ReverbSc is reached
through the component `effects/sc-reverb/`, and Tone is the string's damper. Only this project and `sc-reverb-fx` link LGPL
code, and no other firmware here changes.

**Passing on a binary** brings LGPL-2.1 duties for those two parts: the licence text, their source, and a way for the recipient
to relink the firmware with a changed library, for example the object files. Publishing a binary is the owner's decision.
Replacing both removes the duty, but it changes the sound, since the reverb is in the feedback loop.
