# Claude work order: Nebulae Mod on Daisy with SD sharing

## Deliverable

Build a local Windows hardware-test package for Nebulae on Daisy (Seed3 Desktop Kit).
It must include a new Nebulae MOD firmware image, the shared Mod on Daisy editor,
the shared Windows SD panel, a component manifest, checksums and a short test sheet.
Provide an obvious way to switch between editing modulation and managing the card.
Reuse the existing Windows SD workflow, including its ownership and eject checks.

Work on `codex/nebulae-mod-sd` in this isolated worktree. It starts at `32560ed`
on `origin/main`. Read `AGENTS.md`, `README.md`, `HANDOVER.md`, the Granular README and
`docs/MODULAR_DEVELOPMENT_POLICY.md`. The user's `dcp` checkout has untracked work.
Use a PR for integration and report local validation and hardware status there.

## Firmware

- Start with Nebulae 0.2.1, including the pending-restart ownership fix from PR #78.
  Add a separate `granular-mod` product, following `looper-mod`. Keep regular
  `granular` behavior intact. Use a separate build directory and image name,
  a clear firmware/IDENTIFY identity, and the existing component version sources.
- Add a Nebulae adapter to the shared matrix. Expose eight global targets in CC20–27
  order: Start, Speed, Size, Density, Pitch, Overlap, Blend and Window. Use the
  existing primary parameter register as the base, apply bounded offsets before
  the existing parameter laws/smoothing, and report unmodulated bases. Preserve
  pickup, center/deadband behavior, note priority and record-time parameter holds.
  Verify the empty matrix gives bit-identical production audio.
- Follow the existing 1 kHz control path where safe. Inspect the actual timer,
  event queues and 256-frame callback first. Keep one owner for each shared state
  and use a bounded handoff if needed. Document both the tick rate and the rate at
  which offsets reach DSP. Audio callbacks must stay free of allocation, locks,
  blocking operations and USB sends.
- Keep Bootloader (BL), Nebulae STATUS/SHARE (NB) and Mod on Daisy (MD) SysEx routed
  independently on J7. Retain card bank/slot restoration, FAT32/exFAT and current
  SHARE, BUSY, Live-loop confirmation, eject and interruption behavior. Patch state
  is volatile; support saving/loading via the editor. Do not add flash storage.
  Leave protocol framing unchanged if the existing device-driven target schema
  suffices.

## Windows workflow

- Use the shared Mod on Daisy editor (`editors/mod-on-daisy`). The firmware should
  describe the columns and bases so the existing JUCE, browser and VST3 clients
  can render them. Extend shared client paths only if compatibility requires it.
- Reuse Daisy Diagnostic 0.6.0's **SD share** tab (`diagnostics/windows`). Provide
  a single test entry point with clear **Modulation** and **SD card** choices.
  An explicit switch between the two existing executables is sufficient for
  this test package. Close/disconnect the active MIDI client before the other
  opens J7 on Windows. Require user action to start sharing. After Windows eject
  and **Return to Nebulae**, reconnect the editor and reload the saved patch.
  Do not copy the MIDI/CDC transport or the card ownership state machine.
- Document that entering SD management restarts the kit and discards the volatile
  Live loop after confirmation. Document the patch save/reload step before sharing.

## Acceptance and handoff

1. Run the regular and MOD Granular host suites and targeted sanitizer tests.
   Cover the production callback with an empty matrix and at least one active
   route, target discovery, base monitor, clamping and record-time holds. Verify
   the same USB path handles NB, MD and BL, and exercise SD return in the MOD
   build on simulated FAT32/exFAT with bank/slot and WAV checks. Cover BUSY, Live
   confirmation and interrupted access through existing shared tests where possible.
2. Build both ARM images with the pinned toolchain. Run the firmware, ELF and SD
   symbol checks on the MOD image. Record image size, memory margins and results.
   Run targeted editor core/browser checks and Windows panel tests. Smoke-test
   the packaged Windows apps in simulation, including the Nebulae SD workflow.
3. Package the verified firmware BIN, Windows executables, entry point, SHA-256
   sums, manifest and test sheet together. Record the exact revision, component
   versions and commands. In the sheet, cover J7 connection, eight targets, an
   audible LFO route, callback peaks/overruns, patch save/reload, and a complete
   SD share/eject/return on FAT32 and exFAT.
4. Keep hardware results **NOT MEASURED** until the human runs the kit tests.
   No flashing, bootloader install, release dispatch or publication. Use local
   builds and the existing short CI policy.

Starting references: `instruments/looper/docs/MODULATION.md`,
`instruments/granular/docs/CONTROL_PROTOCOL.md`, `diagnostics/docs/NEBULAE_PROFILE.md`,
`docs/SD_CARD_INTEGRATION_2026-11-03.md`, `modules/mod-on-daisy/docs/ARCHITECTURE.md`
and `components/registry.json`. Local xPack, LLVM-MinGW and JUCE 9.0.4 are in the
original Daisy-Seed checkout under `.tools`. The prior SD-integration worktree has
the tested 0.6.0 Windows app in `diagnostics/windows/dist`.
