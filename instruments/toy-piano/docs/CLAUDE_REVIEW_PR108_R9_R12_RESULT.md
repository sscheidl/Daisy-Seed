# Claude Code: independent review of PR #108 fixes R9–R12 — result

**Verdict: READY FOR PACKAGE PREPARATION.** R9, R10, R11 and R12 are **CONFIRMED**.
Two new **P3** findings, R13 and R14, are reported below. Neither is reachable in the products that
would be packaged, and neither blocks package preparation; the reasoning is under each finding.
This is not hardware acceptance. Board timing, IRQ duration, stack high-water mark, listening and
flashing remain **NOT MEASURED**.

> **Update, 2026-11-12, after this review:** the owner asked for R13 and R14 to be fixed. Claude
> fixed both on top of the reviewed state: WetHost 0.2.0 requires a bool `Process`; Echo Delay and
> SC Reverb are 0.1.1; the controller's retry metadata no longer changes for frames it does not
> cache. The fixes and their validation are in [PR108_FIXES.md](PR108_FIXES.md#r13-and-r14-after-the-claude-review).
> Everything below describes the **reviewed snapshot** (diff fingerprint `3bf424a5…6677`); the
> source has changed since, so that fingerprint no longer matches the working tree. The R13/R14
> fixes were written by this reviewer, so they have not had an independent review.

Review date: **2026-11-12**, Windows 11 host. Reviewer: Claude Code (Opus 5.5), as ordered in
[CLAUDE_REVIEW_PR108_R9_R12.md](CLAUDE_REVIEW_PR108_R9_R12.md). Report only: no source,
protocol, golden-hash, package or commit changes. The only files written in this worktree are this
report and one verdict line in `HANDOVER.md`. Build outputs went to ignored `build/` directories.

## Reviewed state

| Item | Value |
| --- | --- |
| Repository / PR / branch | `sscheidl/Daisy-Seed`, PR #108, `claude/toy-piano-granular-fx` |
| HEAD | `af53b325450bfaf09ec6846e611670d0d28cd752` (no newer commits) |
| Working tree | 13 modified tracked files plus 2 untracked docs (`PR108_FIXES.md`, the review order) |
| `git diff` fingerprint (SHA-256 of the full diff text) | `3bf424a53eecd60331883342d621a891420ec829afcd9b5eeb69c65b20006677`, the same at the start and end of the review |
| `PR108_FIXES.md` SHA-256 | `f41bc6752f3b0f67b9dc81daa0b03209f46c6b8778b27e68e463b1ee32f557a9` |
| Key source SHA-256 | `cumulonimbus_host.h` `ab160b10…c13a`, `main.cpp` `a393d085…b8bab`, `wet_host.h` `9a77ada5…a5bf5`, `controller.h` `973ec4f5…112f2` |

The source did not change during the review. All results below refer to this snapshot.
The full diff is saved as `.work/pr108-claude-review/worktree.diff` in the main checkout.

Component versions (read from the version sources and the built manifests): Toy Piano on
Cumulonimbus **0.1.2** (all four variants); standalone Cumulonimbus FX firmware **0.1.2**;
`cumulonimbus` DSP **0.1.1**; `cumulonimbus-chain` **0.1.0**; `cumulonimbus-meter` **0.2.0**;
`wet-host` **0.1.1**; `torus-reverb` **0.1.0**; `toy-piano` **0.5**; `mod-on-daisy` **0.1.0**;
web editor **0.4.1**; JUCE/VST3 editor **0.5.1**. The protocol (`protocol.h`, `PROTOCOL.md`) is
unchanged.

## Status per finding

| Finding | Status | Short reason |
| --- | --- | --- |
| R9: rate hand-off without generations | **CONFIRMED** | Exhaustive interleaving model: about 20,000 schedules, 0 violations; two mutants are caught. Real code: 1,023–4,097 edit laps with 0 reappearances. Four multi-threaded runs with concurrent pots converge with no older report reappearing. |
| R10: retry cache keeps rejected writes | **CONFIRMED** | 13 NAK cases on all seven cached commands keep their exact command, error and payload over repeated local edits and three retries. All five successful CRC commands return the current CRC and are not re-executed. A pre-existing P3 edge case is reported as R14. |
| R11: coherent drop-counter windows | **CONFIRMED** | Production `main.cpp`: the original 40-write case reports **8** rejected writes (and 8 note overflows) with blocks=2. Capture, reset, delay, disconnect and wrap at `UINT32_MAX-5` all behave as required. |
| R12: safe rejection in the shared host | **CONFIRMED** (scope: fallible bool adapters) | A/B with stack poisoning: the old host is wrong in 30/42 rejected configurations, the new host is exact dry in 42/42. The following blocks are bit-identical to those of a host that never saw the rejection. Void adapters are not covered by the fix: R13. |

## R9 — rate mailbox

### Code trace (observation)

Each of the three sources has one `std::atomic<uint32_t>` word: `0`, `kRateEditor|rate` (bit 31) or
`kRatePot|rate` (bit 30, rate in bits 0..13). A rate of 0 is `0x80000000` or `0x40000000`, so it
is distinct from an empty mailbox.

* **Main, `FromPatch`** (`cumulonimbus_host.h:251–261`): when the patch rate differs from
  `applied_`, `exchange(kRateEditor|patch)` replaces anything in the word. If the old word was a pot
  report, the stale counter is incremented. The newest edit therefore always replaces an older edit
  or an unconsumed pot. Boot (`started_ == false`) only records `applied_`, so boot pots keep
  ownership.
* **Audio, `Apply`** (`:220–228`): this loads an editor word, writes it to the panel, then runs
  `CAS(w → 0)`. If main stored a newer edit in between, the CAS fails and the newer word stays for
  the next block. An old acknowledgement therefore cannot erase a newer edit. `panel.Write` clears
  the pot-write flag and the pickup, so a pot moved in the same block cannot publish over the edit.
* **Audio, `Publish`** (`:229–245`): this runs at most two `CAS(w → kRatePot|rate)` attempts and
  only while no editor bit is set. Otherwise the pot event is counted as stale.
* **Main, `FromPots`** (`:262–278`): this loads a pot word and runs `CAS(w → 0)`. Only a successful
  CAS consumes the report. A failed CAS means audio replaced it with a newer report, which stays for
  the next service. A consumed report cannot reappear, because only the audio thread writes pot
  words, and only from a new `TakePotWrite`.
* **Production order** (`main.cpp:510–517, 729–740`): per block, the audio callback runs
  `ReadControls` → external writes → `Apply` → `Publish`. Per main-loop iteration, main runs
  `FromPatch` → `FromPots` → `LoadPatchLocal` (on a pot change). If `LoadPatchLocal` were to reject
  a rate, the next `FromPatch` would see patch ≠ `applied_` and re-send the patch as an edit. The
  register therefore re-converges to the patch rather than diverging.

**ABA and repeated equal words (inference):** the words carry only the value, not a version. If a
CAS succeeds against an equal word that was replaced and then restored (edit 7000 → 7001 → 7000, or
pot a → b → a), the value it acts on is the current latest value. The audio thread writes 7000 to
the register, and main's `applied_` is 7000. Main applies pot value a, which is again the newest
report. Intermediate values are coalesced away, which is the intended last-value semantics. No
older value can win, because each word is either the latest value or already superseded.

**Two-attempt bound (inference, then model-checked):** on the board, main never runs during the
audio ISR, so every `Publish`/`Apply` CAS succeeds on the first attempt. On a multi-core host, the
first attempt can fail only because main consumed (P→0) or stored an edit (→E). After a consume, the
word is 0 and only an edit can change it again: main cannot consume twice without a new report from
this sole producer. The second attempt therefore either succeeds or sees E and correctly refuses.
A failed `FromPots` CAS leaves the newer report in place.

**Memory ordering and lock freedom (observation):** all accesses that hand a value over use
`acquire`/`acq_rel`. The stale counter is `relaxed` and only counted. On ARM,
`static_assert(is_always_lock_free)` holds, because the images link. In the current D48 and P48 ELFs,
`AudioCallback` and `ServiceUsbMidi` contain only inline `LDREX`/`STREX`/`DMB` sequences, with
**0** calls to `__atomic_*`/`__sync_*` library functions. The callback work is bounded: three
sources, at most two CAS attempts each, and no allocation, lock or blocking call. The Piano suite's
callback allocation counter stays at 0. Host timing was not used to infer board CPU headroom.

### Independent reproductions (MEASURED 2026-11-12)

1. **Exhaustive interleaving model** (`repro/r9_model.py`): a hand transcription of the four
   functions with every atomic operation as a step. Five scenarios cover edit-then-consume against
   pot traffic, ABA edits, a zero-rate edit racing a zero pot, consumes racing three pots, and an
   edit storm against a pot storm. It explored **172 / 1,134 / 177 / 8,124 / 10,459**
   interleavings. Checked properties: P1 (`Publish` gives up only while an edit is pending), P2 (no
   pot is consumed while the newest edit is unacknowledged), P3 (no report is consumed twice or out
   of order), P4 (register == patch after a quiescent drain), P5 (stale count == discarded
   reports; every published report is consumed, discarded or superseded). Result: **PASS, 0
   violations.** Mutation check: with only one CAS attempt, P1 and P4 fail (a pot is lost and the
   register diverges). With `Apply` clearing by a plain store, P2 and P4 fail (a newer edit is
   erased). The model is therefore sensitive to both failure classes.
   *Limitation:* this is a transcription verified by inspection, not the compiled code.
2. **Original 1,024-edit scenario and wider laps against the real classes**
   (`repro/review_repro.cpp`): the boot pot 8192 is published and consumed, then
   n ∈ {1023, 1024, 1025, 2048, 4096, 4097} edits run on LFO1 and LFO2. The edit values cycle
   through 9000, **0**, 16383 and 9001. Audio runs every iteration (immediate), never (deferred) or
   every other iteration. Result for all 18 runs: **0** reappearances; the final register equals
   the last edit; stale = 0; every mailbox is empty at the end. Compare the original failure, which
   returned 8192 after 1,024 edits.
3. **Concurrent main and audio threads with moving pots** (same file): source 0 has a sweeping pot
   and editor edits; source 1 has a stationary pot and editor edits only; source 2 has a sweeping
   pot and no edits. The run uses 200,000 audio blocks with fast audio and 20,000 blocks with slow
   audio, where main services up to about 60 M times.
   * In "far edit" mode, the edits are outside the pot's reach, including 0. No pot report won after
     the first edit: **0**.
   * In "near edit" mode, the edits are 60 units above the newest pot value. Pickup and edit/pot
     collisions are frequent: 1,088 and 7,731 pot-applied events.
   * In all four runs: no older report reappeared; source 2 progressed independently (724–748
     reports); and after a quiescent drain, the register equals the patch for all three sources.
4. **FX-page boot**: no rate pot report is published; the register equals the patch defaults
   6000/6000/8000.
5. All of the above also pass under ASan/UBSan (`-O1`, recovery disabled).
   *Not run:* TSan (no Linux environment; see below).

The project's `TestRateSync` passes in all four Piano variants, both normally and with ASan/UBSan.
It covers the 4,096-edit cases, rate zero, a consumed boot pot, stale counting, an edit between
`Apply` and `Publish`, and a 20,000-edit two-thread run.

## R10 — retry cache keeps rejected writes

### Code trace (observation)

`controller.h:283–292`: after a state-changing request is queued and remembered,
`last_crc_reply_` is set only if the reply parses, echoes `command|reply bit`, has length 7 and
status 0. That is exactly the success shape produced by `Change()` (`:567–577`). The duplicate
branch (`:258`) refreshes the CRC only when all of the following hold:

* `local_since_last_` is set;
* `last_crc_reply_` is set;
* `RepliesWithCrc(last_command_)` is true.

In every other case, the cached bytes are queued unchanged. A NAK (`kNak`, length 2) can no longer
be rebuilt as an ACK.

* `SetCalibration` and `SetScenario` successes (length 1) are not CRC replies and stay
  byte-identical.
* Echo suppression (`MOD_ON_DAISY_IGNORE_REPLIES`) is untouched.
* All new code is inside `#if MOD_ON_DAISY_LOCAL_EDITS`, so default builds are unchanged.
* The protocol and patch formats are unchanged.

### Independent reproductions (MEASURED 2026-11-12, `repro/review_repro.cpp`)

* **NAKs preserved.** Each case: the request is NAKed, followed by two local edits and three
  identical retries. In all **13** cases, the command, error code and payload stay identical and the
  patch is not modified:
  * `SetMatrix`: short payload (err 0x02); checksum (0x01).
  * `SetCell`: depth 127 (0x04); bits 4 (0x04); checksum.
  * `ClearMatrix`: extra byte (0x02); checksum.
  * `SetSource`: source 9 (0x04); checksum.
  * `SetName`: 40-character length (0x04); checksum.
  * `SetCalibration`: device 16 (0x04).
  * `SetScenario`: without diagnostics (0x09).
* **Successes refreshed.** `SetMatrix`, `SetCell`, `ClearMatrix`, `SetSource` and `SetName` were
  each ACKed. Then 3 rounds × (local edit + 2 identical retries) all return status 0, length 7 and
  `CRC == CurrentCrc()`. The original write is not re-executed over the newer local edit (rate and
  matrix are unchanged after the retries).
* **`SetCalibration` success**: after a local edit, the retry is byte-identical (length 1).
* The module suites base/64/ext/local/echo/thread pass normally and with ASan/UBSan. The modified
  module `Makefile` also builds and runs `test_mod_local` (with `-Wshadow`) through
  `make -C modules/mod-on-daisy test HOST_CXX=clang++`. That make run then stops at the
  `python3 mod_probe.py selftest` recipe because of the Windows Store alias, which is an
  environment failure. The same selftest passes when run with `python`.
* The probe for R14 is described below.

## R11 — drop-counter windows

### Code trace (observation)

* **Writers**:
  * `control_drops` (`main.cpp:840`) and `midi_overruns` (`:853`) are incremented only in the main
    loop, in `ServiceUsbMidi`.
  * `rate_boxes.stale` is incremented by main (`FromPatch`) and by audio (`Publish`).
  * All three are `std::atomic<uint32_t>`.
* **Readers**: only the audio callback.
  * `RestartStatistics` (`:216–225`) captures the baselines `base_*` in the same block as
    `meter.ResetNow()`.
  * Every block, `SetApp(kMidiOverruns/kControlDrops/kStaleRates, total − base)` (`:681–683`) runs
    before `meter.End`. `End` copies `app_` into the snapshot slot, under `request_`/`ready_`
    ownership.
* **Main's role**: main only copies the released snapshot (`FillMeterMetrics`, which starts from
  `m = s.app`) and adds the stack fields. It no longer overwrites drop metrics with live totals and
  no longer rebases counters. `base_*` are audio-only plain variables.
* **Other reset paths**: the Piano has no other meter reset path (`RequestReset` is unused there).
  The standalone FX firmware has no main-owned drop counters.
* **Threading assumption**: the STM32H750 is single-core and the audio callback is an ISR that
  preempts main. An ISR that interrupts main's `LDREX`/`STREX` increment clears the exclusive
  monitor, so main retries. The audio thread sees either the old or the new total, never a torn
  value.
* **Attribution**: a drop belongs to the window in which main completes the increment, as seen by
  the next audio load. It does not belong to the window in which the MIDI byte arrived.
* **Wrap**: modulo-2³² subtraction is correct.
* The invalid-window classification and the separate `kAccountingMax` metric are unchanged
  (`stage_meter.h`; the probe tests pass).

### Independent reproductions (MEASURED 2026-11-12, production `main.cpp`, `repro/r11_repro.cpp`)

Each scenario was run with totals starting at 0, 77 and `UINT32_MAX-5`, with identical results:

| Scenario | Encoded telemetry (control / MIDI / stale) |
| --- | --- |
| A: drops in the prior window, then reset, then an overflowing main batch (40 CC70, queue+8 notes) before diagnostics service | **8 / 8 / 0**, blocks=2 (the original R11 case gave control_drops=0) |
| B: snapshot captured, then another overflow batch in the same main iteration before encoding | **8 / 8** (live totals were 16/16); a later snapshot in the same window: **16 / 16** |
| C: reset after capture but before encoding | captured **16 / 16** retained; the next snapshot in the new window: **0 / 0** |
| D: cap-change reset (CC 16/21) with prior-window drops (4 CC / 2 notes), main delayed 10 blocks, then overflow | **8 / 3**, blocks=11; prior-window drops excluded |
| E: USB host not configured (no telemetry sent), overflow, reconnect, snapshot | **16 / 11**: the window keeps counting across the disconnection |
| F: reset, then main replaces an unconsumed pot report with a patch edit | stale **1** |

Codex's `TestCounterWindow` passes in both profiling builds, normally and with ASan/UBSan.
My repro also passes under ASan/UBSan.

## R12 — safe rejection in the shared host

### Code trace (observation)

`wet_host.h:41–47`: if `ProcessWet` returns false, the host copies `left`/`right` to the outputs
before touching `wet_*`, and returns. In-place use per channel is safe because the input is copied
onto itself. The dispatch at `:57–65` uses `if constexpr(std::is_void_v<decltype(effect_.Process(block))>)`.
Cumulonimbus (`cumulonimbus.h:130`) and Torus (`torus_reverb.h:43`) return `bool`. StereoEcho
(`echo.h:61`) and ScReverb (`reverb.h:54`) return `void`. `frames > MaxBlock` still returns without
writing outputs, which is unchanged from before.

### Independent reproductions (MEASURED 2026-11-12, `repro/r12_repro.cpp`, `-ftrivial-auto-var-init=pattern`)

Poisoning is a diagnostic that makes reads of uninitialized stack memory visible. It is not
production output. The same source was compiled against the `af53b32` WetHost 0.1.0 and the current
WetHost 0.1.1. The rejected configurations cover F32 and F48, unprepared and prepared, frames
0/47/48, separate and in-place buffers, and mix 0/0.5/1:

| Host | Rejected configurations not exact dry |
| --- | --- |
| WetHost 0.1.0 (af53b32) | **30 of 42**: every non-empty rejected block, including mix 0 (poison × 0 is non-finite) |
| WetHost 0.1.1 (reviewed) | **0 of 42**; samples beyond `frames` were untouched |

* **After a rejected block (no ramp in progress)**: the following 200 valid 48-frame blocks are
  **bit-identical** to those of a never-rejected host, at F32 and F48. The rejection changes no
  later sound.
* **With a feed ramp in progress**: a fallible identity adapter (wet = feed) shows that the rejected
  47-frame block still advances the host's feed ramp. The next block runs at feed 0.900 instead of
  0.998, while the mix ramp does not advance.
  * This is observation O1. It is harmless and outside every production path: Toy Piano and the
    standalone FX firmware always call 48 frames on prepared effects.
* **Tail**: a rejected block returns dry for that block only. The effect's internal state is not
  touched, because Cumulonimbus rejects before any work. Tail processing resumes on the next valid
  block (the identical-output result above).
* **Consumers**: Echo Delay component and production-firmware consumer tests pass normally and with
  ASan/UBSan. **SC Reverb was NOT TESTED**: the pinned `DaisySP/DaisySP-LGPL` checkout is empty
  locally, and it was not initialised. Its adapter was inspected only (see R13).

**Scope of the fix:** it covers bool-returning (fallible) adapters. It does not establish that
void adapters are infallible. See R13.

## New findings

### R13 — P3: void adapters can silently reject a block that WetHost treats as accepted

* **Location**:
  * `common/audio/wet_host.h:57–63`;
  * `effects/echo-delay/include/echo_delay/echo.h:61–63`;
  * `effects/sc-reverb/include/sc_reverb/reverb.h:54–56`.
* **Trigger**: a void adapter returns without writing when it is unprepared (`!line_[0]`,
  `!reverb_`) or when `!audio::Valid(config_, block)`. The second case occurs, for example, when the
  adapter was prepared with a `config.max_block` smaller than the host's `MaxBlock`. The host then
  mixes its uninitialized `wet_*` stack buffers.
  * The comment "Existing infallible adapters return void and fill every valid block" is therefore
    inaccurate: the adapter's notion of "valid" can be narrower than the host's.
* **MEASURED reproduction** (poisoned, `repro/r12_repro.cpp`), using `WetHost<StereoEcho, 48>` at
  mix 0.5:
  * an unprepared StereoEcho gives **48/48** non-finite outputs;
  * a StereoEcho prepared with `max_block = 32` and called with 48 frames gives **48/48** non-finite
    outputs.
* **Reachability (inference from code)**: not reachable in the current compositions.
  * In both the Echo Delay and SC Reverb firmware, `Prepare({48000, 48})` must succeed before
    `StartAudio` (`main()` halts otherwise).
  * `config.max_block == MaxBlock == 48`.
  * The callback rejects `n != 48`.
  * The host buffers are non-null and distinct.
  * Toy Piano and Cumulonimbus host only bool adapters.
* **Consequence if reached**: undefined wet input; in practice garbage or NaN audio until the
  output guard clamps it.
* **Suggested fix**: give StereoEcho and ScReverb a bool `Process` (each a component version bump
  with its own tests), or make WetHost assert the void precondition. Do not add a per-block buffer
  clear to the normal path.
* **Does not block** Toy Piano package preparation.

### R14 — P3: an oversize state-changing SysEx clears the local-edit marker of the cached reply

* **Location**: `modules/mod-on-daisy/include/mod_on_daisy/controller.h:282–292` together with
  `Remember()` at `:626–628`.
* **Trigger**: a frame of our protocol whose body is 121–128 bytes. libDaisy keeps up to 128 body
  bytes; the protocol maximum is 120.
  * `Verify` NAKs it with `ErrLength`.
  * `Remember` silently skips it because `size > sizeof(last_request_)`.
  * The metadata (`last_command_`, `last_crc_reply_`, `local_since_last_ = false`) is still updated.
  * The cache keeps the earlier request and reply, but its local-edit marker is now cleared.
* **MEASURED reproduction** (`repro/review_repro.cpp`, probe section):
  1. `SET_CELL` is ACKed.
  2. A local edit follows.
  3. A 125-byte `SET_NAME` frame is NAKed (0x7f).
  4. The identical `SET_CELL` retry returns **0x52 with a CRC ≠ `CurrentCrc()`**.
* **Consequence**: a stale CRC in that one retry reply. It cannot turn a NAK into a success, because
  an oversize body always fails `Verify`. The editor's idle CRC poll (R5) detects the difference on
  its next poll.
* **Origin**: introduced with the R5 retry cache in `d4b89be`, not by R10.
* **Suggested fix**: update the `LOCAL_EDITS` metadata only when `Remember` actually stores the pair
  (for example, let it return bool).
* **Does not block** package preparation: it needs a protocol-violating frame and heals on the next
  idle poll.

### Observations without a requested change

* **O1**: during a rejected block, WetHost advances the feed ramp but not the mix ramp (measured
  above). This only matters for a fallible effect that rejects during a feed transition, which no
  current product does.
* **O2**: the built manifests record `repository_commit: af53b32` and `working_tree_dirty: true`.
  This is correct for this review state. A package should be prepared from a commit containing the
  fixes, so the manifest names a clean commit.

## Commands and results (all MEASURED 2026-11-12)

Runner scripts were copied unchanged from `.work/pr108-fixes/` to `.work/pr108-claude-review/`;
the outputs are in its `artifacts/`. Codex's original logs were not touched. Each exit code and
results JSON was checked.

| Command | Result |
| --- | --- |
| `python run.py inventory before` / `after` | 52 files; before == after (SHA-256, size, mtime_ns); both identical to Codex's `package-before.json` |
| `python run.py core` | 16/16 PASS: Cumulonimbus component and standalone logic at 48/32 kHz, Piano 48/32 × profile 0/1, normal and ASan/UBSan |
| `python run.py mod` | 14/14 PASS: base, 64, ext, **local**, echo, thread, mod-sim; normal and ASan/UBSan |
| `python run.py editor` | 12/12 PASS: six C++ editor suites, normal and ASan/UBSan |
| `python consumers.py` | 4/4 PASS: Echo Delay component and production-firmware tests, normal and ASan/UBSan |
| `python run.py supplementary` | 13/13 PASS: module benchmarks, sq-sim, the 6 Cumulonimbus probe tests, mod_probe selftest, looper/nebulae targets, SD vectors, gen_params, protocol/SQ JS tests, component tests |
| `python run.py arm` | 11/11 targets PASS (F48, F32, P48, D48, P32, D32, P0, FV1, FILTER, TAPE, AMBIENT) and 7/7 `check-firmware.py` PASS on current **0.1.2** images and `ToyPiano-v0.5.bin` |
| `E2E_ONLY=polling node editors/mod-on-daisy/web/tests/e2e.cjs <reviewer-built mod-sim.exe>` with Playwright through an installed-Edge wrapper (`.work/pr108-claude-review/edge_playwright.cjs`) and llvm-mingw on `PATH` | PASS: "instrument-side rate change reloaded by the idle CRC poll, no reload in step" |
| `python scripts/component_manifest.py --validate` | PASS: 40 components, 33 firmware products |
| `component_manifest.py --project <p> --built --output …` for the seven products | 7/7 PASS. All Cumulonimbus products list `wet-host` 0.1.1 and `cumulonimbus` 0.1.1, chain 0.1.0, product 0.1.2; profile builds add the meter 0.2.0 |
| `git diff --check` | PASS (only CRLF notices) |
| Legacy SHA-256 recomputed | 5/5 match `PR108_FIXES.md`: Toy Piano 0.5 `cb029b94…`, FV1 0.10.0 `778315f6…`, Filter `0f539c8d…`, Tape `a730cb2a…`, Ambient 0.1.3 `3e8efb3e…` |
| `python -I repro/r9_model.py` | PASS: 5 scenarios, 20,066 interleavings; both mutants detected |
| `repro/review_repro.exe` (`-O2`) and `review_repro_asan.exe` (ASan/UBSan) | PASS (R9 laps, FX boot, 4 concurrent runs, R10 cases; R14 probe output as reported) |
| `repro/r11_repro.exe` and the ASan/UBSan build | PASS (scenarios A–F × 3 starting totals) |
| `repro/build_r12.sh` (old and new WetHost, poisoned) | new: PASS 42/42 + R13 probe; old: 30 failures (the expected reproduction) |
| `make -C modules/mod-on-daisy test HOST_CXX=clang++ …` | All C++ steps, including the new `test_mod_local`, PASS; the recipe stops at `python3` (Windows Store alias), an environment failure; covered by the runner's `python` selftest |

**ARM rebuild statement:** `make` reported every target as up to date. **No object was rebuilt in
this review**: the current objects came from Codex's incremental build (for example,
`cumulonimbus48-profile/main.o` at 21:00:28, which is newer than the 20:50:56 sources). To confirm
that those images contain the reviewed code, `arm-none-eabi-nm` on the current 0.1.2 profile ELF
shows `base_midi`/`base_control`/`base_stale`, atomic `midi_overruns`/`control_drops` and
`rate_boxes`, and no `reset_epoch`. Older 0.1.1 files remain in the build directories and were not
used.

Repro sources, build scripts and logs are in the main checkout's ignored
`.work/pr108-claude-review/repro/`.

## Environment and limitations

* **Tools**: llvm-mingw clang 23.1.3; xPack ARM GCC 13.2.1 20231009; Python 3.12.10; Node
  v24.22.1; Microsoft Edge 157.0.4341.46 with the bundled Playwright; libDaisy `f715e487`;
  DaisySP `2c72eaf`.
* **NOT RUN**: Linux g++ and TSan (no WSL distribution or Docker on this host; nothing was
  provisioned and no CI was dispatched). The R9 thread checks above are ASan/UBSan plus a model,
  not TSan.
* **NOT TESTED**: the SC Reverb consumer (the pinned DaisySP-LGPL checkout is empty and was not
  initialised).
* **NOT MEASURED**: native JUCE/VST3 execution, board CPU/IRQ timing, stack high-water mark,
  listening and flashing. The 9,384-byte static D32 DTCM stack interval in `PR108_FIXES.md` is a
  link-time interval, not measured runtime headroom.
* These gaps do not affect the verdict:
  * TSan would add assurance for R9. The protocol is exhaustively model-checked, stressed under
    real threads, and its board execution model (an ISR preempting a single core) is a subset of
    the checked interleavings.
  * SC Reverb uses the unchanged void path, whose latent precondition is R13.
* **Package**: the staged `build/cumulonimbus-stage1/` package (52 files) is unchanged and still
  contains the **older images without R9–R12**. A freshly prepared package and store entry built
  from a commit containing these fixes is required before any board session.

## Verdict

**READY FOR PACKAGE PREPARATION.** All four fixes are confirmed. No unresolved correctness finding
affects the products to be packaged:

* R13 is unreachable in every current composition and lies outside the Toy Piano/Cumulonimbus
  products.
* R14 needs a protocol-violating frame, cannot misreport a NAK, and heals on the next idle poll.

Both are recommended as P3 hardening follow-ups. The owner may choose to treat them as blocking.
This verdict is not hardware acceptance.
