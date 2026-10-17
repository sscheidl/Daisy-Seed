# Reverb — Clouds/Nimbus and Slöer

**Status: project plan; no reverb firmware in Daisy-Seed yet.**
Project ID: `reverb-fx`.

## 1. Clouds/Nimbus reverb experiments

**Confirmed by the user:** The intended starting point is the reverb section of
Mutable Instruments **Clouds** and its Daisy port **Nimbus**. The plan is to test
it on the Seed3 Desktop Dev Kit and then develop it further independently. The
public [Clouds reverb](https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/clouds/dsp/fx/reverb.h)
and existing [Nimbus reverb](https://github.com/daisyaudio/DaisyExamples/blob/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Nimbus/dsp/fx/reverb.h)
are the references. CloudSeed is a different project and is not the selected
basis here.

The next task is to isolate the reverb from the full granular application and
check dependencies, the board build, and memory placement. Then connect audio
input, dry/wet mix, bounded output level, and a few controls. Measure impulse
response, stability, RAM use, and callback runtime on the actual kit. Performance
figures from other projects are not measurements of this one.

## 2. Walrus Audio Slöer — possible stereo reverb port

**Confirmed by the user:** The target is the **Walrus Audio Slöer Stereo Reverb**
pedal, a second line of work. It must not be conflated with the Slö or an
arbitrary FV-1 reverb.

**User hardware observation, October 17, 2026:** The user identified a **Spin
Semiconductor FV-1** in an opened Slöer pedal, with the additional marking
**“2211”** below the chip name. This is a report about that device, not an
independent inspection or a claim about every Slöer revision. The meaning of
“2211” has not been established; neither a revision nor a manufacturing date
can be inferred from it.

**Open:** the specific algorithm and source, program version, and whether a
usable port exists. Earlier FV-1 research did not confirm the Slöer's chip; the
user observation adds that hardware evidence. The FV-1 identification alone
does not establish the effect program, clocking, or complete stereo signal
path. A direct port depends on identifying the algorithm. An original sonic
approximation would need to be identified separately from a port.

## 3. Existing FV-1 porting research

The existing [fv-1-DSP-xtractor research](https://github.com/sscheidl/fv-1-DSP-xtractor/tree/d1ef0525d072091d9d4da46f1821d45e930a2745)
documents platform-neutral C++17 Plate and Hall references. These are neither
Slöer implementations nor bit-exact FV-1 emulations. The observed FV-1 makes
this work relevant to the technical porting study, but it does not reveal the
Slöer algorithm.

A port needs explicit sample rate, delay times, modulation, parameter mapping,
and signal levels, followed by host comparison, an ARM build, and a manual
hardware test. Research data remain in their source repository; this project
will develop its own integrated firmware path.

## Current project status

Neither line of work has been imported or implemented as firmware here.
`make PROJECT=reverb-fx` therefore reports the planning status. Clouds/Nimbus
and Slöer will be investigated separately; a shared effect chain or runtime
engine switch has not been implemented.

Pinned sources and validation limits: [reference survey](../../docs/REFERENCES.md).
