# Binding modular development for Daisy

**User decision 01.11.2026:** the modular concept applies generally to all
Daisy developments in this repository. Instruments and FX shall be developed
further separately and then be integrated into firmware products again.
A compatible FX module must be replaceable, for example reverb
for delay, without rewriting the instrument DSP.

This rule makes the existing
[modular components](MODULAR_COMPONENTS.md) and the
[building-block idea](MODULAR_KERNEL_IDEA.md) concrete. It is binding for new developments and
substantial extensions. The existing stable products are adapted step by step
during targeted reworks; no unrequested broad
migration or sound change. Historical reports are kept as such.

## 1. Building blocks and firmware product

* **Instrument:** own sound generation, note/voice states and envelopes.
  No hard-wired FX implementation reachable only internally.
* **FX:** own states, parameters and memory contracts. An FX may consist of
  several separately callable stages if the routing requires it.
  No dependency on the concrete instrument implementation.
* **MIDI/control engine:** reusable transport, queue, dispatch and
  parameter connection. Use or extend existing building blocks in a targeted way;
  instrument and FX receive typed events/parameters instead of their own USB stacks.
* **Modulation:** the existing Mod on Daisy core as a separate building block. No
  new duplicated matrix core per instrument or FX.
* **Control App:** the shared existing interface with component/parameter
  adapters. App, DSP and hardware controls remain separate. The panel is not an
  audio DSP contained in the Daisy firmware.
* **Firmware product/host:** names and connects the components, owns
  memory, audio graph, board/control mapping, routing, mixes, output protection
  and shared services. The product composition is visible and versioned.

Shared code has its own source. The same algorithm is
not copied into several firmware directories for further development.
A single product Makefile or `main.cpp` may connect the components;
this does not turn the DSP cores into a single inseparable module.

## 2. Replaceable FX and audio graph

An FX slot has a documented port/parameter/memory contract.
Compatible FX are selected through the product configuration or a small
host adapter. Swapping reverb for delay changes this
assembly and its parameter description; the instrument core stays the same.
A bypass/pass-through path allows isolated tests of the source.

The existing [FX interface 1.0](../common/audio/fx_contract.h) is the
basis for stereo wet stages: `Prepare(Config, Storage&)`, `SetParameters`,
`Process(StereoBlock)`, memory provided by the host and a dry/wet mix
owned by the host. Do not silently change existing contracts.
Additional ports, control signals or multi-bus stages are explicitly
described and versioned. This way, for example, separate string/solo
paths remain possible without moving their FX prematurely onto the overall mix.

Check before swapping:

* channel/bus layout, format, sample rate, block boundaries, in-place/alias rules;
* memory requirements, alignment, placement, stack and maximum computational load;
* gain, dry/wet, latency, tails, bypass and reset/switching behaviour;
* parameter, status and patch schema as well as applicable modulation targets.

Incompatible components are rejected or connected via an explicitly
tested adapter. No automatic mix/gain change as an
unnoticed side effect of an FX swap.

**First guaranteed development stage:** composition at build time with
an explicit selection in the product. A new FX choice must not merely create a new
copied synthesizer project. A general runtime switch or
dynamic loader is only implemented once state hand-over, pre-reservation,
tail-out, levels, memory and real-time load are actually secured.
The policy does not claim that such a loader already exists.

## 3. Develop modules independently and integrate them again

Every module has a stable component ID, its own version source, a public
interface, README, provenance/licence and targeted host tests. Internal changes
can happen independently as long as the public contract is kept.
Incompatible audio, parameter, patch or memory changes are versioned as a
contract/schema change and documented with a migration strategy.

Every built product manifest names the concrete product version and the
integrated component/contract versions, the Git revision and the matching
control panels. No second manually maintained copy of the module version.
Git states make the selection reproducible and a switch back possible.

A separately developed module is reintegrated in this order:

1. Let the module's own module/contract tests pass with defined inputs.
2. Update the version/dependency selection and, if needed, the host adapter.
3. Test the affected consumers and their audio/event/parameter limits.
4. Justify sound changes, update golden values deliberately and
   check patch compatibility; unchanged variants keep their references.
5. Actually build the composed firmware and document resources from the build.
   Then measure the total CPU load on hardware.

A passed module test alone does not yet confirm a working
overall firmware. Replaceability does not mean that every combination
is possible within every memory/CPU budget.

## 4. Shared parameter and app contract

Kit controls, MIDI, modulation and the control app access the active modules through the same
parameter register. Component/parameter ID,
type/unit, range/default/enum, rate/smoothing, validity and application revision
are unambiguous. The app recognises components and operates their parameters via
the shared contract; special pages can supplement it.

New modules extend the existing Mod on Daisy app at its shared
session/protocol paths. JUCE standalone, browser and VST3 remain consistent.
Changes to existing protocols are versioned together with firmware, CLI probe,
editors and tests. Existing devices and patches remain operable.
No independent editor/transport copy for each new instrument.

Read the state first when connecting. Keep the acceptance of a command and its actual
application in the audio path apart. Reset, module change, reconnect and
preset loading have defined rules. Hardware/device data does not belong
in a musical patch unnoticed.

## 5. Binding review point

Every new instrument/FX work order and PR describes:

* module boundaries, IDs/versions, contracts and product composition;
* the swap path for FX and the reuse of existing shared services;
* separate module checks as well as tests of the affected overall products/app paths;
* memory/CPU assumptions, own sound deviations and parts not yet measured.

Hardware approvals, real-time rules, licence/data limits and the Actions budget from
[AGENTS.md](../AGENTS.md) continue to apply. This policy does not require a
broad automatic build matrix and does not start any migration or hardware action.
