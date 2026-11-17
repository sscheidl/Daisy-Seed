# Claude Code: independent review of PR #108 fixes R9–R12

Owner request: 2026-11-12, following Codex's implementation. **Review and report only.**

## Reviewed state and boundaries

* Repository: `sscheidl/Daisy-Seed`, PR #108, branch `claude/toy-piano-granular-fx`.
* Worktree: `D:\Eigene Dateien\Eigene Dokumente\Playground\Daisy-Seed-worktrees\toy-piano-granular-fx`.
* Main checkout/toolchains: `D:\Eigene Dateien\Eigene Dokumente\Playground\Daisy-Seed`.
* The fixes were left **uncommitted on top of `af53b32`**. Review the actual working-tree diff,
  not merely a commit range ending at `af53b32`. First record HEAD, branch, status and diff;
  if Claude or another person has since added commits, identify and review the complete fix state.
  Read the untracked fix record and this request explicitly: ordinary `git diff` omits new files.
* Read `AGENTS.md`, the modular development policy, `HANDOVER.md`, and
  [PR108_FIXES.md](PR108_FIXES.md). Original evidence is in the sibling L2A checkout at
  `L2A-cases/granular-cross-comparison/review-pr108/REVIEW.md` and `CROSSCHECK.md`.
  Those reviews describe earlier states; do not treat their historical verdict as a new result.
* Do not reset, clean, stash, switch branches, commit, push, merge, flash, change source code,
  edit protocols/golden hashes, initialize missing dependencies or regenerate a test package.
  Local validation builds, separate review logs/reproductions and the requested report/handover
  update are authorized. Preserve existing edits and evidence. If the source changes during
  review, report the mismatch rather than attributing results to the wrong snapshot.

## Questions to resolve

The goal is an independent second opinion, including adversarial cases, rather than just
rerunning tests that Codex wrote. Report each item **CONFIRMED** or **NOT CONFIRMED**, with
reasoning and observed evidence. Add actionable new findings as **R13+**, with priority,
file/line, trigger, consequence and a minimal reproduction or precise execution trace.

### R9: generation-free rate hand-off

Files: `instruments/toy-piano/src/cumulonimbus_host.h`, production integration in `main.cpp`,
and `TestRateSync` in `tests/test_cumulonimbus.cpp`.

1. Reproduce the old 1,024-edit scenario against the new mailbox; test at least 4,096 edits
   with immediate audio service and with audio deferred, including rate zero.
2. Trace every direction change: pending edit → audio acknowledgement → pot publication →
   main consumption. Verify that a pending/newer editor value cannot be erased by an old
   acknowledgement or pot, and that a consumed report cannot reappear. Explain why repeated
   equal packed words/ABA do or do not change the latest-value semantics without a generation.
3. Check main publishing between audio Apply and Publish, main consuming between Publish's
   load and CAS, a newer edit arriving during Apply's CAS, and a newer pot arriving during
   main's consume CAS. Is the **two-attempt bound** valid with the actual producers/consumers?
   Does a failed consume leave the newest report available for the next service?
4. Exercise concurrent pot publication as well as editor updates; the existing concurrent
   test mainly stresses editor coalescing. Verify per-source independence, boot pot ownership,
   pickup, stale-event counting and no unintended patch/register divergence.
5. Verify memory ordering, atomic lock freedom on ARM, bounded callback work and absence of
   allocation/locks/blocking. Do not infer hardware CPU headroom from host timing.

### R10: retry cache preserves rejected writes

Files: `modules/mod-on-daisy/include/mod_on_daisy/controller.h`, module `TestLocalRetryCache`,
the new local-edit Makefile variant and Piano `TestEditorRetryAfterPotEdit`.

1. Verify that `last_crc_reply_` describes the actual cached successful reply, not just the
   request command. Check malformed/checksum failures, range failures, and repeated local edits
   followed by multiple identical retries. A NAK must retain its command/error/payload.
2. Check all five CRC-bearing commands (`SetMatrix`, `SetCell`, `ClearMatrix`, `SetSource`,
   `SetName`), successful replies returning the current CRC, and successful/failed
   `SetCalibration` and `SetScenario` replies without a patch CRC.
3. Ensure the original write is not re-executed over a newer local edit, reply echo suppression
   remains intact, and default builds without `MOD_ON_DAISY_LOCAL_EDITS` retain their behavior.
   Protocol and patch formats must remain compatible with both editors.

### R11: coherent drop-counter windows

Files: `instruments/toy-piano/src/main.cpp`, `RateMailboxes::stale`, and `TestCounterWindow`.

1. Trace all writers/readers of MIDI/control/stale totals, audio-owned baselines, reset and
   snapshot metadata. Verify the stated single-core main/ISR assumption and absence of any
   non-atomic cross-thread access. Explain event attribution at the counter increment.
2. Independently trigger the original failure: audio reset, then overflowing main-loop MIDI
   batch before main's next diagnostics service. The 40-write control case must report its
   **8 rejected writes**, not zero. Test actual note queue overflow too.
3. Generate extra drops after audio snapshot capture but before telemetry consumption. They
   must be absent from that encoded snapshot and present in a later snapshot in the same window.
   Check prior-window exclusion, repeated resets/setting changes, delayed main service,
   MIDI disconnection, and unsigned counter wrap.
4. Check the complete path through `SetApp`, `End`, slot release/acquire, `FillMeterMetrics`
   and encoding. Main must not overwrite captured drop metrics with later live totals.
   Preserve the corrected invalid-window classification and separate accounting-cost metric.

### R12: safe rejection and shared-host compatibility

Files: `common/audio/wet_host.h`, `TestWetHostRejection`, Cumulonimbus/Torus chain and Echo Delay
consumers; inspect SC Reverb's adapter even if its pinned dependency is unavailable.

1. Verify exact dry fallback before any wet-buffer access after a boolean rejection: unprepared
   Cumulonimbus, F32 47 frames, zero frames, separate/in-place channels and mix 0/0.5/1.
   Poisoning automatic variables is a useful diagnostic; distinguish it from production output.
2. Check a valid block after rejection, wet processing at normal 48-frame calls, feed/mix ramp
   state and tail behavior. Identify any unintended sound/state change.
3. Verify compile-time dispatch for legacy void adapters and their actual preconditions. In
   particular, can a void adapter silently reject a call the host accepts, leaving outputs
   unwritten? Do not assume a void return proves infallibility. State the scope of the fix.
4. Run Echo Delay component and production-firmware tests. Run SC Reverb only if its already
   installed pinned DaisySP-LGPL dependency is available; otherwise mark that consumer untested.

## Local validation on this Windows host

Prefer the repository's component Makefiles where practical. Use the main checkout's pinned
toolchains and pass `LIBDAISY_DIR=../../../../Daisy-Seed/libDaisy` and
`DAISYSP_DIR=../../../../Daisy-Seed/DaisySP` from the worktree component directories.
llvm-mingw's DLL directory must be on PATH. Use `HOST_CXX=clang++`, `PYTHON=python` where supported
and `-D_USE_MATH_DEFINES`. Some Windows recursive-make/`python3` steps need the direct commands
recorded by the local runner; distinguish environment failures from code failures.

The exact runners used by Codex are available in the **main checkout's** ignored
`.work/pr108-fixes/`. Inspect them before use; do not overwrite their original logs.
These PowerShell commands create a separate review log directory without changing reviewed code:

```powershell
$repoMain = 'D:\Eigene Dateien\Eigene Dokumente\Playground\Daisy-Seed'
$reviewRun = Join-Path $repoMain '.work/pr108-claude-review'
New-Item -ItemType Directory -Force -Path $reviewRun | Out-Null
Copy-Item -LiteralPath (Join-Path $repoMain '.work/pr108-fixes/run.py') -Destination $reviewRun
Copy-Item -LiteralPath (Join-Path $repoMain '.work/pr108-fixes/consumers.py') -Destination $reviewRun
python (Join-Path $reviewRun 'run.py') inventory before
python (Join-Path $reviewRun 'run.py') core
python (Join-Path $reviewRun 'run.py') mod
python (Join-Path $reviewRun 'run.py') editor
python (Join-Path $reviewRun 'consumers.py')
python (Join-Path $reviewRun 'run.py') supplementary
python (Join-Path $reviewRun 'run.py') arm
python (Join-Path $reviewRun 'run.py') inventory after
```

Check each exit code and every results JSON; do not infer success from the last PowerShell
command alone. The core runner covers both internal rates and all four Piano variants,
normally and with ASan/UBSan. Module tests include the local-edit opt-in configuration.
The ARM runner checks **current 0.1.2 images only**; older version-named files in incremental
build directories may remain and must not be treated as current outputs. Clean builds are
not required; state which objects were rebuilt. If the helper files are unavailable, derive
equivalent direct compiler/Makefile commands and record them in the report.

Also verify browser idle CRC polling (`E2E_ONLY=polling`, existing Edge/Playwright wrapper,
llvm-mingw DLL path, reviewer-built `mod-sim.exe`), `git diff --check`, component registry and
the built manifests. Full CLI examples and baseline hashes are in `PR108_FIXES.md` and the
earlier handover request. Use Linux g++/TSan only if a ready Linux environment exists; otherwise
record **NOT RUN**, not PASS. Do not provision a new environment or dispatch CI for this review.

Recompute the five legacy SHA-256 values against `PR108_FIXES.md`. Verify all **52 staged
package files** have identical SHA-256, size and mtime before/after review. That package still
contains older images and **does not include R9–R12**. Do not regenerate it or run the package
script. Board timing, IRQ duration, stack high-water, listening and flashing remain
**NOT MEASURED**. The 9,384-byte static D32 stack interval is not measured runtime headroom.

## Deliverables and verdict

1. Write `instruments/toy-piano/docs/CLAUDE_REVIEW_PR108_R9_R12_RESULT.md` in this worktree.
   Include reviewed HEAD and working-tree fingerprint, date, one status per R9–R12, new R13+
   findings, independent reproductions, exact commands/results, environment limitations,
   component versions and before/after package verification. Separate observation from inference.
2. Add one short verdict under the current R9–R12 follow-up in `HANDOVER.md`, linking the result.
3. Use either **READY FOR PACKAGE PREPARATION** (all four fixes confirmed, no unresolved
   correctness findings) or **FURTHER FIXES REQUIRED**. Report unavailable tests separately;
   justify whether any gap prevents that verdict. Neither verdict is hardware acceptance.
4. Stop after the report. Do not fix findings, commit/push/merge, publish, regenerate/stage
   firmware or flash the kit as part of this review. A later hardware session needs a freshly
   prepared package/store entry containing the reviewed fixes.
