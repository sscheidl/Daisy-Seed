# Firmware binaries: one store, one naming scheme, one retention rule

**Mandatory for all agents and sessions (user decision 2026-11-07).** Applies to every
firmware image (`.bin`, with its `.elf`) built from this repository for the Seed3 Desktop
Kit: instruments, effects, sequencers, composed products, diagnostics and benches.
Host programs (control panel EXE, editor EXE, VST3) follow the same rules in a second store,
`apps/` (section 8). Not covered: the libDaisy bootloader images and third-party images (for
example Stardust under `instruments/looper/`).

Why: before this rule, 161 firmware files with 98 different contents were spread over
`build/` folders, `.work/` and 19 worktrees. Twelve file names existed with several different
contents (`ToyPianoAmbient-v0.1.2.bin` six times), so a file name did not say what was flashed,
and four images documented in hardware reports no longer existed anywhere.

## 1. One place: the firmware store

* There is exactly one store: **`firmware/` at the root of the main checkout**
  (the first entry of `git worktree list`). Worktrees have no store of their own; from any
  worktree, the store is `<parent of $(git rev-parse --path-format=absolute --git-common-dir)>/firmware/`.
* `firmware/` is git-ignored. The repository is public; images are never committed.
  Published images go to GitHub Releases (section 6).
* **Everything the human flashes, listens to or packs onto an SD card comes from the store.**
  Agents never hand out a path inside `build/`, `.work/` or a worktree, and test packages
  (hardware test folders, SD card ZIPs) are assembled from store images.
* `build/` folders and worktrees hold disposable build output only (section 5).

## 2. Layout and naming

```text
firmware/
  <product-id>/
    <version>+<commit7>[-<variant>]/
      <Image>-v<version>.bin        unchanged build name
      <Image>-v<version>.elf        when the build produced one
      COMPONENTS.json               scripts/component_manifest.py, for registry products
      BUILD.txt                     how the image was built
      SHA256SUMS
      STATUS.md                     test history (section 3)
```

* `<product-id>` is the project ID from `make list` / `components/registry.json`
  (`toy-piano-ambient`, `looper-mod`, ...). Diagnostics uses `diagnostics`; one build that
  produces several images (`DaisyDiagnostic.bin`, `DaisyDiagnostic-CardReader.bin`) shares a folder.
* `<commit7>` is the short commit the image was built from. The same version built from
  another commit therefore gets another folder.
* `-<variant>` names a build that differs only in flags or experiment settings
  (`-fir`, `-block256`). A product plus variant is a **line**; retention works per line.
* Builds from a modified working tree get the suffix `-dirty`, are never release candidates
  and should be the exception.
* The file name stays exactly as the build produced it, because `scripts/check-firmware.py`,
  `scripts/component_manifest.py` and the SD tools validate that name.
* A folder is written once and never changed afterwards, except `STATUS.md`.
  The SHA-256 is the identity of an image; reports quote it.

`BUILD.txt` records: full commit, branch, dirty yes/no, the exact `make` command with all
flags, build date, toolchain (`arm-none-eabi-gcc --version`), libDaisy and DaisySP commits.

Adding an image (until a helper script exists):

```bash
python scripts/check-firmware.py <build>/<Image>-v<version>.bin
python scripts/component_manifest.py --project <product-id> --image <build>/<Image>-v<version>.bin --output <store folder>/COMPONENTS.json
```

then copy the `.bin`/`.elf`, write `BUILD.txt`, `SHA256SUMS` and a `STATUS.md` with the
first line `candidate`.

## 3. Status

`STATUS.md` is a list, newest entry last, one line per event, each with a date and the
report it comes from:

| Status | Meaning | Set by |
| --- | --- | --- |
| `candidate` | Stored, handed out or ready for a test, hardware `NOT MEASURED` | agent, when storing |
| `measured-pass` / `measured-fail` | The human tested it on the kit | agent, recording the human's report |
| `accepted` | The human accepted it as known good | human decision only |
| `released <tag>` | Published as a GitHub Release | human decision only |

Agents never set `measured-*`, `accepted` or `released` from expectations, only from what
the human reported (AGENTS.md, "Report only what was observed").

## 4. Retention: old versions are deleted when a newer one exists

Per line (product plus variant), the store keeps only:

1. the **newest** image, whatever its status;
2. the newest image with status `accepted` or `released`, as the known-good fallback;
3. every image handed to the human for a test whose result has not been reported yet.

Every other image of that line is **superseded** and is deleted, including failed and
untested older candidates. The hardware report keeps its SHA-256 as the record.

Rules for deleting:

* Retention runs whenever an agent adds an image to a line, and only for that line.
* An image is deleted only if its commit exists on GitHub
  (`git branch -r --contains <commit>` is not empty), so it can be rebuilt.
  Otherwise it stays and the agent reports it.
* `-dirty` images cannot be rebuilt; a superseded `-dirty` image is deleted only after the
  human confirms.
* Agents delete only inside `firmware/` under these rules, and list the deleted folders in
  their report or PR. Images of other lines, accepted/released fallbacks and images under
  test are never deleted by an agent.
* Released images stay permanently in their GitHub Release; the local copy follows the same
  rule as any other image.

## 5. Build output and worktrees

* `build/` content may be deleted at any time (`make clean`). After a version bump, the old
  `<Image>-v<old>.bin` in the build folder is stale and is removed by the next clean build.
* Experiment build folders (`build/<experiment>/`, `build/hardware-test-*`) are removed when
  the experiment ends; the images that were handed out live in the store.
* An agent cleans only build folders of its own checkout or worktree, never those of another
  session's worktree.
* A worktree is removed once its branch is merged or abandoned, following the junction rule
  (remove junctions first, never `git worktree remove --force` on a worktree with junctions).
* Before a worktree is removed, check its **ignored** files as well, not only modified,
  untracked and unpushed state: `git -C <worktree> status --porcelain --ignored` lists them as
  `!!`. `git worktree remove` deletes ignored files without asking. Anything that is not plain
  build output (`build/`, toolchain or library folders) — for example `.work/` notes, review
  logs, measurement results — is shown to the human and copied to
  `<main checkout>/.work/rescued/<worktree>/` before removal. (On 2026-11-07 the raw logs of a
  review were lost because only untracked files had been checked.)

## 6. Releases

A GitHub Release attaches the store image of the same SHA-256, with `SHA256SUMS` and the
manifest. It is published only after the human's hardware acceptance (AGENTS.md, "Releases").
The image is not committed to git. `releases/tape-fx-0.1.0/` predates this rule and stays
as it is.

## 7. Transition

* Existing images outside the store are migrated once, as a separate step: images that are
  still needed (newest per line, accepted fallbacks, images under test) move into the store;
  the rest is deleted after the human has confirmed the list. Afterwards `build/hardware-test-*`,
  `.work/*` test folders and stale `build/` images are removed.
* Follow-up: `scripts/hardware_test_package.py` still writes to `build/hardware-test/`; it is to
  be switched to the store, together with a helper script for section 2 and section 4.

## 8. Host apps: the `apps/` store

Added 2026-11-07 (user decision: a folder next to `firmware/`; to be revised if it proves
awkward). Windows programs that the human runs against the kit follow sections 1 to 5 in a
second store, **`apps/` at the root of the main checkout**, next to `firmware/` and git-ignored
in the same way. Everything the human starts (control panel, editor, VST3) comes from there.

```text
apps/
  <app-id>/
    <version>+<commit7>[-<variant>]/
      <unchanged build files>     for example DaisyDiagnostic.exe, Mod on Daisy.vst3/
      BUILD.txt                   commit, version source, build command or CI run, SHA-256
      SHA256SUMS                  every file in the folder (inside a .vst3 bundle: each file)
      STATUS.md                   status history and the firmware it works with
```

| App ID | Files | Version source |
| --- | --- | --- |
| `daisy-diagnostic` | `DaisyDiagnostic.exe` and `COPYING.AGPL-3.0.txt` (the "control panel", SD share tab) | `diagnostics/windows/daisy_diag/__init__.py` |
| `mod-editor` | `Mod on Daisy Editor.exe`, `Mod on Daisy.vst3` (same version) | `editors/mod-on-daisy/CMakeLists.txt` |

A new app gets its ID in this table when it is first stored.

* **Same version, different source:** two builds can carry the same version but speak different
  protocols (control panel 0.6.0 from `main` versus from PR #94). The commit in the folder name
  tells them apart, and `STATUS.md` names the firmware each one was tested with.
* **Status and retention** as in sections 3 and 4, per app line. PyInstaller and JUCE builds are
  not bit-reproducible, so a deleted EXE cannot be rebuilt identically: keep the build that a
  hardware session used until a newer one has passed the same check.
* **Test packages** copy their apps from `apps/` and their images from `firmware/`.
* An installed VST3 (for example in `Common Files\VST3`) is an installation; the stored
  bundle is the source of truth.
* Downloaded CI artifacts of the Editor or Hardware diagnostics workflows are stored here too.
* Host test executables (`test_*.exe`, `build/host/`) are build output, not apps.
