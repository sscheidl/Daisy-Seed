# Streichwurst: validation status

As of 01.11.2026, product 0.1.0 (instrument 0.1.0, FX 0.1.0), Control App 0.3.0.

| Subject | Status |
| --- | --- |
| Instrument DSP, FX DSP, product, firmware host fakes | Host tests passed, also with ASan/UBSan (MEASURED 01.11.2026) |
| ARM firmware | built, image check passed, memory from the map file (MEASURED 01.11.2026) |
| Control App (core, browser, JUCE standalone, VST3) | Tests, self-test and plugin check passed (MEASURED 01.11.2026) |
| Standalone FX firmware (FX first) | built and checked on host fakes; acceptance on the kit **NOT MEASURED** ([FX test sheet](../../../effects/streichwurst-fx/docs/HARDWARE_TEST.md)) |
| Board audio / controls / MIDI / control app on the kit | **MEASURED 01.11.2026 (partly)**, human report: keys, MIDI keyboard, Layer, pots, ensemble modes, JUCE EXE OK, no audio or performance problems. Split, LEDs/pages, presets, panic, bootloader, matrix, patches, VST3 and browser **NOT MEASURED**; details in [HARDWARE_TEST.md](HARDWARE_TEST.md) |
| CPU load / stack at run time (counters) | **NOT MEASURED** (no problems heard, counters not read) |
| Listening comparison with the original plugin/device | **NOT MEASURED** |
| Own listening impression | **MEASURED 01.11.2026**, human report: ensemble modes and phaser very musical and convincing; reverb somewhat weak and metallic (to improve later) |

Software tests do not prove sonic identity with the original. Deviations per module:
[FIDELITY.md](FIDELITY.md).

## Environment (MEASURED 01.11.2026)

Worktree `.work/streichwurst`, branch `claude/streichwurst`, Windows 11.
xPack ARM GCC 13.2.1-1.1, xPack build tools 4.4.1-3, LLVM-MinGW Clang 23.1.3 (host),
Visual Studio 18 2026 (MSVC) with JUCE 9.0.4, Python 3, Node 24, Playwright with the
installed Edge. libDaisy `f715e48`, DaisySP `2c72eaf` unchanged (built locally).

## Commands and results (all MEASURED 01.11.2026)

| Command | Result |
| --- | --- |
| `make -C effects/streichwurst-fx test HOST_CXX=clang++` | passed: delay ranges 4.40–8.00 ms resp. 3.00–9.40 ms, chorus LFO period 3.125 s, DC per mode 2.30420/3.64746/2.87695 (SF-F06), no feedback, exact bypass, transition without jump, Animate ±8000, reverb decays, adapter = component, 20 s of noise finite |
| `make -C effects/streichwurst-fx test-sanitize HOST_CXX=clang++` | passed (ASan + UBSan) |
| `make -C instruments/streichwurst test HOST_CXX=clang++` | passed: instrument (clocks, pitch, band limit: alias of the 5th harmonic −30 dB, τ measurement 0.630/0.370, registration/octaves, same output at block sizes 48/7/1, sustain/retrigger/steal, queue and zero blocks, NaN), product (register, routing, gain word 0.6250, balance centre factor 1, empty matrix bit-identical, panic, headroom: peak 0.691, reverb→delay swap, controls incl. bootloader chord, own fingerprint), firmware on host fakes (SQ 1.1, read first, ACK/revision, retry, echo, CC, pots in the monitor, matrix, channel filter, USB loss, queue overflow without hanging notes, bootloader after panic, no allocation in the callback) |
| `make -C instruments/streichwurst test-sanitize HOST_CXX=clang++` | passed (ASan + UBSan) |
| TSan (`test-thread`) | **not executable**: a trial compilation with `clang++ -fsanitize=thread` reports "unsupported option … for target x86_64-w64-windows-gnu". The hand-over uses the TSan-checked SPSC queue and the triple buffer of Step Seq as well as an atomic counter |
| `make PROJECT=streichwurst` (ARM, `-O3`, BOOT_SRAM) | built: `Streichwurst-v0.1.0.bin`, 239 020 bytes, SHA-256 `f244ae6b…681ca75e9` (local build after merging `main` and the `ClearTails` extension, not published) |
| `python scripts/check-firmware.py instruments/streichwurst/build/Streichwurst-v0.1.0.bin` | `PASS: v0.1.0, 239020 bytes, stack=0x20020000, reset=0x24000749` |
| `make -C effects/streichwurst-fx test` / `test-sanitize` (incl. standalone firmware on host fakes) | passed: exact pass-through with ensemble off, mono input, modes, pots, CC, SQ with monitor, S16 clears (residual energy 0), bootloader buttons, no allocation |
| `make PROJECT=streichwurst-fx` (standalone FX, ARM) | built: `StreichwurstFx-v0.1.0.bin`, 164 508 bytes, SHA-256 `1a0f4c96…5479f9b6`; DTCM 112 760 B (86.0 %), stack reserve 18 312 B, SDRAM 31 872 B |
| `python scripts/check-firmware.py effects/streichwurst-fx/build/StreichwurstFx-v0.1.0.bin` | `PASS: v0.1.0, 164508 bytes, stack=0x20020000, reset=0x240003d1` |
| `python -m unittest discover -s tests -p test_firmware_checker.py` | passed (new Streichwurst cases, Acid Seq unchanged) |
| `python -m unittest discover -s tests -p test_components.py` | passed (register, product = BUILDABLE_PROJECTS, CI selection) |
| `python scripts/component_manifest.py --validate` | `PASS: 23 components, 19 firmware products` |
| `python sequencers/step-seq/tools/gen_params.py --check` | passed |
| `make -C sequencers/step-seq test HOST_CXX=clang++` (C++ parts) and `python tools/sq_probe.py selftest` | passed (shared SQ device file changed) |
| `make -C sequencers/acid-seq test HOST_CXX=clang++` | passed |
| `python -m unittest discover -s tests -p test_acid_seq_contracts.py` | passed |
| Editor core: `test_core`, `test_sq`, `test_streichwurst` (Clang, also ASan/UBSan) | passed |
| `node web/tests/sq_protocol.test.cjs`, `sq_client.test.cjs`, `protocol.test.cjs` | passed (incl. Streichwurst and `.swpatch`) |
| `node web/tests/e2e_sq.cjs build/host/sq_sim build/host/mod_sim` (Playwright/Edge) | passed: Acid Seq unchanged, Streichwurst reads SQ before MD, pages, matrix toggle; MD device unchanged |
| `node web/tests/e2e.cjs build/host/mod_sim` | **failed** locally, but equally with the unchanged state `2a3affb` (cross-check): an environment problem of this PC, not caused by this change |
| JUCE: `cmake -S editors/mod-on-daisy -B build/editor -G "Visual Studio 18 2026" -A x64 -DMOD_EDITOR_JUCE_DIR=…/juce-9.0.4/src`, `cmake --build build/editor --config Release` | Standalone, VST3, plugin check and core tests built |
| `"Mod on Daisy Editor.exe" --selftest selftest-sw2` (with `MOD_ON_DAISY_NO_AUTOCONNECT=1`) | passed, incl. "PASS Streichwurst: SQ pages by contract page, matrix alongside, edits, song patch with matrix" |
| `ModOnDaisyPluginCheck "Mod on Daisy.vst3" legacy-reverb.modpatch` | `PASS: VST3 plugin loads, passes audio, keeps its state` |
| `make compare-rates` | Table in [RATE_COMPARISON.md](RATE_COMPARISON.md) |
| `python -B tests/reference/own_dsp_model.py --test` | unchanged historical model, not used again as evidence |

Not run locally: Linux g++ runs (taken over by `quick.yml` on push/PR with the new
suite `streichwurst`), manual Actions workflows (not started).

## Resources from the map file (MEASURED 01.11.2026, no runtime measurement)

| Region | Usage | Content |
| --- | --- | --- |
| DTCM (128 KiB) | 122 296 bytes (93.3 %) | `.data` 13 448 + `.bss` 108 848; of which libDaisy `hw` 38.7 KiB and `usb_midi` 37.2 KiB, product DSP state ≈ 17 KiB |
| Stack reserve DTCM | 8 776 bytes | Remainder up to `_estack` 0x20020000; check the runtime need via the "Stack free" counter (H18) |
| AXI SRAM (480 KiB, image) | 239 020 bytes (48.6 %) | Code, constants incl. 8 KiB event kernel (`constexpr`) |
| SDRAM | 38 016 bytes | Reverb lines (31.9 KiB) and key tables of the instrument (6 KiB), only used on events |
| RAM_D2_DMA | 18 084 bytes | Audio DMA (libDaisy) |

The audio load on the Cortex-M7 is **NOT MEASURED** (target ≤ 60 %). No M7 load is derived from host or
Python run times. The firmware reports the callback peak,
overruns, queue high-water mark and free stack via SQ `GET_STATS`.

## Own sound fingerprint

`test_product.cpp` checks four RMS values of a fixed sequence (0.116761 / 0.079304 /
0.027349 / 0.002420, tolerance 0.2 %). It pins the **own** behaviour so that
sound changes happen deliberately; it says nothing about the original sound. The tolerance
covers compiler/libm differences between Windows Clang and Linux g++; identical values
under `-O0` and `-O2` were observed locally.

## Historical preparation (01.11.2026, unchanged)

Base `origin/main` at `654e42e`, own worktree `.work/streichwurst/`. At that time:
compiler probes ARM/host, nine model tests, ten registry checks; `make
PROJECT=streichwurst` was deliberately rejected as "planned". The historical
Python model remains an own reference for partial relationships, not an original renderer.
