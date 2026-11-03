# Components and product versions

New modules and substantial extensions follow the
[mandatory modular development policy](../docs/MODULAR_DEVELOPMENT_POLICY.md):
their own module versions and contracts, separate tests, replaceable FX and
reproducible product compositions.

`registry.json` assigns stable IDs to all implemented instruments, the two reverb
components, the Tape FX component (`tape-fx`), Mod on Daisy, the shared bootloader entry (`bootloader-entry`,
in every firmware product) and the three control panels. The
versions are read from their own headers or CMake. The registry contains
no second copy of these version numbers. The browser shows its own
version from `editors/mod-on-daisy/web/version.js`.

```bash
make components                         # check the registry and version sources
make test-components                    # test mapping, identity and dependencies
python3 scripts/component_manifest.py   # show all component versions
python3 scripts/component_manifest.py --project physical-modeling-fx-ambient
make PROJECT=physical-modeling-fx-ambient manifest # check the built BIN, write COMPONENTS.json
```

A manifest lists the product ID/version, component versions with their
version sources and dependencies, matching control panels, Git revision,
working tree status as well as BIN size and SHA-256. Panels are listed separately from
the embedded components: the VST3 editor is not audio DSP in the Daisy BIN.
The manual firmware workflow stores the manifest next to the BIN/ELF and checksum.

New components and product variants use three-part SemVer. Existing
values such as Toy Piano `0.3` and Ambient Reverb `0.2` are kept for accepted images.
A change in sound semantics requires a justified version bump
and a new golden proof; a registry entry alone does not change any sound.

The new composed products use **one** product version from
`composed_version.h` for the file name, firmware marker and IDENTIFY. The historical
Mod4 file names are kept; `legacy_device_version` points out the
known deviating IDENTIFY string there. The manifest is a build artifact,
not a new SysEx message. The composition is not yet shown live in the panel.

**Acid Seq** (work order 3.1, R12): The components `step-seq` (kind
`sequencer`), `acid-303`, `saturation` (kind `dsp`, version in the macro
`SEED_SATURATION_VERSION` of `common/dsp/saturation.h`), `drive` and
`stereo-delay` as well as `fast-math` (kind `dsp`, `common/dsp/fastmath.h`) are in the registry as soon as their
version source exists. Their
firmware products (`step-seq`, `acid-303`, `drive-fx`, `delay-fx`, `acid-seq`) are only
added with the first validated build, because the product list must match
`BUILDABLE_PROJECTS` exactly. Valid kinds: `instrument`, `effect`,
`control`, `panel`, `contract`, `sequencer`, `dsp`.

**Streichwurst** (2026-11-01): components `streichwurst-instrument` (kind `instrument`)
and `streichwurst-fx` (kind `effect`, depends on the FX contract) with their own
version headers; the product `streichwurst` (version `src/version.h`) additionally embeds
Mod on Daisy, the SQ device and the queues from `step-seq` as well as the bootloader entry.
The three panels depend on the Streichwurst parameter contract.

Audio contract and exact limits: [MODULAR_COMPONENTS.md](../docs/MODULAR_COMPONENTS.md).
