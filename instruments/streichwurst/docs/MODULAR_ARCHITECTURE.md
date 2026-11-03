# Binding module structure: Streichwurst

2026-11-01. User clarification after the first preparation. This structure
supplements the historical research draft and is authoritative for Claude.
The **existing Mod on Daisy control app** is extended: JUCE standalone,
browser and the shared VST3 interface. The app runs on the computer and
controls the composed Daisy firmware through its MIDI/control connection.

## Components and ownership

| Building block | Location / planned ID | Responsibility |
| --- | --- | --- |
| Instrument | `instruments/streichwurst/include/streichwurst/`, `streichwurst-instrument` | String section and solos, own tone/note states, envelopes, registration, octaves, solo timbre/tremolo, layer/split |
| FX | `effects/streichwurst-fx/`, `streichwurst-fx` | Streichfett FX functions: Animate, Ensemble/Chorus, Phaser, Reverb; independent states and tests |
| MIDI/control engine | existing shared MIDI/protocol building blocks with a targeted adapter | Transport, parsing, timestamps, bounded queues, note/controller/SysEx distribution and consistent parameter hand-over |
| Firmware product | `instruments/streichwurst/src/`, build ID `streichwurst` | Composition, board, memory, routing, balance, final gain/output protection, bootloader and diagnostics |
| Control App | `editors/mod-on-daisy/` | Component/parameter pages, status, patch management and the existing matrix interface; operate the shared firmware parameters |

These are planned components. Register entries with their own version sources
are only created with the corresponding implementations. Product version,
instrument version, FX version and app version remain separate. The product
manifest names the embedded components and compatible panels.

Instrument and FX are testable on the host without board, MIDI transport or editor.
The firmware owns the memory and reserves it before audio start.
No GUI, HAL or protocol dependency in the DSP core. The app knows parameter
and state contracts instead of DSP memory addresses.

## Audio and control flow

```text
USB/DIN MIDI ─┐
Kit controls  ├─> MIDI/control engine ─> NoteEvents + parameter updates
Control App ─┘                          │
                           ┌───────────┴───────────┐
                           v                       v
                    Instrument parameters     FX parameters
                           │                       │
Instrument:   String section dry            Solos dry
                    │                         │
FX:          Animate/shaping                   │
                    │                         │
                Ensemble                      │
               shared sum                     │
               -> Phaser                      │
             -> stereo tap correction          │
                    └──────────┬──────────────┘
Firmware:                Balance/Mix
                               │
FX:                     global Reverb
                               │
Firmware:               Gain -> output protection -> Stereo
```

The FX building block therefore has separately callable string and global stages.
The firmware orchestrates them around the mix. The dry solo bus
bypasses the string ensemble and string phaser. The stereo tap correction remains part
of the ensemble/phaser routing relationship, including the tap states it
requires. A flat stereo FX chain behind the already mixed
instrument output would change this relationship.

Animate is owned as an FX function. Where its effect needs a
registration/timbre-shaping control value, the FX module publishes
a typed modulation value for the instrument's string source; the firmware
passes it on at a defined control cadence. The static timbre basis and
note management remain in the instrument. Define a shared time base and order;
no direct mutual write access to private states.
The exact original shaping recurrence is still partly open.

## Contracts to implement

* Instrument: `Prepare`, `Reset`, `ApplyEvent`, `SetParameters`,
  `RenderSections`. Output separate string/solo buses with the same
  frame count and time base. Define and test the physical channel format, maximum block,
  buffer aliasing and zero-block semantics before implementation.
* FX: `Prepare`, `Reset`, `SetParameters`, string stage and global reverb stage,
  a delimited Animate control interface. No ownership of the instrument,
  no MIDI parser and no embedded control logic.
* Shared host: defined queue/snapshot hand-over from main/USB to audio,
  the same time base for events and DSP. ACK distinguishes accepted from
  actually applied parameters; report the applied revision from audio.
* The existing [FX interface](../../../common/audio/fx_contract.h)
  remains unchanged for existing consumers. Reusable
  Streichwurst sub-stages can offer suitable adapters. The additional
  multi-bus contract is explicitly versioned, without merging strings and solos
  prematurely. The host mix prevents a doubled dry signal and keeps the
  original mode gain after solo/reverb separate from own headroom rules.
* The same parameter set for pots, MIDI CC and app: stable component
  and parameter IDs, type/unit/range/default/enum, smoothing, storage and
  modulatability properties. Application, readback and reset are unambiguous.
  The eight matrix targets are an own selection, not a limitation of the complete
  instrument/FX parameter pages.

## MIDI and the existing control app

Claude first checks the Mod on Daisy and SQ parameter/descriptor/session
building blocks that actually exist in the repository. A shared MIDI
dispatcher must assign musical events, device parameters, matrix messages and
bootloader commands without collisions. Existing message codes and
component IDs must not be reinterpreted. No second transport stack
in the instrument or the FX; reuse shared helpers in a targeted way.

The current matrix interface alone does not cover full sound control.
Instrument and FX pages as well as product/component identity are
part of this assignment. Document a required protocol extension first,
version it and implement and test it together in firmware, CLI probe, JUCE/browser and the
shared VST3 session paths. Existing devices and
their patches must still be recognised and operated correctly.

On connecting, read the state first. No unrequested overwriting with
app defaults. Pot/MIDI changes become visible in the app; app changes
reach the firmware through the same parameter contract. Handle feedback loops,
stale responses, missing ACKs and connection drops in a bounded way.
Own product patches contain instrument and FX parameters, the component schema
and, where applicable, the matrix state. The existing `.modpatch`/`.acidpatch` formats
remain compatible. The editor does not run any Streichwurst audio DSP.

The existing mod matrix is connected as its own shared module;
describe Streichwurst targets and applied parameters in the contract. An empty matrix
delivers the unchanged own basic sound. New modulation sources or a
general runtime instrument loader are not a necessary part of this product.

## Binding acceptance

Separate host tests for instrument and FX, tests of the MIDI/parameter router,
protocol/session tests of the control app and end-to-end tests of the firmware on
host fakes. Check in particular: solos bypass the string FX; reverb sees the later
mix; FX bypass, tails and reset have defined rules; app, pots
and MIDI see the same applied parameter state. Swapping a component
requires no intervention in the core of the other DSP module.

After that, a real local ARM build and resource check. App self-tests are
software tests; MIDI/audio behaviour on the board and the listening comparison with the
original remain **NOT MEASURED** until actually carried out.
