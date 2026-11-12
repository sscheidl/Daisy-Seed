# PR #108 follow-up fixes: R9–R14

**MEASURED: 2026-11-12, Windows host.** Implemented on
`claude/toy-piano-granular-fx`, based on `af53b32`, after the Codex cross-check.
Toy Piano on Cumulonimbus and the standalone FX firmware are **0.1.2**.
WetHost is **0.2.0**: the 0.1.1 below was the R9–R12 state, never committed, and was then
changed for R13 (see [R13 and R14](#r13-and-r14-after-the-claude-review)).
The granular DSP component remains **0.1.1**, the chain **0.1.0** and the meter **0.2.0**.
Echo Delay and SC Reverb (component and standalone firmware) are **0.1.1**.
Version sources feed the existing component/product manifests.
The Mod on Daisy wire protocol, editor protocol and musical patch schema are unchanged.

## Fixes and regression coverage

| Finding | Implementation | Regression evidence |
| --- | --- | --- |
| R9: old pot report aliases a wrapping generation | One atomic bidirectional mailbox per source replaces both generation-stamped words. Main stores a pending edit, audio acknowledges it, and main consumes pot reports exactly once. A pending edit cannot be overwritten by a pot. Each audio publication has at most two CAS attempts; no queue, wrapping counter, lock or allocation is introduced. | `TestRateSync`: 4,096 edits with audio applying each edit and 4,096 edits with audio deferred, zero-valued edits, independent sources, consumed reports, stale-event counting, an edit between audio Apply/Publish, and a concurrent 20,000-edit main/audio hand-off. |
| R10: cached NAK becomes success after a local edit | The controller records whether the cached response is a successful CRC-bearing ACK. Only that response can receive an updated CRC. Cached NAKs retain their original error and payload. The code remains behind `MOD_ON_DAISY_LOCAL_EDITS`. | Module `TestLocalRetryCache`: checksum-rejected requests for all five CRC-bearing commands, repeated retries after a local edit, invalid-depth `SET_CELL`, and a successful retried write returning the current CRC. The module Makefile now runs this opt-in build too; the existing Piano successful-retry test still passes. |
| R11: reset/snapshot boundaries lose or misattribute drops | Main-owned totals are lock-free atomics. Audio captures their baselines in `RestartStatistics` and puts deltas in the same audio-owned metadata snapshot as the window statistics. Main no longer rebases totals later or appends live totals after capture. Stale-event totals are published directly by their producers. | Production-callback `TestCounterWindow`: real control/note queue overflows after reset, extra drops after snapshot capture, encoded telemetry retaining the earlier counts, a later snapshot including the new drops, reset excluding prior totals, and unsigned 32-bit counter wrap. Runs at both rates in profiling builds. |
| R12: WetHost reads unwritten wet buffers after rejection | A false result from a fallible FX returns exact dry before wet buffers are accessed. Existing void-returning adapters retain their accepted-block behavior through compile-time dispatch (superseded by R13: WetHost 0.2.0 requires bool). No buffer initialization/copy is added to their normal wet path. | `TestWetHostRejection`: unprepared effect, rejected F32 47-frame call, separate/in-place buffers, mixes 0/0.5/1, and a subsequent valid block. Existing Echo Delay component and production-firmware consumer tests pass normally and with sanitizers. |

Instrument/FX routing remains modular. These fixes do not alter the granular renderer,
reverb algorithm, density reference, smoothing, Freeze crossfade or accepted legacy hashes.
Audio processing and snapshot storage remain bounded and allocation-free; the callback
allocation checks pass. The counter-window ordering relies on the board's single-core
main/ISR execution model and uses atomic totals so ISR reads do not race with main writes.

## Validation

All results below are **MEASURED 2026-11-12** on Windows using the pinned local ARM toolchain
and bundled llvm-mingw Clang. Direct host compilation uses `-Wall -Wextra -Werror`, and
sanitized runs use ASan/UBSan with recovery disabled. Local logs and the exact command runner
are in the main checkout's ignored `.work/pr108-fixes/` directory; no broad CI or release
workflow was added or started.

| Check | Result |
| --- | --- |
| Cumulonimbus component, standalone logic and all four Piano rate/profile variants | PASS, 8 normal and 8 ASan/UBSan executions |
| Mod on Daisy base/64/extensions/echo/local-edit and thread tests | PASS, 6 normal and 6 ASan/UBSan executions; both simulator builds pass |
| All six C++ editor test suites | PASS, normally and with ASan/UBSan |
| Shared WetHost Echo Delay component and production-firmware consumers | PASS, normally and with ASan/UBSan |
| Browser idle CRC polling against the real controller simulator (installed Edge) | PASS |
| Cumulonimbus Python probe tests | PASS, all 6 |
| Module benchmarks/probe selftest, looper/nebulae target checks, SD vectors, sequencer protocol/client, parameter generation and component-selection tests | PASS |
| F48/F32, P48/P32, D48/D32 and P0 ARM targets | PASS; incremental builds recompiled affected objects and linked the versioned images |
| FV1/FILTER/TAPE/AMBIENT legacy ARM targets | PASS |
| Seven current image vector/version checks | PASS |
| Component registry and seven built composition manifests | PASS; 40 components, 33 registered products |
| `git diff --check` | PASS |

D32 ELF symbols give `__reserved_for_stack_end__=0x2001db58`, `_estack=0x20020000`
and D2 heap `end=0x30008000`: a static **9,384-byte** DTCM stack interval. This is not a
measurement of runtime stack use or CPU headroom.

The five legacy image SHA-256 values still match the recorded 241f583 baseline exactly:

| Image | SHA-256 |
| --- | --- |
| Toy Piano 0.5 | `cb029b94dc7b2b37c7e68900beaf610a1598e6045abe14f88478ae4d95e19db0` |
| FV1 0.10.0 | `778315f615692a35d3ffb379e149099387c555c5a02087fa917e2918b987a18f` |
| Filter 0.1.0 | `0f539c8da8ed543a9b4efda17e29b0add307ba6f6f8f6ffb28df20dcb4970d31` |
| Tape 0.1.0 | `a730cb2afe9a1bb70304774752cf5e0d934cd2ff4e00003515f67794c3c62be6` |
| Ambient 0.1.3 | `3e8efb3e72801bb64125a519031484d8a29167a63ff5cdc26d3d4af735eab8f7` |

SC Reverb consumer validation was not run because the pinned DaisySP-LGPL checkout is
absent locally. (At R12 its void-returning adapter was preserved; R13 below converts it.)
Linux g++, TSan, native JUCE/VST3 execution, hardware timing,
stack high-water, listening and flashing remain **NOT MEASURED** in this follow-up.

## R13 and R14 after the Claude review

The [independent review](CLAUDE_REVIEW_PR108_R9_R12_RESULT.md) confirmed R9–R12 and reported
two P3 findings. The owner asked for both to be fixed; Claude implemented them on 2026-11-12,
uncommitted, on top of the reviewed state. Because the reviewer also wrote these fixes, they
have no independent second review.

| Finding | Implementation | Regression evidence |
| --- | --- | --- |
| R13: a void adapter can silently reject a block that WetHost treats as accepted, so WetHost mixes unwritten wet buffers | WetHost **0.2.0** requires `bool Process(StereoBlock)` (`static_assert`) and returns the exact dry input when it is false; the void dispatch is removed. StereoEcho and ScReverb now return false when they reject (unprepared or invalid block) and true otherwise; the work on accepted blocks is unchanged. Echo Delay and SC Reverb are 0.1.1 (component and firmware). | Echo `TestContract` checks every return value; new `TestWetHostRejection` (unprepared echo, and an echo prepared for 32 frames called with 48; mix 0/0.5/1; separate and in-place) requires exact dry. A test void adapter no longer compiles. The review's poisoned-stack probe gives 0/48 non-finite outputs (was 48/48). |
| R14: an oversize state-changing SysEx (body 121–128 bytes) is NAKed and not cached, but cleared the cached reply's local-edit marker, so a later retry got a stale CRC | The `MOD_ON_DAISY_LOCAL_EDITS` metadata is updated only when `Remember()` actually stores the pair (`size <= sizeof(last_request_)`). Default builds are textually unchanged. | `TestLocalRetryCache`: SET_CELL ACK, local edit, 125-byte SET_NAME NAK (`ErrLength`), then the identical retry must carry the current CRC. The test fails at that check against the previous logic and passes with the fix. |

Image effects (MEASURED 2026-11-12):

* **Toy Piano on Cumulonimbus (P48/P32/D48/D32):** the images change, because these builds
  enable `MOD_ON_DAISY_LOCAL_EDITS` (R14).
* **Standalone Cumulonimbus FX (F48/F32):** these images also change, although their effects
  already returned bool. An A/B ARM compile of `main.cpp`, against the reconstructed reviewed
  WetHost 0.1.1, shows a `.text` 4 bytes smaller. After normalising addresses there are only
  three equivalent differences:
  * a swapped `cmp` with the complementary condition;
  * a swapped `cmp` before a `beq`;
  * two stores merged into an `strd`.
* **Versions:** the products stay **0.1.2**, which was never committed or released.
* **Legacy images:** the five legacy images keep their SHA-256.

| Check (after R13/R14) | Result |
| --- | --- |
| Core, mod (incl. local-edit), editor, Echo Delay consumer and supplementary runners | PASS: 16 + 14 + 12 + 4 + 13 jobs, normal and ASan/UBSan where applicable |
| Browser idle CRC polling (Edge, rebuilt `mod-sim.exe`) | PASS |
| ARM F48/F32/P48/D48/P32/D32/P0/FV1/FILTER/TAPE/AMBIENT and `echo-delay-fx` | PASS; 7/7 current-image checks and `EchoDelayFx-v0.1.1.bin` check PASS |
| Component registry; built manifests for the seven products plus `echo-delay-fx` | PASS; they list `wet-host` 0.2.0, `echo-delay` 0.1.1 |
| SC Reverb | **NOT RUN**: DaisySP-LGPL absent. Only `-Werror` syntax checks of `test_sc_reverb.cpp` (host) and `src/main.cpp` (ARM flags) against a stand-in `ReverbSc` declaration |
| `git diff --check`, five legacy SHA-256, staged package (52 files) | PASS; package unchanged |

Logs: main checkout `.work/pr108-r13r14/` (ignored).

## Package and handover

All **52 files** in the existing `build/cumulonimbus-stage1/` package retain their SHA-256,
size and modification timestamp. That preserved package still contains the earlier images;
it does **not** contain these fixes. A fresh package/store entry is required before handing
the fixed firmware to the human for board acceptance. Validation build outputs alone are
not a hardware delivery. No push, merge, flashing or package generation was performed.
