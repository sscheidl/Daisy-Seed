# Project rename checklist

Binding procedure for renaming a Daisy project (directory, project ID, firmware prefix, public name), for Claude
and Codex alike. Derived from the FOURIER pilot (`effects/reverb` -> `effects/fourier`, PR #117, merge `84d3a3d`).

A rename is **never a functional change**: DSP, program logic, parameters, presets, MIDI/SysEx/SQ formats, SD paths
and data formats, golden references and the version stay unchanged. The proof is byte-identical firmware images.

## 1. Gates and roles

| Gate | Who decides | What is approved |
| --- | --- | --- |
| **A - scope** | owner | the project's rename map (`scripts/rename/map.example.json` as template), incl. base commit, kept IDs, protected strings, frozen paths and variants |
| **B - merge** | owner, after an independent review (the agent that did not do the rename) | merge of the PR, pinned to the reviewed head: `gh pr merge <n> --merge --match-head-commit <sha>` |

Between A and B the agent works autonomously within the map. One project at a time; no mixing with other work.
Visible product names in **other** projects' code (control panel, diagnostic app GUIs) are not part of a rename
run; they are a separate, tested step after the merge.

## 2. Rename map (one JSON file per project, approved at gate A)

Fields (see the example, which is the real FOURIER map):

* `base_commit`: fixed full SHA on `main`; baseline build and diff lint use it, never the moving `main`.
* `rename`: `directories`, `project_ids`, `image_prefixes`, `ci_suite_keys` (old -> new). These are the only
  substitutions allowed in code, build, CI and registry files.
* `variants`: every product to build, in new IDs (all consumers that compile the renamed sources).
* `keep`: technical IDs that stay (wire keys, markers, registry component IDs, C++ names, consumer IDs).
* `protected`: `{path, string, count}` - the string must occur exactly `count` times **in that file** before
  (path mapped back) and after. Covers SD paths, protocol IDs, markers, golden values.
* `frozen`: path globs whose content must stay byte-identical (DSP sources, golden tests, contracts, presets).
* `allowed_changes`: the few non-substitution edits allowed, each with path, line pattern and reason (e.g. a
  regenerated comment line).
* `historical_docs`: docs whose text stays verbatim; only relative links may change.

## 3. Phases

Each phase ends with the checkpoint (section 5). Evidence stays local (`.work/rename-<project>/`).

1. **Preflight (read only):** clean `main`, no open PR or parallel session touching the area, no running builds,
   store lines of the product listed. Inventory with `git grep` for the old directory, project ID, image prefix and
   CI suite key -> draft map -> **gate A**.
2. **Baseline:** `scripts/rename/build_compare.sh` at `base_commit` in a **fresh clone** (short path; submodules as
   CI: `git submodule update --init libDaisy DaisySP` and `git -C libDaisy submodule update --init --recursive`).
   Only fresh clones are used for the before/after comparison - no incremental builds in the main checkout.
3. **Rename** on `claude|codex/<name>-rename`: `git mv`; substitutions from the map in `projects.mk`, the project
   `Makefile` (`TARGET`), `.github/workflows/build.yml` and `quick.yml` (step, `steps.changes.outputs.<key>`,
   `make PROJECT=`), `components/registry.json`, `scripts/check-firmware.py`, `scripts/select_test_suites.py`
   **and** the suite-key lists in `tests/test_components.py`, consumer Makefiles and `#include` lines; generators that
   embed paths (`sequencers/step-seq/tools/gen_params.py` -> regenerate, `--check`). Explicit, existing pathspecs for
   `git add`.
4. **Docs (own commit):** project README (rename note with old names and kept IDs), CHANGELOG entry "Rename to X"
   (version unchanged), current overviews and instructions; relative links in all docs. Historical commands and
   reports stay verbatim - replacing only the directory next to an old binary name gives a non-reproducible mix.
   Protect intentional "formerly <old path>" notes from global replacements. 0 broken links.
5. **Verify:** `scripts/rename/lint_diff.py --map <map> --head <sha>`; `build_compare.sh` at the branch head
   and `--compare` against the baseline; `git grep` finds no old path/ID/image name outside docs and `keep`.
6. **Store:** new line `firmware/<new-id>/<version>+<commit7>/` (bin, elf, `COMPONENTS.json` via
   `scripts/component_manifest.py`, `BUILD.txt`, `SHA256SUMS`, `STATUS.md` `candidate`, hardware NOT MEASURED).
   The old line is kept; note in its `STATUS.md` that `check-firmware.py` no longer accepts the old file name.
7. **PR + CI:** push, PR (rename table, kept IDs, results), `quick.yml` green. Compact final report -> independent
   review -> **gate B**. After the merge: `git switch main`, `git merge --ff-only origin/main`, check main's tree
   equals the reviewed head's tree; removals (branch, clones) only with approval.

## 4. AUTO and HOLD

**AUTO** (fix without asking, list in the report):

* path/ID/prefix/suite-key references **that are in the map** but were missed;
* wrong doc replacements, broken links, damaged "formerly" notes;
* missing nested submodules in a fresh clone;
* test references adapted to the new names (never weaker assertions);
* regenerated derived files whose only change is covered by `allowed_changes`;
* known Windows-only failures (`M_PI` -> `-D_USE_MATH_DEFINES`, `python3` -> `HOST_PYTHON=python`,
  `tests/test_release_guard.py`) **only if the same test failed the same way in the baseline**;
* `.elf` differences **only if** `.isr_vector`, `.text`, `.rodata` and `.data` are identical.

**HOLD** (stop, investigate, propose; act only after approval):

* any `.bin` SHA-256 different from the baseline, or a `check-firmware.py` failure;
* a new or unexplained build/test failure (no baseline parity), a CI failure not reproducible locally;
* a lint violation: a code line that is not a map substitution, a protected count changed, a frozen file changed;
* a needed change to DSP, logic, parameters, presets, protocols, SD paths or golden references;
* an ID not in the map that might be a kept ID; a visible name in another project;
* unknown or unsaved work, new untracked files (e.g. moved `.gitignore` paths), conflicts with branches/PRs;
* store retention would delete anything; manual GitHub workflows would be needed (Actions budget);
* destructive Git operations or large clean-ups; unclear scope.

## 5. Checkpoint and resume

`.work/rename-<project>/CHECKPOINT.md` is overwritten after every phase (local, not versioned):

```text
project / phase done / next allowed step
base_commit / current head commit
map: path + sha256
checks done: name -> result -> evidence path + sha256
open items / HOLD reason (if any)
```

A resuming agent (Claude or Codex) reads only this file and the map. A recorded check is **not repeated** when the
head commit, the map sha256 and the evidence sha256 are unchanged; otherwise it is re-run.

## 6. Lessons from FOURIER (why the rules exist)

* Stale git-ignored `build/**/*.d` files kept old source paths and broke incremental consumer builds
  ("No rule to make target <old path>") -> fresh clones only.
* `gen_params.py` writes the contract path into `param_table.h`; CI suite keys are repeated in `tests/test_components.py`.
* `git add` with a pathspec that no longer exists rejects the whole command (first commit held only the renames).
* A global directory replacement also rewrote historical commands and two intentional "formerly" notes.
* `check-firmware.py` rejects the old image names of the historical store line after the rename.
* The first fresh build failed because libDaisy's nested submodules were missing; CI's two submodule steps are required.
* Result of the pilot: 12/12 images byte-identical (fresh clone), CI green, independent review GO.
