# Repository history and migration

The diagnostic suite was extracted on 17 October 2026 from the private
[sscheidl/Daisy-Seed](https://github.com/sscheidl/Daisy-Seed) repository at commit
`e2480bc33a5cb01f78c5ff5990c780698a516480`.

History restricted to `diagnostics/` was preserved. Commit IDs changed when that
directory became the repository root. The history includes the initial
implementation, review fixes, and GUI redesign. Historical PR numbers in the
imported reports refer to the original repository.

The extraction also copied `scripts/install-toolchain.sh` and
`scripts/check-firmware.py` and added the pinned libDaisy submodule. Build paths,
CI, and documentation links were adjusted for standalone operation. DaisySP and
Toy Piano source are not dependencies. Application behavior was preserved.

The repository was initially named `Daisy-Seed3-Diagnostics`, then renamed to
**Daisy-Seed3-Desktop-Diagnostics**. Current links use the new name.

At extraction time, hardware tests were pending. Results added later on
17 October 2026 record H1–H10 as passed (H3 shortened), with H11 still open.
See [TEST_REPORT.md](TEST_REPORT.md) for the distinction between protocol recordings,
operator reports, and unmeasured cases. The English-language update translates
the current GUI and documentation without adding hardware evidence or changing DDP.

## Return to Daisy-Seed (23 October 2026)

After PRs #2 and #3 were merged, the standalone repository was merged back into
`diagnostics/` of Daisy-Seed with its full history (`git log -- diagnostics`).
Commit IDs changed again because the files moved into the subdirectory. The
standalone repository is no longer maintained; links to its PRs, CI runs and
release in older reports refer to that former repository.

The suite now uses the shared libDaisy submodule (same pinned commit) and the
shared `scripts/install-toolchain.sh` and `.tools/` of Daisy-Seed. CI runs as
`.github/workflows/diagnostics.yml` when `diagnostics/`, libDaisy or that workflow
changes. Application behavior was not changed.
