# Third-party code of Audrey 3

## `audrey-ii/`

From [Synthux-Academy/Audrey-II](https://github.com/Synthux-Academy/Audrey-II) at `e5ab799`, MIT, see `audrey-ii/LICENSE`.
`KarplusString.h` is itself derived from DaisySP (MIT, Electrosmith). Copied unchanged: `BiquadFilters.h`, `BiquadFilters.cpp`
and `EchoDelay.h`.

| File | Change |
| --- | --- |
| `FeedbackSynthEngine.h` | Header only, from `.h` and `.cpp`. `Engine<Stages>` takes its reverb, echo and damper types as template parameters, and the host passes in the reverb and echoes instead of the SDRAM allocator. New note gate (`SetGate`), which leaves the output exactly unchanged at 1 |
| `KarplusString.h` | Header only, from `.h` and `.cpp`. Damper as template parameter. `StringDcBlock` replaces `daisysp::DcBlock` |
| `DSPUtils.h` | No `arm_math.h`, which only commented-out code needed. `std::tan` instead of `std::tanf`, for portability |

**Why `StringDcBlock`:** Audrey II pins DaisySP `7f67768`, where `DcBlock` uses a fixed 0.99. Our pinned DaisySP uses
`1 - 10/sample_rate`, which moves the corner from about 76 Hz to about 1.6 Hz. The block sits inside the string loop.

Not used: `ParameterRegistry.h`, `SmoothedValue.h` (allocates, replaced by `include/audrey/params.h`), `Source/memory/`, and
`FeedbackSynthControls` (replaced by `include/audrey/control.h`).

## DaisySP-LGPL: ReverbSc and Tone

Both moved out of DaisySP into its LGPL submodule, under **LGPL-2.1**. Nothing is copied into this repository: the
`Makefile` compiles `reverbsc.cpp` and `tone.cpp` from `DaisySP/DaisySP-LGPL`, pinned by the DaisySP commit. Only this project
links them, and no other firmware here changes.

**Passing on a binary** brings LGPL-2.1 duties for those two parts: the licence text, their source, and a way for the recipient
to relink the firmware with a changed library, for example the object files. Publishing a binary is the owner's decision.
Replacing both removes the duty, but it changes the sound, since the reverb is in the feedback loop.
