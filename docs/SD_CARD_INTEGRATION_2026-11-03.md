# Looper and Nebulae SD integration — 2026-11-03

## Scope

Integrates the outstanding Looper SD-management work from PR #78 with current
`main` (`53927d2`), retaining the current modularity and English-first policy.
New test candidates: Looper/Looper MOD **0.5.1**, Nebulae **0.2.1**, existing shared
Windows control panel **0.6.0**. No firmware was flashed, no bootloader was changed,
and no release or expensive Actions workflow was dispatched for this integration.

The two technical PR review findings are fixed:

- Looper rejects ownership-changing SHARE/RETURN commands while a performance or
  service restart is pending. Deferred resets revalidate local ownership and BOT
  idleness. The same race existed in Nebulae's management adapter and is fixed
  there too. Both suites cover performance/service requests, busy ACK delivery,
  an independently changed owner and a busy BOT transport before reset.
- Looper distinguishes cached filesystem readiness from known content version.
  An inserted but unmountable card with unsaved RAM audio now requires explicit
  loss confirmation instead of endlessly reporting SAVING. The production-main
  regression verifies refusal without consent, acceptance with consent, and
  recovery when the filesystem becomes usable again.

The historical documentation-language finding is superseded by the current
English-first AGENTS.md policy. The merge conflict in the editor README retains
the English download/operation instructions and updates the Looper image name.

## Architecture and compatibility

The reader remains the shared `common/sd/reader` component. No DSP, modulation
core or raw reader implementation changed. The small management adapters own
their product-specific restart state. Versions still come from each product's
version source; protocol framing and existing parameter/patch contracts are
unchanged. Nebulae's STATUS limits are checked as MIDI-safe version components
instead of pinning the old patch number in a second source location.

| Product | Local filesystem | Windows sharing |
| --- | --- | --- |
| Looper / Looper MOD 0.5.1 | Writable libDaisy FatFs R0.12c; **FAT32 required** for loop persistence/import | Shared raw-block reader, explicit ownership, stopped audio in SD management; save before sharing or explicit loss confirmation |
| Nebulae 0.2.1 | Shared FatFs R0.16 read-only, FAT32/exFAT sample banks; Live audio stays in RAM | Same raw-block reader; explicit Live-loss confirmation when applicable |

Windows performs its own filesystem access over USB MSC. This does not add exFAT
file writes to Looper's performance mode. Do not globally enable writes in the
read-only shared driver. The follow-up is described in
[the Claude work order](SD_WRITE_EXFAT_CLAUDE.md).

## Local software validation — MEASURED 2026-11-03

Windows, pinned xPack ARM GCC 13.2.1-1.1, LLVM-MinGW clang/clang++, Python 3.12.
The pinned libDaisy and DaisySP sources were checked out independently and their
archives rebuilt; no other task's build outputs were overwritten.

| Validation | Observed result |
| --- | --- |
| `make PROJECT=looper test HOST_CXX=clang++` | PASS; both production callback variants, storage/import/control tests and management tests (79 checks) |
| `make PROJECT=looper test-sanitize HOST_CXX=clang++` | PASS, ASan/UBSan |
| Looper golden, regular and empty-matrix MOD | Both `2A2DBCEC41A54F57`, unchanged from v0.4.0/v0.5.0 |
| `make PROJECT=granular test HOST_CXX=clang++ HOST_CC=clang` | PASS; management 77 checks, production callback, FAT32/exFAT simulated Windows round trips (4 PCM hashes verified per image), queues/bridge |
| `make PROJECT=granular test-sanitize HOST_CXX=clang++ HOST_CC=clang` | PASS, ASan/UBSan/float-cast-overflow |
| `python -m pytest diagnostics/tests/python -q` | 334 passed |
| ARM builds: `looper`, `looper-mod`, `granular` | PASS; all three BOOT_SRAM vector/version checks PASS |
| Granular ELF/actual-capacity boundary tests | PASS; 41,695,328-byte file capacity, isolated AXI DMA, stack/reserve and BIN/ELF checks |
| Granular `sd-check` | PASS; 36 banned, 6 required symbols |
| Packaged control-panel EXE, offscreen `--sim`, `--sim-nebulae`, `--sim-looper --selftest 4` | All exit 0 |

The FatFs Windows compatibility macros exposed an existing include-order problem
in the return-path host test. Loading the C++ math declarations first fixes the
Windows host test without modifying vendored FatFs or the firmware driver.
One initially stale version assertion was also corrected. Successful final
normal/sanitizer suites include these corrections.

Targeted component-selection checks, source/whitespace checks and final PR CI
status are recorded in the PR. No new thread/queue implementation was added;
no TSan result is claimed for this Windows run. Complete local command logs and
packaged-app screenshots remain under `.work/sd-validation/` in the integration
worktree. They are software evidence, not hardware measurements.

## Firmware candidates

| File | Bytes | SHA-256 |
| --- | --- | --- |
| `Looper-v0.5.1.bin` | 186324 | `d4e3b1b320b485c9e7b38cc135e053bb3182d04a9a259fbeaec0dd52d1afd3d1` |
| `LooperMod-v0.5.1.bin` | 220020 | `22d8c8839e5a527507cabbf931f8c3084c068c02c31288c0ab4b0ba1c5e2b4bd` |
| `NebulaeOnDaisy-v0.2.1.bin` | 183740 | `cbc4c37686be32eca39825459f95c676af7a7010c3c719faba1cf0def4e381ff` |

These are application images for the existing external Daisy bootloader.
Bootloader binaries are deliberately absent from the test package. The rebuilt
control panel is 0.6.0, SHA-256
`39510a840d3ac219644418d0a998724b5f249b52eccc2bc7e4e3d62c83f4ac21`.
An existing suitable 0.6.0 panel can be used; the rebuild is not a new GUI version.

## Hardware acceptance — NOT MEASURED

For these new candidates: real personality switching/boot-intent retention,
Windows enumeration, save-before-share timing, card removal and return, audio
transitions and the MOD variant remain NOT MEASURED. The historical Diagnostics
0.3.2 reader measurements do not validate the composed instrument images.

Start with a backed-up FAT32 test card for Looper. For Nebulae, test FAT32 first
and then exFAT; return only after Windows eject or explicit interruption
acknowledgement. A restart loses Nebulae's volatile Live audio. No format/repair,
card write, cable action, flash or listening result was performed or inferred by
the agent. The human controls these actions under the existing project checklist.
