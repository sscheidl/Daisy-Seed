# Idea: modular firmware core ("Daisy building-block kit")

**User decision 01.11.2026:** through the
[binding policy](MODULAR_DEVELOPMENT_POLICY.md), modular development is now the general
requirement for new developments and substantial extensions. This text
remains the historical draft; a general runtime loader is therefore
still not declared as implemented.

**Implementation of the first stage:** [Modular components](MODULAR_COMPONENTS.md)
describes the additional Torus/Ambient FX builds, the shared
audio contract and the version manifests. The following considerations remain the
historical draft for the further-reaching building-block kit including runtime switching.

Inventory of 25.10.2026, before this first stage:
[Rings/Torus, Mod module, editors and versioning](MODULARITY_AUDIT_2026-10-25.md).
It separates the building blocks already implemented from the still open
instrument/FX interfaces and contains re-run checks.

**As of:** idea and first considerations of 24.10.2026 (hardware test evening with
Physical Modeling mod3). **Not yet planned, no code.** Next step: turn it into a
worked-out plan (interfaces, memory and CPU model, control concept,
protocol, migration order).

## 1. The idea

Instead of building every instrument as its own firmware with its own matrix integration, a
**building-block kit of three kinds of blocks** that complement each other and can be swapped:

1. **Instrument** (e.g. Physical Modeling, Toy Piano, Nebulae, Looper)
2. **FX** (e.g. Torus reverb, Ambient Reverb, Stereo Delay, Granular)
3. **Mod matrix** (Mod on Daisy)

As long as memory and CPU suffice, all three shall be freely combinable in one **base firmware**,
also individually:

| Combination | Example |
| --- | --- |
| instrument only | Physical Modeling as today |
| FX only | Ambient Reverb on the audio input |
| Instrument + Mod | Physical Modeling `MOD=1` as today |
| FX + Mod | Ambient Reverb whose parameters the matrix modulates |
| Instrument + FX + Mod | "Rings into Clouds" with matrix |

Goal: new instruments and effects are only written as a building block; core, controls,
MIDI, SysEx, editor connection and measurement exist once. This simplifies further
development and increases flexibility.

## 2. Where the idea came from

On 24.10.2026, wishes for Physical Modeling were collected:

* two alternative FX to the bundled Torus reverb, in the sense of **"Rings into Clouds"**:
  **Stereo Delay**, our **Ambient Reverb** (`effects/reverb`, modelled on Slöer, modes
  Cloud/Halo/Scatter/Abyss/Bloom) or a **simple granular effect like Clouds**;
* show the active model (SW17/SW18 or CC28) at the top of the editor's connection area.

This gave rise to the more general question of whether instrument, FX and matrix should not fundamentally
be interchangeable building blocks.

## 3. What already exists

* **Mod on Daisy** is already an interchangeable building block: Toy Piano and Physical Modeling
  integrate the same matrix via a fixed interface (`InstrumentInfo`, target tables,
  `Engine::Apply`/`ApplyVoice`, [ARCHITECTURE.md](../modules/mod-on-daisy/docs/ARCHITECTURE.md) § 6).
* Shared build `common/desktop.mk`, shared SD driver `common/sd`.
* SysEx protocol 0x7D with two editors (JUCE, browser) and `mod_probe.py`.
* FX candidates:
  * Torus reverb (in Physical Modeling, measured as part of the instrument);
  * Ambient Reverb v0.2 (`effects/reverb`, own code, MIT; **never run on the device**);
  * Mutable sources in the local archive `Firmware Hacking/Eurorack/Rings (NanoRings)/DIY/eurorack-master.zip`:
    `rings/dsp/fx/chorus.h`, `rings/dsp/fx/ensemble.h` (Rings' "Disastrous Peace" itself switches
    between chorus, ensemble and reverb), `clouds/dsp/fx/pitch_shifter.h`,
    `clouds/dsp/fx/diffuser.h` — all on the same `FxEngine` as the Torus reverb;
  * Granular: core of Nebulae on Daisy (`instruments/granular`); the live project
    `effects/granular` is only planned.

## 4. Sketch

```
                ┌──────────── Firmware core ───────────┐
 Audio in ─────►│                                      │
 MIDI/USB ─────►│  [Instrument] ──► [FX] ──► Output    │
 Pots/keys ────►│        ▲            ▲                │
 CV ───────────►│        └── [Mod matrix] ─────────────│
                │  Control mapping · parameter register│
                │  Memory arena · CPU budget · SysEx   │
                └──────────────────────────────────────┘
```

| Building block | Interface (draft) |
| --- | --- |
| Instrument | notes/MIDI in, stereo out; reports parameters (name, range, "per voice", "at note-on") |
| FX | stereo in, stereo out; without an instrument on the audio input ("FX only") |
| Mod matrix | exists; in future, columns from the parameters of **all** active building blocks, i.e. instrument and FX at the same time |

An empty slot is a pass-through; this yields all combinations.

## 5. Sticking points

1. **Memory (the hardest point).** The images run as BOOT_SRAM from the SRAM (480 KB).
   Physical Modeling with matrix occupies 170 KB, the DTCM 84 % (`MEASURED` at the build on
   24.10.2026). Individual images: Toy Piano v0.2 126 KB, Physical Modeling v0.1.0 138 KB,
   Nebulae v0.1.0 163 KB, Looper v0.3.0 147 KB, Ambient Reverb v0.1 121 KB (each with
   its own libDaisy). Everything at once does not fit like this. Options: execute code from the QSPI
   and keep only hot DSP code in SRAM/ITCM; working memory from a
   shared arena in the SDRAM (64 MB, barely used), since only one instrument and one FX are ever active.
2. **CPU.** Every building block brings measured costs; the core rejects combinations that
   do not fit into the 1 ms budget. Only Physical Modeling has been measured reliably (peak 56.71 %,
   4 voices, Western Chords, 22.10.2026). Ambient Reverb, Granular and Looper: `NOT MEASURED`.
3. **Controls.** 8 pots, 16 buttons and 2 switches must be shared by the building blocks. What is needed is
   a control layer (e.g. pots assigned to either instrument or FX via a switch/button combination),
   a fixed CC assignment per building block and the editor as the complete interface.
4. **Protocol and editor.** IDENTIFY would have to report the active composition; the editor shows
   "Instrument · FX · Matrix" at the top (plus the active model) and can switch. Protocol
   changes always together with firmware, `mod_probe.py`, both editors and tests (AGENTS.md).
5. **Sound must not change by accident.** Golden test: an instrument in the core sounds
   bit-identical to its standalone version. Existing images (Toy Piano v0.3, regular Physical
   Modeling, ModBench) remain untouched.

## 6. Proposed approach

* **Stage 1 — building-block kit at build time:** the same interfaces, composition via
  `make INSTRUMENT=physical-modeling FX=ambient-reverb MOD=1`. Little risk, no memory question.
* **Stage 2 — switching at runtime** in one base firmware (QSPI code, arena), as soon as
  measurements show what fits together.
* Migration one after another; **Physical Modeling as the pilot** (best measured).
* Useful beforehand: measure Ambient Reverb (`reverb-fx`) on its own on the kit.

## 7. Open decisions

* Runtime switching as the goal, or is composition at build time sufficient?
* Which FX first (Stereo Delay, Ambient Reverb, Granular à la Clouds; Ensemble/Chorus as a reserve)?
* Control concept on the kit: how do pots switch between instrument and FX?
* FX chain (e.g. Delay → Reverb) or exactly one FX slot?
* New product name/version for the base firmware; relationship to the individual images.
