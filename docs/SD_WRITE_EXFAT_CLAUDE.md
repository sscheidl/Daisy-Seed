# Claude Code follow-up: shared writable SD profile and Looper exFAT

## User request and starting point

After the current FAT32 Looper / FAT32+exFAT Nebulae test candidates are ready,
develop this follow-up separately while the human tests them. Local implementation,
tests and builds are authorized. Hardware actions and flashing are not.
Read AGENTS.md, README.md, HANDOVER.md and the modular development policy first.
Use a separate `codex/` or `claude/` feature branch from current merged `main`;
do not overwrite or replace the test candidates being evaluated.

**The exFAT filesystem already exists. Do not build or import another driver.**

- `common/sd` has FatFs R0.16 with the existing patches, exFAT and long filenames.
  Nebulae uses its intentionally read-only production profile.
- `common/sd/reader` exports raw USB-MSC blocks. Windows handles FAT32/exFAT there;
  this path does not need filesystem write support in the MCU.
- Looper still uses writable libDaisy FatFs R0.12c/FAT32 for its own files. This is
  the missing migration. Diagnostics' local read test also remains on old FatFs.

## Deliverable

Provide an explicitly opt-in writable profile of the shared SD stack and migrate
Looper/Looper MOD to it, retaining FAT32 and adding **local exFAT read/write**.
Keep Nebulae's filesystem profile strictly read-only. Keep the tested raw reader
and its ownership/recovery semantics unchanged unless a demonstrated defect
requires a targeted fix. No NTFS implementation is requested.

Shared block I/O, ownership/abort and filesystem configuration must have clear
contracts, independent versions/tests and manifest integration. Do not copy a
second driver into Looper and do not globally flip `FF_FS_READONLY` to zero.
Document the deliberate update of `common/sd/PLAN.md`'s read-only policy.
Each image must link exactly one FatFs/configuration, and its headers must match
the linked objects. Review include-path and cross-project archive/build hazards.

Implement bounded writes and card-programming/sync completion, error preservation,
cache-safe isolated buffers, card removal, RCC-reset abort and reinitialization.
Check every lower HAL wait/abort path; outer timeouts alone are insufficient.
No allocation, file I/O, locks or blocking work in the audio callback.

Preserve Looper's `loop.tmp`/`loop.wav`, import, erase, rename, recovery and
save-before-share behavior. Do not claim generic atomic rename/power-failure
guarantees from FAT/exFAT. Explicitly reason about each failure boundary and keep
the previously saved loop recoverable where the storage contract permits it.
Audio golden hashes and regular/MOD equivalence must remain unchanged.

## Validation

- Host filesystem images: FAT32 and exFAT through the production block layer;
  real create/write/read/sync/rename/delete and Looper persistence/recall/import.
- Failure injection: partial writes, timeouts, removal, failed sync/close/rename,
  power-loss boundaries represented in card images and restart/recovery.
- Exclusive owner: no local filesystem while Windows owns the medium; correct
  clean eject, interrupted quarantine, explicit return and remount/rescan.
- Positive and negative symbol checks for both profiles: read-only consumers must
  still exclude all filesystem/block mutation paths except their intentional
  separate MSC reader path. Test that checks reject an injected writer.
- Normal/sanitizer tests and ARM builds for Looper, MOD, Nebulae and affected shared
  modules/diagnostics. Target consumer tests; no broad new CI/ARM matrices.
- Produce new versioned artifacts and a dated report. Software observations are
  MEASURED only when actually run. Real exFAT-card, timing and power-interruption
  behavior remain NOT MEASURED until the human reports them.

Diagnostics' protected local read test can then migrate to the shared read-only
profile in a small separate commit/PR with its existing symbol protection and
tests preserved. Explain the replacement of the old blanket `f_open` ban by the
compiled read-only profile; do not weaken write protection as a shortcut.

Use the existing shared control panel, protocols and product adapters. Avoid
unrelated DSP/editor migrations, library/toolchain upgrades, hardware probing,
release publication or Actions dispatches. Finish through a reviewable PR with
exact validation and a practical hardware checklist. Do not merge without the
human's instruction. No real SD card is formatted or written by test tools.
