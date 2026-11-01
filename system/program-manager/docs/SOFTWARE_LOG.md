# Software log: program manager

Only executed checks; hardware throughout **NICHT GEMESSEN**. Environment: Windows 11,
xPack GCC 13.2.1-1.1, GNU Make 4.4.1, clang 23.1.3 (llvm-mingw, host tests), Python 3.12.10;
base `origin/main` `ab79ccf` (PR #79), later merged with `f8af751` (PR #87); libDaisy
`f715e487`, DaisySP `2c72eaf9`, no dependency changed.

## 2026-11-01 – phases 0–3, 5 (software)

| Time | Command | Result |
| --- | --- | --- |
| 20:40 | `make -C system/program-manager test HOST_CXX=clang++ HOST_CC=clang` | test_sha256 14/0, test_image 51/0, test_catalog 95/0, test_loader 77/0, test_manager 125/0, test_protocol 96/0, test_diag_hooks 26/0, test_return_entry 349/0 (checks/failures) |
| 20:45 | `make -C diagnostics/firmware test HOST_CXX=clang++` (regression after hook/adapter) | core 465, app 733 (948 after the merge with main), reader 4654, usb 53, dma 4262, abort 42 – 0 failures |
| 21:05 | `python -I -m unittest discover -s system/program-manager/tests/python` | 11 tests OK (tools and CLI) |
| 21:12 | `make -C diagnostics/firmware -j8` and `CARD_READER=1`, `make check` | both variants built (130 788 B / 138 416 B), BOOT_SRAM vectors and symbol policy PASS |
| 21:13 | `make -C system/program-manager PROFILE=probe -j8`, `make -C system/program-manager -j8` | probe: QSPI 141 128 B, DTCM 101 360 B, SRAM_TARGET 0; manager: QSPI 156 752 B, DTCM 104 200 B, SRAM_HELPER 4 096 B, RAM_D2_DMA 17 096 B, SRAM_TARGET 0 |
| 21:20 | `make … check PYTHON=python` both profiles | `PASS: BOOT_QSPI placement, free SRAM target, vectors, marker`; manager additionally `PASS sd symbol policy: 39 banned, 6 required` |
| 21:22 | `python scripts/component_manifest.py --validate`, `python -m unittest discover -s tests -p test_components.py` | 26 components, 22 products; 10 tests OK |
| 21:23 | `python scripts/component_manifest.py --project program-manager --built` (and `-probe`) | `COMPONENTS.json` written |
| 21:40 | `make -C system/program-manager test-sanitize` | all eight suites under ASan/UBSan, 0 failures |
| 21:41 | `node web/tests/test_pm_protocol.js` | OK |

## 2026-11-01 – phase 4 (return service) and packages

| Command | Result |
| --- | --- |
| `make PROJECT=<p> test HOST_CXX=clang++` for toy-piano, reverb-fx, looper | Reverb and Looper PASS (Looper golden `2A2DBCEC41A54F57` unchanged); Toy Piano: all suites PASS except `test_golden` – the known Windows/clang libm difference, Linux CI is authoritative |
| `make PROJECT=step-seq test` | all C++ suites PASS (golden `0x7528DC6F`); the `python3` steps run manually with `python` on Windows: PASS |
| `make PROJECT=<p>` (ARM) for toy-piano, reverb-fx, looper, step-seq | all four built (exit 0), again after the merge with main |
| `dsy_pack.py pack --project …` for the four images, `pm_card.py init/add/check` | four packages, card template `build/packages/card` PASS (root free of `.bin`) |

## Artefacts (final build, after the merge with main, commit of PR #88 head)

| Profile | File | Bytes | SHA-256 |
| --- | --- | --- | --- |
| probe | `build/probe/DaisyProgramManagerProbe-v0.1.0.bin` | 141 120 | `096becddf694832740e9cc2c2b5f1d4274d62c15760cd45ca099b85288261b9b` |
| manager | `build/manager/DaisyProgramManager-v0.1.0.bin` | 156 744 | `45196252be6ba0540405d2c5238504d01077962fb348f140ac20643e06ee08b1` |

The embedded `build=` string carries the source commit; a rebuild from another commit
changes the hash although the code is identical.

## Deliberately not done / open

* **RM0433**: not obtainable (CONTRACT §11 C1).
* **§12 Fable trial**: not performed (user decision on 2026-11-01 because of context/budget
  limits; the session ran on Fable 5.1 anyway).
* **CI**: no change to `quick.yml`; host tests local.
* **JUCE/VST3**: deferred, semantics described in PROTOCOL.md only.
* **TSan**: no threaded code in the manager (`make test-thread` is a no-op).

## Products (image profile / NOLOAD / return)

| Product | `dsy_pack.py check` | NOLOAD in the target | Return service |
| --- | --- | --- | --- |
| toy-piano 0.4 | PASS (161 172 B, `d61c84c2338239d4…`) | none | yes (S1+S16, SysEx PM) |
| reverb-fx 0.4.0 | PASS (147 460 B, `fe37f066aeb3a074…`) | none | yes |
| looper 0.4.0 | PASS (166 712 B, `060834287e89f08a…`) | none | yes |
| step-seq 0.1.0 | PASS (158 216 B, `07e259c5f1b24789…`) | none | yes |
| toy-piano-ambient, toy-piano-tape, looper-mod | not built in this session; same main.cpp, return service included | as the base product (`.axi_bss` with AMB_COMPACT=1 → `--noload .axi_bss`) | yes |
| other products (physical-modeling*, granular, tape-fx, drive-fx, delay-fx, acid-*, streichwurst*, mod-bench) | not checked | – | no (legacy: return only through service) |

Package hashes above are from the builds before the merge with main; the package contents
of reverb-fx and toy-piano changed with that merge (Ambient 0.5.0) and must be re-packaged
with `dsy_pack.py` before a card is prepared.
