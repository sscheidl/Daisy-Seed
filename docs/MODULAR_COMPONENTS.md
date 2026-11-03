# Modular expansion: first concrete stage

**Binding user clarification 01.11.2026:** the
[modular development policy](MODULAR_DEVELOPMENT_POLICY.md) applies generally
to new Daisy developments and substantial extensions. It requires
replaceable FX, independently versioned/tested modules and verified
reintegration. The following sections document the first
implemented stage and its validation status at that time.

**Current, 01.11.2026:** Ambient is first developed and validated as a standalone FX component.
Core 0.4.0 / adapter 0.2.0 define the 32–12 kHz wet clock and
pitch shifter use centrally. All existing instrument hosts use
these sources; control mappings remain in the host. Hardware acceptance follows
first for the standalone FX, then separately for Toy Piano and Rings/Torus.
Details and historical branch assignment: [FX performance](../effects/reverb/docs/PERFORMANCE.md).

Instruments, effects and control panels are developed separately and combined into
named products at build time. This stage supplements the existing
Rings/Torus/Mod on Daisy setup. There is no runtime loader, no second
modulation matrix and no additional editor protocol.

## Implemented boundaries

* **Component register:** instruments, FX, Mod core and panels have stable IDs
  and their own version sources. The [register](../components/README.md) also describes
  every built firmware composition.
* **Audio contract 1.0:** `Prepare(Config, Storage&)`, component-specific
  `SetParameters`, `Process(StereoBlock)`. No virtual dispatch layer.
  All three FX (Torus, Ambient, Tape) are wet-only; memory belongs to the host, Prepare runs before
  audio start. Invalid blocks are rejected before buffer accesses; permitted
  blocks are bounded and may work in place exactly per channel.
* **Torus FX:** identical original DSP and normalised parameters; the
  adapter contains no hardware mapping.
* **Ambient FX:** same DSP as the pure FX firmware, with the complete
  native parameter set. Dry/mix, output gain and bypass are set to pure
  wet processing in the adapter; routing and final mixing are the
  host's. Parameter application and filter service are limited to 50 Hz.
* **Tape FX** (`effects/tape-fx`, since 26.10.2026): own native DSP port of
  drumlogue TapeVibe/MixForge with a host-side `TapeMixer` (dry/wet, bypass,
  output protection); 16 KiB of memory from the host. Products `tape-fx` and `toy-piano-tape`.
* **Application adapter:** `physical-modeling/src/composed_fx.h` connects the
  existing Torus parameters with the selected effect. This keeps the
  control mapping outside the FX core.

The dry/wet paths are preserved on a Torus model change. Model 6 keeps
its dry right-channel polarity inversion; the send to the FX is tapped before it,
as before. The host keeps the existing dry/wet mix and output protection.

## Separate products

| Build target | Content | Product version |
| --- | --- | --- |
| `physical-modeling` | accepted regular version | unchanged `0.1.0` |
| `physical-modeling-mod` | accepted Mod4 version | historical file name `0.1.0`, IDENTIFY `0.1.0-mod4` |
| `physical-modeling-fx-torus` | Torus + Mod core + Torus FX adapter | `0.2.0` |
| `physical-modeling-fx-ambient` | Torus + Mod core + Ambient FX adapter | `0.2.0` |
| `reverb-fx` | existing Ambient FX with external audio inputs | unchanged `0.2` |
| `looper-mod` | Looper + Mod core (`MOD=1`) | Looper version, e.g. `0.4.0`, for file name, marker `LOOPER-MOD-VERSION` and IDENTIFY |

The new products have their own build directories and markers; file name and
IDENTIFY read the same product version. The version number of the integrated
instrument DSP is separate from it. The new combinations are pilot variants
without device acceptance, not new published releases.

The audio processing of the regular version and of Mod4 is separated from the new composition by compile
flags. Their old BINs must remain byte-identical. The new Torus FX version must deliver the same
golden hash in sound as the old version. For Ambient, the sound change is explicitly the
goal; end-to-end and adapter tests apply there instead of an identical hash.

### Controls of the pilot

The 16 piano keys, six Torus models, existing pots/CCs and the
modulation matrix are kept. External audio inputs are not used in these
instrument variants. The two switches select Torus models,
not Ambient programs.

In the Ambient pilot, the previous global reverb parameters control the
Cloud mode: Decay is mapped logarithmically to 1–45 s, Damping to 14,000–400 Hz,
Diffusion to 0–1 and Input Gain to the send level. The final
mix remains the existing dry/wet control. Further native Ambient controls,
Stretch and the additional Ambient modes remain technically available via the adapter,
but are not yet exposed in this Torus control mapping.
They are not presented as already operable in the existing editor.

## Builds and checks

```bash
make components test-components test-fx
make PROJECT=physical-modeling test
make PROJECT=physical-modeling-fx-torus test
make PROJECT=physical-modeling-fx-ambient test
make PROJECT=physical-modeling-fx-torus -j4
make PROJECT=physical-modeling-fx-ambient -j4
make PROJECT=physical-modeling-fx-torus manifest
make PROJECT=physical-modeling-fx-ambient manifest
python3 scripts/check-firmware.py instruments/physical-modeling/build/fx-torus/PhysicalModelingFxTorus-v0.2.1.bin
python3 scripts/check-firmware.py instruments/physical-modeling/build/fx-ambient/PhysicalModelingFxAmbient-v0.2.1.bin
```

The official Makefiles use flat object names and sorted VPATHs.
Since Ambient also contains a `main.cpp`, the new composition explicitly binds its
`main.o` to the instrument entry point. Marker and
manifest checks must reject a misassigned firmware.

Targeted quick checks take into account the consumers of the shared
Mod core and the FX headers. Native builds, ARM artifacts and releases remain
manual according to AGENTS.md; no automatic ARM matrix is introduced.

## Deliberately still open

The building-block system currently describes concrete build targets; a free selection of all
instrument/FX pairs and a general instrument host are further steps.
The [Toy Piano/Reverb combination](../instruments/toy-piano/docs/AMBIENT.md)
is implemented as a separate product with six modes including Shimmer
(PR #41, building on this foundation). Existing HTML/JUCE/VST3 panels remain matrix editors and
continue to save Mod patches. Full sound parameters, live component
discovery, DSP application revisions and a complete song patch format
need a jointly tested extension of the existing contract.

The combination with Ambient is **NOT MEASURED on the device**. CPU load and
stack high-water marks must not be added up or guaranteed from individual images.
The known deadline overruns on Torus model changes remain relevant.
Before use on the device, a low listening level, a level/tail check and a measurement
with the existing CPU telemetry are necessary. No flash or bootloader change is
part of this implementation.

## Validation of 25.10.2026

**MEASURED on the host / ARM build**, base `a074663` with these changes:

| Check | Result |
| --- | --- |
| Component/identity/dependency tests | 5 Python tests passed; 12 components, 9 firmware products |
| Shared FX tests, also ASan/UBSan | passed: buffer limits, wet-only, tails, no audio allocation, sample-accurate Torus reference comparison |
| Existing Torus host suite | passed, including both previous golden variants and the Mod integration |
| Both new production callback variants, also sanitizers | passed; Torus composition still golden `37DE0BF9D93B81CE` |
| Standalone Ambient host tests | passed |
| Editor core and browser protocol tests | passed; protocol unchanged |
| xPack 13.2.1-1.1 ARM builds, firmware and ELF check | both old and both new Torus products passed |

The previous BINs remain byte-identical: regular
`eb8f412a973a9b58dde92bf63c7c0314d137cb6e929e6f4ad8e3c7daa8ce0fc2`,
Mod4 `9517db27fa25cb3c967923304d15c081f025e942d34182f6153d2acb5b6f92e1`.

| New composition | BIN | DTCM static | SDRAM static | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| Torus FX | 172,692 B | 110,376 B | 197,964 B | `051e46c2f50349d50fc906a4caf80fd9c758157983baa52a0869a80451439583` |
| Ambient FX | 189,908 B | 110,376 B | 1,261,564 B | `01bce977d15a554b43e62cfe26122c98385c078deaac6e0cc218ca7acd4cab7c` |

The DTCM linker reserve in both new products is 20,696 B; this is not a
measured stack reserve. In the isolated worktree, the existing,
unchanged official library checkouts with the pinned commits were used via
`LIBDAISY_DIR`/`DAISYSP_DIR`; host tests prioritise the project fakes.
No new native JUCE/VST3 builds and no hardware check were carried out.
