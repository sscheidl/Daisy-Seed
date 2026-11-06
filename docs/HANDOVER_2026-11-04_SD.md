# Handover 2026-11-04: SD card work (Nebulae, Looper, control panel)

> **Status 2026-11-06: historical.** PR #94 is closed, superseded by PR #100 (Looper 0.6.0 on
> `main`). The sections below describe the state at the time of writing.

For the next chat. Read [AGENTS.md](../AGENTS.md), [MODULAR_DEVELOPMENT_POLICY.md](MODULAR_DEVELOPMENT_POLICY.md)
and this page first. State as of 2026-11-03 22:00 UTC.

## 1. Where things stand

| What | State |
| --- | --- |
| `main` @ `bf08bb3` | Contains PR #76 (Nebulae 0.2.0, Claude) and **PR #78** (Codex integration, merged 2026-11-03 19:50 UTC). Candidates: **Nebulae 0.2.1**, **Looper / Looper MOD 0.5.1**, control panel **0.6.0**. Report: [SD_CARD_INTEGRATION_2026-11-03.md](SD_CARD_INTEGRATION_2026-11-03.md) |
| Hardware test 2026-11-04 | Run sheet: [HARDWARE_TEST_SESSION_2026-11-04.md](HARDWARE_TEST_SESSION_2026-11-04.md). Tests the `main` candidates. All results NOT MEASURED until the human reports them |
| **PR #94** (branch `claude/new-session-fqack4`, head `17c4bfe` before this handover) | **Open, conflicts with `main`** (mergeable_state `dirty`). Built in parallel with PR #78 from the same base `53927d2`. Its Looper SD management duplicates PR #78 with a different protocol. **Do not merge as is** (section 3) |
| Review order (2026-11-06) | [REVIEW_ORDER_SD_2026-11-06.md](REVIEW_ORDER_SD_2026-11-06.md): part A now (Looper 0.5.1 share handoff on `main`), part B after the PR #94 rework |
| Follow-up order for Claude | [SD_WRITE_EXFAT_CLAUDE.md](SD_WRITE_EXFAT_CLAUDE.md): writable shared SD profile + Looper exFAT, on a branch from current `main`, without replacing the test candidates |

### `main` in short

* **Nebulae 0.2.1** (`instruments/granular`): read-only shared FatFs R0.16 (`common/sd`,
  FAT32 + exFAT), sample banks 16x16, SW17, SD management via a restart into CDC + MSC
  (`common/sd/reader`), control SysEx `F0 7D 4E 42`. 0.2.1 fixes a race: SHARE/RETURN
  is rejected while a restart is pending.
* **Looper 0.5.1** (`instruments/looper`): local files are still **libDaisy FatFs R0.12c,
  FAT32 only**. SD management is Codex's design: `src/sd_admin.{h,cpp}`,
  `src/sd_management.cpp`, `src/control_sysex.h` (`F0 7D 4C 50`, 13-byte STATUS,
  result 5 = SAVING), HELLO `app=LooperOnDaisy`, capability `loopmgmt`, DDP `LOOPER
  a=STATUS|PERFORM`, boot intent magic `LPR1`. With an unmountable card and unsaved
  audio it asks for confirmation instead of reporting SAVING forever.
* **Control panel 0.6.0** (`diagnostics/windows`): tab "SD share", `nebulae.py` + new
  `looper.py`/`looper_sim.py`, `--sim-looper`. EXE: local Codex build, SHA-256
  `39510a84…4ac21`. No CI artifact from `main` yet.

Software checks of `main`, **MEASURED 2026-11-03** (Linux, this session): all three
images rebuilt bit-identically to the integration report hashes, `check-firmware.py`
PASS, `make PROJECT=looper test` and `make PROJECT=granular test` exit 0, panel pytest
334 passed.

## 2. Hardware risks to watch in the results

| Risk | Where it shows | What to do with the result |
| --- | --- | --- |
| R1: the bootloader clears backup SRAM `0x38800F00` (boot intent lost) | After Share the kit comes back as MIDI; the panel says "did not enter SD management" | Blocks the whole SD-management concept for both instruments. Find out which bootloader version, then design an alternative (for example an intent in a different retained area) |
| Click at the fade before the restart | A6.3, B5.2 | Note the settings; fade code is in each `main.cpp` |
| Windows enumeration of the CDC + MSC composite | Driver install, drive letter | Reader is the same as Diagnostics 0.3.2 (MEASURED 2026-10-31), but the composed images are new |
| SW17 lever direction | A3 | `kSw17BankWhenPressed` in `instruments/granular/src/control.h` |
| CPU peak (F3) | A5 | Firmware counters only; D16 latched |
| Looper save time (R0.12c, FAT32) | B3, B5.2 | Panel waits up to 180 s |

## 3. PR #94: what is in it and what to do with it

Commits on `claude/new-session-fqack4` (base `53927d2`):

```text
7900b14 Looper: plan for v0.5.0 (writable common/sd driver, exFAT, SD management)
cedf68b common/sd: writable configuration (rw/ffconf.h, disk_write, real sync)
45a364d Looper: card on the shared writable SD driver (FatFs R0.16, FAT32 + exFAT)
9cbce3e common/sdshare: shared SD management component
fd3582c Looper 0.5.0: SD management from Windows (save first, then restart)
c7fa6ce Control panel 0.6.0: SD share tab for Nebulae and Looper
c5e5361 common/sd: write_timeout_ms only in the writable build (read-only images byte-identical)
aec4241 Whitespace
6a0096a Looper 0.5.0: documentation and validation
17c4bfe Looper 0.5.0: record CI run and control panel 0.6.0 EXE hash
```

**Still valuable. This is most of what SD_WRITE_EXFAT_CLAUDE.md asks for:**

* `cedf68b`, `c5e5361`: `common/sd/rw/ffconf.h` (FatFs R0.16, `FF_FS_READONLY 0`,
  exFAT, LFN, no mkfs/label), `disk_write` in `ro_sd.c` (multi-block IDMA through a
  32 KB bounce buffer, waits for programming completion, bounded, RCC-reset abort),
  real `CTRL_SYNC`, `SD_WRITABLE = 1` in `sd.mk`, symbol policy per profile,
  `tests/test_rw_sd.cpp` (faults, removal, full card, 300 random power cuts),
  `tests/fakes/fake_card.h`, `mkimage exfat-32k`. The read-only Nebulae image stayed
  byte-identical.
* `45a364d`: Looper `fatfs_volume.cpp` on the writable profile (`USE_FATFS = 0`),
  portable `fat_volume.{h,cpp}`, `tests/test_card.cpp` (FAT32/exFAT images, power-loss
  campaign at every block of a save: FAT32 143, exFAT 137 cut points, always old or new
  loop).

**Superseded by `main`:** `9cbce3e` (`common/sdshare`), `fd3582c` (PR #94's Looper SD
management and `LP` protocol variant), `c7fa6ce` (PR #94's panel 0.6.0) and the 0.5.0
version/docs. `main` has its own versions of all three.

**Codex review on PR #94** (3 open threads, commit `6a0096a`):

1. P1 `instruments/looper/src/main.cpp:456`: `InSync()` uses a cached version, so an
   erase/undo during the final `Close()` can restart with unsaved changes. Check how
   `main`'s `sd_admin`/`LoopStore` handles the same handoff when integrating.
2. P1 `instruments/looper/Makefile`: new shared component without a version source or
   registry entry. This applies equally to the writable `common/sd` profile.
   `components/registry.json` lists neither `common/sd` nor its reader today.
3. P2 `common/sdshare/admin.cpp`: SHARE accepted while a restart is pending. Already
   fixed on `main` for both instruments (0.2.1/0.5.1). Moot once `common/sdshare` is
   dropped.

**Recommended path (ask the human first; do not decide alone):** rework the branch from
current `main` per SD_WRITE_EXFAT_CLAUDE.md:

1. `git checkout -B claude/new-session-fqack4 origin/main`, keeping the old head as a
   backup ref (`git branch backup/pr94-17c4bfe 17c4bfe`). Then port `cedf68b` + `c5e5361`
   (common/sd) and adapt `45a364d` to `main`'s Looper (`sd_admin`, `sd_management`,
   `control_sysex` stay as on `main`; only the local filesystem changes).
2. Coordinate the writable FatFs with `main`'s `sd_management.cpp` reader and abort
   paths. In PR #94 that was `diag::AbortReaderSd` + `ReleaseCard()` before the restart.
3. Version and register the SD profile(s) in `components/registry.json` and the product
   compositions (review finding 2). Update `common/sd/PLAN.md`'s read-only policy
   deliberately, with the reason.
4. Symbol checks: positive and negative for both profiles, plus a test that an injected
   writer fails the read-only check.
5. Looper version 0.6.0 (or as the human decides), golden `2A2DBCEC41A54F57` unchanged,
   regular = MOD. Panel: the "FAT32 only" Looper texts change to FAT32/exFAT.
6. Do not claim atomic rename on FAT/exFAT. Describe each failure boundary
   (`loop.tmp` write, close, unlink, rename) as the work order asks.
7. Then force-with-lease push and update PR #94's title/body. Or, if the human prefers,
   close #94 and open a new PR.

Diagnostics' local read test moving to the read-only profile is a separate small PR
afterwards (work order, last section).

## 4. Environment (cloud container)

```bash
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive
bash scripts/install-toolchain.sh            # xPack GCC 13.2.1-1.1 into .tools/
export PATH=$PWD/.tools/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin:$PATH
pip install PySide6==6.8.3 pyserial==3.5 pytest==9.0.3   # panel tests, QT_QPA_PLATFORM=offscreen
```

Validation per area (run only what you change; see AGENTS.md for the full table):

| Area | Commands |
| --- | --- |
| Looper | `make PROJECT=looper -j8`, `make PROJECT=looper-mod -j8`, `make PROJECT=looper test test-sanitize`, `python3 scripts/check-firmware.py <bin>` |
| Nebulae | `make PROJECT=granular -j8`, `make PROJECT=granular test test-sanitize test-thread` |
| Shared SD | `make -C common/sd test test-sanitize arm-check` |
| Control panel | `QT_QPA_PLATFORM=offscreen python3 -m pytest -q diagnostics/tests/python` |
| Repo | `make test-projects`, `python3 -m pytest -q tests/test_components.py`, `git diff --check` |

## 5. Rules that matter here

* Hardware is the human's: no flashing, `program-boot` or `program-dfu` by the agent.
  Record hardware results only as reported: **MEASURED (date)** or **NOT MEASURED**.
* English first for code, docs, GUI and GitHub texts. Reply to the human in German.
* Actions budget: only `quick.yml` runs automatically. Firmware, "Hardware
  diagnostics" (panel EXE) and Release are dispatched only when the human wants the
  artifacts. No release without the human.
* Golden hashes change only on purpose. Nebulae's read-only image must stay
  read-only. No library/toolchain bumps.
* Work on `claude/new-session-fqack4`. Merge only on the human's explicit request.
  Commit trailer as given in the session.

## 6. After the hardware session

1. Take the human's results table (HARDWARE_TEST_SESSION_2026-11-04.md) and record it
   in `instruments/granular/docs/VALIDATION.md`, `instruments/looper/docs/VALIDATION.md`
   and the integration report, with date and exact observations.
2. If R1 appears, stop SD-management work and analyse the bootloader first.
3. Then continue with section 3, if the human agrees.
4. Open ideas, not started: an eject button in the panel (the human deferred it; Windows
   eject stays the safe path).
