# Review order: Looper SD share handoff (main) and the PR #94 rework

Written 2026-11-06. Read [AGENTS.md](../AGENTS.md), [the SD handover](HANDOVER_2026-11-04_SD.md)
and [SD_WRITE_EXFAT_CLAUDE.md](SD_WRITE_EXFAT_CLAUDE.md) first.

Review only: no flashing, no hardware actions, no merge. Report findings as P1/P2/P3
with `file:line` and, where possible, a host-test reproduction. Mark results MEASURED
only when actually run.

## A. Now: targeted review of `main` @ `62a821a` (Looper 0.5.1)

Background: Codex's P1 finding on PR #94 (stale cached version before the restart)
applies only partly to `main`. There, SHARE compares a fresh engine snapshot with
`store.SavedVersion()`. A reading of the code (not tested) leaves a window of about
one audio block: after ACCEPTED the audio callback keeps processing keys and MIDI
until `StopAudio()`.

1. **Share handoff race** in `instruments/looper/src/main.cpp`.
   - `HandleControl` (l. 492) decides ACCEPTED from `CurrentStatus()` (l. 461, snapshot
     version vs `store.SavedVersion()`).
   - `PrepareShare` (l. 587) stops audio only after the fade.
   - `AudioCallback` (l. 380) keeps running `Controls()` and `engine.Process()` until
     then.

   Can an undo, erase or CC edit between the decision and `StopAudio()` be lost
   without confirmation? If yes, propose the smallest fix and a production-callback
   test in `tests/test_callback.cpp`. One possible fix: block edits once `fadeOut` is
   set, then re-check `engine.ContentVersion() == SavedVersion()` after `StopAudio()`,
   falling back to "not shared" or CONFIRM_REQUIRED.
2. **Restart pending.** Confirm that the PR #94 finding "SHARE accepted while a restart
   is pending" is fully closed on `main` for both instruments
   (`instruments/looper/src/sd_admin.cpp`, `instruments/granular/src/admin.cpp`) and
   covered by tests.

## B. Later: mandatory review after PR #94 is rebuilt on `main`

1. **Writable `common/sd` profile.**
   - Each image links exactly one FatFs/configuration.
   - Headers match the linked objects.
   - The Nebulae image stays read-only, and its symbol check rejects an injected
     writer.
   - Positive and negative symbol checks exist for both profiles.
2. **`disk_write`/sync.**
   - Bounded at every HAL wait, with programming completion and the RCC-reset abort.
   - Cache-safe buffers, card removal and reinitialisation handled.
   - Coexists with the card reader in `instruments/looper/src/sd_management.cpp`
     (abort and release before the restart).
3. **Looper persistence on FAT32 and exFAT.**
   - The failure boundaries `loop.tmp` → `loop.wav` (write, close, unlink, rename) are
     documented without claiming an atomic rename.
   - The power-loss campaign is still green.
   - Save-before-share and the 0.5.1 "unusable card → confirm" path are unchanged.
4. **Registry.** `common/sd` (and its reader) are versioned and registered in
   `components/registry.json` and in the looper, looper-mod and nebulae compositions.
5. **Unchanged behaviour.**
   - Golden `2A2DBCEC41A54F57` unchanged, regular = MOD.
   - The panel no longer says "FAT32 only" for the Looper.
   - No protocol change.

Validation to rerun:

- `make PROJECT=looper test test-sanitize`
- `make PROJECT=looper-mod`
- `make PROJECT=granular test`
- `make -C common/sd test test-sanitize arm-check`
- `pytest diagnostics/tests/python`
- `scripts/check-firmware.py` and `sd-check` on all images
