# Ambient Reverb 0.5.0 controls and app

The 2026-11-01 revision deliberately strengthens the physical controls after the
user's stable Toy Piano Ambient / FX 0.4.0 listening test. That older acceptance
does not establish hardware acceptance of this new sound.

## Module boundaries and ranges

The FX component owns `src/control_mapping.h`: maximum depth is 3.99 ms (+33%),
maximum X is 1.25 (+25%), and maximum Bloom rise is 5 s (+25%). Scatter's diffusion
blend is limited to unity. The DSP accepts 4.9875 ms depth for the combined depth
and Cloud X controls of hosts without a Sustain button. Physical pot assignments
belong to each firmware host. The 32–12 kHz wet-clock and pitch-gating policies
remain in this same shared FX; no instrument DSP is changed.

The wet-only audio adapter remains 0.2.0. The independent `ambient-reverb-control`
adapter is 0.1.0. It reuses the existing SQ codec/device and SPSC/triple-buffer
services; audio owns DSP values, the main loop owns USB/protocol state. A four-item
queue bounds audio work; excess requests return BUSY. Accepted and audio-applied
revisions remain distinct. No locks, allocations, storage writes or coefficient
design are added to the audio callback.

## Reverb LFO rate in the existing control app

Control app 0.4.0 (JUCE standalone, shared VST3 UI and browser) adds an **Ambient FX**
page with **Reverb LFO rate**, logarithmic from **0.03 to 1.5 Hz**, default 0.18 Hz.
These are the existing DSP rate limits, not the independent Mod-on-Daisy LFOs.
Rates are nominal at the 32 kHz wet clock; maximum Stretch scales their audible
rate by 12/32. Both editors read the device before writing anything.

The contract is `effects/reverb/contracts/parameters.json`, generated into the
firmware table and both editors' labels. Component `ambient-reverb`, schema 1,
parameter ID 1, key `reverb_lfo_rate`, unit Hz; SQ values are 30–1500 milli-Hz.
The existing SQ wire format is unchanged; the generic probe supports it:

```sh
python sequencers/step-seq/tools/sq_probe.py params ambient-reverb
python sequencers/step-seq/tools/sq_probe.py set ambient-reverb reverb_lfo_rate 0.72
```

Standalone firmware identifies as `ambient-reverb` over SQ 1.0. On standalone FX,
VR7 still controls rate; an app setting holds until VR7 actually moves. Other
knobs do not overwrite it. Values live in RAM and reset on reboot.

Toy Piano integration uses SQ 1.1 with the existing matrix capability, retaining
Mod on Daisy on the same port. The short SQ product alias is `toy-piano-amb`
(the existing SQ product string has a 16-character limit); its MD identity remains
`toy-piano-ambient`. Integration is versioned and tested separately.

## Validation

**GEMESSEN 2026-11-01, Windows:** targeted FX performance/headroom tests and actual
standalone control tests pass, including maximum settings in six modes, no guard
events in those synthetic tails, physical/app rate handover and the 32–12 kHz cap.
The C++ editor client and browser client pass round trips against the production
control adapter: device-first reads, rate endpoints and application, monitor
readback and bounded queue. Focused browser UI interaction passes in installed
Edge/Playwright. Native JUCE standalone and VST3 builds pass; the standalone
self-test reports PASS.

The standalone xPack GCC 13.2.1-1.1 image is 166,364 bytes. Firmware identity,
BOOT_SRAM vectors, generic ELF/BIN identity and SDRAM placement pass. Static DTCM
reserve is 9,600 bytes; SDRAM is 1,114,112 bytes. Static reserve is not a stack
high-water measurement. Component versions and image hash are in the build's
`COMPONENTS.json`.

Only targeted checks were run; no sanitizer/thread/full-release sweep was added.
**NICHT GEMESSEN:** hardware sound, app interaction on the kit, CPU/overruns and
stack high-water for 0.5.0. Standalone FX and each consumer require their own
hardware checks. No device was flashed and no GitHub release was published.
