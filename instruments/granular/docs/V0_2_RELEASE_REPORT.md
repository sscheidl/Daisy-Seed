# Nebulae on Daisy v0.2.0 - release report

**Verdict: SOFTWARE PASS / HARDWARE PENDING** (2026-11-01). The BIN below is a software
release candidate. It was not flashed, not listened to and not published. Release and
hardware acceptance are the human's decision.

| Item | Value |
| --- | --- |
| Branch | `claude/new-session-fqack4` (PR against `main`, not merged) |
| Firmware | `instruments/granular/build/NebulaeOnDaisy-v0.2.0.bin`, 183,652 B, SHA256 `3734dde5ee3d6ab462706f3c711c9ff107539959a7dfbb71d9a7663a4e5e2f19`, reproduced bit-identically by a second clean build |
| Control panel | Diagnostics app 0.5.0 (`diagnostics/windows`), Windows EXE from `Actions -> Hardware diagnostics` (see PR for the run and its SHA256) |
| Evidence | [VALIDATION.md](VALIDATION.md), [architecture](V0_2_ARCHITECTURE.md), [protocol](CONTROL_PROTOCOL.md) |

## Gates

| Gate | Result | Evidence |
| --- | --- | --- |
| G0 audit, architecture | **PASS** | [V0_2_ARCHITECTURE.md](V0_2_ARCHITECTURE.md): sources, baseline v0.1.1 reproduced (`e30380f7…e50a`), reuse plan, restart strategy, memory, 10 risks |
| G1 SD architecture, ownership | **PASS** (software) | card reader moved to `common/sd/reader`, Diagnostics images byte-identical; one FatFs (R0.16 read-only) per image, `sd-check`; ownership/quarantine/eject tests; simulated Windows write -> eject -> return -> rescan with matching hashes on FAT32 and exFAT |
| G2 USB, mode switch | **PASS** (software), enumeration HARDWARE PENDING | single image, two personalities via checksummed one-shot boot intent; SysEx STATUS/SHARE with token, confirm, busy, no-card, version/length errors; PREPARE_SHARE order; S4 escape; power-on always performance; reader descriptor tests unchanged and passing; MIDI regressions in `test_callback` |
| G3 control panel | **PASS** (software) | 309 Python/GUI tests incl. 28 new; Nebulae tab with warning/confirm/eject help/return/acknowledge; `--sim-nebulae`; diagnostics functions unchanged (existing 281 tests); local PyInstaller smoke build self-tests in both simulation modes; Windows EXE built in CI |
| G4 banks, SW17, LEDs | **PASS** (software) | 0/1/16/17 banks and slots, natural order, filters, legacy names, errors, SW17 debounce/suppression, dimming, FILE per bank, card removal, real FatFs in the production callback test |
| G5 DSP/review fixes | **PASS** (software), CPU HARDWARE PENDING | F3 packed FFT (no hop offset, A/B -88 dB) + latched D16 overrun LED; F6 6x clock cap as original lines 446-470; F7 gesture only in NEBULAE; sanitizers and TSan |
| G6 release candidate | **PASS** | ARM BIN/ELF/MAP/SHA256SUMS, image/ELF/symbol checks, `all-projects`, `test-projects`, `test-release`, `git diff --check`; PR created |

## Review findings

| ID | v0.1.1 | v0.2.0 |
| --- | --- | --- |
| F3 CPU peak | open | **reduced and instrumented**: one complex FFT less per channel and hop; overrun LED and STATUS counters. Real headroom NICHT GEMESSEN. The suggested 256-sample L/R hop offset was **not** used (stereo/transient risk). Moving `Mincer` to AXI SRAM was not done: it would leave little SRAM, and its benefit needs a board measurement. |
| F6 trigger grain size | open | **fixed** as the original; first grains after enabling trigger mode are short until the smoothed clock settles (also as the original) |
| F7 KEYBOARD bootloader | open | **fixed**, variant a (NEBULAE only); repo-wide rule deviation documented in the README |

## Known residual items (no P0/P1)

| Prio | Item |
| --- | --- |
| P2 | R1: whether the external bootloader keeps backup SRAM at `0x38800F00` is unproven; the safe failure is "back in performance", which the panel reports |
| P2 | Mincer A/B differs by float rounding (max 1.5e-4, -88 dB); inaudibility not verified by listening |
| P2 | exFAT names outside code page 850 cannot be opened (no 8.3 alias): error 1 on that slot |
| P3 | Bank/slot restore is by number; renaming folders in Windows can shift bank numbers |
| P3 | The Windows drive letter is not shown (optional in the order); the panel explains the eject |
| P3 | `python-rtmidi` is required for the performance-mode discovery; without it only SD management is found |
| P3 | Codec click/pop across the restart and the 0.8-2 s restart time are NICHT GEMESSEN |

## Handbook

The professionally designed Nebulae handbook (PDF) is **not in this repository** (no
branch contains it), so it could not be checked line by line. Changes the next handbook
version must cover, from the final firmware:

1. Version 0.2.0; FAT32 **and exFAT**; free file names; banks (folder layout, 16 x 16,
   natural order, ignored entries, legacy `01.wav` cards).
2. SW17 now has a function in SAMPLE (bank selection, LED brightness meaning); lever
   direction to be filled in after the hardware test.
3. FILE steps inside the selected bank.
4. Firmware update gesture S9+S16 **only in NEBULAE**; SysEx unchanged.
5. New chapter "SD card from Windows": control panel, Live-loop warning, eject, return,
   interruption/acknowledge, S4 escape, LED codes D1/D2/D4/D5/D16 in SD management.
6. D16 in NEBULAE = latched audio overrun.
7. Trigger mode: grain size follows the external clock at minimum DENSITY.
8. What is kept and what is lost across the restart (CONTROL_PROTOCOL.md table).

## To-dos for the human

1. Run [HARDWARE_TEST_PLAN.md](HARDWARE_TEST_PLAN.md) with a test card; record GEMESSEN
   or NICHT GEMESSEN per step (especially steps 6 and 7).
2. Set `kSw17BankWhenPressed` if the bank position is the other lever direction.
3. Decide on merge, release workflow and publication after hardware acceptance.
4. Update the handbook with the list above.

## HANDOFF

All gates are done; no unfinished work. Next commands after hardware results:
`make PROJECT=granular test`, update VALIDATION.md and this report, then the manual
release workflow if the human decides so.
