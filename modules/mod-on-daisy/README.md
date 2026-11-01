# Mod on Daisy

Lean, universally usable **modulation matrix** for instruments and effects on
the Seed3 Desktop Development Kit. The module is standalone, is integrated into existing
projects and is not hard-wired into any firmware. The graphical
editor is developed separately under [`editors/mod-on-daisy/`](../../editors/mod-on-daisy/).

**Status: phase 2 implemented in software.**

* The header-only core, the host tests (ASan/UBSan/TSan, fuzzing) and the measurement firmware
  `ModBench` are done, as are the test script `tools/mod_probe.py` and CI.
* Measurements and functional tests on the Desktop Kit are pending
  ([docs/VALIDATION.md](docs/VALIDATION.md), [bench/README.md](bench/README.md)).
* **Integrated:**
  * [Toy Piano v0.3](../../instruments/toy-piano/README.md#modulation-v03) as the
    development environment;
  * a separate build of
    [Physical Modeling (Rings/Torus)](../../instruments/physical-modeling/docs/MODULATION.md)
    with Poly AT per voice;
  * a separate build of the [Looper](../../instruments/looper/docs/MODULATION.md)
    (`make PROJECT=looper MOD=1`) with eight global columns: Speed, Start, Size, Slice,
    Skip, Inertia, Frippertronics decay and Mix.
* With an empty matrix, all of them are bit-identical to the previous sound (golden tests). The
  regular Physical Modeling and Looper firmwares have stayed byte-identical.

## v0.1 at a glance

| | |
| --- | --- |
| Matrix | 8×8 grid, depth −100…+100 per crosspoint, at most 8 active connections |
| Sources | LFO 1, LFO 2, Random/S&H, CV 1, CV 2, mod wheel (CC1), Channel AT, Poly AT (per voice; on global targets the most recently played note) |
| LFO | Sine, triangle, saw rising/falling, square, S&H; sync free / MIDI clock / Gate 1 or 2 |
| Targets | 8 instrument-specific columns; unsupported cells grayed out |
| Rate | Control rate 1 kHz (or once per audio block), **no audio rate** |
| Operation | Editor (JUCE, Windows) via USB MIDI/SysEx `0x7D` |
| Resources | 1 936 B RAM (measured, ARM build), CPU < 0.3 % (estimate, measurement pending) |
| Storage | QSPI specified, not yet implemented |
| Pilot | Toy Piano (technical), then Rings with Poly AT, later Nebulae |

## Documents

| Document | Content |
| --- | --- |
| [docs/DECISIONS.md](docs/DECISIONS.md) | Decisions v0.1, **technical objections**, architecture changes, open points |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Module structure, data structures, processing, global and per-voice modulation, instrument interface, estimate |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | MIDI protocol v1 |
| [docs/STORAGE.md](docs/STORAGE.md) | QSPI layout, write and erase procedure, data integrity |
| [docs/TEST_PLAN_PHASE2.md](docs/TEST_PLAN_PHASE2.md) | Host tests, measurement firmware, scenarios, hardware tests, acceptance |
| [docs/VALIDATION.md](docs/VALIDATION.md) | Phase 2 results |
| [docs/HARDWARE_TEST_RUN.md](docs/HARDWARE_TEST_RUN.md) | Hardware test run sheet (measurement firmware, Toy Piano, Physical Modeling, editors) with agent/human roles |
| [bench/README.md](bench/README.md) | Measurement firmware: building, operation, measurement procedure |
| [../../editors/mod-on-daisy/docs/GUI.md](../../editors/mod-on-daisy/docs/GUI.md) | Grid GUI and editor architecture |

## Build and test

```bash
make -C modules/mod-on-daisy test          # host tests (also: test-sanitize, test-thread)
make PROJECT=mod-bench -j4                 # measurement firmware (toolchain as in docs/BUILD.md)
python3 modules/mod-on-daisy/tools/mod_probe.py selftest
# column table of an instrument without hardware, via the simulator from `make test`:
python3 modules/mod-on-daisy/tools/mod_probe.py --sim modules/mod-on-daisy/build/host/mod_sim --instrument looper targets
```

Integration into an instrument: [docs/ARCHITECTURE.md § 6](docs/ARCHITECTURE.md#6-instrumentenschnittstelle).

## Guiding principle

The hardware stays immediately playable. Modulation assignments belong in the
graphical interface, not in hardware menus.
